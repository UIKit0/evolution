/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-table-field-chooser.c
 * Copyright (C) 2001  Ximian, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserverui/e-source-selector.h>
#include <e-util/e-util.h>
#include "eab-gui-util.h"
#include "util/eab-book-util.h"
#include <libebook/e-destination.h>
#include "e-util/e-error.h"
#include "misc/e-image-chooser.h"
#include <e-util/e-icon-factory.h>
#include "eab-contact-merging.h"
#include <gnome.h>

#include "addressbook/gui/contact-editor/eab-editor.h"
#include "addressbook/gui/contact-editor/e-contact-editor.h"
#include "addressbook/gui/contact-list-editor/e-contact-list-editor.h"
#include "addressbook/gui/component/addressbook-component.h"
#include "addressbook/gui/component/addressbook.h"

/* the NULL's in this table correspond to the status codes
   that should *never* be generated by a backend */
static const char *status_to_string[] = {
	/* E_BOOK_ERROR_OK */                        		N_("Success"),
	/* E_BOOK_ERROR_INVALID_ARG */               		NULL,
	/* E_BOOK_ERROR_BUSY */                      		N_("Backend busy"),
	/* E_BOOK_ERROR_REPOSITORY_OFFLINE */        		N_("Repository offline"),
	/* E_BOOK_ERROR_NO_SUCH_BOOK */              		N_("Address Book does not exist"),
	/* E_BOOK_ERROR_NO_SELF_CONTACT */           		N_("No Self Contact defined"),
	/* E_BOOK_ERROR_URI_NOT_LOADED */            		NULL,
	/* E_BOOK_ERROR_URI_ALREADY_LOADED */        		NULL,
	/* E_BOOK_ERROR_PERMISSION_DENIED */         		N_("Permission denied"),
	/* E_BOOK_ERROR_CONTACT_NOT_FOUND */         		N_("Contact not found"),
	/* E_BOOK_ERROR_CONTACT_ID_ALREADY_EXISTS */ 		N_("Contact ID already exists"),
	/* E_BOOK_ERROR_PROTOCOL_NOT_SUPPORTED */    		N_("Protocol not supported"),
	/* E_BOOK_ERROR_CANCELLED */                 		N_("Canceled"),
	/* E_BOOK_ERROR_COULD_NOT_CANCEL */                     N_("Could not cancel"),
	/* E_BOOK_ERROR_AUTHENTICATION_FAILED */                N_("Authentication Failed"),
	/* E_BOOK_ERROR_AUTHENTICATION_REQUIRED */              N_("Authentication Required"),
	/* E_BOOK_ERROR_TLS_NOT_AVAILABLE */                    N_("TLS not Available"),
	/* E_BOOK_ERROR_CORBA_EXCEPTION */                      NULL,
	/* E_BOOK_ERROR_NO_SUCH_SOURCE */                       N_("No such source"),
	/* E_BOOK_ERROR_OFFLINE_UNAVAILABLE */			N_("Not available in offline mode"),
	/* E_BOOK_ERROR_OTHER_ERROR */                          N_("Other error"),
	/* E_BOOK_ERROR_INVALID_SERVER_VERSION */		N_("Invalid server version")
};

void
eab_error_dialog (const char *msg, EBookStatus status)
{
	const char *status_str = status_to_string [status];
	
	if (status_str)
		e_error_run (NULL, "addressbook:generic-error", msg, _(status_str), NULL);
}

void
eab_load_error_dialog (GtkWidget *parent, ESource *source, EBookStatus status)
{
	char *label_string, *label = NULL, *uri;
	GtkWidget *dialog;
	
	g_return_if_fail (source != NULL);

	uri = e_source_get_uri (source);

	if (status == E_BOOK_ERROR_OFFLINE_UNAVAILABLE) {
		label_string = _("We were unable to open this addressbook. This either means "
                                 "this book is not marked for offline usage or not yet downloaded "
                                 "for offline usage. Please load the addressbook once in online mode "
                                 "to download its contents");
	}
		
	else if (!strncmp (uri, "file:", 5)) {
		char *path = g_filename_from_uri (uri, NULL, NULL);
		label = g_strdup_printf (
			_("We were unable to open this addressbook.  Please check that the "
			  "path %s exists and that you have permission to access it."), path);
		g_free (path);
		label_string = label;
	}
	else if (!strncmp (uri, "ldap:", 5)) {
		/* special case for ldap: contact folders so we can tell the user about openldap */
#if HAVE_LDAP
		label_string = 
			_("We were unable to open this addressbook.  This either "
			  "means you have entered an incorrect URI, or the LDAP server "
			  "is unreachable.");
#else
		label_string =
			_("This version of Evolution does not have LDAP support "
			  "compiled in to it.  If you want to use LDAP in Evolution, "
			  "you must install an LDAP-enabled Evolution package.");
#endif
	} else {
		/* other network folders */
		label_string =
			_("We were unable to open this addressbook.  This either "
			  "means you have entered an incorrect URI, or the server "
			  "is unreachable.");
	}
	
	dialog  = e_error_new ((GtkWindow *) parent, "addressbook:load-error", label_string, NULL);
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
	
	gtk_widget_show (dialog);
	g_free (label);	
	g_free (uri);
}

void
eab_search_result_dialog      (GtkWidget *parent,
			       EBookViewStatus status)
{
	char *str = NULL;

	switch (status) {
	case E_BOOK_VIEW_STATUS_OK:
		return;
	case E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED:
		str = _("More cards matched this query than either the server is \n"
			"configured to return or Evolution is configured to display.\n"
			"Please make your search more specific or raise the result limit in\n"
			"the directory server preferences for this addressbook.");
		break;
	case E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED:
		str = _("The time to execute this query exceeded the server limit or the limit\n"
			"you have configured for this addressbook.  Please make your search\n"
			"more specific or raise the time limit in the directory server\n"
			"preferences for this addressbook.");
		break;
	case E_BOOK_VIEW_ERROR_INVALID_QUERY:
		str = _("The backend for this addressbook was unable to parse this query.");
		break;
	case E_BOOK_VIEW_ERROR_QUERY_REFUSED:
		str = _("The backend for this addressbook refused to perform this query.");
		break;
	case E_BOOK_VIEW_ERROR_OTHER_ERROR:
		str = _("This query did not complete successfully.");
		break;
	default:
		g_assert_not_reached ();
	}
	
	e_error_run ((GtkWindow *) parent, "addressbook:search-error", str, NULL);
}

gint
eab_prompt_save_dialog (GtkWindow *parent)
{
	return e_error_run (parent, "addressbook:prompt-save", NULL);
}

static void
added_cb (EBook* book, EBookStatus status, EContact *contact,
	  gpointer data)
{
	gboolean is_list = GPOINTER_TO_INT (data);

	if (status != E_BOOK_ERROR_OK && status != E_BOOK_ERROR_CANCELLED) {
		eab_error_dialog (is_list ? _("Error adding list") : _("Error adding contact"), status);
	}
}

static void
modified_cb (EBook* book, EBookStatus status, EContact *contact,
	     gpointer data)
{
	gboolean is_list = GPOINTER_TO_INT (data);

	if (status != E_BOOK_ERROR_OK && status != E_BOOK_ERROR_CANCELLED) {
		eab_error_dialog (is_list ? _("Error modifying list") : _("Error modifying contact"),
				  status);
	}
}

static void
deleted_cb (EBook* book, EBookStatus status, EContact *contact,
	    gpointer data)
{
	gboolean is_list = GPOINTER_TO_INT (data);

	if (status != E_BOOK_ERROR_OK) {
		eab_error_dialog (is_list ? _("Error removing list") : _("Error removing contact"),
				  status);
	}
}

static void
editor_closed_cb (GtkObject *editor, gpointer data)
{
	g_object_unref (editor);
}

EContactEditor *
eab_show_contact_editor (EBook *book, EContact *contact,
			 gboolean is_new_contact,
			 gboolean editable)
{
	EContactEditor *ce;

	ce = e_contact_editor_new (book, contact, is_new_contact, editable);

	g_signal_connect (ce, "contact_added",
			  G_CALLBACK (added_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (ce, "contact_modified",
			  G_CALLBACK (modified_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (ce, "contact_deleted",
			  G_CALLBACK (deleted_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (ce, "editor_closed",
			  G_CALLBACK (editor_closed_cb), NULL);

	return ce;
}

EContactListEditor *
eab_show_contact_list_editor (EBook *book, EContact *contact,
			      gboolean is_new_contact,
			      gboolean editable)
{
	EContactListEditor *ce;

	ce = e_contact_list_editor_new (book, contact, is_new_contact, editable);

	g_signal_connect (ce, "contact_added",
			  G_CALLBACK (added_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (ce, "contact_modified",
			  G_CALLBACK (modified_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (ce, "contact_deleted",
			  G_CALLBACK (deleted_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (ce, "editor_closed",
			  G_CALLBACK (editor_closed_cb), GINT_TO_POINTER (TRUE));

	eab_editor_show (EAB_EDITOR (ce));

	return ce;
}

static void
view_contacts (EBook *book, GList *list, gboolean editable)
{
	for (; list; list = list->next) {
		EContact *contact = list->data;
		if (e_contact_get (contact, E_CONTACT_IS_LIST))
			eab_show_contact_list_editor (book, contact, FALSE, editable);
		else
			eab_show_contact_editor (book, contact, FALSE, editable);
	}
}

void
eab_show_multiple_contacts (EBook *book,
			    GList *list,
			    gboolean editable)
{
	if (list) {
		int length = g_list_length (list);
		if (length > 5) {
			GtkWidget *dialog;
			gint response;

			dialog = gtk_message_dialog_new (NULL,
							 0,
							 GTK_MESSAGE_QUESTION,
							 GTK_BUTTONS_YES_NO,
							 ngettext("Opening %d contact will open %d new window as well.\n"
								  "Do you really want to display this contact?",
								  "Opening %d contacts will open %d new windows as well.\n"
								  "Do you really want to display all of these contacts?",
								  length),
							 length,
							 length);

			response = gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			if (response == GTK_RESPONSE_YES)
				view_contacts (book, list, editable);
		} else {
			view_contacts (book, list, editable);
		}
	}
}


static gint
file_exists(GtkWindow *window, const char *filename)
{
	GtkWidget *dialog;
	gint response;
	char * utf8_filename;

	utf8_filename = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
	dialog = gtk_message_dialog_new (window,
					 0,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 _("%s already exists\nDo you want to overwrite it?"), utf8_filename);
	g_free (utf8_filename);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				_("Overwrite"), GTK_RESPONSE_ACCEPT,
				NULL);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	return response;
}

typedef struct {
	GtkWidget *filesel;
	char *vcard;
	gboolean has_multiple_contacts; 
} SaveAsInfo;

static void
save_it(GtkWidget *widget, SaveAsInfo *info)
{
	const char *filename;
	char *uri;
	gint error = 0;
	gint response = 0;
	

#ifdef USE_GTKFILECHOOSER
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (info->filesel));
	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (info->filesel));	
#else
	filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (info->filesel));
#endif
	
	if (filename && g_file_test (filename, G_FILE_TEST_EXISTS)) {
		response = file_exists(GTK_WINDOW (info->filesel), filename);
		switch (response) {
			case GTK_RESPONSE_ACCEPT : /* Overwrite */
				break;
			case GTK_RESPONSE_CANCEL : /* cancel */
				return;
		}
	}

	error = e_write_file_uri (uri, info->vcard);	    
	if (error != 0) {
		char *err_str_ext;
		if (info->has_multiple_contacts) {
			/* more than one, finding the total number of contacts might
			 * hit performance while saving large number of contacts 
			 */
			err_str_ext = ngettext ("contact", "contacts", 2);
		}
		else {
			err_str_ext = ngettext ("contact", "contacts", 1);
		}

		/* translators: Arguments, err_str_ext (item to be saved: "contact"/"contacts"), 
		 * destination file name, and error code will fill the placeholders 
		 * {0}, {1} and {2}, respectively in the error message formed 
		 */
		e_error_run (GTK_WINDOW (info->filesel), "addressbook:save-error", 
					 err_str_ext, filename, g_strerror (errno));
		gtk_widget_destroy(GTK_WIDGET(info->filesel));
		return;
	}
	    
	gtk_widget_destroy(GTK_WIDGET(info->filesel));
}

static void
close_it(GtkWidget *widget, SaveAsInfo *info)
{
	gtk_widget_destroy (GTK_WIDGET (info->filesel));
}

static void
destroy_it(void *data, GObject *where_the_object_was)
{
	SaveAsInfo *info = data;
	g_free (info->vcard);
	g_free (info);
}

#ifdef USE_GTKFILECHOOSER
static void
filechooser_response (GtkWidget *widget, gint response_id, SaveAsInfo *info)
{
	if (response_id == GTK_RESPONSE_ACCEPT)
		save_it  (widget, info);
	else
		close_it (widget, info);
}
#endif

static char *
make_safe_filename (char *name)
{
	char *safe;

	if (!name) {
		/* This is a filename. Translators take note. */
		name = _("card.vcf");
	}

	if (!g_strrstr (name, ".vcf"))
		safe = g_strdup_printf ("%s%s", name, ".vcf");
	else
		safe = g_strdup (name);

	e_filename_make_safe (safe);
	
	return safe;
}

static void
source_selection_changed_cb (GtkWidget *selector, GtkWidget *ok_button)
{
	gtk_widget_set_sensitive (ok_button,
				  e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (selector)) ?
				  TRUE : FALSE);
}

ESource *
eab_select_source (const gchar *title, const gchar *message, const gchar *select_uid, GtkWindow *parent)
{
	ESource *source;
	ESourceList *source_list;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	/* GtkWidget *label; */
	GtkWidget *selector;
	GtkWidget *scrolled_window;
	gint response;

	if (!e_book_get_addressbooks (&source_list, NULL))
		return NULL;

	dialog = gtk_dialog_new_with_buttons (_("Select Address Book"), parent,
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 350, 300);

	cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_ACCEPT);
	gtk_widget_set_sensitive (ok_button, FALSE);

	/* label = gtk_label_new (message); */

	selector = e_source_selector_new (source_list);
	e_source_selector_show_selection (E_SOURCE_SELECTOR (selector), FALSE);
	g_signal_connect (selector, "primary_selection_changed",
			  G_CALLBACK (source_selection_changed_cb), ok_button);

	if (select_uid) {
		source = e_source_list_peek_source_by_uid (source_list, select_uid);
		if (source)
			e_source_selector_set_primary_selection (E_SOURCE_SELECTOR (selector), source);
	}

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scrolled_window), selector);

	/* gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 4); */
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), scrolled_window, TRUE, TRUE, 4);

	gtk_widget_show_all (dialog);
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response == GTK_RESPONSE_ACCEPT)
		source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (selector));
	else
		source = NULL;

	gtk_widget_destroy (dialog);
	return source;
}

void
eab_contact_save (char *title, EContact *contact, GtkWindow *parent_window)
{
	GtkWidget *filesel;
	char *file;
	char *name;
#ifndef USE_GTKFILECHOOSER
	char *full_filename;
#endif
	SaveAsInfo *info = g_new(SaveAsInfo, 1);

	name = e_contact_get (contact, E_CONTACT_FILE_AS);
	file = make_safe_filename (name);

	info->has_multiple_contacts = FALSE;

#ifdef USE_GTKFILECHOOSER
	filesel = gtk_file_chooser_dialog_new (title,
					       parent_window,
					       GTK_FILE_CHOOSER_ACTION_SAVE,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					       NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (filesel), GTK_RESPONSE_ACCEPT);

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filesel), g_get_home_dir ());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (filesel), file);
	gtk_file_chooser_set_local_only (filesel, FALSE);
	
	info->filesel = filesel;
	info->vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	g_signal_connect (G_OBJECT (filesel), "response",
			  G_CALLBACK (filechooser_response), info);
	g_object_weak_ref (G_OBJECT (filesel), destroy_it, info);
#else
	filesel = gtk_file_selection_new (title);

	full_filename = g_strdup_printf ("%s/%s", g_get_home_dir (), file);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel), full_filename);
	g_free (full_filename);
	
	info->filesel = filesel;
	info->vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	g_signal_connect(G_OBJECT (GTK_FILE_SELECTION (filesel)->ok_button), "clicked",
			 G_CALLBACK (save_it), info);
	g_signal_connect(G_OBJECT (GTK_FILE_SELECTION (filesel)->cancel_button), "clicked",
			 G_CALLBACK (close_it), info);
	g_object_weak_ref (G_OBJECT (filesel), destroy_it, info);
#endif

	if (parent_window) {
		gtk_window_set_transient_for (GTK_WINDOW (filesel),
					      parent_window);
		gtk_window_set_modal (GTK_WINDOW (filesel), TRUE);
	}

	gtk_widget_show(GTK_WIDGET(filesel));
	g_free (file);
}

void
eab_contact_list_save (char *title, GList *list, GtkWindow *parent_window)
{
	GtkWidget *filesel;
	SaveAsInfo *info = g_new(SaveAsInfo, 1);
	char *file;
#ifndef USE_GTKFILECHOOSER
	char *full_filename;
#endif

#ifdef USE_GTKFILECHOOSER
	filesel = gtk_file_chooser_dialog_new (title,
					       parent_window,
					       GTK_FILE_CHOOSER_ACTION_SAVE,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					       NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (filesel), GTK_RESPONSE_ACCEPT);
	gtk_file_chooser_set_local_only (filesel, FALSE);
#else
	filesel = gtk_file_selection_new(title);
#endif

	/* Check if the list has more than one contact */
	if (g_list_next (list))
		info->has_multiple_contacts = TRUE;
	else
		info->has_multiple_contacts = FALSE;

	/* This is a filename. Translators take note. */
	if (list && list->data && list->next == NULL) {
		char *name;
		name = e_contact_get (E_CONTACT (list->data), E_CONTACT_FILE_AS);
		if (!name)
			name = e_contact_get (E_CONTACT (list->data), E_CONTACT_FULL_NAME);

		file = make_safe_filename (name);
	} else {
		file = make_safe_filename (_("list"));
	}

#ifdef USE_GTKFILECHOOSER
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filesel), g_get_home_dir ());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (filesel), file);
#else
	full_filename = g_strdup_printf ("%s/%s", g_get_home_dir (), file);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel), full_filename);
	g_free (full_filename);
#endif

	info->filesel = filesel;
	info->vcard = eab_contact_list_to_string (list);

#ifdef USE_GTKFILECHOOSER
	g_signal_connect (G_OBJECT (filesel), "response",
			  G_CALLBACK (filechooser_response), info);
	g_object_weak_ref (G_OBJECT (filesel), destroy_it, info);
#else
	g_signal_connect(G_OBJECT (GTK_FILE_SELECTION (filesel)->ok_button), "clicked",
			 G_CALLBACK (save_it), info);
	g_signal_connect(G_OBJECT (GTK_FILE_SELECTION (filesel)->cancel_button), "clicked",
			 G_CALLBACK (close_it), info);
	g_object_weak_ref (G_OBJECT (filesel), destroy_it, info);
#endif

	if (parent_window) {
		gtk_window_set_transient_for (GTK_WINDOW (filesel),
					      parent_window);
		gtk_window_set_modal (GTK_WINDOW (filesel), TRUE);
	}

	gtk_widget_show(GTK_WIDGET(filesel));
	g_free (file);
}

typedef struct ContactCopyProcess_ ContactCopyProcess;

typedef void (*ContactCopyDone) (ContactCopyProcess *process);

struct ContactCopyProcess_ {
	int count;
	gboolean book_status;
	GList *contacts;
	EBook *source;
	EBook *destination;
	ContactCopyDone done_cb;
};

static void
contact_deleted_cb (EBook* book, EBookStatus status, gpointer user_data)
{
	if (status != E_BOOK_ERROR_OK) {
		eab_error_dialog (_("Error removing contact"), status);
	}
}

static void
do_delete (gpointer data, gpointer user_data)
{
	EBook *book = user_data;
	EContact *contact = data;
	const char *id;
	
	id = e_contact_get_const (contact, E_CONTACT_UID);
	e_book_remove_contact(book, id, NULL);
}

static void
delete_contacts (ContactCopyProcess *process)
{
	if (process->book_status == TRUE) {
		g_list_foreach (process->contacts,
				do_delete,
				process->source);
	}
}

static void
process_unref (ContactCopyProcess *process)
{
	process->count --;
	if (process->count == 0) {
		if (process->done_cb) {
			process->done_cb (process);
		}
		e_free_object_list(process->contacts);
		g_object_unref (process->source);
		g_object_unref (process->destination);
		g_free (process);
	}
}

static void
contact_added_cb (EBook* book, EBookStatus status, const char *id, gpointer user_data)
{
	ContactCopyProcess *process = user_data;

	if (status != E_BOOK_ERROR_OK && status != E_BOOK_ERROR_CANCELLED) {
		process->book_status = FALSE;
		eab_error_dialog (_("Error adding contact"), status);
	} 
	else if (status == E_BOOK_ERROR_CANCELLED) {
		process->book_status = FALSE;
	}
	else {
		/* success */
		process->book_status = TRUE;
	}
	process_unref (process);
}

static void
do_copy (gpointer data, gpointer user_data)
{
	EBook *book;
	EContact *contact;
	ContactCopyProcess *process;

	process = user_data;
	contact = data;

	book = process->destination;

	process->count ++;
	eab_merging_book_add_contact(book, contact, contact_added_cb, process);
}

static void
got_book_cb (EBook *book, EBookStatus status, gpointer closure)
{
	ContactCopyProcess *process;
	process = closure;
	if (status == E_BOOK_ERROR_OK) {
		process->destination = book;
		process->book_status = TRUE;
		g_object_ref (book);
		g_list_foreach (process->contacts,
				do_copy,
				process);
	}
	process_unref (process);
}

void
eab_transfer_contacts (EBook *source, GList *contacts /* adopted */, gboolean delete_from_source, GtkWindow *parent_window)
{
	EBook *dest;
	ESource *destination_source;
	static char *last_uid = NULL;
	ContactCopyProcess *process;
	char *desc;

	if (contacts == NULL)
		return;

	if (last_uid == NULL)
		last_uid = g_strdup ("");

	if (contacts->next == NULL) {
		if (delete_from_source)
			desc = _("Move contact to");
		else
			desc = _("Copy contact to");
	} else {
		if (delete_from_source)
			desc = _("Move contacts to");
		else
			desc = _("Copy contacts to");
	}

	destination_source = eab_select_source (desc, NULL,
						last_uid, parent_window);

	if (!destination_source)
		return;

	if (strcmp (last_uid, e_source_peek_uid (destination_source)) != 0) {
		g_free (last_uid);
		last_uid = g_strdup (e_source_peek_uid (destination_source));
	}

	process = g_new (ContactCopyProcess, 1);
	process->count = 1;
	process->book_status = FALSE;
	process->source = source;
	g_object_ref (source);
	process->contacts = contacts;
	process->destination = NULL;

	if (delete_from_source)
		process->done_cb = delete_contacts;
	else
		process->done_cb = NULL;

	dest = e_book_new (destination_source, NULL);
	addressbook_load (dest, got_book_cb, process);
}

#include <Evolution-Composer.h>

#define COMPOSER_OAFID "OAFIID:GNOME_Evolution_Mail_Composer:" BASE_VERSION

typedef struct {
	EContact *contact;
	int email_num; /* if the contact is a person (not a list), the email address to use */
} ContactAndEmailNum;

static void
eab_send_to_contact_and_email_num_list (GList *c)
{
	GNOME_Evolution_Composer composer_server;
	CORBA_Environment ev;
	GNOME_Evolution_Composer_RecipientList *to_list, *cc_list, *bcc_list;
	CORBA_char *subject;
	int to_i, bcc_i;
	GList *iter;
	gint to_length = 0, bcc_length = 0;

	if (c == NULL)
		return;

	CORBA_exception_init (&ev);
	
	composer_server = bonobo_activation_activate_from_id (COMPOSER_OAFID, 0, NULL, &ev);

	/* Figure out how many addresses of each kind we have. */
	for (iter = c; iter != NULL; iter = g_list_next (iter)) {
		ContactAndEmailNum *ce = iter->data;
		EContact *contact = ce->contact;
		GList *emails = e_contact_get (contact, E_CONTACT_EMAIL);
		if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
			gint len = g_list_length (emails);
			if (e_contact_get (contact, E_CONTACT_LIST_SHOW_ADDRESSES))
				to_length += len;
			else
				bcc_length += len;
		} else {
			if (emails != NULL)
				++to_length;
		}
		g_list_foreach (emails, (GFunc)g_free, NULL);
		g_list_free (emails);
	}

	/* Now I have to make a CORBA sequences that represents a recipient list with
	   the right number of entries, for the contacts. */
	to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	to_list->_maximum = to_length;
	to_list->_length = to_length;
	if (to_length > 0) {
		to_list->_buffer = CORBA_sequence_GNOME_Evolution_Composer_Recipient_allocbuf (to_length);
	}

	cc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	cc_list->_maximum = cc_list->_length = 0;
		
	bcc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	bcc_list->_maximum = bcc_length;
	bcc_list->_length = bcc_length;
	if (bcc_length > 0) {
		bcc_list->_buffer = CORBA_sequence_GNOME_Evolution_Composer_Recipient_allocbuf (bcc_length);
	}

	to_i = 0;
	bcc_i = 0;
	while (c != NULL) {
		ContactAndEmailNum *ce = c->data;
		EContact *contact = ce->contact;
		int nth = ce->email_num;
		char *name, *addr;
		gboolean is_list, is_hidden;
		GNOME_Evolution_Composer_Recipient *recipient;
		GList *emails = e_contact_get (contact, E_CONTACT_EMAIL);
		GList *iterator;

		if (emails != NULL) {
			is_list = e_contact_get (contact, E_CONTACT_IS_LIST) != NULL;
			is_hidden = is_list && !e_contact_get (contact, E_CONTACT_LIST_SHOW_ADDRESSES);

			if (is_list) {
				for (iterator = emails; iterator; iterator = iterator->next) {
					
					if (is_hidden) {
						recipient = &(bcc_list->_buffer[bcc_i]);
						++bcc_i;
					} else {
						recipient = &(to_list->_buffer[to_i]);
						++to_i;
					}
					
					name = NULL;
					addr = NULL;
					if (iterator && iterator->data) {
						/* XXX we should probably try to get the name from the attribute parameter here.. */
						addr = g_strdup ((char*)iterator->data);
					}

					recipient->name    = CORBA_string_dup (name ? name : "");
					recipient->address = CORBA_string_dup (addr ? addr : "");
					
					g_free (name);
					g_free (addr);
				}
			}
			else {
				EContactName *contact_name = e_contact_get (contact, E_CONTACT_NAME);
				int length = g_list_length (emails);

				if (is_hidden) {
					recipient = &(bcc_list->_buffer[bcc_i]);
					++bcc_i;
				} else {
					recipient = &(to_list->_buffer[to_i]);
					++to_i;
				}

				if (nth >= length)
					nth = 0;
					
				if (contact_name) {
					name = e_contact_name_to_string (contact_name);
					e_contact_name_free (contact_name);
				}
				else
					name = NULL;

				addr = g_strdup (g_list_nth_data (emails, nth));
					

				recipient->name    = CORBA_string_dup (name ? name : "");
				recipient->address = CORBA_string_dup (addr ? addr : "");

				g_free (name);
				g_free (addr);
			}

			g_list_foreach (emails, (GFunc)g_free, NULL);
			g_list_free (emails);
		}

		c = c->next;
	}

	subject = CORBA_string_dup ("");

	GNOME_Evolution_Composer_setHeaders (composer_server, "", to_list, cc_list, bcc_list, subject, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_printerr ("gui/e-meeting-edit.c: I couldn't set the composer headers via CORBA! Aagh.\n");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_free (to_list);
	CORBA_free (cc_list);
	CORBA_free (bcc_list);
	CORBA_free (subject);

	GNOME_Evolution_Composer_show (composer_server, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_printerr ("gui/e-meeting-edit.c: I couldn't show the composer via CORBA! Aagh.\n");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);
}

static void
eab_send_contact_list_as_attachment (GList *contacts)
{
	GNOME_Evolution_Composer composer_server;
	CORBA_Environment ev;
	CORBA_char *content_type, *filename, *description;
	GNOME_Evolution_Composer_AttachmentData *attach_data;
	CORBA_boolean show_inline;
	char *tempstr;
	GNOME_Evolution_Composer_RecipientList *to_list, *cc_list, *bcc_list;
	CORBA_char *subject;

	if (contacts == NULL)
		return;

	CORBA_exception_init (&ev);
	
	composer_server = bonobo_activation_activate_from_id (COMPOSER_OAFID, 0, NULL, &ev);


		
	content_type = CORBA_string_dup ("text/x-vcard");
	filename = CORBA_string_dup ("");

	if (contacts->next) {
		description = CORBA_string_dup (_("Multiple VCards"));
	} else {
		char *file_as = e_contact_get (E_CONTACT (contacts->data), E_CONTACT_FILE_AS);
		tempstr = g_strdup_printf (_("VCard for %s"), file_as);
		description = CORBA_string_dup (tempstr);
		g_free (tempstr);
		g_free (file_as);
	}

	show_inline = FALSE;

	tempstr = eab_contact_list_to_string (contacts);
	attach_data = GNOME_Evolution_Composer_AttachmentData__alloc();
	attach_data->_maximum = attach_data->_length = strlen (tempstr);
	attach_data->_buffer = CORBA_sequence_CORBA_char_allocbuf (attach_data->_length);
	memcpy (attach_data->_buffer, tempstr, attach_data->_length);
	g_free (tempstr);

	GNOME_Evolution_Composer_attachData (composer_server, 
					     content_type, filename, description,
					     show_inline, attach_data,
					     &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_printerr ("gui/e-meeting-edit.c: I couldn't attach data to the composer via CORBA! Aagh.\n");
		CORBA_exception_free (&ev);
		return;
	}
	
	CORBA_free (content_type);
	CORBA_free (filename);
	CORBA_free (description);
	CORBA_free (attach_data);

	to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	to_list->_maximum = to_list->_length = 0;
		
	cc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	cc_list->_maximum = cc_list->_length = 0;

	bcc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
	bcc_list->_maximum = bcc_list->_length = 0;

	if (!contacts || contacts->next) {
		subject = CORBA_string_dup (_("Contact information"));
	} else {
		EContact *contact = contacts->data;
		const gchar *tempstr2;

		tempstr2 = e_contact_get_const (contact, E_CONTACT_FILE_AS);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_ORG);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_EMAIL_2);
		if (!tempstr2 || !*tempstr2)
			tempstr2 = e_contact_get_const (contact, E_CONTACT_EMAIL_3);

		if (!tempstr2 || !*tempstr2)
			tempstr = g_strdup_printf (_("Contact information"));
		else
			tempstr = g_strdup_printf (_("Contact information for %s"), tempstr2);
		subject = CORBA_string_dup (tempstr);
		g_free (tempstr);
	}
		
	GNOME_Evolution_Composer_setHeaders (composer_server, "", to_list, cc_list, bcc_list, subject, &ev);

	CORBA_free (to_list);
	CORBA_free (cc_list);
	CORBA_free (bcc_list);
	CORBA_free (subject);

	GNOME_Evolution_Composer_show (composer_server, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_printerr ("gui/e-meeting-edit.c: I couldn't show the composer via CORBA! Aagh.\n");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);
}

void
eab_send_contact_list (GList *contacts, EABDisposition disposition)
{
	switch (disposition) {
	case EAB_DISPOSITION_AS_TO: {
		GList *list = NULL, *l;

		for (l = contacts; l; l = l->next) {
			ContactAndEmailNum *ce = g_new (ContactAndEmailNum, 1);
			ce->contact = l->data;
			ce->email_num = 0; /* hardcode this */

			list = g_list_append (list, ce);
		}

		eab_send_to_contact_and_email_num_list (list);

		g_list_foreach (list, (GFunc)g_free, NULL);
		g_list_free (list);
		break;
	}
	case EAB_DISPOSITION_AS_ATTACHMENT:
		eab_send_contact_list_as_attachment (contacts);
		break;
	}
}

void
eab_send_contact (EContact *contact, int email_num, EABDisposition disposition)
{
	GList *list = NULL;

	switch (disposition) {
	case EAB_DISPOSITION_AS_TO: {
		ContactAndEmailNum ce;

		ce.contact = contact;
		ce.email_num = email_num;

		list = g_list_prepend (NULL, &ce);
		eab_send_to_contact_and_email_num_list (list);
		break;
	}
	case EAB_DISPOSITION_AS_ATTACHMENT: {
		list = g_list_prepend (NULL, contact);
		eab_send_contact_list_as_attachment (list);
		break;
	}
	}

	g_list_free (list);
}

GtkWidget *
eab_create_image_chooser_widget(gchar *name,
				gchar *string1, gchar *string2,
				gint int1, gint int2)
{
	char *filename;
	GtkWidget *w = NULL;

	w = e_image_chooser_new ();
	gtk_widget_show_all (w);

	if (string1) {
		filename = e_icon_factory_get_icon_filename (string1, E_ICON_SIZE_DIALOG);

		e_image_chooser_set_from_file (E_IMAGE_CHOOSER (w), filename);

		g_free (filename);
	}

	return w;
}
