/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@helixcode.com>
 *
 *  Copyright 2000 Helix Code, Inc. (www.helixcode.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#include <config.h>
#include "camel-imap-stream.h"
#include <sys/types.h>
#include <errno.h>

static CamelStreamClass *parent_class = NULL;

/* Returns the class for a CamelImapStream */
#define CIS_CLASS(so) CAMEL_IMAP_STREAM_CLASS (GTK_OBJECT(so)->klass)

static ssize_t stream_read (CamelStream *stream, char *buffer, size_t n);
static int stream_reset (CamelStream *stream);
static gboolean stream_eos (CamelStream *stream);

static void finalize (GtkObject *object);

static void
camel_imap_stream_class_init (CamelImapStreamClass *camel_imap_stream_class)
{
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_imap_stream_class);
	GtkObjectClass *gtk_object_class =
		GTK_OBJECT_CLASS (camel_imap_stream_class);

	parent_class = gtk_type_class (camel_stream_get_type ());

	/* virtual method overload */
	camel_stream_class->read  = stream_read;
	/*camel_stream_class->write = stream_write;*/
	camel_stream_class->reset = stream_reset;
	camel_stream_class->eos   = stream_eos;

	gtk_object_class->finalize = finalize;
}

static void
camel_imap_stream_init (gpointer object, gpointer klass)
{
	CamelImapStream *imap_stream = CAMEL_IMAP_STREAM (object);

	imap_stream->cache = NULL;
	imap_stream->cache_ptr = NULL;
}

GtkType
camel_imap_stream_get_type (void)
{
	static GtkType camel_imap_stream_type = 0;

	if (!camel_imap_stream_type) {
		GtkTypeInfo camel_imap_stream_info =
		{
			"CamelImapStream",
			sizeof (CamelImapStream),
			sizeof (CamelImapStreamClass),
			(GtkClassInitFunc) camel_imap_stream_class_init,
			(GtkObjectInitFunc) camel_imap_stream_init,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		camel_imap_stream_type = gtk_type_unique (camel_stream_get_type (), &camel_imap_stream_info);
	}

	return camel_imap_stream_type;
}

CamelStream *
camel_imap_stream_new (CamelImapFolder *folder, char *command)
{
	CamelImapStream *imap_stream;

	imap_stream = gtk_type_new (camel_imap_stream_get_type ());

	imap_stream->folder  = folder;
	gtk_object_ref(GTK_OBJECT (imap_stream->folder));
	
	imap_stream->command = g_strdup(command);
	
	return CAMEL_STREAM (imap_stream);
}

static void
finalize (GtkObject *object)
{
	CamelImapStream *imap_stream = CAMEL_IMAP_STREAM (object);

	g_free(imap_stream->cache);
	g_free(imap_stream->command);

	if (imap_stream->folder)
		gtk_object_unref(imap_stream->folder);

	GTK_OBJECT_CLASS (parent_class)->finalize (object);
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	ssize_t nread;
	
	/* do we want to do any IMAP specific parsing in here? If not, maybe rename to camel-stream-cache? */
	CamelImapStream *imap_stream = CAMEL_IMAP_STREAM (stream);

	if (!imap_stream->cache) {
		/* We need to send the IMAP command since this is our first fetch */
		CamelImapStore *store = CAMEL_IMAP_STORE (imap_stream->folder->parent_store);
		gint status;
		
		status = camel_imap_command_extended(store->ostream, imap_stream->cache, "%s",
						     imap_stream->command);

		if (status != CAMEL_IMAP_OK) {
			/* we got an error, dump this stuff */
			g_free(imap_stream->cache);
			imap_stream->cache = NULL;
			
			return -1;
		}
		
		/* we don't need the folder anymore... */
		gtk_object_unref(GTK_OBJECT (imap_stream->folder));

		imap_stream->cache_ptr = imap_stream->cache;
	}
	
	/* we've already read this stream, so return whats in the cache */
	nread = MIN (n, strlen(imap_stream->cache_ptr));
	
	if (nread > 0) {
		memcpy(buffer, imap_stream->cache_ptr, nread);
		imap_stream->cache_ptr += nread;
	} else {
		nread = -1;
	}

	return nread;
}

static int
stream_write (CamelStream *stream, const char *buffer, unsigned int n)
{
	/* I don't think we need/want this functionality */
	CamelImapStream *imap_stream = CAMEL_IMAP_STREAM (stream);

	if (!imap_stream->cache) {
		imap_stream->cache = g_malloc0(n + 1);
		memcpy(imap_stream->cache, buffer, n);
	} else {
		imap_stream->cache = g_realloc(strlen(imap_stream->cache) + n + 1);
		memcpy(imap_stream->cache[strlen(imap_stream->cache)], buffer, n);
	}

	return n;
}

static int
stream_reset (CamelStream *stream)
{
	CamelImapStream *imap_stream = CAMEL_IMAP_STREAM (stream);
	
	imap_stream->cache_ptr = imap_stream->cache;

	return 1;
}

static gboolean
stream_eos (CamelStream *stream)
{
	CamelImapStream *imap_stream = CAMEL_IMAP_STREAM (stream);

	return (imap_stream->cache_ptr && strlen(imap_stream->cache_ptr));
}











