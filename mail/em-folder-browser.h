/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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


#ifndef _EM_FOLDER_BROWSER_H
#define _EM_FOLDER_BROWSER_H

#include "em-folder-view.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

typedef struct _EMFolderBrowser EMFolderBrowser;
typedef struct _EMFolderBrowserClass EMFolderBrowserClass;

struct _EMFolderBrowser {
	EMFolderView view;

	GtkWidget *vpane;
	struct _EFilterBar *search;

	struct _EMFolderBrowserPrivate *priv;
};

struct _EMFolderBrowserClass {
	EMFolderViewClass parent_class;
};

GType em_folder_browser_get_type(void);

GtkWidget *em_folder_browser_new(void);

void em_folder_browser_show_preview(EMFolderBrowser *emfv, gboolean state);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _EM_FOLDER_BROWSER_H */
