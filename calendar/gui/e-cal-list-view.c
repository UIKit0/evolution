/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Authors:
 *  Hans Petter Jansson  <hpj@ximian.com>
 *
 * Copyright 2003, Ximian, Inc.
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

/*
 * ECalListView - display calendar events in an ETable.
 */

#include <config.h>

#include "e-cal-list-view.h"
#include "ea-calendar.h"

#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkvscrollbar.h>
#include <gtk/gtkwindow.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-unicode.h>
#include <gal/util/e-util.h>
#include <gal/e-table/e-table-memory-store.h>
#include <gal/e-table/e-cell-checkbox.h>
#include <gal/e-table/e-cell-toggle.h>
#include <gal/e-table/e-cell-text.h>
#include <gal/e-table/e-cell-combo.h>
#include <gal/widgets/e-popup-menu.h>
#include <widgets/misc/e-cell-date-edit.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-exec.h>
#include <libgnome/gnome-util.h>
#include <e-util/e-categories-config.h>
#include <e-util/e-dialog-utils.h>

#include "cal-util/timeutil.h"
#include "e-cal-model-calendar.h"
#include "e-cell-date-edit-text.h"
#include "dialogs/delete-comp.h"
#include "dialogs/delete-error.h"
#include "dialogs/send-comp.h"
#include "dialogs/cancel-comp.h"
#include "dialogs/recur-comp.h"
#include "comp-util.h"
#include "itip-utils.h"
#include "calendar-commands.h"
#include "calendar-config.h"
#include "goto.h"
#include "misc.h"

static void      e_cal_list_view_class_init             (ECalListViewClass *class);
static void      e_cal_list_view_init                   (ECalListView *cal_list_view);
static void      e_cal_list_view_destroy                (GtkObject *object);
static void      e_cal_list_view_update_query           (ECalView *cal_view);

static GList    *e_cal_list_view_get_selected_events    (ECalView *cal_view);
static gboolean  e_cal_list_view_get_visible_time_range (ECalView *cal_view, time_t *start_time,
							 time_t *end_time);

static gboolean  e_cal_list_view_popup_menu             (GtkWidget *widget);

static void      e_cal_list_view_show_popup_menu        (ECalListView *cal_list_view, gint row,
							 GdkEvent *gdk_event);
static gboolean  e_cal_list_view_on_table_right_click   (GtkWidget *table, gint row, gint col,
							 GdkEvent *event, gpointer data);

static GtkTableClass *parent_class;  /* Should be ECalViewClass? */

E_MAKE_TYPE (e_cal_list_view, "ECalListView", ECalListView, e_cal_list_view_class_init,
	     e_cal_list_view_init, e_cal_view_get_type ());

static void
e_cal_list_view_class_init (ECalListViewClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	ECalViewClass *view_class;

	parent_class = g_type_class_peek_parent (class);
	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	view_class = (ECalViewClass *) class;

	/* Method override */
	object_class->destroy		= e_cal_list_view_destroy;

	widget_class->popup_menu = e_cal_list_view_popup_menu;

	view_class->get_selected_events = e_cal_list_view_get_selected_events;
	view_class->get_visible_time_range = e_cal_list_view_get_visible_time_range;

	view_class->update_query        = e_cal_list_view_update_query;
}

static gint
date_compare_cb (gconstpointer a, gconstpointer b)
{
	ECellDateEditValue *dv1 = (ECellDateEditValue *) a;
	ECellDateEditValue *dv2 = (ECellDateEditValue *) b;
	struct icaltimetype tt;

	/* First check if either is NULL. NULL dates sort last. */
	if (!dv1 || !dv2) {
		if (dv1 == dv2)
			return 0;
		else if (dv1)
			return -1;
		else
			return 1;
	}

	/* Copy the 2nd value and convert it to the same timezone as the
	   first. */
	tt = dv2->tt;

	icaltimezone_convert_time (&tt, dv2->zone, dv1->zone);

	/* Now we can compare them. */

	return icaltime_compare (dv1->tt, tt);
}

static void
e_cal_list_view_init (ECalListView *cal_list_view)
{
	cal_list_view->query = NULL;
	cal_list_view->table_scrolled = NULL;
	cal_list_view->cursor_event = NULL;
	cal_list_view->set_table_id = 0;
}

/* Returns the current time, for the ECellDateEdit items.
   FIXME: Should probably use the timezone of the item rather than the
   current timezone, though that may be difficult to get from here. */
static struct tm
get_current_time_cb (ECellDateEdit *ecde, gpointer data)
{
	char *location;
	icaltimezone *zone;
	struct tm tmp_tm = { 0 };
	struct icaltimetype tt;

	/* Get the current timezone. */
	location = calendar_config_get_timezone ();
	zone = icaltimezone_get_builtin_timezone (location);

	tt = icaltime_from_timet_with_zone (time (NULL), FALSE, zone);

	/* Now copy it to the struct tm and return it. */
	tmp_tm.tm_year  = tt.year - 1900;
	tmp_tm.tm_mon   = tt.month - 1;
	tmp_tm.tm_mday  = tt.day;
	tmp_tm.tm_hour  = tt.hour;
	tmp_tm.tm_min   = tt.minute;
	tmp_tm.tm_sec   = tt.second;
	tmp_tm.tm_isdst = -1;

	return tmp_tm;
}

static void
load_table_state (ECalListView *cal_list_view)
{
	struct stat st;

	if (!cal_list_view->table_state_path)
		return;

	if (stat (cal_list_view->table_state_path, &st) == 0 && st.st_size > 0 && S_ISREG (st.st_mode)) {
		e_table_load_state (e_table_scrolled_get_table (cal_list_view->table_scrolled),
				    cal_list_view->table_state_path);
	}
}

static void
save_table_state (ECalListView *cal_list_view)
{
	if (!cal_list_view->table_state_path)
		return;

	e_table_save_state (e_table_scrolled_get_table (cal_list_view->table_scrolled),
			    cal_list_view->table_state_path);
}

static void
setup_e_table (ECalListView *cal_list_view)
{
	ECalModelCalendar *model;
	ETableExtras      *extras;
	GList             *strings;
	ECell             *cell, *popup_cell;
	GnomeCanvas       *canvas;
	GtkStyle          *style;

	model = E_CAL_MODEL_CALENDAR (e_cal_view_get_model (E_CAL_VIEW (cal_list_view)));

	if (cal_list_view->table_scrolled) {
		save_table_state (cal_list_view);
		gtk_widget_destroy (GTK_WIDGET (cal_list_view->table_scrolled));
	}

	/* Create the header columns */

	extras = e_table_extras_new();

	/* Normal string fields */

	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	g_object_set (G_OBJECT (cell),
		      "bg_color_column", E_CAL_MODEL_FIELD_COLOR,
		      NULL);

	e_table_extras_add_cell (extras, "calstring", cell);

	/* Date fields */

	cell = e_cell_date_edit_text_new (NULL, GTK_JUSTIFY_LEFT);
	g_object_set (G_OBJECT (cell),
		      "bg_color_column", E_CAL_MODEL_FIELD_COLOR,
		      NULL);

	popup_cell = e_cell_date_edit_new ();
	e_cell_popup_set_child (E_CELL_POPUP (popup_cell), cell);
	g_object_unref (cell);
	e_table_extras_add_cell (extras, "dateedit", popup_cell);
	cal_list_view->dates_cell = E_CELL_DATE_EDIT (popup_cell);

	e_cell_date_edit_set_get_time_callback (E_CELL_DATE_EDIT (popup_cell),
						get_current_time_cb,
						cal_list_view, NULL);

	/* Combo fields */

	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	g_object_set (G_OBJECT (cell),
		      "bg_color_column", E_CAL_MODEL_FIELD_COLOR,
		      "editable", FALSE,
		      NULL);

	popup_cell = e_cell_combo_new ();
	e_cell_popup_set_child (E_CELL_POPUP (popup_cell), cell);
	g_object_unref (cell);

	strings = NULL;
	strings = g_list_append (strings, (char*) _("Public"));
	strings = g_list_append (strings, (char*) _("Private"));
	strings = g_list_append (strings, (char*) _("Confidential"));
	e_cell_combo_set_popdown_strings (E_CELL_COMBO (popup_cell),
					  strings);

	e_table_extras_add_cell (extras, "classification", popup_cell);

	/* Sorting */

	e_table_extras_add_compare (extras, "date-compare",
				    date_compare_cb);

	/* Create table view */

	cal_list_view->table_scrolled = E_TABLE_SCROLLED (
		e_table_scrolled_new_from_spec_file (E_TABLE_MODEL (model),
						     extras,
						     EVOLUTION_ETSPECDIR "/e-cal-list-view.etspec",
						     NULL));

	/* Make sure text is readable on top of our color coding */

	canvas = GNOME_CANVAS (e_table_scrolled_get_table (cal_list_view->table_scrolled)->table_canvas);
	style = gtk_widget_get_style (GTK_WIDGET (canvas));

	style->fg [GTK_STATE_SELECTED] = style->text [GTK_STATE_NORMAL];
	style->fg [GTK_STATE_ACTIVE]   = style->text [GTK_STATE_NORMAL];
	gtk_widget_set_style (GTK_WIDGET (canvas), style);

	/* Load state, if possible */

	load_table_state (cal_list_view);

	/* Connect signals */

	g_signal_connect (e_table_scrolled_get_table (cal_list_view->table_scrolled),
			  "right-click", G_CALLBACK (e_cal_list_view_on_table_right_click), cal_list_view);

	/* Attach and show widget */

	gtk_table_attach (GTK_TABLE (cal_list_view), GTK_WIDGET (cal_list_view->table_scrolled),
			  0, 2, 0, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 1, 1);
	gtk_widget_show (GTK_WIDGET (cal_list_view->table_scrolled));
}

GtkWidget *
e_cal_list_view_construct (ECalListView *cal_list_view, const gchar *table_state_path)
{
	if (table_state_path)
		cal_list_view->table_state_path = g_strdup (table_state_path);
	else
		cal_list_view->table_state_path = NULL;

	setup_e_table (cal_list_view);

	return GTK_WIDGET (cal_list_view);
}

/**
 * e_cal_list_view_new:
 * @Returns: a new #ECalListView.
 *
 * Creates a new #ECalListView.
 **/
GtkWidget *
e_cal_list_view_new (const gchar *table_state_path)
{
	ECalListView *cal_list_view;

	cal_list_view = g_object_new (e_cal_list_view_get_type (), NULL);
	if (!e_cal_list_view_construct (cal_list_view, table_state_path)) {
		g_message ("e_cal_list_view(): Could not construct the calendar list GUI");
		g_object_unref (cal_list_view);
		return NULL;
	}

	return GTK_WIDGET (cal_list_view);
}

static void
e_cal_list_view_destroy (GtkObject *object)
{
	ECalListView *cal_list_view;

	cal_list_view = E_CAL_LIST_VIEW (object);

	if (cal_list_view->query) {
		g_signal_handlers_disconnect_matched (cal_list_view->query, G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL, cal_list_view);
		g_object_unref (cal_list_view->query);
		cal_list_view->query = NULL;
	}

	if (cal_list_view->set_table_id) {
		g_source_remove (cal_list_view->set_table_id);
		cal_list_view->set_table_id = 0;
	}

	if (cal_list_view->table_state_path) {
		save_table_state (cal_list_view);
		g_free (cal_list_view->table_state_path);
		cal_list_view->table_state_path = NULL;
	}

	if (cal_list_view->cursor_event) {
		g_free (cal_list_view->cursor_event);
		cal_list_view->cursor_event = NULL;
	}

	if (cal_list_view->table_scrolled) {
		gtk_widget_destroy (GTK_WIDGET (cal_list_view->table_scrolled));
		cal_list_view->table_scrolled = NULL;
	}

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

static gboolean
setup_e_table_cb (gpointer data)
{
	setup_e_table (E_CAL_LIST_VIEW (data));
	E_CAL_LIST_VIEW (data)->set_table_id = 0;
	return FALSE;
}

static void
e_cal_list_view_update_query (ECalView *cal_list_view)
{
	e_cal_view_set_status_message (E_CAL_VIEW (cal_list_view), _("Searching"));

	if (!E_CAL_LIST_VIEW (cal_list_view)->set_table_id)
		E_CAL_LIST_VIEW (cal_list_view)->set_table_id =
			g_idle_add (setup_e_table_cb, cal_list_view);

	e_cal_view_set_status_message (E_CAL_VIEW (cal_list_view), NULL);
}

static void
e_cal_list_view_show_popup_menu (ECalListView *cal_list_view, gint row, GdkEvent *gdk_event)
{
	GtkMenu *popup;

	popup = e_cal_view_create_popup_menu (E_CAL_VIEW (cal_list_view));
	e_popup_menu (popup, gdk_event);
}

static gboolean
e_cal_list_view_popup_menu (GtkWidget *widget)
{
	ECalListView *cal_list_view = E_CAL_LIST_VIEW (widget);

	e_cal_list_view_show_popup_menu (cal_list_view, -1, NULL);
	return TRUE;
}

static gboolean
e_cal_list_view_on_table_right_click (GtkWidget *table, gint row, gint col, GdkEvent *event,
				      gpointer data)
{
	ECalListView *cal_list_view = E_CAL_LIST_VIEW (data);

	e_cal_list_view_show_popup_menu (cal_list_view, row, event);
	return TRUE;
}

static GList *
e_cal_list_view_get_selected_events (ECalView *cal_view)
{
	GList *event_list = NULL;
	gint   cursor_row;

	if (E_CAL_LIST_VIEW (cal_view)->cursor_event) {
		g_free (E_CAL_LIST_VIEW (cal_view)->cursor_event);
		E_CAL_LIST_VIEW (cal_view)->cursor_event = NULL;
	}

	cursor_row = e_table_get_cursor_row (e_table_scrolled_get_table (E_CAL_LIST_VIEW (cal_view)->table_scrolled));

	if (cursor_row >= 0) {
		ECalViewEvent *event;

		event = E_CAL_LIST_VIEW (cal_view)->cursor_event = g_new0 (ECalViewEvent, 1);
		event->comp_data =
			e_cal_model_get_component_at (e_cal_view_get_model (cal_view),
						      cursor_row);
		event_list = g_list_prepend (event_list, event);
	}

	return event_list;
}

static void
adjust_range (icaltimetype icaltime, time_t *earliest, time_t *latest, gboolean *set)
{
	time_t t;

	if (!icaltime_is_valid_time (icaltime))
		return;

	t = icaltime_as_timet (icaltime);
	*earliest = MIN (*earliest, t);
	*latest   = MAX (*latest, t);

	*set = TRUE;
}

/* NOTE: Time use for this function increases linearly with number of events. This is not
 * ideal, since it's used in a couple of places. We could probably be smarter about it,
 * and use do it less frequently... */
static gboolean
e_cal_list_view_get_visible_time_range (ECalView *cal_view, time_t *start_time, time_t *end_time)
{
	time_t   earliest = G_MAXINT, latest = 0;
	gboolean set = FALSE;
	gint     n_rows, i;

	n_rows = e_table_model_row_count (E_TABLE_MODEL (e_cal_view_get_model (cal_view)));

	for (i = 0; i < n_rows; i++) {
		ECalModelComponent *comp;
		icalcomponent      *icalcomp;

		comp = e_cal_model_get_component_at (e_cal_view_get_model (cal_view), i);
		if (!comp)
			continue;

		icalcomp = comp->icalcomp;
		if (!icalcomp)
			continue;

		adjust_range (icalcomponent_get_dtstart (icalcomp), &earliest, &latest, &set);
		adjust_range (icalcomponent_get_dtend (icalcomp), &earliest, &latest, &set);
	}

	if (set) {
		*start_time = earliest;
		*end_time   = latest;
		return TRUE;
	}

	return FALSE;
}

gboolean
e_cal_list_view_get_range_shown (ECalListView *cal_list_view, GDate *start_date, gint *days_shown)
{
	time_t  first, last;
	GDate   end_date;

	if (!e_cal_list_view_get_visible_time_range (E_CAL_VIEW (cal_list_view), &first, &last))
		return FALSE;

	time_to_gdate_with_zone (start_date, first, e_cal_view_get_timezone (E_CAL_VIEW (cal_list_view)));
	time_to_gdate_with_zone (&end_date, last, e_cal_view_get_timezone (E_CAL_VIEW (cal_list_view)));

	*days_shown = g_date_days_between (start_date, &end_date);
	return TRUE;
}
