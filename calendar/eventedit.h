/*
 * EventEditor widget
 * Copyright (C) 1998 the Free Software Foundation
 *
 * Author: Miguel de Icaza (miguel@kernel.org)
 */

#ifndef EVENT_EDITOR_H
#define EVENT_EDITOR_H

#include "gnome-cal.h"

BEGIN_GNOME_DECLS


#define EVENT_EDITOR(obj)         GTK_CHECK_CAST(obj, event_editor_get_type(), EventEditor)
#define EVENT_EDITOR_CLASS(class) GTK_CHECK_CAST_CLASS(class, event_editor_get_type(), EventEditorClass)
#define IS_EVENT_EDITOR(obj)      GTK_CHECK_TYPE(obj, event_editor_get_type())


typedef struct {
	GtkWindow  window;
	GtkWidget  *notebook;
	GtkWidget  *vbox;

        GtkWidget  *general;
        GtkWidget  *general_table;
        GtkWidget  *general_time_table;
	GtkWidget  *general_allday;
	GtkWidget  *general_owner;
	GtkWidget  *general_summary;
	GtkWidget  *start_time, *end_time;
	GtkWidget  *general_radios;

	GtkWidget  *recur_page_label;
	GtkWidget  *recur_table;

	GSList     *recur_rr_group;
	GtkWidget  *recur_rr_notebook;
	GtkWidget  *recur_rr_day_period;
	GtkWidget  *recur_rr_week_period;
	GtkWidget  *recur_rr_week_days [7];
	GtkWidget  *recur_rr_month_date;
	GtkWidget  *recur_rr_month_date_label;
	GtkWidget  *recur_rr_month_day;
	GtkWidget  *recur_rr_month_weekday;
	GtkWidget  *recur_rr_month_period;
	GtkWidget  *recur_rr_year_period;

	GSList     *recur_ed_group;
	GtkWidget  *recur_ed_end_on;
	GtkWidget  *recur_ed_end_after;

	GtkWidget  *recur_ex_date;
	GtkWidget  *recur_ex_vbox;
	GtkWidget  *recur_ex_clist;
	
	/* The associated ical object */
	iCalObject *ical;

	/* The calendar owner of this event */
        GnomeCalendar *gnome_cal;
} EventEditor;

typedef struct {
	GtkWindowClass parent_class;
} EventEditorClass;


guint      event_editor_get_type (void);
GtkWidget *event_editor_new      (GnomeCalendar *owner, iCalObject *);


END_GNOME_DECLS

#endif
