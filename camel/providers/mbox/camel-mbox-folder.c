/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-mbox-folder.c : Abstract class for an email folder */

/* 
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com> 
 *          Michael Zucchi <notzed@helixcode.com>
 *          Jeffrey Stedfast <fejj@helixcode.com>
 *
 * Copyright (C) 1999, 2000 Helix Code Inc.
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

#include <config.h>

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "camel-mbox-folder.h"
#include "camel-mbox-store.h"
#include "string-utils.h"
#include "camel-stream-fs.h"
#include "camel-mbox-summary.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-stream-filter.h"
#include "camel-mime-filter-from.h"
#include "camel-exception.h"

#define d(x)

static CamelFolderClass *parent_class = NULL;

/* Returns the class for a CamelMboxFolder */
#define CMBOXF_CLASS(so) CAMEL_MBOX_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMBOXS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))


static void mbox_refresh_info (CamelFolder *folder, CamelException *ex);
static void mbox_sync(CamelFolder *folder, gboolean expunge, CamelException *ex);
static gint mbox_get_message_count(CamelFolder *folder);
static gint mbox_get_unread_message_count(CamelFolder *folder);
static void mbox_append_message(CamelFolder *folder, CamelMimeMessage * message, const CamelMessageInfo * info,

				CamelException *ex);
static GPtrArray *mbox_get_uids(CamelFolder *folder);
static GPtrArray *mbox_get_summary(CamelFolder *folder);
static CamelMimeMessage *mbox_get_message(CamelFolder *folder, const gchar * uid, CamelException *ex);

static void mbox_expunge(CamelFolder *folder, CamelException *ex);

static const CamelMessageInfo *mbox_get_message_info(CamelFolder *folder, const char *uid);

static GPtrArray *mbox_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);
static void mbox_search_free(CamelFolder *folder, GPtrArray * result);

static guint32 mbox_get_message_flags(CamelFolder *folder, const char *uid);
static void mbox_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static gboolean mbox_get_message_user_flag(CamelFolder *folder, const char *uid, const char *name);
static void mbox_set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value);
static const char *mbox_get_message_user_tag(CamelFolder *folder, const char *uid, const char *name);
static void mbox_set_message_user_tag(CamelFolder *folder, const char *uid, const char *name, const char *value);


static void mbox_finalize(CamelObject * object);

static void
camel_mbox_folder_class_init(CamelMboxFolderClass * camel_mbox_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_mbox_folder_class);

	parent_class = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs(camel_folder_get_type()));

	/* virtual method definition */

	/* virtual method overload */
	camel_folder_class->refresh_info = mbox_refresh_info;
	camel_folder_class->sync = mbox_sync;
	camel_folder_class->get_message_count = mbox_get_message_count;
	camel_folder_class->get_unread_message_count = mbox_get_unread_message_count;
	camel_folder_class->append_message = mbox_append_message;
	camel_folder_class->get_uids = mbox_get_uids;
	camel_folder_class->free_uids = camel_folder_free_deep;
	camel_folder_class->get_summary = mbox_get_summary;
	camel_folder_class->free_summary = camel_folder_free_nop;
	camel_folder_class->expunge = mbox_expunge;

	camel_folder_class->get_message = mbox_get_message;

	camel_folder_class->search_by_expression = mbox_search_by_expression;
	camel_folder_class->search_free = mbox_search_free;

	camel_folder_class->get_message_info = mbox_get_message_info;

	camel_folder_class->get_message_flags = mbox_get_message_flags;
	camel_folder_class->set_message_flags = mbox_set_message_flags;
	camel_folder_class->get_message_user_flag = mbox_get_message_user_flag;
	camel_folder_class->set_message_user_flag = mbox_set_message_user_flag;
	camel_folder_class->get_message_user_tag = mbox_get_message_user_tag;
	camel_folder_class->set_message_user_tag = mbox_set_message_user_tag;
}

static void
mbox_init(gpointer object, gpointer klass)
{
	CamelFolder *folder = object;
	CamelMboxFolder *mbox_folder = object;

	folder->has_summary_capability = TRUE;
	folder->has_search_capability = TRUE;

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
	    CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT |
	    CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_USER;
	/* FIXME: we don't actually preserve user flags right now. */

	mbox_folder->summary = NULL;
	mbox_folder->search = NULL;
}

static void
mbox_finalize(CamelObject * object)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(object);

	if (mbox_folder->index)
		ibex_close(mbox_folder->index);

	g_free(mbox_folder->folder_file_path);
	g_free(mbox_folder->summary_file_path);
	g_free(mbox_folder->folder_dir_path);
	g_free(mbox_folder->index_file_path);

	camel_folder_change_info_free(mbox_folder->changes);
}

CamelType camel_mbox_folder_get_type(void)
{
	static CamelType camel_mbox_folder_type = CAMEL_INVALID_TYPE;

	if (camel_mbox_folder_type == CAMEL_INVALID_TYPE) {
		camel_mbox_folder_type = camel_type_register(CAMEL_FOLDER_TYPE, "CamelMboxFolder",
							     sizeof(CamelMboxFolder),
							     sizeof(CamelMboxFolderClass),
							     (CamelObjectClassInitFunc) camel_mbox_folder_class_init,
							     NULL,
							     (CamelObjectInitFunc) mbox_init,
							     (CamelObjectFinalizeFunc) mbox_finalize);
	}

	return camel_mbox_folder_type;
}

CamelFolder *
camel_mbox_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;
	CamelMboxFolder *mbox_folder;
	const char *root_dir_path, *name;
	struct stat st;
	int forceindex;

	folder = CAMEL_FOLDER (camel_object_new (CAMEL_MBOX_FOLDER_TYPE));
	mbox_folder = (CamelMboxFolder *)folder;

	name = strrchr(full_name, '/');
	if (name)
		name++;
	else
		name = full_name;

	camel_folder_construct(folder, parent_store, full_name, name);

	root_dir_path = camel_mbox_store_get_toplevel_dir(CAMEL_MBOX_STORE(folder->parent_store));

	mbox_folder->folder_file_path = g_strdup_printf("%s/%s", root_dir_path, full_name);
	mbox_folder->summary_file_path = g_strdup_printf("%s/%s-ev-summary", root_dir_path, full_name);
	mbox_folder->folder_dir_path = g_strdup_printf("%s/%s.sdb", root_dir_path, full_name);
	mbox_folder->index_file_path = g_strdup_printf("%s/%s.ibex", root_dir_path, full_name);

	mbox_folder->changes = camel_folder_change_info_new();

	/* if we have no index file, force it */
	forceindex = stat(mbox_folder->index_file_path, &st) == -1;
	if (flags & CAMEL_STORE_FOLDER_BODY_INDEX) {

		mbox_folder->index = ibex_open(mbox_folder->index_file_path, O_CREAT | O_RDWR, 0600);
		if (mbox_folder->index == NULL) {
			/* yes, this isn't fatal at all */
			g_warning("Could not open/create index file: %s: indexing not performed", strerror(errno));
			forceindex = FALSE;
		}
	} else {
		/* if we do have an index file, remove it */
		if (forceindex == FALSE) {
			unlink(mbox_folder->index_file_path);
		}
		forceindex = FALSE;
	}
	/* no summary (disk or memory), and we're proverbially screwed */
	mbox_folder->summary = camel_mbox_summary_new(mbox_folder->summary_file_path,
						      mbox_folder->folder_file_path, mbox_folder->index);
	if (mbox_folder->summary == NULL || camel_mbox_summary_load(mbox_folder->summary, forceindex) == -1) {
		camel_exception_set(ex, CAMEL_EXCEPTION_FOLDER_INVALID,	/* FIXME: right error code */
				    "Could not create summary");
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}

	return folder;
}

static void
mbox_refresh_info (CamelFolder *folder, CamelException *ex)
{
	/* we are always in a consistent state, or fix it when we need to */
	return;
}

static void
mbox_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	if (expunge)
		mbox_expunge(folder, ex);
	else {
		camel_mbox_summary_sync(mbox_folder->summary, FALSE, mbox_folder->changes, ex);
		camel_object_trigger_event(CAMEL_OBJECT(folder), "folder_changed", mbox_folder->changes);
		camel_folder_change_info_clear(mbox_folder->changes);
	}

	/* save index */
	if (mbox_folder->index)
		ibex_save(mbox_folder->index);
	if (mbox_folder->summary)
		camel_folder_summary_save(CAMEL_FOLDER_SUMMARY(mbox_folder->summary));
}

static void
mbox_expunge(CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	camel_mbox_summary_sync(mbox_folder->summary, TRUE, mbox_folder->changes, ex);
	camel_object_trigger_event(CAMEL_OBJECT(folder), "folder_changed", mbox_folder->changes);
	camel_folder_change_info_clear(mbox_folder->changes);
}

static gint
mbox_get_message_count(CamelFolder *folder)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	g_return_val_if_fail(mbox_folder->summary != NULL, -1);

	return camel_folder_summary_count(CAMEL_FOLDER_SUMMARY(mbox_folder->summary));
}

static gint
mbox_get_unread_message_count(CamelFolder *folder)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);
	CamelMessageInfo *info;
	GPtrArray *infolist;
	gint i, max, count = 0;

	g_return_val_if_fail(mbox_folder->summary != NULL, -1);

	max = camel_folder_summary_count(CAMEL_FOLDER_SUMMARY(mbox_folder->summary));
	if (max == -1)
		return -1;

	infolist = mbox_get_summary(folder);

	for (i = 0; i < infolist->len; i++) {
		info = (CamelMessageInfo *) g_ptr_array_index(infolist, i);
		if (!(info->flags & CAMEL_MESSAGE_SEEN))
			count++;
	}

	return count;
}

/* FIXME: this may need some tweaking for performance? */
static void
mbox_append_message(CamelFolder *folder, CamelMimeMessage * message, const CamelMessageInfo * info, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);
	CamelStream *output_stream = NULL, *filter_stream = NULL;
	CamelMimeFilter *filter_from = NULL;
	CamelMessageInfo *newinfo;
	struct stat st;
	off_t seek = -1;
	char *xev, last;
	guint32 uid;
	char *fromline = NULL;

	if (stat(mbox_folder->folder_file_path, &st) != 0)
		goto fail;

	output_stream = camel_stream_fs_new_with_name(mbox_folder->folder_file_path, O_RDWR, 0600);
	if (output_stream == NULL)
		goto fail;

	if (st.st_size) {
		seek = camel_seekable_stream_seek((CamelSeekableStream *) output_stream, st.st_size - 1, SEEK_SET);
		if (++seek != st.st_size)
			goto fail;

		/* If the mbox doesn't end with a newline, fix that. */
		if (camel_stream_read(output_stream, &last, 1) != 1)
			goto fail;
		if (last != '\n')
			camel_stream_write(output_stream, "\n", 1);
	} else
		seek = 0;

	/* assign a new x-evolution header/uid */
	camel_medium_remove_header(CAMEL_MEDIUM(message), "X-Evolution");
	uid = camel_folder_summary_next_uid(CAMEL_FOLDER_SUMMARY(mbox_folder->summary));
	/* important that the header matches exactly 00000000-0000 */
	xev = g_strdup_printf("%08x-%04x", uid, info ? info->flags & 0xFFFF : 0);
	camel_medium_add_header(CAMEL_MEDIUM(message), "X-Evolution", xev);
	g_free(xev);

	/* we must write this to the non-filtered stream ... */
	fromline = camel_mbox_summary_build_from(CAMEL_MIME_PART(message)->headers);
	if (camel_stream_write_string(output_stream, fromline) == -1)
		goto fail;

	/* and write the content to the filtering stream, that translated '\nFrom' into '\n>From' */
	filter_stream = (CamelStream *) camel_stream_filter_new_with_stream(output_stream);
	filter_from = (CamelMimeFilter *) camel_mime_filter_from_new();
	camel_stream_filter_add((CamelStreamFilter *) filter_stream, filter_from);
	if (camel_data_wrapper_write_to_stream(CAMEL_DATA_WRAPPER(message), filter_stream) == -1)
		goto fail;

	if (camel_stream_close(filter_stream) == -1)
		goto fail;

	/* filter stream ref's the output stream itself, so we need to unref it too */
	camel_object_unref(CAMEL_OBJECT(filter_from));
	camel_object_unref(CAMEL_OBJECT(filter_stream));
	camel_object_unref(CAMEL_OBJECT(output_stream));
	g_free(fromline);

	/* force a summary update - will only update from the new position, if it can */
	if (camel_mbox_summary_update(mbox_folder->summary, seek, mbox_folder->changes) == 0) {
		char uidstr[16];

		sprintf(uidstr, "%u", uid);
		newinfo = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mbox_folder->summary), uidstr);

		if (info && newinfo) {
			CamelFlag *flag = info->user_flags;
			CamelTag *tag = info->user_tags;

			while (flag) {
				camel_flag_set(&(newinfo->user_flags), flag->name, TRUE);
				flag = flag->next;
			}

			while (tag) {
				camel_tag_set(&(newinfo->user_tags), tag->name, tag->value);
				tag = tag->next;
			}
		}
		camel_object_trigger_event(CAMEL_OBJECT(folder), "folder_changed", mbox_folder->changes);
		camel_folder_change_info_clear(mbox_folder->changes);
	}

	return;

      fail:
	if (camel_exception_is_set(ex)) {
		camel_exception_setv(ex, camel_exception_get_id(ex),
				     "Cannot append message to mbox file: %s", camel_exception_get_description(ex));
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     "Cannot append message to mbox file: %s", g_strerror(errno));
	}
	if (filter_stream) {
		/*camel_stream_close (filter_stream); */
		camel_object_unref(CAMEL_OBJECT(filter_stream));
	}
	if (output_stream)
		camel_object_unref(CAMEL_OBJECT(output_stream));

	if (filter_from)
		camel_object_unref(CAMEL_OBJECT(filter_from));

	g_free(fromline);

	/* make sure the file isn't munged by us */
	if (seek != -1) {
		int fd = open(mbox_folder->folder_file_path, O_WRONLY, 0600);

		if (fd != -1) {
			ftruncate(fd, st.st_size);
			close(fd);
		}
	}
}

static GPtrArray *
mbox_get_uids(CamelFolder *folder)
{
	GPtrArray *array;
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);
	int i, count;

	count = camel_folder_summary_count(CAMEL_FOLDER_SUMMARY(mbox_folder->summary));
	array = g_ptr_array_new();
	g_ptr_array_set_size(array, count);
	for (i = 0; i < count; i++) {
		CamelMboxMessageInfo *info =
		    (CamelMboxMessageInfo *) camel_folder_summary_index(CAMEL_FOLDER_SUMMARY(mbox_folder->summary), i);

		array->pdata[i] = g_strdup(info->info.uid);
	}

	return array;
}

static CamelMimeMessage *
mbox_get_message(CamelFolder *folder, const gchar * uid, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	CamelMboxMessageInfo *info;
	CamelMimeParser *parser = NULL;
	char *buffer;
	int len;

	/* get the message summary info */
	info = (CamelMboxMessageInfo *) camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mbox_folder->summary), uid);

	if (info == NULL) {
		errno = ENOENT;
		goto fail;
	}

	/* if this has no content, its an error in the library */
	g_assert(info->info.content);
	g_assert(info->frompos != -1);

	/* where we read from */
	message_stream = camel_stream_fs_new_with_name(mbox_folder->folder_file_path, O_RDONLY, 0);
	if (message_stream == NULL)
		goto fail;

	/* we use a parser to verify the message is correct, and in the correct position */
	parser = camel_mime_parser_new();
	camel_mime_parser_init_with_stream(parser, message_stream);
	camel_object_unref(CAMEL_OBJECT(message_stream));
	camel_mime_parser_scan_from(parser, TRUE);

	camel_mime_parser_seek(parser, info->frompos, SEEK_SET);
	if (camel_mime_parser_step(parser, &buffer, &len) != HSCAN_FROM) {
		g_warning("File appears truncated");
		goto fail;
	}

	if (camel_mime_parser_tell_start_from(parser) != info->frompos) {
		/* TODO: This should probably perform a re-sync/etc, and try again? */
		g_warning("Summary doesn't match the folder contents!  eek!\n"
			  "  expecting offset %ld got %ld", (long int)info->frompos,
			  (long int)camel_mime_parser_tell_start_from(parser));
		errno = EINVAL;
		goto fail;
	}

	message = camel_mime_message_new();
	if (camel_mime_part_construct_from_parser(CAMEL_MIME_PART(message), parser) == -1) {
		g_warning("Construction failed");
		goto fail;
	}
	camel_object_unref(CAMEL_OBJECT(parser));

	return message;

      fail:
	camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID, "Cannot get message: %s", g_strerror(errno));

	if (parser)
		camel_object_unref(CAMEL_OBJECT(parser));
	if (message)
		camel_object_unref(CAMEL_OBJECT(message));

	return NULL;
}

GPtrArray *
mbox_get_summary(CamelFolder *folder)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	return CAMEL_FOLDER_SUMMARY(mbox_folder->summary)->messages;
}

/* get a single message info, by uid */
static const CamelMessageInfo *
mbox_get_message_info(CamelFolder *folder, const char *uid)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	return camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mbox_folder->summary), uid);
}

static GPtrArray *
mbox_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	if (mbox_folder->search == NULL) {
		mbox_folder->search = camel_folder_search_new();
	}

	camel_folder_search_set_folder(mbox_folder->search, folder);
	if (mbox_folder->summary) {
		/* FIXME: dont access summary array directly? */
		camel_folder_search_set_summary(mbox_folder->search,
						CAMEL_FOLDER_SUMMARY(mbox_folder->summary)->messages);
	}

	camel_folder_search_set_body_index(mbox_folder->search, mbox_folder->index);

	return camel_folder_search_execute_expression(mbox_folder->search, expression, ex);
}

static void
mbox_search_free(CamelFolder *folder, GPtrArray * result)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER(folder);

	camel_folder_search_free_result(mbox_folder->search, result);
}

static guint32
mbox_get_message_flags(CamelFolder *folder, const char *uid)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = CAMEL_MBOX_FOLDER(folder);

	info = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mf->summary), uid);
	g_return_val_if_fail(info != NULL, 0);

	return info->flags;
}

static void
mbox_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = CAMEL_MBOX_FOLDER(folder);

	info = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mf->summary), uid);
	g_return_if_fail(info != NULL);

	info->flags = (info->flags & ~flags) | (set & flags) | CAMEL_MESSAGE_FOLDER_FLAGGED;
	camel_folder_summary_touch(CAMEL_FOLDER_SUMMARY(mf->summary));

	camel_object_trigger_event(CAMEL_OBJECT(folder), "message_changed", (char *) uid);
}

static gboolean
mbox_get_message_user_flag(CamelFolder *folder, const char *uid, const char *name)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = CAMEL_MBOX_FOLDER(folder);

	info = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mf->summary), uid);
	g_return_val_if_fail(info != NULL, FALSE);

	return camel_flag_get(&info->user_flags, name);
}

static void
mbox_set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = CAMEL_MBOX_FOLDER(folder);

	info = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mf->summary), uid);
	g_return_if_fail(info != NULL);

	camel_flag_set(&info->user_flags, name, value);
	info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
	camel_folder_summary_touch(CAMEL_FOLDER_SUMMARY(mf->summary));
	camel_object_trigger_event(CAMEL_OBJECT(folder), "message_changed", (char *) uid);
}

static const char *mbox_get_message_user_tag(CamelFolder *folder, const char *uid, const char *name)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = CAMEL_MBOX_FOLDER(folder);

	info = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mf->summary), uid);
	g_return_val_if_fail(info != NULL, FALSE);

	return camel_tag_get(&info->user_tags, name);
}

static void mbox_set_message_user_tag(CamelFolder *folder, const char *uid, const char *name, const char *value)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = CAMEL_MBOX_FOLDER(folder);

	info = camel_folder_summary_uid(CAMEL_FOLDER_SUMMARY(mf->summary), uid);
	g_return_if_fail(info != NULL);

	camel_tag_set(&info->user_tags, name, value);
	info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
	camel_folder_summary_touch(CAMEL_FOLDER_SUMMARY(mf->summary));
	camel_object_trigger_event(CAMEL_OBJECT(folder), "message_changed", (char *) uid);
}


