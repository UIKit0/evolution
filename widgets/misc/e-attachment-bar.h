/*
 * e-attachment-bar.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_ATTACHMENT_BAR_H
#define E_ATTACHMENT_BAR_H

#include <gtk/gtk.h>
#include <misc/e-attachment-view.h>

/* Standard GObject macros */
#define E_TYPE_ATTACHMENT_BAR \
	(e_attachment_bar_get_type ())
#define E_ATTACHMENT_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ATTACHMENT_BAR, EAttachmentBar))
#define E_ATTACHMENT_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_ATTACHMENT_BAR, EAttachmentBarClass))
#define E_IS_ATTACHMENT_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_ATTACHMENT_BAR))
#define E_IS_ATTACHMENT_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_ATTACHMENT_BAR))
#define E_ATTACHMENT_BAR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_ATTACHMENT_BAR, EAttachmentBarClass))

G_BEGIN_DECLS

typedef struct _EAttachmentBar EAttachmentBar;
typedef struct _EAttachmentBarClass EAttachmentBarClass;
typedef struct _EAttachmentBarPrivate EAttachmentBarPrivate;

struct _EAttachmentBar {
	GtkVBox parent;
	EAttachmentBarPrivate *priv;
};

struct _EAttachmentBarClass {
	GtkVBoxClass parent_class;
};

GType		e_attachment_bar_get_type	(void);
GtkWidget *	e_attachment_bar_new		(EAttachmentStore *store);
gint		e_attachment_bar_get_active_view
						(EAttachmentBar *bar);
void		e_attachment_bar_set_active_view
						(EAttachmentBar *bar,
						 gint active_view);
gboolean	e_attachment_bar_get_expanded
						(EAttachmentBar *bar);
void		e_attachment_bar_set_expanded
						(EAttachmentBar *bar,
						 gboolean expanded);
EAttachmentStore *
		e_attachment_bar_get_store	(EAttachmentBar *bar);

G_END_DECLS

#endif /* E_ATTACHMENT_BAR_H */