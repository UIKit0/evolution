/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* mail-component.c
 *
 * Copyright (C) 2003  Ximian Inc.
 *
 * This  program is free  software; you  can redistribute  it and/or
 * modify it under the terms of version 2  of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "e-storage-browser.h"

#include "em-folder-tree.h"
#include "em-folder-selector.h"
#include "em-folder-selection.h"

#include "folder-browser-factory.h"
#include "mail-config.h"
#include "mail-component.h"
#include "mail-folder-cache.h"
#include "mail-vfolder.h"
#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-tools.h"
#include "mail-send-recv.h"
#include "mail-session.h"

#include "em-popup.h"
#include "em-utils.h"
#include "em-migrate.h"

#include <gtk/gtklabel.h>

#include <e-util/e-mktemp.h>
#include <e-util/e-dialog-utils.h>

#include <gal/e-table/e-tree.h>
#include <gal/e-table/e-tree-memory.h>

#include <camel/camel.h>
#include <camel/camel-file-utils.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-widget.h>


#define d(x) x

#define MESSAGE_RFC822_TYPE   "message/rfc822"
#define TEXT_URI_LIST_TYPE    "text/uri-list"
#define UID_LIST_TYPE         "x-uid-list"
#define FOLDER_TYPE           "x-folder"

/* Drag & Drop types */
enum DndDragType {
	DND_DRAG_TYPE_FOLDER,          /* drag an evo folder */
	DND_DRAG_TYPE_TEXT_URI_LIST,   /* drag to an mbox file */
};

enum DndDropType {
	DND_DROP_TYPE_UID_LIST,        /* drop a list of message uids */
	DND_DROP_TYPE_FOLDER,          /* drop an evo folder */
	DND_DROP_TYPE_MESSAGE_RFC822,  /* drop a message/rfc822 stream */
	DND_DROP_TYPE_TEXT_URI_LIST,   /* drop an mbox file */
};

static GtkTargetEntry drag_types[] = {
	{ UID_LIST_TYPE,       0, DND_DRAG_TYPE_FOLDER         },
	{ TEXT_URI_LIST_TYPE,  0, DND_DRAG_TYPE_TEXT_URI_LIST  },
};

static const int num_drag_types = sizeof (drag_types) / sizeof (drag_types[0]);

static GtkTargetEntry drop_types[] = {
	{ UID_LIST_TYPE,       0, DND_DROP_TYPE_UID_LIST       },
	{ FOLDER_TYPE,         0, DND_DROP_TYPE_FOLDER         },
	{ MESSAGE_RFC822_TYPE, 0, DND_DROP_TYPE_MESSAGE_RFC822 },
	{ TEXT_URI_LIST_TYPE,  0, DND_DROP_TYPE_TEXT_URI_LIST  },
};

static const int num_drop_types = sizeof (drop_types) / sizeof (drop_types[0]);


#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _MailComponentPrivate {
	char *base_directory;
	
	EMFolderTree *emft;
	
	MailAsyncEvent *async_event;
	GHashTable *store_hash; /* storage by store */
	
	RuleContext *search_context;
	
	char *context_path;	/* current path for right-click menu */
	
	CamelStore *local_store;
};

/* Utility functions.  */

static void
add_storage (MailComponent *component, const char *name, CamelService *store, CamelException *ex)
{
	camel_object_ref (store);
	g_hash_table_insert (component->priv->store_hash, store, g_strdup (name));
	em_folder_tree_add_store (component->priv->emft, (CamelStore *) store, name);
	mail_note_store ((CamelStore *) store, NULL, NULL, NULL);
}

static void
load_accounts (MailComponent *component, EAccountList *accounts)
{
	EIterator *iter;
	
	/* Load each service (don't connect!). Check its provider and
	 * see if this belongs in the shell's folder list. If so, add
	 * it.
	 */
	
	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		EAccountService *service;
		EAccount *account;
		const char *name;
		
		account = (EAccount *) e_iterator_get (iter);
		service = account->source;
		name = account->name;
		
		if (account->enabled && service->url != NULL)
			mail_component_load_storage_by_uri (component, service->url, name);
		
		e_iterator_next (iter);
	}
	
	g_object_unref (iter);
}

static inline gboolean
type_is_mail (const char *type)
{
	return !strcmp (type, "mail") || !strcmp (type, "mail/public");
}

static inline gboolean
type_is_vtrash (const char *type)
{
	return !strcmp (type, "vtrash");
}

static void
storage_go_online (gpointer key, gpointer value, gpointer data)
{
	CamelStore *store = key;
	CamelService *service = CAMEL_SERVICE (store);
	
	if (! (service->provider->flags & CAMEL_PROVIDER_IS_REMOTE)
	    || (service->provider->flags & CAMEL_PROVIDER_IS_EXTERNAL))
		return;
	
	if ((CAMEL_IS_DISCO_STORE (service)
	     && camel_disco_store_status (CAMEL_DISCO_STORE (service)) == CAMEL_DISCO_STORE_OFFLINE)
	    || service->status != CAMEL_SERVICE_DISCONNECTED) {
		mail_store_set_offline (store, FALSE, NULL, NULL);
		mail_note_store (store, NULL, NULL, NULL);
	}
}

static void
go_online (MailComponent *component)
{
	camel_session_set_online(session, TRUE);
	mail_session_set_interactive(TRUE);
	mail_component_storages_foreach(component, storage_go_online, NULL);
}

static void
setup_search_context (MailComponent *component)
{
	MailComponentPrivate *priv = component->priv;
	char *user = g_strdup_printf ("%s/evolution/searches.xml", g_get_home_dir ()); /* EPFIXME should be somewhere else. */
	char *system = g_strdup (EVOLUTION_PRIVDATADIR "/vfoldertypes.xml");
	
	priv->search_context = rule_context_new ();
	g_object_set_data_full (G_OBJECT (priv->search_context), "user", user, g_free);
	g_object_set_data_full (G_OBJECT (priv->search_context), "system", system, g_free);
	
	rule_context_add_part_set (priv->search_context, "partset", filter_part_get_type (),
				   rule_context_add_part, rule_context_next_part);
	
	rule_context_add_rule_set (priv->search_context, "ruleset", filter_rule_get_type (),
				   rule_context_add_rule, rule_context_next_rule);
	
	rule_context_load (priv->search_context, system, user);
}

/* Local store setup.  */
char *default_drafts_folder_uri;
CamelFolder *drafts_folder = NULL;
char *default_sent_folder_uri;
CamelFolder *sent_folder = NULL;
char *default_outbox_folder_uri;
CamelFolder *outbox_folder = NULL;
char *default_inbox_folder_uri;
CamelFolder *inbox_folder = NULL;

static struct {
	char *base;
	char **uri;
	CamelFolder **folder;
} default_folders[] = {
	{ "Inbox", &default_inbox_folder_uri, &inbox_folder },
	{ "Drafts", &default_drafts_folder_uri, &drafts_folder },
	{ "Outbox", &default_outbox_folder_uri, &outbox_folder },
	{ "Sent", &default_sent_folder_uri, &sent_folder },
};

static void
setup_local_store(MailComponent *component)
{
	MailComponentPrivate *p = component->priv;
	CamelException ex;
	char *store_uri;
	int i;

	g_assert(p->local_store == NULL);

	/* EPFIXME It should use base_directory once we have moved it.  */
	store_uri = g_strconcat("mbox:", g_get_home_dir(), "/.evolution/mail/local", NULL);
	p->local_store = mail_component_load_storage_by_uri(component, store_uri, _("On this Computer"));
	camel_object_ref(p->local_store);
	
	camel_exception_init (&ex);
	for (i=0;i<sizeof(default_folders)/sizeof(default_folders[0]);i++) {
		/* FIXME: should this uri be account relative? */
		*default_folders[i].uri = g_strdup_printf("%s#%s", store_uri, default_folders[i].base);
		*default_folders[i].folder = camel_store_get_folder(p->local_store, default_folders[i].base,
								    CAMEL_STORE_FOLDER_CREATE, &ex);
		camel_exception_clear(&ex);
	}

	g_free(store_uri);
}


static BonoboControl *
create_noselect_control (void)
{
	GtkWidget *label;

	label = gtk_label_new (_("This folder cannot contain messages."));
	gtk_widget_show (label);
	return bonobo_control_new (label);
}

static GtkWidget *
create_view_callback (EStorageBrowser *browser, const char *path, void *unused_data)
{
	EMFolderTree *emft = e_storage_browser_peek_tree_widget (browser);
	BonoboControl *control;
	const char *noselect;
	const char *uri;
	CamelURL *url;
	
	if ((uri = em_folder_tree_get_selected_uri (emft)))
		return gtk_label_new ("(You should not be seeing this label)");
	
	url = camel_url_new (uri, NULL);
	noselect = url ? camel_url_get_param (url, "noselect") : NULL;
	if (noselect && !strcasecmp (noselect, "yes"))
		control = create_noselect_control ();
	else
		control = folder_browser_factory_new_control (uri);
	camel_url_free (url);
	
	if (!control)
		return NULL;
	
	/* EPFIXME: This leaks the control.  */
	return bonobo_widget_new_control_from_objref (BONOBO_OBJREF (control), CORBA_OBJECT_NIL);
}



static void
drag_text_uri_list (EMFolderTree *emft, const char *path, const char *uri, GtkSelectionData *selection, gpointer user_data)
{
	CamelFolder *src, *dest;
	const char *tmpdir;
	CamelStore *store;
	CamelException ex;
	GtkWidget *dialog;
	GPtrArray *uids;
	char *url;
	
	camel_exception_init (&ex);
	
	if (!(src = mail_tool_uri_to_folder (uri, 0, &ex))) {
		dialog = gtk_message_dialog_new ((GtkWindow *) emft, GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						 _("Could not open source folder: %s"),
						 camel_exception_get_description (&ex));
		
		gtk_dialog_run ((GtkDialog *) dialog);
		gtk_widget_destroy (dialog);
		
		camel_exception_clear (&ex);
		
		return;
	}
	
	if (!(tmpdir = e_mkdtemp ("drag-n-drop-XXXXXX"))) {
		dialog = gtk_message_dialog_new ((GtkWindow *) emft, GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						 _("Could not create temporary directory: %s"),
						 g_strerror (errno));
		
		gtk_dialog_run ((GtkDialog *) dialog);
		gtk_widget_destroy (dialog);
		
		camel_object_unref (src);
		
		return;
	}
	
	url = g_strdup_printf ("mbox:%s", tmpdir);
	if (!(store = camel_session_get_store (session, url, &ex))) {
		dialog = gtk_message_dialog_new ((GtkWindow *) emft, GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						 _("Could not create temporary mbox store: %s"),
						 camel_exception_get_description (&ex));
		
		gtk_dialog_run ((GtkDialog *) dialog);
		gtk_widget_destroy (dialog);
		
		camel_exception_clear (&ex);
		camel_object_unref (src);
		g_free (url);
		
		return;
	}
	
	if (!(dest = camel_store_get_folder (store, "mbox", CAMEL_STORE_FOLDER_CREATE, &ex))) {
		dialog = gtk_message_dialog_new ((GtkWindow *) emft, GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						 _("Could not create temporary mbox folder: %s"),
						 camel_exception_get_description (&ex));
		
		gtk_dialog_run ((GtkDialog *) dialog);
		gtk_widget_destroy (dialog);
		
		camel_exception_clear (&ex);
		camel_object_unref (store);
		camel_object_unref (src);
		g_free (url);
		
		return;
	}
	
	camel_object_unref (store);
	uids = camel_folder_get_uids (src);
	
	camel_folder_transfer_messages_to (src, uids, dest, NULL, FALSE, &ex);
	if (camel_exception_is_set (&ex)) {
		dialog = gtk_message_dialog_new ((GtkWindow *) emft, GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
						 _("Could not copy messages to temporary mbox folder: %s"),
						 camel_exception_get_description (&ex));
		
		gtk_dialog_run ((GtkDialog *) dialog);
		gtk_widget_destroy (dialog);
		
		camel_folder_free_uids (src, uids);
		camel_exception_clear (&ex);
		camel_object_unref (dest);
		camel_object_unref (src);
		g_free (url);
		
		return;
	}
	
	camel_folder_free_uids (src, uids);
	camel_object_unref (dest);
	camel_object_unref (src);
	
	memcpy (url, "file", 4);
	
	gtk_selection_data_set (selection, selection->target, 8, url, strlen (url));
	
	g_free (url);
}

static void
folder_dragged_cb (EMFolderTree *emft, const char *path, const char *uri, GdkDragContext *context,
		   GtkSelectionData *selection, guint info, guint time, gpointer user_data)
{
	printf ("dragging folder `%s'\n", path);
	
	switch (info) {
	case DND_DRAG_TYPE_FOLDER:
		/* dragging @path to a new location in the folder tree */
		gtk_selection_data_set (selection, selection->target, 8, path, strlen (path) + 1);
		break;
	case DND_DRAG_TYPE_TEXT_URI_LIST:
		/* dragging @path to some place external to evolution */
		drag_text_uri_list (emft, path, uri, selection, user_data);
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
drop_uid_list (EMFolderTree *emft, const char *path, const char *uri, gboolean move, GtkSelectionData *selection, gpointer user_data)
{
	CamelFolder *src, *dest;
	CamelException ex;
	GPtrArray *uids;
	char *src_uri;
	
	em_utils_selection_get_uidlist (selection, &src_uri, &uids);
	
	camel_exception_init (&ex);
	
	if (!(src = mail_tool_uri_to_folder (src_uri, 0, &ex))) {
		/* FIXME: report error to user? */
		camel_exception_clear (&ex);
		em_utils_uids_free (uids);
		g_free (src_uri);
		return;
	}
	
	g_free (src_uri);
	
	if (!(dest = mail_tool_uri_to_folder (uri, 0, &ex))) {
		/* FIXME: report error to user? */
		camel_exception_clear (&ex);
		em_utils_uids_free (uids);
		camel_object_unref (src);
		return;
	}
	
	camel_folder_transfer_messages_to (src, uids, dest, NULL, move, &ex);
	if (camel_exception_is_set (&ex)) {
		/* FIXME: report error to user? */
		camel_exception_clear (&ex);
		em_utils_uids_free (uids);
		camel_object_unref (dest);
		camel_object_unref (src);
		return;
	}
	
	em_utils_uids_free (uids);
	camel_object_unref (dest);
	camel_object_unref (src);
}

static void
drop_folder (EMFolderTree *emft, const char *path, const char *uri, gboolean move, GtkSelectionData *selection, gpointer user_data)
{
	CamelFolder *src, *dest;
	CamelFolder *store;
	CamelException ex;
	
	camel_exception_init (&ex);
	
	/* get the destination folder (where the user dropped). this
	 * will become the parent folder of the folder that got
	 * dragged */
	if (!(dest = mail_tool_uri_to_folder (uri, 0, &ex))) {
		/* FIXME: report error to user? */
		camel_exception_clear (&ex);
		return;
	}
	
	/* get the folder being dragged */
	if (!(src = mail_tool_uri_to_folder (selection->data, 0, &ex))) {
		/* FIXME: report error to user? */
		camel_exception_clear (&ex);
		camel_object_unref (dest);
		return;
	}
	
	if (src->parent_store == dest->parent_store && move) {
		/* simple rename() action */
		char *old_name, *new_name;
		
		old_name = g_strdup (src->full_name);
		new_name = g_strdup_printf ("%s/%s", dest->full_name, src->name);
		camel_object_unref (src);
		
		camel_store_rename_folder (dest->parent_store, old_name, new_name, &ex);
		if (camel_exception_is_set (&ex)) {
			/* FIXME: report error to user? */
			camel_exception_clear (&ex);
			camel_object_unref (dest);
			g_free (old_name);
			g_free (new_name);
			return;
		}
		
		camel_object_unref (dest);
		g_free (old_name);
		g_free (new_name);
	} else {
		/* copy the folder */
		camel_object_unref (dest);
		camel_object_unref (src);
	}
}

static gboolean
import_message_rfc822 (CamelFolder *dest, CamelStream *stream, gboolean scan_from, CamelException *ex)
{
	CamelMimeParser *mp;
	
	mp = camel_mime_parser_new ();
	camel_mime_parser_scan_from (mp, scan_from);
	camel_mime_parser_init_with_stream (mp, stream);
	
	while (camel_mime_parser_step (mp, 0, 0) == CAMEL_MIME_PARSER_STATE_FROM) {
		CamelMessageInfo *info;
		CamelMimeMessage *msg;
		
		msg = camel_mime_message_new ();
		if (camel_mime_part_construct_from_parser (CAMEL_MIME_PART (msg), mp) == -1) {
			camel_object_unref (msg);
			camel_object_unref (mp);
			return FALSE;
		}
		
		/* append the message to the folder... */
		info = g_new0 (CamelMessageInfo, 1);
		camel_folder_append_message (dest, msg, info, NULL, ex);
		camel_object_unref (msg);
		
		if (camel_exception_is_set (ex)) {
			camel_object_unref (mp);
			return FALSE;
		}
		
		/* skip over the FROM_END state */
		camel_mime_parser_step (mp, 0, 0);
	}
	
	camel_object_unref (mp);
	
	return TRUE;
}

static void
drop_message_rfc822 (EMFolderTree *emft, const char *path, const char *uri, GtkSelectionData *selection, gpointer user_data)
{
	CamelFolder *folder;
	CamelStream *stream;
	CamelException ex;
	gboolean scan_from;
	
	camel_exception_init (&ex);
	
	if (!(folder = mail_tool_uri_to_folder (uri, 0, &ex))) {
		/* FIXME: report error to user? */
		camel_exception_clear (&ex);
		return;
	}
	
	scan_from = selection->length > 5 && !strncmp (selection->data, "From ", 5);
	stream = camel_stream_mem_new_with_buffer (selection->data, selection->length);
	
	if (!import_message_rfc822 (folder, stream, scan_from, &ex)) {
		/* FIXME: report to user? */
	}
	
	camel_exception_clear (&ex);
	
	camel_object_unref (stream);
	camel_object_unref (folder);
}

static void
drop_text_uri_list (EMFolderTree *emft, const char *path, const char *uri, GtkSelectionData *selection, gpointer user_data)
{
	CamelFolder *folder;
	CamelStream *stream;
	CamelException ex;
	char **urls, *tmp;
	int i;
	
	camel_exception_init (&ex);
	
	if (!(folder = mail_tool_uri_to_folder (uri, 0, &ex))) {
		/* FIXME: report to user? */
		camel_exception_clear (&ex);
		return;
	}
	
	tmp = g_strndup (selection->data, selection->length);
	urls = g_strsplit (tmp, "\n", 0);
	g_free (tmp);
	
	for (i = 0; urls[i] != NULL; i++) {
		CamelURL *uri;
		char *url;
		int fd;
		
		/* get the path component */
		url = g_strstrip (urls[i]);
		uri = camel_url_new (url, NULL);
		g_free (url);
		
		if (!uri || strcmp (uri->protocol, "file") != 0) {
			camel_url_free (uri);
			continue;
		}
		
		url = uri->path;
		uri->path = NULL;
		camel_url_free (uri);
		
		if ((fd = open (url, O_RDONLY)) == -1) {
			g_free (url);
			continue;
		}
		
		stream = camel_stream_fs_new_with_fd (fd);
		if (!import_message_rfc822 (folder, stream, TRUE, &ex)) {
			/* FIXME: report to user? */
		}
		
		camel_exception_clear (&ex);
		camel_object_unref (stream);
		g_free (url);
	}
	
	camel_object_unref (folder);
	g_free (urls);
}

static void
folder_receive_drop_cb (EMFolderTree *emft, const char *path, const char *uri, GdkDragContext *context,
			GtkSelectionData *selection, guint info, guint time, gpointer user_data)
{
	gboolean move = context->action == GDK_ACTION_MOVE;
	
	/* this means we are receiving no data */
	if (!selection->data || selection->length == -1)
		return;
	
	switch (info) {
	case DND_DROP_TYPE_UID_LIST:
		/* import a list of uids from another evo folder */
		drop_uid_list (emft, path, uri, move, selection, user_data);
		printf ("* dropped a x-uid-list\n");
		break;
	case DND_DROP_TYPE_FOLDER:
		/* rename a folder */
		drop_folder (emft, path, uri, move, selection, user_data);
		printf ("* dropped a x-folder\n");
		break;
	case DND_DROP_TYPE_MESSAGE_RFC822:
		/* import a message/rfc822 stream */
		drop_message_rfc822 (emft, path, uri, selection, user_data);
		printf ("* dropped a message/rfc822\n");
		break;
	case DND_DROP_TYPE_TEXT_URI_LIST:
		/* import an mbox, maildir, or mh folder? */
		drop_text_uri_list (emft, path, uri, selection, user_data);
		printf ("* dropped a text/uri-list\n");
		break;
	default:
		g_assert_not_reached ();
	}
	
	gtk_drag_finish (context, TRUE, TRUE, time);
}


/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	MailComponentPrivate *priv = MAIL_COMPONENT (object)->priv;

	if (priv->storage_set != NULL) {
		g_object_unref (priv->storage_set);
		priv->storage_set = NULL;
	}

	if (priv->folder_type_registry != NULL) {
		g_object_unref (priv->folder_type_registry);
		priv->folder_type_registry = NULL;
	}

	if (priv->search_context != NULL) {
		g_object_unref (priv->search_context);
		priv->search_context = NULL;
	}

	if (priv->local_store != NULL) {
		camel_object_unref (CAMEL_OBJECT (priv->local_store));
		priv->local_store = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	MailComponentPrivate *priv = MAIL_COMPONENT (object)->priv;

	g_free (priv->base_directory);

	mail_async_event_destroy (priv->async_event);

	g_hash_table_destroy (priv->storages_hash); /* EPFIXME free the data within? */

	if (mail_async_event_destroy (priv->async_event) == -1) {
		g_warning("Cannot destroy async event: would deadlock");
		g_warning(" system may be unstable at exit");
	}

	g_free(priv->context_path);
	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Evolution::Component CORBA methods.  */

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *corba_sidebar_control,
		     Bonobo_Control *corba_view_control,
		     CORBA_Environment *ev)
{
	MailComponent *mail_component = MAIL_COMPONENT (bonobo_object_from_servant (servant));
	MailComponentPrivate *priv = mail_component->priv;
	EStorageBrowser *browser;
	GtkWidget *tree_widget;
	GtkWidget *view_widget;
	BonoboControl *sidebar_control;
	BonoboControl *view_control;
	
	browser = e_storage_browser_new (priv->storage_set, "/", create_view_callback, NULL);
	
	tree_widget = e_storage_browser_peek_tree_widget (browser);
	tree_widget_scrolled = e_storage_browser_peek_tree_widget_scrolled (browser);
	view_widget = e_storage_browser_peek_view_widget (browser);
	
	/*e_storage_set_view_set_drag_types ((EStorageSetView *) tree_widget, drag_types, num_drag_types);
	  e_storage_set_view_set_drop_types ((EStorageSetView *) tree_widget, drop_types, num_drop_types);
	  e_storage_set_view_set_allow_dnd ((EStorageSetView *) tree_widget, TRUE);*/
	
	g_signal_connect (tree_widget, "folder-dragged", G_CALLBACK (folder_dragged_cb), browser);
	g_signal_connect (tree_widget, "folder-receive_drop", G_CALLBACK (folder_receive_drop_cb), browser);
	
	gtk_widget_show (tree_widget);
	gtk_widget_show (view_widget);
	
	sidebar_control = bonobo_control_new (tree_widget);
	view_control = bonobo_control_new (view_widget);
	
	*corba_sidebar_control = CORBA_Object_duplicate (BONOBO_OBJREF (sidebar_control), ev);
	*corba_view_control = CORBA_Object_duplicate (BONOBO_OBJREF (view_control), ev);
	
	g_signal_connect_object (browser, "page_switched",
				 G_CALLBACK (browser_page_switched_callback), view_control, 0);
}


/* Initialization.  */

static void
mail_component_class_init (MailComponentClass *class)
{
	POA_GNOME_Evolution_Component__epv *epv = &class->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	epv->createControls = impl_createControls;
}

static void
mail_component_init (MailComponent *component)
{
	MailComponentPrivate *priv;
	EAccountList *accounts;
	struct stat st;
	char *mail_dir;
	
	priv = g_new0 (MailComponentPrivate, 1);
	component->priv = priv;
	
	priv->base_directory = g_build_filename (g_get_home_dir (), ".evolution", NULL);
	if (camel_mkdir (priv->base_directory, 0777) == -1 && errno != EEXIST)
		abort ();
	
	/* EPFIXME: Turn into an object?  */
	mail_session_init (priv->base_directory);
	
	priv->async_event = mail_async_event_new();
	priv->storages_hash = g_hash_table_new (NULL, NULL);
	
	priv->folder_type_registry = e_folder_type_registry_new ();
	priv->storage_set = e_storage_set_new (priv->folder_type_registry);
	
	/* migrate evolution 1.x folders to 2.0's location/format */
	mail_dir = g_strdup_printf ("%s/mail", priv->base_directory);
	if (stat (mail_dir, &st) == -1) {
		CamelException ex;
		
		camel_exception_init (&ex);
		if (em_migrate (component, &ex) == -1) {
			GtkWidget *dialog;
			
			dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
							 _("The following error occured while migrating your mail data:\n%s"),
							 camel_exception_get_description (&ex));
			
			g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
			gtk_widget_show (dialog);
			
			camel_exception_clear (&ex);
		}
	}
	g_free (mail_dir);
	
	setup_local_store (component);
	
	accounts = mail_config_get_accounts ();
	load_accounts(component, accounts);
	
	/* mail_autoreceive_setup (); EPFIXME keep it off for testing */
	
	setup_search_context (component);
	
	/* EPFIXME not sure about this.  */
	go_online (component);
}


/* Public API.  */

MailComponent *
mail_component_peek (void)
{
	static MailComponent *component = NULL;

	if (component == NULL) {
		component = g_object_new (mail_component_get_type (), NULL);

		/* FIXME: this should all be initialised in a starutp routine, not from the peek function,
		   this covers much of the ::init method's content too */
		vfolder_load_storage();
	}

	return component;
}


const char *
mail_component_peek_base_directory (MailComponent *component)
{
	return component->priv->base_directory;
}

RuleContext *
mail_component_peek_search_context (MailComponent *component)
{
	return component->priv->search_context;
}


void
mail_component_add_store (MailComponent *component,
			  CamelStore *store,
			  const char *name)
{
	CamelException ex;

	camel_exception_init (&ex);
	
	if (name == NULL) {
		char *service_name;
		
		service_name = camel_service_get_name ((CamelService *) store, TRUE);
		add_storage (component, service_name, (CamelService *) store, &ex);
		g_free (service_name);
	} else {
		add_storage (component, name, (CamelService *) store, &ex);
	}
	
	camel_exception_clear (&ex);
}


/**
 * mail_component_load_storage_by_uri:
 * @component: 
 * @uri: 
 * @name: 
 * 
 * 
 * 
 * Return value: Pointer to the newly added CamelStore.  The caller is supposed
 * to ref the object if it wants to store it.
 **/
CamelStore *
mail_component_load_storage_by_uri (MailComponent *component,
				    const char *uri,
				    const char *name)
{
	CamelException ex;
	CamelService *store;
	CamelProvider *prov;
	
	camel_exception_init (&ex);
	
	/* Load the service (don't connect!). Check its provider and
	 * see if this belongs in the shell's folder list. If so, add
	 * it.
	 */
	
	prov = camel_session_get_provider (session, uri, &ex);
	if (prov == NULL) {
		/* EPFIXME: real error dialog */
		g_warning ("couldn't get service %s: %s\n", uri,
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
		return NULL;
	}
	
	if (!(prov->flags & CAMEL_PROVIDER_IS_STORAGE) ||
	    (prov->flags & CAMEL_PROVIDER_IS_EXTERNAL))
		return NULL;
	
	store = camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, &ex);
	if (store == NULL) {
		/* EPFIXME: real error dialog */
		g_warning ("couldn't get service %s: %s\n", uri,
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
		return NULL;
	}
	
	if (name != NULL) {
		add_storage (component, name, store, &ex);
	} else {
		char *service_name;
		
		service_name = camel_service_get_name (store, TRUE);
		add_storage (component, service_name, store, &ex);
		g_free (service_name);
	}
	
	if (camel_exception_is_set (&ex)) {
		/* EPFIXME: real error dialog */
		g_warning ("Cannot load storage: %s",
			   camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
	}
	
	camel_object_unref (CAMEL_OBJECT (store));
	return CAMEL_STORE (store);		/* (Still has one ref in the hash.)  */
}


static void
store_disconnect (CamelStore *store,
		  void *event_data,
		  void *data)
{
	camel_service_disconnect (CAMEL_SERVICE (store), TRUE, NULL);
	camel_object_unref (store);
}

void
mail_component_remove_storage (MailComponent *component, CamelStore *store)
{
	MailComponentPrivate *priv = component->priv;
	char *name;
	
	/* Because the storages_hash holds a reference to each store
	 * used as a key in it, none of them will ever be gc'ed, meaning
	 * any call to camel_session_get_{service,store} with the same
	 * URL will always return the same object. So this works.
	 */
	
	if (!(name = g_hash_table_lookup (priv->store_hash, store)))
		return;
	
	g_hash_table_remove (priv->store_hash, store);
	g_free (name);
	
	/* so i guess potentially we could have a race, add a store while one
	   being removed.  ?? */
	mail_note_store_remove (store);
	
	em_folder_tree_remove_store (priv->emft, store);
	
	mail_async_event_emit (priv->async_event, MAIL_ASYNC_THREAD, (MailAsyncFunc) store_disconnect, store, NULL, NULL);
}


void
mail_component_remove_storage_by_uri (MailComponent *component,
				      const char *uri)
{
	CamelProvider *prov;
	CamelService *store;

	prov = camel_session_get_provider (session, uri, NULL);
	if (!prov)
		return;
	if (!(prov->flags & CAMEL_PROVIDER_IS_STORAGE) ||
	    (prov->flags & CAMEL_PROVIDER_IS_EXTERNAL))
		return;

	store = camel_session_get_service (session, uri, CAMEL_PROVIDER_STORE, NULL);
	if (store != NULL) {
		mail_component_remove_storage (component, CAMEL_STORE (store));
		camel_object_unref (CAMEL_OBJECT (store));
	}
}


int
mail_component_get_storage_count (MailComponent *component)
{
	return g_hash_table_size (component->priv->store_hash);
}


void
mail_component_storages_foreach (MailComponent *component, GHFunc func, void *user_data)
{
	g_hash_table_foreach (component->priv->storages_hash, func, user_data);
}

extern struct _CamelSession *session;

char *em_uri_from_camel(const char *curi)
{
	CamelURL *curl;
	EAccount *account;
	const char *uid, *path;
	char *euri;
	CamelProvider *provider;

	provider = camel_session_get_provider(session, curi, NULL);
	if (provider == NULL)
		return g_strdup(curi);

	curl = camel_url_new(curi, NULL);
	if (curl == NULL)
		return g_strdup(curi);

	account = mail_config_get_account_by_source_url(curi);
	uid = (account == NULL)?"local@local":account->uid;
	path = (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH)?curl->fragment:curl->path;
	if (path[0] == '/')
		path++;
	euri = g_strdup_printf("email://%s/%s", uid, path);
	
	d(printf("em uri from camel '%s' -> '%s'\n", curi, euri));
	
	return euri;
}

char *em_uri_to_camel(const char *euri)
{
	EAccountList *accounts;
	const EAccount *account;
	EAccountService *service;
	CamelProvider *provider;
	CamelURL *eurl, *curl;
	char *uid, *curi;

	eurl = camel_url_new(euri, NULL);
	if (eurl == NULL)
		return g_strdup(euri);

	if (strcmp(eurl->protocol, "email") != 0) {
		camel_url_free(eurl);
		return g_strdup(euri);
	}

	g_assert(eurl->user != NULL);
	g_assert(eurl->host != NULL);

	if (strcmp(eurl->user, "local") == 0 && strcmp(eurl->host, "local") == 0) {
		curi = g_strdup_printf("mbox:%s/.evolution/mail/local#%s", g_get_home_dir(), eurl->path);
		camel_url_free(eurl);
		return curi;
	}

	uid = g_strdup_printf("%s@%s", eurl->user, eurl->host);

	accounts = mail_config_get_accounts();
	account = e_account_list_find(accounts, E_ACCOUNT_FIND_UID, uid);
	g_free(uid);

	if (account == NULL) {
		camel_url_free(eurl);
		return g_strdup(euri);
	}

	service = account->source;
	provider = camel_session_get_provider(session, service->url, NULL);

	curl = camel_url_new(service->url, NULL);
	if (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH)
		camel_url_set_fragment(curl, eurl->path);
	else
		camel_url_set_path(curl, eurl->path);

	curi = camel_url_to_string(curl, 0);

	camel_url_free(eurl);
	camel_url_free(curl);

	d(printf("em uri to camel '%s' -> '%s'\n", euri, curi));

	return curi;
}


CamelFolder *
mail_component_get_folder_from_evomail_uri (MailComponent *component,
					    guint32 flags,
					    const char *evomail_uri,
					    CamelException *ex)
{
	CamelException local_ex;
	EAccountList *accounts;
	EIterator *iter;
	const char *p;
	const char *q;
	const char *folder_name;
	char *uid;

	camel_exception_init (&local_ex);

	if (strncmp (evomail_uri, "evomail:", 8) != 0)
		return NULL;

	p = evomail_uri + 8;
	while (*p == '/')
		p ++;

	q = strchr (p, '/');
	if (q == NULL)
		return NULL;

	uid = g_strndup (p, q - p);
	folder_name = q + 1;

	/* since we have no explicit account for 'local' folders, make one up */
	if (strcmp(uid, "local") == 0) {
		g_free(uid);
		return camel_store_get_folder(component->priv->local_store, folder_name, flags, ex);
	}

	accounts = mail_config_get_accounts ();
	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		EAccount *account = (EAccount *) e_iterator_get (iter);
		EAccountService *service = account->source;
		CamelProvider *provider;
		CamelStore *store;

		if (strcmp (account->uid, uid) != 0)
			continue;

		provider = camel_session_get_provider (session, service->url, &local_ex);
		if (provider == NULL)
			goto fail;

		store = (CamelStore *) camel_session_get_service (session, service->url, CAMEL_PROVIDER_STORE, &local_ex);
		if (store == NULL)
			goto fail;

		g_free (uid);
		return camel_store_get_folder (store, folder_name, flags, ex);
	}

 fail:
	camel_exception_clear (&local_ex);
	g_free (uid);
	return NULL;
}


char *
mail_component_evomail_uri_from_folder (MailComponent *component,
					CamelFolder *folder)
{
	CamelStore *store = camel_folder_get_parent_store (folder);
	EAccount *account;
	char *service_url;
	char *evomail_uri;
	const char *uid;

	if (store == NULL)
		return NULL;

	service_url = camel_service_get_url (CAMEL_SERVICE (store));
	account = mail_config_get_account_by_source_url (service_url);

	if (account == NULL) {
		/* since we have no explicit account for 'local' folders, make one up */
		/* TODO: check the folder is really a local one, folder->parent_store == local_store? */
		uid = "local";
		/*g_free (service_url);
		return NULL;*/
	} else {
		uid = account->uid;
	}

	evomail_uri = g_strconcat ("evomail:///", uid, "/", camel_folder_get_full_name (folder), NULL);
	g_free (service_url);

	return evomail_uri;
}


BONOBO_TYPE_FUNC_FULL (MailComponent, GNOME_Evolution_Component, PARENT_TYPE, mail_component)
