/*
 * Evolution calendar - Recurring calendar component dialog
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Authors:
 *		JP Rosevear <jpr@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef RECUR_COMP_H
#define RECUR_COMP_H

#include <gtk/gtk.h>
#include <libecal/libecal.h>

gboolean recur_component_dialog (ECalClient *client,
				 ECalComponent *comp,
				 CalObjModType *mod,
				 GtkWindow *parent,
				 gboolean delegated);

#endif
