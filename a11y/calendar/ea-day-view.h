/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* Evolution Accessibility: ea-day-view.h
 *
 * Copyright (C) 2003 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
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
 * Author: Bolian Yin <bolian.yin@sun.com> Sun Microsystem Inc., 2003
 *
 */

#ifndef __EA_DAY_VIEW_H__
#define __EA_DAY_VIEW_H__

#include <gtk/gtkaccessible.h>
#include "e-day-view.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define EA_TYPE_DAY_VIEW                     (ea_day_view_get_type ())
#define EA_DAY_VIEW(obj)                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), EA_TYPE_DAY_VIEW, EaDayView))
#define EA_DAY_VIEW_CLASS(klass)             (G_TYPE_CHECK_CLASS_CAST ((klass), EA_TYPE_DAY_VIEW, EaDayViewClass))
#define EA_IS_DAY_VIEW(obj)                  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EA_TYPE_DAY_VIEW))
#define EA_IS_DAY_VIEW_CLASS(klass)          (G_TYPE_CHECK_CLASS_TYPE ((klass), EA_TYPE_DAY_VIEW))
#define EA_DAY_VIEW_GET_CLASS(obj)           (G_TYPE_INSTANCE_GET_CLASS ((obj), EA_TYPE_DAY_VIEW, EaDayViewClass))

typedef struct _EaDayView                   EaDayView;
typedef struct _EaDayViewClass              EaDayViewClass;

struct _EaDayView
{
    GtkAccessible parent;
};

GType ea_day_view_get_type (void);

struct _EaDayViewClass
{
    GtkAccessibleClass parent_class;

    /*
     * Signal handler for notify signal on GTK widget
     */
    void (*notify_gtk)                (GObject             *object,
                                       GParamSpec          *pspec);
    /*
     * Signal handler for focus_in_event and focus_out_event signal
     * on GTK widget 
     */
    gboolean (*focus_gtk)              (GtkWidget           *widget,
                                        GdkEventFocus       *event);

};

AtkObject*     ea_day_view_new         (GtkWidget       *widget);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __EA_DAY_VIEW_H__ */
