/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-ops.c: callbacks for the mail toolbar/menus */

/* 
 * Authors: 
 *  Dan Winship <danw@ximian.com>
 *  Peter Williams <peterw@ximian.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <errno.h>
#include <gtkhtml/gtkhtml.h>

#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprintui/gnome-print-dialog.h>
#include <libgnomeprintui/gnome-print-master-preview.h>

#include <bonobo/bonobo-widget.h>
#include <bonobo/bonobo-socket.h>
#include <gal/e-table/e-table.h>
#include <gal/widgets/e-gui-utils.h>
#include <e-util/e-dialog-utils.h>
#include <filter/filter-editor.h>

#include "mail.h"
#include "message-browser.h"
#include "mail-callbacks.h"
#include "mail-config.h"
#include "mail-accounts.h"
#include "mail-config-druid.h"
#include "mail-mt.h"
#include "mail-tools.h"
#include "mail-ops.h"
#include "mail-local.h"
#include "mail-search.h"
#include "mail-send-recv.h"
#include "mail-vfolder.h"
#include "mail-folder-cache.h"
#include "folder-browser.h"
#include "subscribe-dialog.h"
#include "message-tag-editor.h"
#include "message-tag-followup.h"
#include "e-messagebox.h"

#include "Evolution.h"
#include "evolution-storage.h"

#include "evolution-shell-client.h"

#define d(x) x

#define FB_WINDOW(fb) GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (fb), GTK_TYPE_WINDOW))


struct _composer_callback_data {
	unsigned int ref_count;
	
	CamelFolder *drafts_folder;
	char *drafts_uid;
	
	CamelFolder *folder;
	guint32 flags, set;
	char *uid;
};

static struct _composer_callback_data *
ccd_new (void)
{
	struct _composer_callback_data *ccd;
	
	ccd = g_new (struct _composer_callback_data, 1);
	ccd->ref_count = 1;
	ccd->drafts_folder = NULL;
	ccd->drafts_uid = NULL;
	ccd->folder = NULL;
	ccd->flags = 0;
	ccd->set = 0;
	ccd->uid = NULL;
	
	return ccd;
}

static void
free_ccd (struct _composer_callback_data *ccd)
{
	if (ccd->drafts_folder)
		camel_object_unref (ccd->drafts_folder);
	g_free (ccd->drafts_uid);
	
	if (ccd->folder)
		camel_object_unref (ccd->folder);
	g_free (ccd->uid);
	g_free (ccd);
}

static void
ccd_ref (struct _composer_callback_data *ccd)
{
	ccd->ref_count++;
}

static void
ccd_unref (struct _composer_callback_data *ccd)
{
	ccd->ref_count--;
	if (ccd->ref_count == 0)
		free_ccd (ccd);
}


static void
composer_destroy_cb (gpointer user_data, GObject *deadbeef)
{
	ccd_unref (user_data);
}


static void
druid_destroy_cb (gpointer user_data, GObject *deadbeef)
{
	gtk_main_quit ();
}

static gboolean
configure_mail (FolderBrowser *fb)
{
	MailConfigDruid *druid;
	GtkWidget *dialog;
	int resp;

	dialog = gtk_message_dialog_new (FB_WINDOW (fb), GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s",
					 _("You have not configured the mail client.\n"
					   "You need to do this before you can send,\n"
					   "receive or compose mail.\n"
					   "Would you like to configure it now?"));
	
	gtk_dialog_set_default_response ((GtkDialog *) dialog, GTK_RESPONSE_YES);
	
	resp = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy(dialog);

	switch (resp) {
	case GTK_RESPONSE_YES:
		druid = mail_config_druid_new (fb->shell);
		g_object_weak_ref ((GObject *) druid, (GWeakNotify) druid_destroy_cb, NULL);
		gtk_widget_show ((GtkWidget *)druid);
		gtk_grab_add ((GtkWidget *)druid);
		gtk_main ();
		break;
	case GTK_RESPONSE_NO:
	default:
		break;
	}
	
	return mail_config_is_configured ();
}

static gboolean
check_send_configuration (FolderBrowser *fb)
{
	const MailConfigAccount *account;
	GtkWidget *dialog;
	
	/* Check general */
	if (!mail_config_is_configured () && !configure_mail (fb))
		return FALSE;
	
	/* Get the default account */
	account = mail_config_get_default_account ();
	
	/* Check for an identity */
	if (!account) {
		dialog = gtk_message_dialog_new (FB_WINDOW (fb), GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
						 "%s", _("You need to configure an identity\n"
							 "before you can compose mail."));
		
		g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
		gtk_widget_show (dialog);
		
		return FALSE;
	}
	
	/* Check for a transport */
	if (!account->transport || !account->transport->url) {
		dialog = gtk_message_dialog_new (FB_WINDOW (fb), GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
						 "%s", _("You need to configure a mail transport\n"
							 "before you can compose mail."));
		
		g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
		gtk_widget_show (dialog);
		
		return FALSE;
	}
	
	return TRUE;
}

static void
msgbox_destroy_cb (gpointer user_data, GObject *widget)
{
	gboolean *show_again = user_data;
	GtkWidget *checkbox;
	
	checkbox = e_message_box_get_checkbox (E_MESSAGE_BOX (widget));
	*show_again = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox));
}

static gboolean
ask_confirm_for_unwanted_html_mail (EMsgComposer *composer, EDestination **recipients)
{
	gboolean show_again = TRUE;
	GString *str;
	GtkWidget *mbox;
	int i, button;
	
	if (!mail_config_get_confirm_unwanted_html ()) {
		g_message ("doesn't want to see confirm html messages!");
		return TRUE;
	}
	
	/* FIXME: this wording sucks */
	str = g_string_new (_("You are sending an HTML-formatted message. Please make sure that\n"
			      "the following recipients are willing and able to receive HTML mail:\n"));
	for (i = 0; recipients[i] != NULL; ++i) {
		if (!e_destination_get_html_mail_pref (recipients[i])) {
			const char *name;
			
			name = e_destination_get_textrep (recipients[i]);
			
			g_string_sprintfa (str, "     %s\n", name);
		}
	}
	
	g_string_append (str, _("Send anyway?"));
	
	mbox = e_message_box_new (str->str,
				  E_MESSAGE_BOX_QUESTION,
				  GTK_STOCK_YES,
				  GTK_STOCK_NO,
				  NULL);
	
	g_string_free (str, TRUE);
	
	g_object_weak_ref ((GObject *) mbox, (GWeakNotify) msgbox_destroy_cb, &show_again);
	
	button = gtk_dialog_run (GTK_DIALOG (mbox));
	
	if (!show_again) {
		mail_config_set_confirm_unwanted_html (show_again);
		g_message ("don't show HTML warning again");
	}
	
	return (button == GTK_RESPONSE_YES);
}

static gboolean
ask_confirm_for_empty_subject (EMsgComposer *composer)
{
	/* FIXME: EMessageBox should really handle this stuff
           automagically. What Miguel thinks would be nice is to pass
           in a unique id which could be used as a key in the config
           file and the value would be an int. -1 for always show or
           the button pressed otherwise. This probably means we'd have
           to write e_messagebox_run () */
	gboolean show_again = TRUE;
	GtkWidget *mbox;
	int button;
	
	if (!mail_config_get_prompt_empty_subject ())
		return TRUE;
	
	mbox = e_message_box_new (_("This message has no subject.\nReally send?"),
				  E_MESSAGE_BOX_QUESTION,
				  GTK_STOCK_YES,
				  GTK_STOCK_NO,
				  NULL);
	
	g_object_weak_ref ((GObject *) mbox, (GWeakNotify) msgbox_destroy_cb, &show_again);
	
	button = gtk_dialog_run (GTK_DIALOG (mbox));
	
	mail_config_set_prompt_empty_subject (show_again);
	
	return (button == GTK_RESPONSE_YES);
}

static gboolean
ask_confirm_for_only_bcc (EMsgComposer *composer, gboolean hidden_list_case)
{
	/* FIXME: EMessageBox should really handle this stuff
           automagically. What Miguel thinks would be nice is to pass
           in a message-id which could be used as a key in the config
           file and the value would be an int. -1 for always show or
           the button pressed otherwise. This probably means we'd have
           to write e_messagebox_run () */
	gboolean show_again = TRUE;
	GtkWidget *mbox;
	int button;
	const char *first_text;
	char *message_text;
	
	if (!mail_config_get_prompt_only_bcc ())
		return TRUE;
	
	/* If the user is mailing a hidden contact list, it is possible for
	   them to create a message with only Bcc recipients without really
	   realizing it.  To try to avoid being totally confusing, I've changed
	   this dialog to provide slightly different text in that case, to
	   better explain what the hell is going on. */
	
	if (hidden_list_case) {
		first_text =  _("Since the contact list you are sending to "
				"is configured to hide the list's addresses, "
				"this message will contain only Bcc recipients.");
	} else {
		first_text = _("This message contains only Bcc recipients.");
	}
	
	message_text = g_strdup_printf ("%s\n%s", first_text,
					_("It is possible that the mail server may reveal the recipients "
					  "by adding an Apparently-To header.\nSend anyway?"));
	
	mbox = e_message_box_new (message_text, 
				  E_MESSAGE_BOX_QUESTION,
				  GNOME_STOCK_BUTTON_YES,
				  GNOME_STOCK_BUTTON_NO,
				  NULL);
	
	g_object_weak_ref ((GObject *) mbox, (GWeakNotify) msgbox_destroy_cb, &show_again);
	g_free (message_text);
	
	button = gtk_dialog_run (GTK_DIALOG (mbox));
	
	mail_config_set_prompt_only_bcc (show_again);
	
	return (button == GTK_RESPONSE_YES);
}


struct _send_data {
	struct _composer_callback_data *ccd;
	EMsgComposer *composer;
	gboolean send;
};

static void
composer_send_queued_cb (CamelFolder *folder, CamelMimeMessage *msg, CamelMessageInfo *info,
			 int queued, const char *appended_uid, void *data)
{
	struct _composer_callback_data *ccd;
	struct _send_data *send = data;
	
	ccd = send->ccd;
	
	if (queued) {
		if (ccd && ccd->drafts_folder) {
			/* delete the old draft message */
			camel_folder_set_message_flags (ccd->drafts_folder, ccd->drafts_uid,
							CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
							CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
			camel_object_unref (ccd->drafts_folder);
			ccd->drafts_folder = NULL;
			g_free (ccd->drafts_uid);
			ccd->drafts_uid = NULL;
		}
		
		if (ccd && ccd->folder) {
			/* set any replied flags etc */
			camel_folder_set_message_flags (ccd->folder, ccd->uid, ccd->flags, ccd->set);
			camel_object_unref (ccd->folder);
			ccd->folder = NULL;
			g_free (ccd->uid);
			ccd->uid = NULL;
		}
		
		gtk_widget_destroy (GTK_WIDGET (send->composer));
		
		if (send->send && camel_session_is_online (session)) {
			/* queue a message send */
			mail_send ();
		}
	} else {
		if (!ccd) {
			ccd = ccd_new ();
			
			/* disconnect the previous signal handlers */
			g_signal_handlers_disconnect_matched(send->composer, G_SIGNAL_MATCH_FUNC, 0,
							     0, NULL, composer_send_cb, NULL);
			g_signal_handlers_disconnect_matched(send->composer, G_SIGNAL_MATCH_FUNC, 0,
							     0, NULL, composer_save_draft_cb, NULL);
			
			/* reconnect to the signals using a non-NULL ccd for the callback data */
			g_signal_connect (send->composer, "send", G_CALLBACK (composer_send_cb), ccd);
			g_signal_connect (send->composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
			
			g_object_weak_ref ((GObject *) send->composer, (GWeakNotify) composer_destroy_cb, ccd);
		}
		
		e_msg_composer_set_enable_autosave (send->composer, TRUE);
		gtk_widget_show (GTK_WIDGET (send->composer));
		g_object_unref (send->composer);
	}
	
	camel_message_info_free (info);
	
	if (send->ccd)
		ccd_unref (send->ccd);
	
	g_free (send);
}

static CamelMimeMessage *
composer_get_message (EMsgComposer *composer, gboolean post, gboolean save_html_object_data)
{
	const MailConfigAccount *account;
	CamelMimeMessage *message = NULL;
	EDestination **recipients, **recipients_bcc;
	char *subject;
	int i;
	int hidden = 0, shown = 0;
	int num = 0, num_bcc = 0;
	
	/* We should do all of the validity checks based on the composer, and not on
	   the created message, as extra interaction may occur when we get the message
	   (e.g. to get a passphrase to sign a message) */
	
	/* get the message recipients */
	recipients = e_msg_composer_get_recipients (composer);
	
	/* see which ones are visible/present, etc */
	if (recipients) {
		for (i = 0; recipients[i] != NULL; i++) {
			const char *addr = e_destination_get_address (recipients[i]);
			
			if (addr && addr[0]) {
				num++;
				if (e_destination_is_evolution_list (recipients[i])
				    && !e_destination_list_show_addresses (recipients[i])) {
					hidden++;
				} else {
					shown++;
				}
			}
		}
	}
	
	recipients_bcc = e_msg_composer_get_bcc (composer);
	if (recipients_bcc) {
		for (i = 0; recipients_bcc[i] != NULL; i++) {
			const char *addr = e_destination_get_address (recipients_bcc[i]);
			
			if (addr && addr[0])
				num_bcc++;
		}
		e_destination_freev (recipients_bcc);
	}
	
	/* I'm sensing a lack of love, er, I mean recipients. */
	if (num == 0 && !post) {
		GtkWidget *dialog;
		
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_CLOSE, "%s",
						 _("You must specify recipients in order to "
						   "send this message."));
		
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		goto finished;
	}
	
	if (num > 0 && (num == num_bcc || shown == 0)) {
		/* this means that the only recipients are Bcc's */		
		if (!ask_confirm_for_only_bcc (composer, shown == 0))
			goto finished;
	}
	
	/* Only show this warning if our default is to send html.  If it isn't, we've
	   manually switched into html mode in the composer and (presumably) had a good
	   reason for doing this. */
	if (e_msg_composer_get_send_html (composer) && mail_config_get_send_html ()
	    && mail_config_get_confirm_unwanted_html ()) {
		gboolean html_problem = FALSE;
		
		if (recipients) {
			for (i = 0; recipients[i] != NULL && !html_problem; ++i) {
				if (!e_destination_get_html_mail_pref (recipients[i]))
					html_problem = TRUE;
			}
		}
		
		if (html_problem) {
			html_problem = !ask_confirm_for_unwanted_html_mail (composer, recipients);
			if (html_problem)
				goto finished;
		}
	}
	
	/* Check for no subject */
	subject = e_msg_composer_get_subject (composer);
	if (subject == NULL || subject[0] == '\0') {
		if (!ask_confirm_for_empty_subject (composer)) {
			g_free (subject);
			goto finished;
		}
	}
	g_free (subject);
	
	/* actually get the message now, this will sign/encrypt etc */
	message = e_msg_composer_get_message (composer, save_html_object_data);
	if (message == NULL)
		goto finished;
	
	/* Add info about the sending account */
	account = e_msg_composer_get_preferred_account (composer);
	
	if (account) {
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Account", account->name);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Transport", account->transport->url);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-Evolution-Fcc", account->sent_folder_uri);
		if (account->id->organization)
			camel_medium_set_header (CAMEL_MEDIUM (message), "Organization", account->id->organization);
	}
	
	/* Get the message recipients and 'touch' them, boosting their use scores */
	if (recipients)
		e_destination_touchv (recipients);
	
 finished:
	
	if (recipients)
		e_destination_freev (recipients);
	
	return message;
}

static void
got_post_folder (char *uri, CamelFolder *folder, void *data)
{
	CamelFolder **fp = data;
	
	*fp = folder;
	
	if (folder)
		camel_object_ref (folder);
}

void
composer_send_cb (EMsgComposer *composer, gpointer user_data)
{
	extern CamelFolder *outbox_folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	struct _send_data *send;
	gboolean post = FALSE;
	CamelFolder *folder;
	XEvolution *xev;
	char *url;
	
	url = e_msg_composer_hdrs_get_post_to ((EMsgComposerHdrs *) composer->hdrs);
	if (url && *url) {
		post = TRUE;
		
		mail_msg_wait (mail_get_folder (url, 0, got_post_folder, &folder, mail_thread_new));
		
		if (!folder) {
			g_free (url);
			return;
		}
	} else {
		folder = outbox_folder;
		camel_object_ref (folder);
	}
	
	g_free (url);
	
	message = composer_get_message (composer, post, FALSE);
	if (!message)
		return;
	
	if (post) {
		/* Remove the X-Evolution* headers if we are in Post-To mode */
		xev = mail_tool_remove_xevolution_headers (message);
		mail_tool_destroy_xevolution (xev);
	}
	
	info = camel_message_info_new ();
	info->flags = CAMEL_MESSAGE_SEEN;
	
	send = g_malloc (sizeof (*send));
	send->ccd = user_data;
	if (send->ccd)
		ccd_ref (send->ccd);
	send->send = !post;
	send->composer = composer;
	g_object_ref (composer);
	gtk_widget_hide (GTK_WIDGET (composer));
	
	e_msg_composer_set_enable_autosave (composer, FALSE);
	
	mail_append_mail (folder, message, info, composer_send_queued_cb, send);
	camel_object_unref (message);
	camel_object_unref (folder);
}

struct _save_draft_info {
	struct _composer_callback_data *ccd;
	EMsgComposer *composer;
	int quit;
};

static void
save_draft_done (CamelFolder *folder, CamelMimeMessage *msg, CamelMessageInfo *info, int ok,
		 const char *appended_uid, void *user_data)
{
	struct _save_draft_info *sdi = user_data;
	struct _composer_callback_data *ccd;
	CORBA_Environment ev;
	
	if (!ok)
		goto done;
	CORBA_exception_init (&ev);
	GNOME_GtkHTML_Editor_Engine_runCommand (sdi->composer->editor_engine, "saved", &ev);
	CORBA_exception_free (&ev);
	
	if ((ccd = sdi->ccd) == NULL) {
		ccd = ccd_new ();
		
		/* disconnect the previous signal handlers */
		gtk_signal_disconnect_by_func (GTK_OBJECT (sdi->composer),
					       G_CALLBACK (composer_send_cb), NULL);
		gtk_signal_disconnect_by_func (GTK_OBJECT (sdi->composer),
					       G_CALLBACK (composer_save_draft_cb), NULL);
		
		/* reconnect to the signals using a non-NULL ccd for the callback data */
		g_signal_connect (sdi->composer, "send", G_CALLBACK (composer_send_cb), ccd);
		g_signal_connect (sdi->composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
		
		g_object_weak_ref ((GObject *) sdi->composer, (GWeakNotify) composer_destroy_cb, ccd);
	}
	
	if (ccd->drafts_folder) {
		/* delete the original draft message */
		camel_folder_set_message_flags (ccd->drafts_folder, ccd->drafts_uid,
						CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
						CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
		camel_object_unref (ccd->drafts_folder);
		ccd->drafts_folder = NULL;
		g_free (ccd->drafts_uid);
		ccd->drafts_uid = NULL;
	}
	
	if (ccd->folder) {
		/* set the replied flags etc */
		camel_folder_set_message_flags (ccd->folder, ccd->uid, ccd->flags, ccd->set);
		camel_object_unref (ccd->folder);
		ccd->folder = NULL;
		g_free (ccd->uid);
		ccd->uid = NULL;
	}
	
	if (appended_uid) {
		camel_object_ref (folder);
		ccd->drafts_folder = folder;
		ccd->drafts_uid = g_strdup (appended_uid);
	}
	
	if (sdi->quit)
		gtk_widget_destroy (GTK_WIDGET (sdi->composer));
	
 done:
	g_object_unref (sdi->composer);
	if (sdi->ccd)
		ccd_unref (sdi->ccd);
	g_free (info);
	g_free (sdi);
}

static void
use_default_drafts_cb (int reply, gpointer data)
{
	extern CamelFolder *drafts_folder;
	CamelFolder **folder = data;
	
	if (reply == 0) {
		*folder = drafts_folder;
		camel_object_ref (drafts_folder);
	}
}

static void
save_draft_folder (char *uri, CamelFolder *folder, gpointer data)
{
	CamelFolder **save = data;
	
	if (folder) {
		*save = folder;
		camel_object_ref (folder);
	}
}

void
composer_save_draft_cb (EMsgComposer *composer, int quit, gpointer user_data)
{
	extern char *default_drafts_folder_uri;
	extern CamelFolder *drafts_folder;
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	const MailConfigAccount *account;
	struct _save_draft_info *sdi;
	CamelFolder *folder = NULL;
	
	account = e_msg_composer_get_preferred_account (composer);
	if (account && account->drafts_folder_uri &&
	    strcmp (account->drafts_folder_uri, default_drafts_folder_uri) != 0) {
		int id;
		
		id = mail_get_folder (account->drafts_folder_uri, 0, save_draft_folder, &folder, mail_thread_new);
		mail_msg_wait (id);
		
		if (!folder) {
			GtkWidget *dialog;
			int response;
			
			dialog = gtk_message_dialog_new (GTK_WINDOW (composer), GTK_DIALOG_MODAL,
							 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
							 "%s", _("Unable to open the drafts folder for this account.\n"
								 "Would you like to use the default drafts folder?"));
			
			response = gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			
			if (response != GTK_RESPONSE_YES)
				return;
			
			folder = drafts_folder;
			camel_object_ref (drafts_folder);
		}
	} else {
		folder = drafts_folder;
		camel_object_ref (folder);
	}
	
	msg = e_msg_composer_get_message_draft (composer);
	
	info = g_new0 (CamelMessageInfo, 1);
	info->flags = CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_SEEN;
	
	sdi = g_malloc (sizeof (struct _save_draft_info));
	sdi->composer = composer;
	g_object_ref (composer);
	sdi->ccd = user_data;
	if (sdi->ccd)
		ccd_ref (sdi->ccd);
	sdi->quit = quit;
	
	mail_append_mail (folder, msg, info, save_draft_done, sdi);
	camel_object_unref (folder);
	camel_object_unref (msg);
}

static GtkWidget *
create_msg_composer (const MailConfigAccount *account, gboolean post, const char *url)
{
	EMsgComposer *composer;
	gboolean send_html;
	
	/* Make sure that we've actually been passed in an account. If one has
	 * not been passed in, grab the default account.
	 */
	if (account == NULL) {
		account = mail_config_get_default_account ();
	}
	
	send_html = mail_config_get_send_html ();
	
	if (post)
		composer = e_msg_composer_new_post ();
	else if (url)
		composer = e_msg_composer_new_from_url (url);
	else
		composer = e_msg_composer_new ();
	
	if (composer) {
		e_msg_composer_hdrs_set_from_account (E_MSG_COMPOSER_HDRS (composer->hdrs), account->name);
		e_msg_composer_set_send_html (composer, send_html);
		e_msg_composer_unset_changed (composer);
		e_msg_composer_drop_editor_undo (composer);
		return GTK_WIDGET (composer);
	} else
		return NULL;
}

void
compose_msg (GtkWidget *widget, gpointer user_data)
{
	const MailConfigAccount *account;
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	struct _composer_callback_data *ccd;
	GtkWidget *composer;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	/* Figure out which account we want to initially compose from */
	account = mail_config_get_account_by_source_url (fb->uri);
	
	composer = create_msg_composer (account, FALSE, NULL);
	if (!composer)
		return;
	
	ccd = ccd_new ();
	
	g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
	g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
	
	g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
	
	gtk_widget_show (composer);
}

/* Send according to a mailto (RFC 2368) URL. */
void
send_to_url (const char *url)
{
	struct _composer_callback_data *ccd;
	GtkWidget *composer;
	
	/* FIXME: no way to get folder browser? Not without
	 * big pain in the ass, as far as I can tell */
	if (!check_send_configuration (NULL))
		return;
	
	/* Tell create_msg_composer to use the default email profile */
	composer = create_msg_composer (NULL, FALSE, url);
	if (!composer)
		return;
	
	ccd = ccd_new ();
	
	g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
	g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
	
	g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
	
	gtk_widget_show (composer);
}	

static GList *
list_add_addresses (GList *list, const CamelInternetAddress *cia, GHashTable *account_hash,
		    GHashTable *rcpt_hash, const MailConfigAccount **me)
{
	const MailConfigAccount *account;
	const char *name, *addr;
	int i;
	
	for (i = 0; camel_internet_address_get (cia, i, &name, &addr); i++) {
		/* Here I'll check to see if the cc:'d address is the address
		   of the sender, and if so, don't add it to the cc: list; this
		   is to fix Bugzilla bug #455. */
		account = g_hash_table_lookup (account_hash, addr);
		if (account && me && !*me)
			*me = account;
		
		if (!account && !g_hash_table_lookup (rcpt_hash, addr)) {
			EDestination *dest;
			
			dest = e_destination_new ();
			e_destination_set_name (dest, name);
			e_destination_set_email (dest, addr);
			
			list = g_list_append (list, dest);
			g_hash_table_insert (rcpt_hash, (char *) addr, GINT_TO_POINTER (1));
		}
	}
	
	return list;
}

static const MailConfigAccount *
guess_me (const CamelInternetAddress *to, const CamelInternetAddress *cc, GHashTable *account_hash)
{
	const MailConfigAccount *account = NULL;
	const char *addr;
	int i;
	
	/* "optimization" */
	if (!to && !cc)
		return NULL;
	
	if (to) {
		for (i = 0; camel_internet_address_get (to, i, NULL, &addr); i++) {
			account = g_hash_table_lookup (account_hash, addr);
			if (account)
				goto found;
		}
	}
	
	if (cc) {
		for (i = 0; camel_internet_address_get (cc, i, NULL, &addr); i++) {
			account = g_hash_table_lookup (account_hash, addr);
			if (account)
				goto found;
		}
	}
	
 found:
	
	return account;
}

static const MailConfigAccount *
guess_me_from_accounts (const CamelInternetAddress *to, const CamelInternetAddress *cc, const GSList *accounts)
{
	const MailConfigAccount *account;
	GHashTable *account_hash;
	const GSList *l;
	
	account_hash = g_hash_table_new (g_strcase_hash, g_strcase_equal);
	l = accounts;
	while (l) {
		account = l->data;
		g_hash_table_insert (account_hash, (char *) account->id->address, (void *) account);
		l = l->next;
	}
	
	account = guess_me (to, cc, account_hash);
	
	g_hash_table_destroy (account_hash);
	
	return account;
}

inline static void
mail_ignore (EMsgComposer *composer, const char *name, const char *address)
{
	e_msg_composer_ignore (composer, name && *name ? name : address);
}

static void
mail_ignore_address (EMsgComposer *composer, const CamelInternetAddress *addr)
{
	const char *name, *address;
	int i, max;
	
	max = camel_address_length (CAMEL_ADDRESS (addr));
	for (i = 0; i < max; i++) {
		camel_internet_address_get (addr, i, &name, &address);
		mail_ignore (composer, name, address);
	}
}

#define MAX_SUBJECT_LEN  1024

static EMsgComposer *
mail_generate_reply (CamelFolder *folder, CamelMimeMessage *message, const char *uid, int mode)
{
	const CamelInternetAddress *reply_to, *sender, *to_addrs, *cc_addrs;
	const char *name = NULL, *address = NULL, *source = NULL;
	const char *message_id, *references, *mlist = NULL;
	char *text = NULL, *subject, format[256];
	const MailConfigAccount *def, *account, *me = NULL;
	const GSList *l, *accounts = NULL;
	GHashTable *account_hash = NULL;
	CamelMessageInfo *info = NULL;
	GList *to = NULL, *cc = NULL;
	EDestination **tov, **ccv;
	EMsgComposer *composer;
	CamelMimePart *part;
	time_t date;
	char *url;
	
	if (mode == REPLY_POST) {
		composer = e_msg_composer_new_post ();
		if (composer != NULL) {
			url = mail_tools_folder_to_url (folder);
			e_msg_composer_hdrs_set_post_to ((EMsgComposerHdrs *) composer->hdrs, url);
			g_free (url);
		}
	} else
		composer = e_msg_composer_new ();
	
	if (!composer)
		return NULL;
	
	e_msg_composer_add_message_attachments (composer, message, TRUE);
	
	/* Set the recipients */
	accounts = mail_config_get_accounts ();
	account_hash = g_hash_table_new (g_strcase_hash, g_strcase_equal);
	
	/* add the default account to the hash first */
	if ((def = mail_config_get_default_account ())) {
		if (def->id->address)
			g_hash_table_insert (account_hash, (char *) def->id->address, (void *) def);
	}
	
	l = accounts;
	while (l) {
		account = l->data;
		
		if (account->id->address) {
			const MailConfigAccount *acnt;
			
			/* Accounts with identical email addresses that are enabled
			 * take precedence over the accounts that aren't. If all
			 * accounts with matching email addresses are disabled, then
			 * the first one in the list takes precedence. The default
			 * account always takes precedence no matter what.
			 */
			acnt = g_hash_table_lookup (account_hash, account->id->address);
			if (acnt && acnt != def && !acnt->source->enabled && account->source->enabled) {
				g_hash_table_remove (account_hash, acnt->id->address);
				acnt = NULL;
			}
			
			if (!acnt)
				g_hash_table_insert (account_hash, (char *) account->id->address, (void *) account);
		}
		
		l = l->next;
	}
	
	to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	mail_ignore_address (composer, to_addrs);
	mail_ignore_address (composer, cc_addrs);
	
	if (mode == REPLY_LIST) {
		/* make sure we can reply to an mlist */
		info = camel_folder_get_message_info (folder, uid);
		if (!(mlist = camel_message_info_mlist (info)) || *mlist == '\0') {
			camel_folder_free_message_info (folder, info);
			mode = REPLY_ALL;
			info = NULL;
		}
	}
	
 determine_recipients:
	if (mode == REPLY_LIST) {
		EDestination *dest;
		int i, max;
		
		/* look through the recipients to find the *real* mailing list address */
		d(printf ("we are looking for the mailing list called: %s\n", mlist));
		
		max = camel_address_length (CAMEL_ADDRESS (to_addrs));
		for (i = 0; i < max; i++) {
			camel_internet_address_get (to_addrs, i, &name, &address);
			if (!strcasecmp (address, mlist))
				break;
		}
		
		if (i == max) {
			max = camel_address_length (CAMEL_ADDRESS (cc_addrs));
			for (i = 0; i < max; i++) {
				camel_internet_address_get (cc_addrs, i, &name, &address);
				if (!strcasecmp (address, mlist))
					break;
			}
		}
		
		if (address && i != max) {
			dest = e_destination_new ();
			e_destination_set_name (dest, name);
			e_destination_set_email (dest, address);
			
			to = g_list_append (to, dest);
		} else {
			/* mailing list address wasn't found */
			if (strchr (mlist, '@')) {
				/* mlist string has an @, maybe it's valid? */
				dest = e_destination_new ();
				e_destination_set_email (dest, mlist);
				
				to = g_list_append (to, dest);
			} else {
				/* give up and just reply to all recipients? */
				mode = REPLY_ALL;
				camel_folder_free_message_info (folder, info);
				goto determine_recipients;
			}
		}
		
		me = guess_me (to_addrs, cc_addrs, account_hash);
		camel_folder_free_message_info (folder, info);
	} else {
		GHashTable *rcpt_hash;
		EDestination *dest;
		
		rcpt_hash = g_hash_table_new (g_strcase_hash, g_strcase_equal);
		
		reply_to = camel_mime_message_get_reply_to (message);
		if (!reply_to)
			reply_to = camel_mime_message_get_from (message);
		
		if (reply_to) {
			int i;
			
			for (i = 0; camel_internet_address_get (reply_to, i, &name, &address); i++) {
				/* ignore references to the Reply-To address in the To and Cc lists */
				if (address && !(mode == REPLY_ALL && g_hash_table_lookup (account_hash, address))) {
					/* In the case that we are doing a Reply-To-All, we do not want
					   to include the user's email address because replying to oneself
					   is kinda silly. */
					dest = e_destination_new ();
					e_destination_set_name (dest, name);
					e_destination_set_email (dest, address);
					to = g_list_append (to, dest);
					g_hash_table_insert (rcpt_hash, (char *) address, GINT_TO_POINTER (1));
					mail_ignore (composer, name, address);
				}
			}
		}
		
		if (mode == REPLY_ALL) {
			cc = list_add_addresses (cc, to_addrs, account_hash, rcpt_hash, me ? NULL : &me);
			cc = list_add_addresses (cc, cc_addrs, account_hash, rcpt_hash, me ? NULL : &me);
			
			/* promote the first Cc: address to To: if To: is empty */
			if (to == NULL && cc != NULL) {
				to = cc;
				cc = g_list_remove_link (cc, to);
			}
		} else {
			me = guess_me (to_addrs, cc_addrs, account_hash);
		}
		
		g_hash_table_destroy (rcpt_hash);
	}
	
	g_hash_table_destroy (account_hash);
	
	if (!me) {
		/* default 'me' to the source account... */
		if ((source = camel_mime_message_get_source (message)))
			me = mail_config_get_account_by_source_url (source);
	}
	
	/* set body text here as we want all ignored words to take effect */
	switch (mail_config_get_default_reply_style ()) {
	case MAIL_CONFIG_REPLY_DO_NOT_QUOTE:
		/* do nothing */
		break;
	case MAIL_CONFIG_REPLY_ATTACH:
		/* attach the original message as an attachment */
		part = mail_tool_make_message_attachment (message);
		e_msg_composer_attach (composer, part);
		camel_object_unref (part);
		break;
	case MAIL_CONFIG_REPLY_QUOTED:
	default:
		/* do what any sane user would want when replying... */
		sender = camel_mime_message_get_from (message);
		if (sender != NULL && camel_address_length (CAMEL_ADDRESS (sender)) > 0) {
			camel_internet_address_get (sender, 0, &name, &address);
		} else {
			name = _("an unknown sender");
		}
		
		date = camel_mime_message_get_date (message, NULL);
		strftime (format, sizeof (format), _("On %a, %Y-%m-%d at %H:%M, %%s wrote:"), localtime (&date));
		text = mail_tool_quote_message (message, format, name && *name ? name : address);
		mail_ignore (composer, name, address);
		if (text) {
			e_msg_composer_set_body_text (composer, text);
			g_free (text);
		}
		break;
	}
	
	/* Set the subject of the new message. */
	subject = (char *) camel_mime_message_get_subject (message);
	if (!subject)
		subject = g_strdup ("");
	else {
		if (!strncasecmp (subject, "Re: ", 4))
			subject = g_strndup (subject, MAX_SUBJECT_LEN);
		else {
			if (strlen (subject) < MAX_SUBJECT_LEN) {
				subject = g_strdup_printf ("Re: %s", subject);
			} else {
				/* We can't use %.*s because it depends on the locale being C/POSIX
                                   or UTF-8 to work correctly in glibc */
				char *sub;
				
				/*subject = g_strdup_printf ("Re: %.*s...", MAX_SUBJECT_LEN, subject);*/
				sub = g_malloc (MAX_SUBJECT_LEN + 8);
				memcpy (sub, "Re: ", 4);
				memcpy (sub + 4, subject, MAX_SUBJECT_LEN);
				memcpy (sub + 4 + MAX_SUBJECT_LEN, "...", 4);
				subject = sub;
			}
		}
	}
	
	tov = e_destination_list_to_vector (to);
	ccv = e_destination_list_to_vector (cc);
	
	g_list_free (to);
	g_list_free (cc);
	
	e_msg_composer_set_headers (composer, me ? me->name : NULL, tov, ccv, NULL, subject);
	
	e_destination_freev (tov);
	e_destination_freev (ccv);
	
	g_free (subject);
	
	/* Add In-Reply-To and References. */
	message_id = camel_medium_get_header (CAMEL_MEDIUM (message), "Message-Id");
	references = camel_medium_get_header (CAMEL_MEDIUM (message), "References");
	if (message_id) {
		char *reply_refs;
		
		e_msg_composer_add_header (composer, "In-Reply-To", message_id);
		
		if (references)
			reply_refs = g_strdup_printf ("%s %s", references, message_id);
		else
			reply_refs = g_strdup (message_id);
		
		e_msg_composer_add_header (composer, "References", reply_refs);
		g_free (reply_refs);
	} else if (references) {
		e_msg_composer_add_header (composer, "References", references);
	}
	
	e_msg_composer_drop_editor_undo (composer);
	
	return composer;
}

static void
requeue_mail_reply (CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *data)
{
	int mode = GPOINTER_TO_INT (data);
	
	if (msg != NULL)
		mail_reply (folder, msg, uid, mode);
}

void
mail_reply (CamelFolder *folder, CamelMimeMessage *msg, const char *uid, int mode)
{
	struct _composer_callback_data *ccd;
	EMsgComposer *composer;
	
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	
	if (!msg) {
		mail_get_message (folder, uid, requeue_mail_reply,
				  GINT_TO_POINTER (mode), mail_thread_new);
		return;
	}
	
	composer = mail_generate_reply (folder, msg, uid, mode);
	if (!composer)
		return;
	
	ccd = ccd_new ();
	
	camel_object_ref (folder);
	ccd->folder = folder;
	ccd->uid = g_strdup (uid);
	ccd->flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_SEEN;
	if (mode == REPLY_LIST || mode == REPLY_ALL)
		ccd->flags |= CAMEL_MESSAGE_ANSWERED_ALL;
	ccd->set = ccd->flags;
	
	g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
	g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
	
	g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
	
	gtk_widget_show (GTK_WIDGET (composer));	
	e_msg_composer_unset_changed (composer);
}

void
reply_to_sender (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	mail_reply (fb->folder, NULL, fb->message_list->cursor_uid, REPLY_SENDER);
}

void
reply_to_list (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	mail_reply (fb->folder, NULL, fb->message_list->cursor_uid, REPLY_LIST);
}

void
reply_to_all (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	mail_reply(fb->folder, NULL, fb->message_list->cursor_uid, REPLY_ALL);
}

void
enumerate_msg (MessageList *ml, const char *uid, gpointer data)
{
	g_return_if_fail (ml != NULL);
	
	g_ptr_array_add ((GPtrArray *) data, g_strdup (uid));
}


static EMsgComposer *
forward_get_composer (CamelMimeMessage *message, const char *subject)
{
	const MailConfigAccount *account = NULL;
	struct _composer_callback_data *ccd;
	EMsgComposer *composer;
	
	if (message) {
		const CamelInternetAddress *to_addrs, *cc_addrs;
		const GSList *accounts = NULL;
		
		accounts = mail_config_get_accounts ();
		to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
		cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
		
		account = guess_me_from_accounts (to_addrs, cc_addrs, accounts);
		
		if (!account) {
			const char *source;
			
			source = camel_mime_message_get_source (message);
			account = mail_config_get_account_by_source_url (source);
		}
	}
	
	if (!account)
		account = mail_config_get_default_account ();
	
	composer = e_msg_composer_new ();
	if (composer) {
		ccd = ccd_new ();
		
		g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
		g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
		
		g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
		
		e_msg_composer_set_headers (composer, account->name, NULL, NULL, NULL, subject);
	} else {
		g_warning ("Could not create composer");
	}
	
	return composer;
}

static void
do_forward_non_attached (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *data)
{
	MailConfigForwardStyle style = GPOINTER_TO_INT (data);
	CamelMimeMessage *message;
	char *subject, *text;
	int i;
	
	if (messages->len == 0)
		return;
	
	for (i = 0; i < messages->len; i++) {
		message = messages->pdata[i];
		subject = mail_tool_generate_forward_subject (message);
		text = mail_tool_forward_message (message, style == MAIL_CONFIG_FORWARD_QUOTED);
		
		if (text) {
			EMsgComposer *composer = forward_get_composer (message, subject);
			if (composer) {
				CamelDataWrapper *wrapper;
				
				e_msg_composer_set_body_text (composer, text);
				
				wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (message));
				if (CAMEL_IS_MULTIPART (wrapper))
					e_msg_composer_add_message_attachments (composer, message, FALSE);
				
				gtk_widget_show (GTK_WIDGET (composer));
				e_msg_composer_unset_changed (composer);
				e_msg_composer_drop_editor_undo (composer);
			}
			g_free (text);
		}
		
		g_free (subject);
	}
}

static void
forward_message (FolderBrowser *fb, MailConfigForwardStyle style)
{
	GPtrArray *uids;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	mail_get_messages (fb->folder, uids, do_forward_non_attached, GINT_TO_POINTER (style));
}

void
forward_inline (GtkWidget *widget, gpointer user_data)
{
	forward_message (user_data, MAIL_CONFIG_FORWARD_INLINE);
}

void
forward_quoted (GtkWidget *widget, gpointer user_data)
{
	forward_message (user_data, MAIL_CONFIG_FORWARD_QUOTED);
}

static void
do_forward_attach (CamelFolder *folder, GPtrArray *messages, CamelMimePart *part, char *subject, void *data)
{
	CamelMimeMessage *message = data;
	
	if (part) {
		EMsgComposer *composer = forward_get_composer (message, subject);
		if (composer) {
			e_msg_composer_attach (composer, part);
			gtk_widget_show (GTK_WIDGET (composer));
			e_msg_composer_unset_changed (composer);
			e_msg_composer_drop_editor_undo (composer);
		}
	}
}

void
forward_attached (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = (FolderBrowser *) user_data;
	GPtrArray *uids;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	mail_build_attachment (fb->folder, uids, do_forward_attach,
			       uids->len == 1 ? fb->mail_display->current_message : NULL);
}

void
forward (GtkWidget *widget, gpointer user_data)
{
	MailConfigForwardStyle style = mail_config_get_default_forward_style ();
	
	if (style == MAIL_CONFIG_FORWARD_ATTACHED)
		forward_attached (widget, user_data);
	else
		forward_message (user_data, style);
}


void
post_to_url (const char *url)
{
	struct _composer_callback_data *ccd;
	GtkWidget *composer;
	
	composer = create_msg_composer (NULL, TRUE, NULL);
	if (!composer)
		return;
	
	e_msg_composer_hdrs_set_post_to ((EMsgComposerHdrs *) ((EMsgComposer *) composer)->hdrs, url);
	
	ccd = ccd_new ();
	
	g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
	g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
	
	g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
	
	gtk_widget_show (composer);
}

void
post_message (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	char *url;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	url = mail_tools_folder_to_url (fb->folder);
	post_to_url (url);
	g_free (url);
}

void
post_reply (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb) || !check_send_configuration (fb))
		return;
	
	mail_reply (fb->folder, NULL, fb->message_list->cursor_uid, REPLY_POST);
}


static EMsgComposer *
redirect_get_composer (CamelMimeMessage *message)
{
	const MailConfigAccount *account = NULL;
	const CamelInternetAddress *to_addrs, *cc_addrs;
	const GSList *accounts = NULL;
	struct _composer_callback_data *ccd;
	EMsgComposer *composer;
	
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);
	
	accounts = mail_config_get_accounts ();
	to_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	cc_addrs = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	
	account = guess_me_from_accounts (to_addrs, cc_addrs, accounts);
	
	if (!account) {
		const char *source;
		
		source = camel_mime_message_get_source (message);
		account = mail_config_get_account_by_source_url (source);
	}
	
	if (!account)
		account = mail_config_get_default_account ();
	
	/* QMail will refuse to send a message if it finds one of
           it's Delivered-To headers in the message, so remove all
           Delivered-To headers. Fixes bug #23635. */
	while (camel_medium_get_header (CAMEL_MEDIUM (message), "Delivered-To"))
		camel_medium_remove_header (CAMEL_MEDIUM (message), "Delivered-To");
	
	composer = e_msg_composer_new_redirect (message, account->name);
	if (composer) {
		ccd = ccd_new ();
		
		g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
		g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
		
		g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
	} else {
		g_warning ("Could not create composer");
	}
	
	return composer;
}

static void
do_redirect (CamelFolder *folder, const char *uid, CamelMimeMessage *message, void *data)
{
	EMsgComposer *composer;
	
	if (!message)
		return;
	
	composer = redirect_get_composer (message);
	if (composer) {
		CamelDataWrapper *wrapper;
		
		wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (message));
		if (CAMEL_IS_MULTIPART (wrapper))
			e_msg_composer_add_message_attachments (composer, message, FALSE);
		
		gtk_widget_show (GTK_WIDGET (composer));
		e_msg_composer_unset_changed (composer);
		e_msg_composer_drop_editor_undo (composer);
	}
}

void
redirect (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = (FolderBrowser *) user_data;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (!check_send_configuration (fb))
		return;
	
	mail_get_message (fb->folder, fb->message_list->cursor_uid,
			  do_redirect, NULL, mail_thread_new);
}

static void
transfer_msg_done (gboolean ok, void *data)
{
	FolderBrowser *fb = data;
	int row;
	
	if (ok && !FOLDER_BROWSER_IS_DESTROYED (fb)) {
		row = e_tree_row_of_node (fb->message_list->tree,
					  e_tree_get_cursor (fb->message_list->tree));
		
		/* If this is the last message and deleted messages
                   are hidden, select the previous */
		if ((row + 1 == e_tree_row_count (fb->message_list->tree))
		    && mail_config_get_hide_deleted ())
			message_list_select (fb->message_list, MESSAGE_LIST_SELECT_PREVIOUS,
					     0, CAMEL_MESSAGE_DELETED, FALSE);
		else
			message_list_select (fb->message_list, MESSAGE_LIST_SELECT_NEXT,
					     0, 0, FALSE);
	}
	
	g_object_unref (fb);
}

static void
transfer_msg (FolderBrowser *fb, gboolean delete_from_source)
{
	static const char *allowed_types[] = { "mail/*", "vtrash", NULL };
	extern EvolutionShellClient *global_shell_client;
	GNOME_Evolution_Folder *folder;
	static char *last_uri = NULL;
	GPtrArray *uids;
	char *desc;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (last_uri == NULL)
		last_uri = g_strdup (fb->uri);
	
	if (delete_from_source)
		desc = _("Move message(s) to");
	else
		desc = _("Copy message(s) to");
	
	evolution_shell_client_user_select_folder (global_shell_client,
						   GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (fb))),
						   desc, last_uri, allowed_types,
						   &folder);
	if (!folder)
		return;
	
	if (strcmp (last_uri, folder->evolutionUri) != 0) {
		g_free (last_uri);
		last_uri = g_strdup (folder->evolutionUri);
	}
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (delete_from_source) {
		g_object_ref (fb);
		mail_transfer_messages (fb->folder, uids, delete_from_source,
					folder->physicalUri, 0,
					transfer_msg_done, fb);
	} else {
		mail_transfer_messages (fb->folder, uids, delete_from_source,
					folder->physicalUri, 0, NULL, NULL);
	}
	
	CORBA_free (folder);
}

void
move_msg_cb (GtkWidget *widget, gpointer user_data)
{
	transfer_msg (user_data, TRUE);
}

void
move_msg (BonoboUIComponent *uih, void *user_data, const char *path)
{
	transfer_msg (user_data, TRUE);
}

void
copy_msg_cb (GtkWidget *widget, gpointer user_data)
{
	transfer_msg (user_data, FALSE);
}

void
copy_msg (BonoboUIComponent *uih, void *user_data, const char *path)
{
	transfer_msg (user_data, FALSE);
}

/* Copied from e-shell-view.c */
static GtkWidget *
find_socket (GtkContainer *container)
{
        GList *children, *tmp;
	
        children = gtk_container_children (container);
        while (children) {
                if (BONOBO_IS_SOCKET (children->data))
                        return children->data;
                else if (GTK_IS_CONTAINER (children->data)) {
                        GtkWidget *socket = find_socket (children->data);
                        if (socket)
                                return socket;
                }
                tmp = children->next;
                g_list_free_1 (children);
                children = tmp;
        }
	
	return NULL;
}

static void
popup_listener_cb (BonoboListener *listener,
		   const char *event_name,
		   const CORBA_any *any,
		   CORBA_Environment *ev,
		   gpointer user_data)
{
	char *type = bonobo_event_subtype (event_name);
	
	if (!strcmp (type, "Destroy")) {
		gtk_widget_destroy (GTK_WIDGET (user_data));
	}
	
	g_free (type);
}

void
addrbook_sender (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	const char *addr_str;
	CamelMessageInfo *info;
	GtkWidget *win;
	GtkWidget *control;
	GtkWidget *socket;
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	if (uids->len != 1)
		goto done;
	
	info = camel_folder_get_message_info (fb->folder, uids->pdata[0]);
	if (info == NULL || (addr_str = camel_message_info_from (info)) == NULL)
		goto done;
	
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (win), _("Sender"));
	
	control = bonobo_widget_new_control ("OAFIID:GNOME_Evolution_Addressbook_AddressPopup",
					     CORBA_OBJECT_NIL);
	bonobo_widget_set_property (BONOBO_WIDGET (control),
				    "email", addr_str,
				    NULL);
	
	bonobo_event_source_client_add_listener (bonobo_widget_get_objref (BONOBO_WIDGET (control)),
						 popup_listener_cb, NULL, NULL, win);
	
	socket = find_socket (GTK_CONTAINER (control));
	gtk_signal_connect_object (GTK_OBJECT (socket),
				   "destroy",
				   G_CALLBACK (gtk_widget_destroy),
				   GTK_OBJECT (win));
	
	gtk_container_add (GTK_CONTAINER (win), control);
	gtk_widget_show_all (win);

done:
	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);
	g_ptr_array_free (uids, TRUE);
}

void
add_sender_to_addrbook (BonoboUIComponent *uih, void *user_data, const char *path)
{
	addrbook_sender (NULL, user_data);
}

void
apply_filters (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	mail_filter_on_demand (fb->folder, uids);
}

void
select_all (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	ESelectionModel *etsm;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (GTK_WIDGET_HAS_FOCUS (fb->mail_display->html)) {
		gtk_html_select_all (fb->mail_display->html);
	} else {
		etsm = e_tree_get_selection_model (fb->message_list->tree);
		
		e_selection_model_select_all (etsm);
	}
}

/* Thread selection */

typedef struct thread_select_info {
	MessageList *ml;
	GPtrArray *paths;
} thread_select_info_t;

static gboolean
select_node (ETreeModel *tm, ETreePath path, gpointer user_data)
{
	thread_select_info_t *tsi = (thread_select_info_t *) user_data;
	
	g_ptr_array_add (tsi->paths, path);
	return FALSE; /*not done yet*/
}

static void
thread_select_foreach (ETreePath path, gpointer user_data)
{
	thread_select_info_t *tsi = (thread_select_info_t *) user_data;
	ETreeModel *tm = tsi->ml->model;
	ETreePath node;
	
	/* @path part of the initial selection. If it has children,
	 * we select them as well. If it doesn't, we select its siblings and
	 * their children (ie, the current node must be inside the thread
	 * that the user wants to mark.
	 */
	
	if (e_tree_model_node_get_first_child (tm, path)) 
		node = path;
	else {
		node = e_tree_model_node_get_parent (tm, path);
		
		/* Let's make an exception: if no parent, then we're about
		 * to mark the whole tree. No. */
		if (e_tree_model_node_is_root (tm, node)) 
			node = path;
	}
	
	e_tree_model_node_traverse (tm, node, select_node, tsi);
}

void
select_thread (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	ETreeSelectionModel *selection_model;
	thread_select_info_t tsi;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	/* For every selected item, select the thread containing it.
	 * We can't alter the selection while iterating through it,
	 * so build up a list of paths.
	 */
	
	tsi.ml = fb->message_list;
	tsi.paths = g_ptr_array_new ();
	
	e_tree_selected_path_foreach (fb->message_list->tree, thread_select_foreach, &tsi);
	
	selection_model = E_TREE_SELECTION_MODEL (e_tree_get_selection_model (fb->message_list->tree));
	
	for (i = 0; i < tsi.paths->len; i++)
		e_tree_selection_model_add_to_selection (selection_model,
							 tsi.paths->pdata[i]);
	g_ptr_array_free (tsi.paths, TRUE);
}

void
invert_selection (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	ESelectionModel *etsm;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (GTK_WIDGET_HAS_FOCUS (fb->message_list)) {
		etsm = e_tree_get_selection_model (fb->message_list->tree);
		
		e_selection_model_invert_selection (etsm);
	}
}

/* flag all selected messages. Return number flagged */
static int
flag_messages (FolderBrowser *fb, guint32 mask, guint32 set)
{
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return 0;
	
	/* could just use specific callback but i'm lazy */
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	camel_folder_freeze (fb->folder);
	for (i = 0; i < uids->len; i++) {
		camel_folder_set_message_flags (fb->folder, uids->pdata[i], mask, set);
		g_free (uids->pdata[i]);
	}
	camel_folder_thaw (fb->folder);
	
	g_ptr_array_free (uids, TRUE);
	
	return i;
}

static int
toggle_flags (FolderBrowser *fb, guint32 mask)
{
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return 0;
	
	/* could just use specific callback but i'm lazy */
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	camel_folder_freeze (fb->folder);
	for (i = 0; i < uids->len; i++) {
		guint32 flags;
		
		flags = ~(camel_folder_get_message_flags (fb->folder, uids->pdata[i]));
		
		/* if we're flagging a message important, always undelete it too */
		if (mask & flags & CAMEL_MESSAGE_FLAGGED) {
			flags &= ~CAMEL_MESSAGE_DELETED;
			mask |= CAMEL_MESSAGE_DELETED;
		}
		
		/* if we're flagging a message deleted, mark it seen. If 
		 * we're undeleting it, we also want it to be seen, so always do this.
		 */
		if (mask & CAMEL_MESSAGE_DELETED) {
			flags |= CAMEL_MESSAGE_SEEN;
			mask |= CAMEL_MESSAGE_SEEN;
		}
		
		camel_folder_set_message_flags (fb->folder, uids->pdata[i], mask, flags);
		
		g_free (uids->pdata[i]);
	}
	camel_folder_thaw (fb->folder);
	
	g_ptr_array_free (uids, TRUE);
	
	return i;
}

void
mark_as_seen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
}

void
mark_as_unseen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	/* Remove the automatic mark-as-read timer first */
	if (fb->seen_id) {
		gtk_timeout_remove (fb->seen_id);
		fb->seen_id = 0;
	}
	
	flag_messages (fb, CAMEL_MESSAGE_SEEN, 0);
}

void
mark_all_as_seen (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = camel_folder_get_uids (fb->folder);
	camel_folder_freeze (fb->folder);
	for (i = 0; i < uids->len; i++)
		camel_folder_set_message_flags (fb->folder, uids->pdata[i], CAMEL_MESSAGE_SEEN, ~0);
	camel_folder_thaw (fb->folder);
	g_ptr_array_free (uids, TRUE);
}

void
mark_as_important (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_FLAGGED);
}

void
mark_as_unimportant (BonoboUIComponent *uih, void *user_data, const char *path)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED, 0);
}

void
toggle_as_important (BonoboUIComponent *uih, void *user_data, const char *path)
{
	toggle_flags (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_FLAGGED);
}


struct _tag_editor_data {
	MessageTagEditor *editor;
	FolderBrowser *fb;
	GPtrArray *uids;
};

static void
tag_editor_response(GtkWidget *gd, int button, struct _tag_editor_data *data)
{
	CamelFolder *folder;
	CamelTag *tags, *t;
	GPtrArray *uids;
	int i;

	/*if (FOLDER_BROWSER_IS_DESTROYED (data->fb))
	  goto done;*/

	if (button == GTK_RESPONSE_OK
	    && (tags = message_tag_editor_get_tag_list (data->editor))) {
		folder = data->fb->folder;
		uids = data->uids;
	
		camel_folder_freeze (folder);
		for (i = 0; i < uids->len; i++) {
			for (t = tags; t; t = t->next)
				camel_folder_set_message_user_tag (folder, uids->pdata[i], t->name, t->value);
		}
		camel_folder_thaw (folder);
		camel_tag_list_free (&tags);
	}

	gtk_widget_destroy(gd);

	g_object_unref (data->fb);
	g_ptr_array_free (data->uids, TRUE);
	g_free (data);
}

void
flag_for_followup (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	struct _tag_editor_data *data;
	GtkWidget *editor;
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	editor = (GtkWidget *) message_tag_followup_new ();
	
	data = g_new (struct _tag_editor_data, 1);
	data->editor = MESSAGE_TAG_EDITOR (editor);
	gtk_widget_ref (GTK_WIDGET (fb));
	data->fb = fb;
	data->uids = uids;
	
	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *info;
		
		info = camel_folder_get_message_info (fb->folder, uids->pdata[i]);
		message_tag_followup_append_message (MESSAGE_TAG_FOLLOWUP (editor),
						     camel_message_info_from (info),
						     camel_message_info_subject (info));
	}

	g_signal_connect(editor, "response", G_CALLBACK(tag_editor_response), data);
	
	/* special-case... */
	if (uids->len == 1) {
		CamelMessageInfo *info;
		
		info = camel_folder_get_message_info (fb->folder, uids->pdata[0]);
		if (info) {
			if (info->user_tags)
				message_tag_editor_set_tag_list (MESSAGE_TAG_EDITOR (editor), info->user_tags);
			camel_folder_free_message_info (fb->folder, info);
		}
	}
	
	gtk_widget_show (editor);
}

void
flag_followup_completed (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	char *now;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	now = header_format_date (time (NULL), 0);
	
	camel_folder_freeze (fb->folder);
	for (i = 0; i < uids->len; i++) {
		const char *tag;
		
		tag = camel_folder_get_message_user_tag (fb->folder, uids->pdata[i], "follow-up");
		if (tag == NULL || *tag == '\0')
			continue;
		
		camel_folder_set_message_user_tag (fb->folder, uids->pdata[i], "completed-on", now);
	}
	camel_folder_thaw (fb->folder);
	
	g_free (now);
	
	g_ptr_array_free (uids, TRUE);
}

void
flag_followup_clear (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	camel_folder_freeze (fb->folder);
	for (i = 0; i < uids->len; i++) {
		camel_folder_set_message_user_tag (fb->folder, uids->pdata[i], "follow-up", "");
		camel_folder_set_message_user_tag (fb->folder, uids->pdata[i], "due-by", "");
		camel_folder_set_message_user_tag (fb->folder, uids->pdata[i], "completed-on", "");
	}
	camel_folder_thaw (fb->folder);
	
	g_ptr_array_free (uids, TRUE);
}

void
zoom_in (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	gtk_html_zoom_in (fb->mail_display->html);
}

void
zoom_out (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	gtk_html_zoom_out (fb->mail_display->html);
}

void
zoom_reset (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	gtk_html_zoom_reset (fb->mail_display->html);
}

static void
do_edit_messages (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *data)
{
	FolderBrowser *fb = (FolderBrowser *) data;
	int i;
	
	if (messages == NULL)
		return;
	
	for (i = 0; i < messages->len; i++) {
		struct _composer_callback_data *ccd;
		EMsgComposer *composer;
		
		camel_medium_remove_header (CAMEL_MEDIUM (messages->pdata[i]), "X-Mailer");
		
		composer = e_msg_composer_new_with_message (messages->pdata[i]);
		e_msg_composer_unset_changed (composer);
		e_msg_composer_drop_editor_undo (composer);
		
		if (composer) {
			ccd = ccd_new ();
			if (folder_browser_is_drafts (fb)) {
				camel_object_ref (folder);
				ccd->drafts_folder = folder;
				ccd->drafts_uid = g_strdup (uids->pdata[i]);
			}
			
			g_signal_connect (composer, "send", G_CALLBACK (composer_send_cb), ccd);
			g_signal_connect (composer, "save-draft", G_CALLBACK (composer_save_draft_cb), ccd);
			
			g_object_weak_ref ((GObject *) composer, (GWeakNotify) composer_destroy_cb, ccd);
			
			gtk_widget_show (GTK_WIDGET (composer));
		}
	}
}

static gboolean
are_you_sure (const char *msg, GPtrArray *uids, FolderBrowser *fb)
{
	GtkWidget *dialog;
	char *buf;
	int button, i;
	
	buf = g_strdup_printf (msg, uids->len);
	dialog = e_gnome_ok_cancel_dialog_parented (buf, NULL, NULL, FB_WINDOW (fb));
	button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	if (button != 0) {
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		g_ptr_array_free (uids, TRUE);
	}
	
	return button == 0;
}

static void
edit_msg_internal (FolderBrowser *fb)
{
	GPtrArray *uids;
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (uids->len > 10 && !are_you_sure (_("Are you sure you want to edit all %d messages?"), uids, fb)) {
		int i;
		
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		
		g_ptr_array_free (uids, TRUE);
		
		return;
	}
	
	mail_get_messages (fb->folder, uids, do_edit_messages, fb);
}

void
edit_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (!folder_browser_is_drafts (fb)) {
		GtkWidget *dialog;
		
		dialog = gnome_warning_dialog_parented (_("You may only edit messages saved\n"
							  "in the Drafts folder."),
							FB_WINDOW (fb));
		gnome_dialog_set_close (GNOME_DIALOG (dialog), TRUE);
		gtk_widget_show (dialog);
		return;
	}
	
	edit_msg_internal (fb);
}

static void
do_resend_messages (CamelFolder *folder, GPtrArray *uids, GPtrArray *messages, void *data)
{
	int i;
	
	for (i = 0; i < messages->len; i++) {
		/* generate a new Message-Id because they need to be unique */
		camel_mime_message_set_message_id (messages->pdata[i], NULL);
	}
	
	/* "Resend" should open up the composer to let the user edit the message */
	do_edit_messages (folder, uids, messages, data);
}



void
resend_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (!folder_browser_is_sent (fb)) {
		GtkWidget *dialog;
		
		dialog = gnome_warning_dialog_parented (_("You may only resend messages\n"
							  "in the Sent folder."),
							FB_WINDOW (fb));
		gnome_dialog_set_close (GNOME_DIALOG (dialog), TRUE);
		gtk_widget_show (dialog);
		return;
	}
	
	if (!check_send_configuration (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (uids->len > 10 && !are_you_sure (_("Are you sure you want to resend all %d messages?"), uids, fb)) {
		int i;
		
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		
		g_ptr_array_free (uids, TRUE);
		
		return;
	}
	
	mail_get_messages (fb->folder, uids, do_resend_messages, fb);
}

void
search_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GtkWidget *w;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (fb->mail_display->current_message == NULL) {
		GtkWidget *dialog;
		
		dialog = gnome_warning_dialog_parented (_("No Message Selected"), FB_WINDOW (fb));
		gnome_dialog_set_close (GNOME_DIALOG (dialog), TRUE);
		gtk_widget_show (dialog);
		return;
	}
	
	w = mail_search_new (fb->mail_display);
	gtk_widget_show_all (w);
}

void
load_images (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	mail_display_load_images (fb->mail_display);
}

static void
save_msg_ok (GtkWidget *widget, gpointer user_data)
{
	CamelFolder *folder;
	GPtrArray *uids;
	const char *path;
	int fd, ret = 0;
	struct stat st;
	
	path = gtk_file_selection_get_filename (GTK_FILE_SELECTION (user_data));
	if (path[0] == '\0')
		return;
	
	/* make sure we can actually save to it... */
	if (stat (path, &st) != -1 && !S_ISREG (st.st_mode))
		return;
	
	fd = open (path, O_RDONLY);
	if (fd != -1) {
		GtkWidget *dialog;
		GtkWidget *label;
		
		close (fd);
		
		dialog = gtk_dialog_new_with_buttons (_("Overwrite file?"), GTK_WINDOW (user_data),
						      GTK_DIALOG_DESTROY_WITH_PARENT,
						      GTK_STOCK_YES, GTK_RESPONSE_YES,
						      GTK_STOCK_NO, GTK_RESPONSE_NO,
						      NULL);
		
		label = gtk_label_new (_("A file by that name already exists.\nOverwrite it?"));
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, TRUE, TRUE, 4);
		gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);
		gtk_widget_show (label);
		
		ret = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
	
	if (ret == GTK_RESPONSE_OK) {
		folder = g_object_get_data ((GObject *) user_data, "folder");
		uids = g_object_get_data ((GObject *) user_data, "uids");
		/* FIXME: what's this supposed to become? */
		gtk_object_remove_no_notify (GTK_OBJECT (user_data), "uids");
		mail_save_messages (folder, uids, path, NULL, NULL);
		gtk_widget_destroy (GTK_WIDGET (user_data));
	}
}

static void
save_msg_destroy (gpointer user_data)
{
	GPtrArray *uids = user_data;
	
	if (uids) {
		int i;
		
		for (i = 0; i < uids->len; i++)
			g_free (uids->pdata[i]);
		
		g_ptr_array_free (uids, TRUE);
	}
}

void
save_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GtkFileSelection *filesel;
	GPtrArray *uids;
	char *title, *path;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);
	
	if (uids->len == 1)
		title = _("Save Message As...");
	else
		title = _("Save Messages As...");
	
	filesel = GTK_FILE_SELECTION (gtk_file_selection_new (title));
	path = g_strdup_printf ("%s/", g_get_home_dir ());
	gtk_file_selection_set_filename (filesel, path);
	g_free (path);
	
	g_object_set_data_full ((GObject *) filesel, "uids", uids, save_msg_destroy);
	g_object_set_data ((GObject *) filesel, "folder", fb->folder);
	
	g_signal_connect (filesel->ok_button, "clicked", G_CALLBACK (save_msg_ok), filesel);
	g_signal_connect_swapped (filesel->cancel_button, "clicked",
				  G_CALLBACK (gtk_widget_destroy), filesel);
	
	gtk_widget_show (GTK_WIDGET (filesel));
}

void
colour_msg (GtkWidget *widget, gpointer user_data)
{
	/* FIXME: implement me? */
}

void
delete_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	int deleted, row;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	deleted = flag_messages (fb, CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
				 CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
	
	/* Select the next message if we are only deleting one message */
	if (deleted == 1) {
		row = e_tree_row_of_node (fb->message_list->tree,
					  e_tree_get_cursor (fb->message_list->tree));
		
		/* If this is the last message and deleted messages
                   are hidden, select the previous */
		if ((row + 1 == e_tree_row_count (fb->message_list->tree))
		    && mail_config_get_hide_deleted ())
			message_list_select (fb->message_list, MESSAGE_LIST_SELECT_PREVIOUS,
					     0, CAMEL_MESSAGE_DELETED, FALSE);
		else
			message_list_select (fb->message_list, MESSAGE_LIST_SELECT_NEXT,
					     0, 0, FALSE);
	}
}

void
undelete_msg (GtkWidget *button, gpointer user_data)
{
	flag_messages (FOLDER_BROWSER (user_data), CAMEL_MESSAGE_DELETED, 0);
}


#if 0
static gboolean
confirm_goto_next_folder (FolderBrowser *fb)
{
	GtkWidget *dialog, *label, *checkbox;
	int button;
	
	if (!mail_config_get_confirm_goto_next_folder ())
		return mail_config_get_goto_next_folder ();
	
	dialog = gnome_dialog_new (_("Go to next folder with unread messages?"),
				   GNOME_STOCK_BUTTON_YES,
				   GNOME_STOCK_BUTTON_NO,
				   NULL);
	
	e_gnome_dialog_set_parent (GNOME_DIALOG (dialog), FB_WINDOW (fb));
	
	label = gtk_label_new (_("There are no more new messages in this folder.\n"
				 "Would you like to go to the next folder?"));
	
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label, TRUE, TRUE, 4);
	
	checkbox = gtk_check_button_new_with_label (_("Do not ask me again."));
	g_object_ref((checkbox));
	gtk_widget_show (checkbox);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), checkbox, TRUE, TRUE, 4);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)))
		mail_config_set_confirm_goto_next_folder (FALSE);
	
	g_object_unref((checkbox));
	
	if (button == 0) {
		mail_config_set_goto_next_folder (TRUE);
		return TRUE;
	} else {
		mail_config_set_goto_next_folder (FALSE);
		return FALSE;
	}
}

static CamelFolderInfo *
find_current_folder (CamelFolderInfo *root, const char *current_uri)
{
	CamelFolderInfo *node, *current = NULL;
	
	node = root;
	while (node) {
		if (!strcmp (current_uri, node->url)) {
			current = node;
			break;
		}
		
		current = find_current_folder (node->child, current_uri);
		if (current)
			break;
		
		node = node->sibling;
	}
	
	return current;
}

static CamelFolderInfo *
find_next_folder_r (CamelFolderInfo *node)
{
	CamelFolderInfo *next;
	
	while (node) {
		if (node->unread_message_count > 0)
			return node;
		
		next = find_next_folder_r (node->child);
		if (next)
			return next;
		
		node = node->sibling;
	}
	
	return NULL;
}

static CamelFolderInfo *
find_next_folder (CamelFolderInfo *current)
{
	CamelFolderInfo *next;
	
	/* first search subfolders... */
	next = find_next_folder_r (current->child);
	if (next)
		return next;
	
	/* now search siblings... */
	next = find_next_folder_r (current->sibling);
	if (next)
		return next;
	
	/* now go up one level (if we can) and search... */
	if (current->parent && current->parent->sibling) {
		return find_next_folder_r (current->parent->sibling);
	} else {
		return NULL;
	}
}

static void
do_evil_kludgy_goto_next_folder_hack (FolderBrowser *fb)
{
	CamelFolderInfo *root, *current, *node;
	CORBA_Environment ev;
	CamelStore *store;
	
	store = camel_folder_get_parent_store (fb->folder);
	
	/* FIXME: loop over all available mail stores? */
	
	root = camel_store_get_folder_info (store, "", CAMEL_STORE_FOLDER_INFO_RECURSIVE |
					    CAMEL_STORE_FOLDER_INFO_SUBSCRIBED, NULL);
	
	if (!root)
		return;
	
	current = find_current_folder (root, fb->uri);
	g_assert (current != NULL);
	
	node = find_next_folder (current);
	if (node) {
		g_warning ("doin' my thang...");
		CORBA_exception_init (&ev);
		GNOME_Evolution_ShellView_changeCurrentView (fb->shell_view, "evolution:/local/Inbox", &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("got an exception");
		CORBA_exception_free (&ev);
	} else {
		g_warning ("can't find a folder with unread mail?");
	}
	
	camel_store_free_folder_info (store, root);
}
#endif

void
next_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	message_list_select (fb->message_list, MESSAGE_LIST_SELECT_NEXT, 0, 0, FALSE);
}

void
next_unread_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (!message_list_select (fb->message_list, MESSAGE_LIST_SELECT_NEXT, 0, CAMEL_MESSAGE_SEEN, TRUE)) {
#if 0
		if (confirm_goto_next_folder (fb))
			do_evil_kludgy_goto_next_folder_hack (fb);
#endif
	}
}

void
next_flagged_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	message_list_select (fb->message_list, MESSAGE_LIST_SELECT_NEXT,
			     CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED, FALSE);
}

void
next_thread (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	message_list_select_next_thread (fb->message_list);
}

void
previous_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	message_list_select (fb->message_list, MESSAGE_LIST_SELECT_PREVIOUS,
			     0, 0, FALSE);
}

void
previous_unread_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	message_list_select (fb->message_list, MESSAGE_LIST_SELECT_PREVIOUS,
			     0, CAMEL_MESSAGE_SEEN, TRUE);
}

void
previous_flagged_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	message_list_select (fb->message_list, MESSAGE_LIST_SELECT_PREVIOUS,
			     CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED, TRUE);
}

static void
expunged_folder (CamelFolder *f, void *data)
{
	FolderBrowser *fb = data;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	fb->expunging = NULL;
	gtk_widget_set_sensitive (GTK_WIDGET (fb->message_list), TRUE);
}

static gboolean
confirm_expunge (FolderBrowser *fb)
{
	GtkWidget *dialog, *label, *checkbox;
	int button;
	
	if (!mail_config_get_confirm_expunge ())
		return TRUE;
	
	dialog = gnome_dialog_new (_("Warning"),
				   GNOME_STOCK_BUTTON_YES,
				   GNOME_STOCK_BUTTON_NO,
				   NULL);
	
	e_gnome_dialog_set_parent (GNOME_DIALOG (dialog), FB_WINDOW (fb));
	
	label = gtk_label_new (_("This operation will permanently erase all messages marked as deleted. If you continue, you will not be able to recover these messages.\n\nReally erase these messages?"));
	
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label, TRUE, TRUE, 4);
	
	checkbox = gtk_check_button_new_with_label (_("Do not ask me again."));
	g_object_ref((checkbox));
	gtk_widget_show (checkbox);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), checkbox, TRUE, TRUE, 4);
	
	/* Set the 'No' button as the default */
	gnome_dialog_set_default (GNOME_DIALOG (dialog), 1);
	
	button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	
	if (button == 0 && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)))
		mail_config_set_confirm_expunge (FALSE);
	
	g_object_unref((checkbox));
	
	if (button == 0)
		return TRUE;
	else
		return FALSE;
}

void
expunge_folder (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (fb->folder && (fb->expunging == NULL || fb->folder != fb->expunging) && confirm_expunge (fb)) {
		CamelMessageInfo *info;
		
		/* hide the deleted messages so user can't click on them while we expunge */
		gtk_widget_set_sensitive (GTK_WIDGET (fb->message_list), FALSE);
		
		/* Only blank the mail display if the message being
                   viewed is one of those to be expunged */
		if (fb->loaded_uid) {
			info = camel_folder_get_message_info (fb->folder, fb->loaded_uid);
			
			if (!info || info->flags & CAMEL_MESSAGE_DELETED)
				mail_display_set_message (fb->mail_display, NULL, NULL, NULL);
		}
		
		fb->expunging = fb->folder;
		mail_expunge_folder (fb->folder, expunged_folder, fb);
	}
}

/********************** Begin Filter Editor ********************/

static GtkWidget *filter_editor = NULL;

static void
filter_editor_response (GtkWidget *dialog, int button, FolderBrowser *fb)
{
	FilterContext *fc;
	
	if (button == GTK_RESPONSE_ACCEPT) {
		char *user;
		
		fc = g_object_get_data(G_OBJECT(dialog), "context");
		user = g_strdup_printf ("%s/filters.xml", evolution_dir);
		rule_context_save ((RuleContext *)fc, user);
		g_free (user);
	}

	gtk_widget_destroy(dialog);

	filter_editor = NULL;
}

static const char *filter_source_names[] = {
	"incoming",
	"outgoing",
	NULL,
};

void
filter_edit (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	FilterContext *fc;
	char *user, *system;
	
	if (filter_editor) {
		gdk_window_raise (GTK_WIDGET (filter_editor)->window);
		return;
	}
	
	fc = filter_context_new ();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = EVOLUTION_DATADIR "/evolution/filtertypes.xml";
	rule_context_load ((RuleContext *)fc, system, user);
	g_free (user);
	
	if (((RuleContext *)fc)->error) {
		GtkWidget *dialog;
		char *err;
		
		err = g_strdup_printf (_("Error loading filter information:\n%s"),
				       ((RuleContext *)fc)->error);
		dialog = gnome_warning_dialog_parented (err, FB_WINDOW (fb));
		g_free (err);
		
		gnome_dialog_set_close (GNOME_DIALOG (dialog), TRUE);
		gtk_widget_show (dialog);
		return;
	}
	
	filter_editor = (GtkWidget *)filter_editor_new (fc, filter_source_names);
	/* maybe this needs destroy func? */
	gtk_window_set_transient_for((GtkWindow *)filter_editor, FB_WINDOW(fb));
	gtk_window_set_title (GTK_WINDOW (filter_editor), _("Filters"));
	gtk_dialog_add_button((GtkDialog *)filter_editor, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	g_object_set_data_full(G_OBJECT(filter_editor), "context", fc, (GtkDestroyNotify)g_object_unref);
	g_signal_connect(filter_editor, "response", G_CALLBACK(filter_editor_response), fb);
	gtk_widget_show (GTK_WIDGET (filter_editor));
}

/********************** End Filter Editor ********************/

void
vfolder_edit_vfolders (BonoboUIComponent *uih, void *user_data, const char *path)
{
	vfolder_edit ();
}


/* static void
header_print_cb (GtkHTML *html, GnomePrintContext *print_context,
		 double x, double y, double width, double height, gpointer user_data)
{
	printf ("header_print_cb %f,%f x %f,%f\n", x, y, width, height);

	gnome_print_newpath (print_context);
	gnome_print_setlinewidth (print_context, 12.0);
	gnome_print_setrgbcolor (print_context, 1.0, 0.0, 0.0);
	gnome_print_moveto (print_context, x, y);
	gnome_print_lineto (print_context, x+width, y-height);
	gnome_print_strokepath (print_context);
} */

struct footer_info {
	GnomeFont *local_font;
	gint page_num, pages;
};

static void
footer_print_cb (GtkHTML *html, GnomePrintContext *print_context,
		 double x, double y, double width, double height, gpointer user_data)
{
	struct footer_info *info = (struct footer_info *) user_data;

	if (info->local_font) {
		gchar *text = g_strdup_printf (_("Page %d of %d"), info->page_num, info->pages);
		/*gdouble tw = gnome_font_get_width_string (info->local_font, text);*/
		/* FIXME: work out how to measure this */
		gdouble tw = strlen(text) * 8;

		gnome_print_gsave       (print_context);
		gnome_print_newpath     (print_context);
		gnome_print_setrgbcolor (print_context, .0, .0, .0);
		gnome_print_moveto      (print_context, x + width - tw, y - gnome_font_get_ascender (info->local_font));
		gnome_print_setfont     (print_context, info->local_font);
		gnome_print_show        (print_context, text);
		gnome_print_grestore    (print_context);

		g_free (text);
		info->page_num++;
	}
}

static void
footer_info_free (struct footer_info *info)
{
	if (info->local_font)
		gnome_font_unref (info->local_font);
	g_free (info);
}

static struct footer_info *
footer_info_new (GtkHTML *html, GnomePrintContext *pc, gdouble *line)
{
	struct footer_info *info;

	info = g_new (struct footer_info, 1);
	info->local_font = gnome_font_find_closest ("Helvetica", 10.0);
	if (info->local_font) {
		*line = gnome_font_get_ascender (info->local_font) + gnome_font_get_descender (info->local_font);
	}
	info->page_num = 1;
	info->pages = gtk_html_print_get_pages_num (html, pc, 0.0, *line);

	return info;
}

static void
do_mail_print (FolderBrowser *fb, gboolean preview)
{
	GtkHTML *html;
	GnomePrintContext *print_context;
	GnomePrintMaster *print_master;
	GnomePrintDialog *dialog;
	GnomePrintConfig *config = NULL;
	gdouble line = 0.0;
	struct footer_info *info;

	if (!preview) {
		dialog = GNOME_PRINT_DIALOG (gnome_print_dialog_new (_("Print Message"),
								     GNOME_PRINT_DIALOG_COPIES));
		gtk_dialog_set_default_response((GtkDialog *)dialog, GNOME_PRINT_DIALOG_RESPONSE_PRINT);
		e_gnome_dialog_set_parent (GNOME_DIALOG (dialog), FB_WINDOW (fb));
		
		switch(gtk_dialog_run((GtkDialog *)dialog)) {
		case GNOME_PRINT_DIALOG_RESPONSE_PRINT:
			break;	
		case GNOME_PRINT_DIALOG_RESPONSE_PREVIEW:
			preview = TRUE;
			break;
		default:
			gtk_widget_destroy((GtkWidget *)dialog);
			return;
		}
		
		config = gnome_print_dialog_get_config(dialog);
		gtk_widget_destroy((GtkWidget *)dialog);
	}
	
	if (config) {
		print_master = gnome_print_master_new_from_config(config);
		gnome_print_config_unref(config);
	} else
		print_master = gnome_print_master_new ();
	
	/* paper size settings? */
	/*gnome_print_master_set_paper (print_master, paper);*/
	print_context = gnome_print_master_get_context (print_master);
	
	html = GTK_HTML (gtk_html_new ());
	mail_display_initialize_gtkhtml (fb->mail_display, html);
	
	/* Set our 'printing' flag to true and render.  This causes us
	   to ignoring any adjustments we made to accomodate the
	   user's theme. */
	fb->mail_display->printing = TRUE;
	
	if (!GTK_WIDGET_REALIZED (GTK_WIDGET (html)))
		gtk_widget_realize (GTK_WIDGET (html));
	mail_display_render (fb->mail_display, html, TRUE);
	gtk_html_print_set_master (html, print_master);
	
	info = footer_info_new (html, print_context, &line);
	gtk_html_print_with_header_footer (html, print_context, 0.0, line, NULL, footer_print_cb, info);
	footer_info_free (info);
	
	fb->mail_display->printing = FALSE;
	
	gnome_print_master_close (print_master);
	
	if (preview){
		GtkWidget *preview;

		preview = gnome_print_master_preview_new(print_master, _("Print Preview"));
		gtk_widget_show(preview);
	} else {
		int result = gnome_print_master_print (print_master);
		
		if (result == -1){
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("Printing of message failed"));
		}
	}
	
	/* FIXME: We are leaking the GtkHTML object */
}

/* This is pretty evil.  FolderBrowser's API should be extended to allow these sorts of
   things to be done in a more natural way. */

/* <evil_code> */

struct blarg_this_sucks {
	FolderBrowser *fb;
	gboolean preview;
};

static void
done_message_selected (CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *data)
{
	struct blarg_this_sucks *blarg = data;
	FolderBrowser *fb = blarg->fb;
	gboolean preview = blarg->preview;
	CamelMessageInfo *info;
	
	g_free (blarg);
	
	info = camel_folder_get_message_info (fb->folder, uid);
	mail_display_set_message (fb->mail_display, (CamelMedium *) msg, fb->folder, info);
	if (info)
		camel_folder_free_message_info (fb->folder, info);
	
	g_free (fb->loaded_uid);
	fb->loaded_uid = fb->loading_uid;
	fb->loading_uid = NULL;

	if (msg)
		do_mail_print (fb, preview);
}

/* Ack!  Most of this is copied from folder-browser.c */
static void
do_mail_fetch_and_print (FolderBrowser *fb, gboolean preview)
{
	if (!fb->preview_shown || fb->mail_display->current_message == NULL) {
		/* If the preview pane is closed, we have to do some
		   extra magic to load the message. */
		struct blarg_this_sucks *blarg = g_new (struct blarg_this_sucks, 1);
		
		blarg->fb = fb;
		blarg->preview = preview;
		
		fb->loading_id = 0;
		
		/* if we are loading, then set a pending, but leave the loading, coudl cancel here (?) */
		if (fb->loading_uid) {
			g_free (fb->pending_uid);
			fb->pending_uid = g_strdup (fb->new_uid);
		} else {
			if (fb->new_uid) {
				fb->loading_uid = g_strdup (fb->new_uid);
				mail_get_message (fb->folder, fb->loading_uid, done_message_selected, blarg, mail_thread_new);
			} else {
				mail_display_set_message (fb->mail_display, NULL, NULL, NULL);
				g_free (blarg);
			}
		}
	} else {
		do_mail_print (fb, preview);
	}
}

/* </evil_code> */


void
print_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	do_mail_fetch_and_print (fb, FALSE);
}

void
print_preview_msg (GtkWidget *button, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	do_mail_fetch_and_print (fb, TRUE);
}

/******************** Begin Subscription Dialog ***************************/

static GtkObject *subscribe_dialog = NULL;

static void
subscribe_dialog_destroy (GtkWidget *widget, gpointer user_data)
{
	g_object_unref (subscribe_dialog);
	subscribe_dialog = NULL;
}

void
manage_subscriptions (BonoboUIComponent *uih, void *user_data, const char *path)
{
	if (!subscribe_dialog) {
		subscribe_dialog = subscribe_dialog_new ();
		g_signal_connect(SUBSCRIBE_DIALOG (subscribe_dialog)->app, "destroy",
				 G_CALLBACK(subscribe_dialog_destroy), NULL);
		
		subscribe_dialog_show (subscribe_dialog);
	} else {
		gdk_window_raise (SUBSCRIBE_DIALOG (subscribe_dialog)->app->window);
	}
}

/******************** End Subscription Dialog ***************************/

static void
local_configure_done(const char *uri, CamelFolder *folder, void *data)
{
	FolderBrowser *fb = data;

	if (FOLDER_BROWSER_IS_DESTROYED (fb)) {
		g_object_unref((GtkObject *)fb);
		return;
	}

	if (folder == NULL)
		folder = fb->folder;

	message_list_set_folder(fb->message_list, folder, FALSE);
	g_object_unref((GtkObject *)fb);
}

void
configure_folder (BonoboUIComponent *uih, void *user_data, const char *path)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (fb->uri) {
		if (strncmp (fb->uri, "vfolder:", 8) == 0) {
			vfolder_edit_rule (fb->uri);
		} else {
			message_list_set_folder(fb->message_list, NULL, FALSE);
			g_object_ref((GtkObject *)fb);
			mail_local_reconfigure_folder(fb->uri, local_configure_done, fb);
		}
	}
}

static void
do_view_message (CamelFolder *folder, const char *uid, CamelMimeMessage *message, void *data)
{
	FolderBrowser *fb = FOLDER_BROWSER (data);
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (message) {
		GtkWidget *mb;
		
		camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
		mb = message_browser_new (fb->shell, fb->uri, uid);
		gtk_widget_show (mb);
	}
}

void
view_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	GPtrArray *uids;
	int i;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	uids = g_ptr_array_new ();
	message_list_foreach (fb->message_list, enumerate_msg, uids);

	if (uids->len > 10 && !are_you_sure (_("Are you sure you want to open all %d messages in separate windows?"), uids, fb))
		return;
	
	/* FIXME: use mail_get_messages() */
	for (i = 0; i < uids->len; i++) {
		mail_get_message (fb->folder, uids->pdata [i], do_view_message, fb, mail_thread_queued);
		g_free (uids->pdata [i]);
	}
	g_ptr_array_free (uids, TRUE);
}

void
open_msg (GtkWidget *widget, gpointer user_data)
{
	FolderBrowser *fb = FOLDER_BROWSER (user_data);
	extern CamelFolder *outbox_folder;
	
	if (FOLDER_BROWSER_IS_DESTROYED (fb))
		return;
	
	if (folder_browser_is_drafts (fb) || fb->folder == outbox_folder)
		edit_msg_internal (fb);
	else
		view_msg (NULL, user_data);
}

void
open_message (BonoboUIComponent *uih, void *user_data, const char *path)
{
	open_msg (NULL, user_data);
}

void
edit_message (BonoboUIComponent *uih, void *user_data, const char *path)
{
        edit_msg (NULL, user_data);
}

void
stop_threads (BonoboUIComponent *uih, void *user_data, const char *path)
{
	camel_operation_cancel (NULL);
}

static void
empty_trash_expunged_cb (CamelFolder *folder, void *data)
{
	camel_object_unref (CAMEL_OBJECT (folder));
}

void
empty_trash (BonoboUIComponent *uih, void *user_data, const char *path)
{
	MailConfigAccount *account;
	CamelProvider *provider;
	const GSList *accounts;
	CamelFolder *vtrash;
	FolderBrowser *fb;
	CamelException ex;
	
	fb = user_data ? FOLDER_BROWSER (user_data) : NULL;
	
	if (fb && !confirm_expunge (fb))
		return;
	
	camel_exception_init (&ex);
	
	/* expunge all remote stores */
	accounts = mail_config_get_accounts ();
	while (accounts) {
		account = accounts->data;
		
		/* make sure this is a valid source */
		if (account->source && account->source->enabled && account->source->url) {
			provider = camel_session_get_provider (session, account->source->url, &ex);
			if (provider) {
				/* make sure this store is a remote store */
				if (provider->flags & CAMEL_PROVIDER_IS_STORAGE &&
				    provider->flags & CAMEL_PROVIDER_IS_REMOTE) {
					vtrash = mail_tool_get_trash (account->source->url, FALSE, &ex);
					
					if (vtrash) {
						mail_expunge_folder (vtrash, empty_trash_expunged_cb, NULL);
					}
				}
			}
			
			/* clear the exception for the next round */
			camel_exception_clear (&ex);
		}
		accounts = accounts->next;
	}
	
	/* Now empty the local trash folder */
	vtrash = mail_tool_get_trash ("file:/", TRUE, &ex);
	if (vtrash) {
		mail_expunge_folder (vtrash, empty_trash_expunged_cb, NULL);
	}
	
	camel_exception_clear (&ex);
}
