/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mail-folder-cache.c: Stores information about open folders */

/* 
 * Authors: Peter Williams <peterw@ximian.com>
 *
 * Copyright 2000,2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include "config.h"
#endif

#include <bonobo/bonobo-exception.h>

#include "mail-mt.h"
#include "mail-folder-cache.h"

#define ld(x)
#define d(x)
#define dm(args...) /*g_message ("folder cache: " args)*/

/* Structures */

typedef enum mail_folder_info_flags {
	MAIL_FIF_UNREAD_VALID = (1 << 0),
	MAIL_FIF_TOTAL_VALID = (1 << 1),
	MAIL_FIF_HIDDEN_VALID = (1 << 2),
	MAIL_FIF_FOLDER_VALID = (1 << 3),
	MAIL_FIF_NEED_UPDATE = (1 << 4),
	MAIL_FIF_PATH_VALID = (1 << 5),
	MAIL_FIF_NAME_VALID = (1 << 6),
	MAIL_FIF_UPDATE_QUEUED = (1 << 7)
} mfif;

typedef enum mail_folder_info_update_mode {
	MAIL_FIUM_UNKNOWN,
	MAIL_FIUM_EVOLUTION_STORAGE,
	MAIL_FIUM_LOCAL_STORAGE /*,*/
	/*MAIL_FIUM_SHELL_VIEW*/
} mfium;

typedef union mail_folder_info_update_info {
	EvolutionStorage *es;
	GNOME_Evolution_LocalStorage ls;
} mfiui;

typedef struct _mail_folder_info {
	gchar *uri;
	gchar *path;

	CamelFolder *folder;
	gchar *name;

	guint flags;
	guint unread, total, hidden;

	mfium update_mode;
	mfiui update_info;
} mail_folder_info;

static GHashTable *folders = NULL;
static GStaticMutex folders_lock = G_STATIC_MUTEX_INIT;

#define LOCK_FOLDERS()   G_STMT_START { ld(dm ("Locking folders")); g_static_mutex_lock (&folders_lock); } G_STMT_END
#define UNLOCK_FOLDERS() G_STMT_START { ld(dm ("Unocking folders")); g_static_mutex_unlock (&folders_lock); } G_STMT_END

static GNOME_Evolution_ShellView shell_view = CORBA_OBJECT_NIL;

/* Private functions */

static mail_folder_info *
get_folder_info (const gchar *uri)
{
	mail_folder_info *mfi;

	g_return_val_if_fail (uri, NULL);

	LOCK_FOLDERS ();

	if (folders == NULL) {
		dm("Initializing");
		folders = g_hash_table_new (g_str_hash, g_str_equal);
	}

	mfi = g_hash_table_lookup (folders, uri);

	if (!mfi) {
		dm("New entry for uri %s", uri);

		mfi = g_new (mail_folder_info, 1);
		mfi->uri = g_strdup (uri); /* XXX leak leak leak */
		mfi->path = NULL;
		mfi->folder = NULL;
		mfi->name = NULL;
		mfi->flags = 0;
		mfi->update_mode = MAIL_FIUM_UNKNOWN;
		mfi->update_info.es = NULL;

		g_hash_table_insert (folders, mfi->uri, mfi);
	} else
		dm("Hit cache for uri %s", uri);

	UNLOCK_FOLDERS ();

	return mfi;
}

static gchar *
make_string (mail_folder_info *mfi, gboolean full)
{
	gboolean set_one = FALSE;
	GString *work;
	gchar *ret;

	LOCK_FOLDERS ();

	/* Build the display string */

	work = g_string_new (mfi->name);

	if (mfi->flags == 0)
		goto done;

	if (!full &&
	    (!(mfi->flags & MAIL_FIF_UNREAD_VALID) ||
	     mfi->unread == 0))
		goto done;

	work = g_string_append (work, " (");

	if (full) {
		if (mfi->flags & MAIL_FIF_UNREAD_VALID) {
			g_string_sprintfa (work, _("%d new"), mfi->unread);
			set_one = TRUE;
		}
		
		if (mfi->flags & MAIL_FIF_HIDDEN_VALID) {
			if (set_one)
				work = g_string_append (work, _(", "));
			g_string_sprintfa (work, _("%d hidden"), mfi->hidden);
			set_one = TRUE;
		}
		
		if (mfi->flags & MAIL_FIF_TOTAL_VALID) {
			if (set_one)
				work = g_string_append (work, _(", "));
			g_string_sprintfa (work, _("%d total"), mfi->total);
		}
	} else {
		g_string_sprintfa (work, "%d", mfi->unread);
	}

	work = g_string_append (work, ")");

 done:
	UNLOCK_FOLDERS ();
	ret = work->str;
	g_string_free (work, FALSE);
	return ret;
}

static gboolean
update_idle (gpointer user_data)
{
	mail_folder_info *mfi = (mail_folder_info *) user_data;
	gchar *wide, *thin;
	gboolean bold;
	gchar *uri, *path;
	mfiui info;
	mfium mode;
	CORBA_Environment ev;

	LOCK_FOLDERS ();

	dm("update_idle called");

	mfi->flags &= (~MAIL_FIF_UPDATE_QUEUED);

	/* Check if this makes sense */

	if (!(mfi->flags & MAIL_FIF_NAME_VALID)) {
		g_warning ("Folder cache update info without \'name\' set");
		UNLOCK_FOLDERS ();
		return FALSE;
	}

	if (mfi->update_mode == MAIL_FIUM_UNKNOWN) {
		g_warning ("Folder cache update info without \'mode\' set");
		UNLOCK_FOLDERS ();
		return FALSE;
	}

	/* Get the display string */

	UNLOCK_FOLDERS ();
	wide = make_string (mfi, TRUE);
	thin = make_string (mfi, FALSE);
	LOCK_FOLDERS ();

	/* bold? */

	if (mfi->flags & MAIL_FIF_UNREAD_VALID &&
	    mfi->unread)
		bold = TRUE;
	else
		bold = FALSE;

	/* Set the value */
	/* Who knows how long these corba calls will take? */
	info = mfi->update_info;
	uri = g_strdup (mfi->uri);
	if (mfi->flags & MAIL_FIF_PATH_VALID)
		path = g_strdup (mfi->path);
	else
		path = NULL;
	mode = mfi->update_mode;

	UNLOCK_FOLDERS ();

	switch (mode) {
	case MAIL_FIUM_LOCAL_STORAGE:
		dm("Updating via LocalStorage");
		CORBA_exception_init (&ev);
		GNOME_Evolution_LocalStorage_updateFolder (info.ls,
							   path,
							   thin,
							   bold,
							   &ev);
		if (BONOBO_EX (&ev))
			g_warning ("Exception in local storage update: %s",
				   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
		break;
	case MAIL_FIUM_EVOLUTION_STORAGE:
		dm("Updating via EvolutionStorage");
		evolution_storage_update_folder_by_uri (info.es,
							uri,
							thin,
							bold);
		break;
	case MAIL_FIUM_UNKNOWN:
	default:
		g_assert_not_reached ();
		break;
	}

	/* Now set the folder bar if possible */

	if (shell_view != CORBA_OBJECT_NIL) {
		dm("Updating via ShellView");
		CORBA_exception_init (&ev);
		GNOME_Evolution_ShellView_setFolderBarLabel (shell_view,
							     wide,
							     &ev);
		if (BONOBO_EX (&ev))
			g_warning ("Exception in folder bar label update: %s",
				   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);

#if 0
		GNOME_Evolution_ShellView_setTitle (shell_view,
						    wide,
						    &ev);
		if (BONOBO_EX (&ev))
			g_warning ("Exception in shell view title update: %s",
				   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
#endif
	}

	/* Cleanup */

	g_free (uri);
	if (path)
		g_free (path);
	g_free (wide);
	g_free (thin);
	return FALSE;
}

static void
maybe_update (mail_folder_info *mfi)
{
	if (mfi->flags & MAIL_FIF_UPDATE_QUEUED)
		return;

	mfi->flags |= MAIL_FIF_UPDATE_QUEUED;
	g_idle_add (update_idle, mfi);
}

static void
update_message_counts_main (CamelObject *object, gpointer event_data,
			    gpointer user_data)
{
	mail_folder_info *mfi = user_data;

	LOCK_FOLDERS ();
	dm("Message counts in CamelFolder changed, queuing idle");
	mfi->flags &= (~MAIL_FIF_NEED_UPDATE);
	maybe_update (mfi);
	UNLOCK_FOLDERS ();
}

static void
update_message_counts (CamelObject *object, gpointer event_data,
		       gpointer user_data)
{
	CamelFolder *folder = CAMEL_FOLDER (object);
	mail_folder_info *mfi = user_data;
	int unread;
	int total;

	dm("CamelFolder %p changed, examining message counts", object);

	unread = camel_folder_get_unread_message_count (folder);
	total = camel_folder_get_message_count (folder);

	LOCK_FOLDERS ();

	mfi->flags &= (~MAIL_FIF_NEED_UPDATE);

	if (mfi->flags & MAIL_FIF_UNREAD_VALID) {
		if (mfi->unread != unread) {
			dm ("-> Unread value is changed");
			mfi->unread = unread;
			mfi->flags |= MAIL_FIF_NEED_UPDATE;
		} else 
			dm ("-> Unread value is the same");
	} else {
		dm ("-> Unread value being initialized");
		mfi->flags |= (MAIL_FIF_UNREAD_VALID | MAIL_FIF_NEED_UPDATE);
		mfi->unread = unread;
	}

	if (mfi->flags & MAIL_FIF_TOTAL_VALID) {
		if (mfi->total != total) {
			dm ("-> Total value is changed");
			mfi->total = total;
			mfi->flags |= MAIL_FIF_NEED_UPDATE;
		} else 
			dm ("-> Total value is the same");
	} else {
		dm ("-> Total value being initialized");
		mfi->flags |= (MAIL_FIF_TOTAL_VALID | MAIL_FIF_NEED_UPDATE);
		mfi->total = total;
	}

	/* while we're here... */
	if (!(mfi->flags & MAIL_FIF_NAME_VALID)) {
		mfi->name = g_strdup (camel_folder_get_name (CAMEL_FOLDER (object)));
		dm ("-> setting name to %s as well", mfi->name);
		mfi->flags |= MAIL_FIF_NAME_VALID;
	}

	if (mfi->flags & MAIL_FIF_NEED_UPDATE) {
		UNLOCK_FOLDERS ();
		dm ("-> Queuing change");
		mail_proxy_event (update_message_counts_main, object, event_data, user_data);
	} else {
		UNLOCK_FOLDERS ();
		dm ("-> No proxy event needed");
	}
}

static void
camel_folder_finalized (CamelObject *object, gpointer event_data,
			gpointer user_data)
{
	mail_folder_info *mfi = user_data;

	dm ("CamelFolder %p finalized, unsetting FOLDER_VALID", object);

	camel_object_unhook_event (object, "message_changed",
				   update_message_counts, mfi);
	camel_object_unhook_event (object, "folder_changed",
				   update_message_counts, mfi);
				   
	LOCK_FOLDERS ();
	mfi->flags &= (~MAIL_FIF_FOLDER_VALID);
	mfi->folder = NULL;
	UNLOCK_FOLDERS ();
}

static void
message_list_built (MessageList *ml, gpointer user_data)
{
	mail_folder_info *mfi = user_data;

	dm ("Message list %p rebuilt, checking hidden", ml);

	LOCK_FOLDERS ();

	if (ml->folder != mfi->folder) {
		g_warning ("folder cache: different folders in cache and messagelist");
		gtk_signal_disconnect_by_data (GTK_OBJECT (ml), user_data);
		UNLOCK_FOLDERS ();
		return;
	}

	MESSAGE_LIST_LOCK (ml, hide_lock);
	if (ml->hidden)
		mfi->hidden = g_hash_table_size (ml->hidden);
	else
		mfi->hidden = 0;
	MESSAGE_LIST_UNLOCK (ml, hide_lock);

	mfi->flags |= MAIL_FIF_HIDDEN_VALID;

	UNLOCK_FOLDERS ();

	maybe_update (mfi);
}

/* get folder info operation */

struct get_mail_info_msg {
	struct _mail_msg msg;

	CamelFolder *folder;
	mail_folder_info *mfi;
};

static char *
get_mail_info_describe (struct _mail_msg *msg, int complete)
{
	struct get_mail_info_msg *gmim = (struct get_mail_info_msg *) msg;

	return g_strdup_printf ("Examining \'%s\'", camel_folder_get_full_name (gmim->folder));
}

static void
get_mail_info_receive (struct _mail_msg *msg)
{
	struct get_mail_info_msg *gmim = (struct get_mail_info_msg *) msg;

	LOCK_FOLDERS ();

	if (!(gmim->mfi->flags & MAIL_FIF_NAME_VALID)) {
		gmim->mfi->name = g_strdup (camel_folder_get_name (gmim->folder));
		gmim->mfi->flags |= MAIL_FIF_NAME_VALID;
	}

	gmim->mfi->unread = camel_folder_get_unread_message_count (gmim->folder);
	gmim->mfi->total = camel_folder_get_message_count (gmim->folder);
	gmim->mfi->flags |= (MAIL_FIF_UNREAD_VALID | MAIL_FIF_TOTAL_VALID);

	UNLOCK_FOLDERS ();
}

static void
get_mail_info_reply (struct _mail_msg *msg)
{
	struct get_mail_info_msg *gmim = (struct get_mail_info_msg *) msg;

	maybe_update (gmim->mfi);
}

static void
get_mail_info_destroy (struct _mail_msg *msg)
{
	struct get_mail_info_msg *gmim = (struct get_mail_info_msg *) msg;

	camel_object_unref (CAMEL_OBJECT (gmim->folder));
}

static mail_msg_op_t get_mail_info_op = {
	get_mail_info_describe,
	get_mail_info_receive,
	get_mail_info_reply,
	get_mail_info_destroy
};

static void
get_mail_info (CamelFolder *folder, mail_folder_info *mfi)
{
	struct get_mail_info_msg *gmim;

	gmim = mail_msg_new (&get_mail_info_op, NULL, sizeof (*gmim));
	gmim->folder = folder;
	camel_object_ref (CAMEL_OBJECT (folder));
	gmim->mfi = mfi;

	e_thread_put (mail_thread_new, (EMsg *) gmim);
}

/* Public functions */

void 
mail_folder_cache_set_update_estorage (const gchar *uri, EvolutionStorage *estorage)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	if (mfi->update_mode != MAIL_FIUM_UNKNOWN) {
		/* we could check to see that update_mode = ESTORAGE */
		/*g_warning ("folder cache: update mode already set??");*/
		UNLOCK_FOLDERS ();
		return;
	}

	dm ("Uri %s updates with EVOLUTION_STORAGE", uri);
	mfi->update_mode = MAIL_FIUM_EVOLUTION_STORAGE;
	mfi->update_info.es = estorage;

	UNLOCK_FOLDERS ();
}

void 
mail_folder_cache_set_update_lstorage (const gchar *uri, GNOME_Evolution_LocalStorage lstorage, const gchar *path)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	if (mfi->update_mode != MAIL_FIUM_UNKNOWN) {
		/*we could check to see that update_mode = lstorage */
		/*g_warning ("folder cache: update mode already set??");*/
		UNLOCK_FOLDERS ();
		return;
	}

	dm ("Uri %s updates with LOCAL_STORAGE", uri);
	/* Note that we don't dup the object or anything. Too lazy. */
	mfi->update_mode = MAIL_FIUM_LOCAL_STORAGE;
	mfi->update_info.ls = lstorage;

	mfi->path = g_strdup (path);
	mfi->flags |= MAIL_FIF_PATH_VALID;

	UNLOCK_FOLDERS ();
}

#if 0
void 
mail_folder_cache_set_update_shellview (const gchar *uri)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	if (mfi->update_mode != MAIL_FIUM_UNKNOWN) {
		/*we could check to see that update_mode = shellview */
		/*g_warning ("folder cache: update mode already set??");*/
		UNLOCK_FOLDERS ();
		return;
	}

	dm ("Uri %s updates with SHELL VIEW", uri);
	mfi->update_mode = MAIL_FIUM_SHELL_VIEW;

	UNLOCK_FOLDERS ();
}
#endif

void
mail_folder_cache_note_folder (const gchar *uri, CamelFolder *folder)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	if (mfi->flags & MAIL_FIF_FOLDER_VALID) {
		if (mfi->folder != folder)
			g_warning ("folder cache: CamelFolder being changed for %s??? I refuse.", uri);
		UNLOCK_FOLDERS ();
		return;
	}

	dm ("Setting uri %s to watch folder %p", uri, folder);

	mfi->flags |= MAIL_FIF_FOLDER_VALID;
	mfi->folder = folder;
	
	UNLOCK_FOLDERS ();

	camel_object_hook_event (CAMEL_OBJECT (folder), "message_changed",
				 update_message_counts, mfi);
	camel_object_hook_event (CAMEL_OBJECT (folder), "folder_changed",
				 update_message_counts, mfi);
	camel_object_hook_event (CAMEL_OBJECT (folder), "finalize",
				 camel_folder_finalized, mfi);

	get_mail_info (folder, mfi);
}

void 
mail_folder_cache_note_message_list (const gchar *uri, MessageList *ml)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	if (!(mfi->flags & MAIL_FIF_FOLDER_VALID)) {
		dm ("No folder specified so ignoring NOTE_ML at %s", uri);
		/* cache the FB? maybe later */
		UNLOCK_FOLDERS ();
		return;
	}

	dm ("Noting message list %p for %s", ml, uri);

	gtk_signal_connect (GTK_OBJECT (ml), "message_list_built",
			    message_list_built, mfi);

	UNLOCK_FOLDERS ();

	dm ("-> faking message_list_built", ml, uri);
	message_list_built (ml, mfi);
}

void 
mail_folder_cache_note_folderinfo (const gchar *uri, CamelFolderInfo *fi)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	dm ("Noting folderinfo %p for %s", fi, uri);

	mfi->unread = fi->unread_message_count;
	mfi->flags |= MAIL_FIF_UNREAD_VALID;

	if (!(mfi->flags & MAIL_FIF_NAME_VALID)) {
		dm ("-> setting name %s", fi->name);
		mfi->name = g_strdup (fi->name);
		mfi->flags |= MAIL_FIF_NAME_VALID;
	}

	UNLOCK_FOLDERS ();

	maybe_update (mfi);
}

void 
mail_folder_cache_note_name (const gchar *uri, const gchar *name)
{
	mail_folder_info *mfi;

	mfi = get_folder_info (uri);
	g_return_if_fail (mfi);

	LOCK_FOLDERS ();

	dm ("Noting name %s for %s", name, uri);

	if (mfi->flags & MAIL_FIF_NAME_VALID) {
		/* we could complain.. */
		dm ("-> name already set: %s", mfi->name);
		UNLOCK_FOLDERS ();
		return;
	}

	mfi->name = g_strdup (name);
	mfi->flags |= MAIL_FIF_NAME_VALID;

	UNLOCK_FOLDERS ();

	maybe_update (mfi);
}

CamelFolder *
mail_folder_cache_try_folder (const gchar *uri)
{
	mail_folder_info *mfi;
	CamelFolder *ret;

	mfi = get_folder_info (uri);
	g_return_val_if_fail (mfi, NULL);

	LOCK_FOLDERS ();

	if (mfi->flags & MAIL_FIF_FOLDER_VALID)
		ret = mfi->folder;
	else
		ret = NULL;

	UNLOCK_FOLDERS ();

	return ret;
}

void
mail_folder_cache_set_shell_view (GNOME_Evolution_ShellView sv)
{
	CORBA_Environment ev;

	g_return_if_fail (sv != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	if (shell_view != CORBA_OBJECT_NIL)
		CORBA_Object_release (shell_view, &ev);

	if (BONOBO_EX (&ev))
		g_warning ("Exception in releasing old shell view: %s",
			   bonobo_exception_get_text (&ev));
	CORBA_exception_free (&ev);

	shell_view = CORBA_Object_duplicate (sv, &ev);
	if (BONOBO_EX (&ev))
		g_warning ("Exception in duping new shell view: %s",
			   bonobo_exception_get_text (&ev));
	CORBA_exception_free (&ev);
}
		
#if d(!)0
#include <stdio.h>

static void
print_item (gpointer key, gpointer value, gpointer user_data)
{
	gchar *uri = key;
	mail_folder_info *mfi = value;

	printf ("* %s\n", uri);

	if (mfi->flags & MAIL_FIF_PATH_VALID)
		printf ("         Path: %s\n", mfi->path);
	if (mfi->flags & MAIL_FIF_NAME_VALID)
		printf ("         Name: %s\n", mfi->name);
	if (mfi->flags & MAIL_FIF_FOLDER_VALID)
		printf ("       Folder: %p\n", mfi->folder);
	if (mfi->flags & MAIL_FIF_UNREAD_VALID)
		printf ("       Unread: %d\n", mfi->unread);
	if (mfi->flags & MAIL_FIF_HIDDEN_VALID)
		printf ("       Hidden: %d\n", mfi->hidden);
	if (mfi->flags & MAIL_FIF_TOTAL_VALID)
		printf ("        Total: %d\n", mfi->total);
	if (mfi->flags & MAIL_FIF_NEED_UPDATE)
		printf ("       Needs an update\n");
	switch (mfi->update_mode) {
	case MAIL_FIUM_UNKNOWN:
		printf ("       Update mode: UNKNOWN\n");
		break;
	case MAIL_FIUM_EVOLUTION_STORAGE:
		printf ("       Update mode: Evolution\n");
		break;
	case MAIL_FIUM_LOCAL_STORAGE:
		printf ("       Update mode: Local\n");
		break;
/*
	case MAIL_FIUM_SHELL_VIEW:
		printf ("       Update mode: Shell View\n");
		break;
*/
	}

	printf ("\n");
}
		
void mail_folder_cache_dump_cache (void);

void
mail_folder_cache_dump_cache (void)
{
	printf ("********** MAIL FOLDER CACHE DUMP ************\n\n");
	LOCK_FOLDERS ();
	g_hash_table_foreach (folders, print_item, NULL);
	UNLOCK_FOLDERS ();
	printf ("********** END OF CACHE DUMP. ****************\n");
}

#endif
