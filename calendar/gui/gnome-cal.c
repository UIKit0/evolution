/*
 * Evolution calendar - Main calendar view widget
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
 * Authors:
 *		Miguel de Icaza <miguel@ximian.com>
 *      Federico Mena-Quintero <federico@ximian.com>
 *      Seth Alves <alves@hungry.com>
 *      Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <libedataserver/e-categories.h>
#include <libedataserver/e-url.h>
#include <libedataserverui/e-passwords.h>

#include <libecal/e-cal-time-util.h>
#include <widgets/menus/gal-view-factory-etable.h>
#include <widgets/menus/gal-view-etable.h>
#include <widgets/menus/gal-define-views-dialog.h>
#include "e-util/e-binding.h"
#include "e-util/e-util.h"
#include "e-util/e-error.h"
#include "e-util/e-util-private.h"
#include "shell/e-shell.h"
#include "e-comp-editor-registry.h"
#include "dialogs/delete-error.h"
#include "dialogs/event-editor.h"
#include "comp-util.h"
#include "e-cal-model-calendar.h"
#include "e-day-view.h"
#include "e-day-view-time-item.h"
#include "e-month-view.h"
#include "e-week-view.h"
#include "e-cal-list-view.h"
#include "gnome-cal.h"
#include "calendar-config.h"
#include "calendar-view.h"
#include "calendar-view-factory.h"
#include "tag-calendar.h"
#include "misc.h"
#include "ea-calendar.h"
#include "common/authentication.h"

#define d(x)

/* hash table define for non intrusive error dialog */
static GHashTable *non_intrusive_error_table = NULL;

/* Private part of the GnomeCalendar structure */
struct _GnomeCalendarPrivate {
	EShellSettings *shell_settings;

	/* The clients for display */
	GHashTable *clients;
	GList *clients_list;
	ECal *default_client;

	/*
	 * Fields for the calendar view
	 */

	/* This is the last time explicitly selected by the user */
	time_t base_view_time;

	/* Widgets */

	GtkWidget   *hpane;

	ECalendar   *date_navigator;

	/* Calendar query for the date navigator */
	GList       *dn_queries; /* list of CalQueries */
	gchar        *sexp;
	gchar        *todo_sexp;
	gchar        *memo_sexp;
	guint        update_timeout;
	guint        update_marcus_bains_line_timeout;

	/* This is the view currently shown. We use it to keep track of the
	   positions of the panes. range_selected is TRUE if a range of dates
	   was selected in the date navigator to show the view. */
	ECalendarView    *views[GNOME_CAL_LAST_VIEW];
	GnomeCalendarViewType current_view_type;
	GList *notifications;

	gboolean range_selected;

	/* These are the saved positions of the panes. They are multiples of
	   calendar month widths & heights in the date navigator, so that they
	   will work OK after theme changes. */
	gint	     hpane_pos;
	gint	     hpane_pos_month_view;

	/* The signal handler id for our GtkCalendar "day_selected" handler. */
	guint	     day_selected_id;

	/* The dates currently shown. If they are -1 then we have no dates
	   shown. We only use these to check if we need to emit a
	   'dates-shown-changed' signal.*/
	time_t visible_start;
	time_t visible_end;
	gboolean updating;

	/* If this is true list view uses range of showing the events as the  dates selected in date navigator which is one month, else
	   it uses the date range set in search bar */
	gboolean lview_select_daten_range;

	/* We should know which calendar has been used to create object, so store it here
	   before emitting "user_created" signal and make it NULL just after the emit. */
	ECal *user_created_cal;

	/* used in update_todo_view, to prevent interleaving when called in separate thread */
	GMutex *todo_update_lock;
};

enum {
	PROP_0,
	PROP_DATE_NAVIGATOR,
	PROP_SHELL_SETTINGS,
	PROP_VIEW
};

enum {
	DATES_SHOWN_CHANGED,
	CALENDAR_SELECTION_CHANGED,
	GOTO_DATE,
	SOURCE_ADDED,
	SOURCE_REMOVED,
	CHANGE_VIEW,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void gnome_calendar_destroy (GtkObject *object);
static void gnome_calendar_goto_date (GnomeCalendar *gcal,
				      GnomeCalendarGotoDateType goto_date);
static void gnome_calendar_change_view (GnomeCalendar *gcal,
					 GnomeCalendarViewType view_type);

static void update_view_times (GnomeCalendar *gcal, time_t start_time);
static void gnome_calendar_update_date_navigator (GnomeCalendar *gcal);

static gboolean gnome_calendar_hpane_resized (GtkWidget *w, GdkEventButton *e, GnomeCalendar *gcal);

static void gnome_calendar_on_date_navigator_selection_changed (ECalendarItem    *calitem,
								GnomeCalendar    *gcal);
static void gnome_calendar_notify_dates_shown_changed (GnomeCalendar *gcal);

static void update_todo_view (GnomeCalendar *gcal);
static void update_memo_view (GnomeCalendar *gcal);
static void default_client_cal_opened_cb (ECal *ecal, ECalendarStatus status, GnomeCalendar *gcal);
static void client_cal_opened_cb (ECal *ecal, ECalendarStatus status, GnomeCalendar *gcal);

/* Simple asynchronous message dispatcher */
typedef struct _Message Message;
typedef void (*MessageFunc) (Message *msg);

struct _Message {
	MessageFunc func;
};

static void
message_proxy (Message *msg)
{
	g_return_if_fail (msg->func != NULL);

	msg->func (msg);
}

static gpointer
create_thread_pool (void)
{
	/* once created, run forever */
	return g_thread_pool_new ((GFunc) message_proxy, NULL, 1, FALSE, NULL);
}

static void
message_push (Message *msg)
{
	static GOnce once = G_ONCE_INIT;

	g_once (&once, (GThreadFunc) create_thread_pool, NULL);

	g_thread_pool_push ((GThreadPool *) once.retval, msg, NULL);
}

G_DEFINE_TYPE (GnomeCalendar, gnome_calendar, GTK_TYPE_VBOX)

static void
update_adjustment (GnomeCalendar *gcal,
                   GtkAdjustment *adjustment,
                   EWeekView *week_view)
{
	GDate date;
	ECalModel *model;
	gint week_offset;
	struct icaltimetype start_tt = icaltime_null_time ();
	time_t lower;
	guint32 old_first_day_julian, new_first_day_julian;
	icaltimezone *timezone;

	/* If we don't have a valid date set yet, just return. */
	if (!g_date_valid (&week_view->first_day_shown))
		return;

	/* Determine the first date shown. */
	date = week_view->base_date;
	week_offset = floor (adjustment->value + 0.5);
	g_date_add_days (&date, week_offset * 7);

	/* Convert the old & new first days shown to julian values. */
	old_first_day_julian = g_date_get_julian (&week_view->first_day_shown);
	new_first_day_julian = g_date_get_julian (&date);

	/* If we are already showing the date, just return. */
	if (old_first_day_julian == new_first_day_julian)
		return;

	/* Convert it to a time_t. */
	start_tt.year = g_date_get_year (&date);
	start_tt.month = g_date_get_month (&date);
	start_tt.day = g_date_get_day (&date);

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);
	lower = icaltime_as_timet_with_zone (start_tt, timezone);

	e_week_view_set_update_base_date (week_view, FALSE);
	gnome_calendar_set_selected_time_range (gcal, lower);
	e_week_view_set_update_base_date (week_view, TRUE);
}

static void
week_view_adjustment_changed_cb (GtkAdjustment *adjustment,
                                 GnomeCalendar *gcal)
{
	ECalendarView *view;

	view = gnome_calendar_get_calendar_view (gcal, GNOME_CAL_WEEK_VIEW);
	update_adjustment (gcal, adjustment, E_WEEK_VIEW (view));
}

static void
month_view_adjustment_changed_cb (GtkAdjustment *adjustment,
                                  GnomeCalendar *gcal)
{
	ECalendarView *view;

	view = gnome_calendar_get_calendar_view (gcal, GNOME_CAL_MONTH_VIEW);
	update_adjustment (gcal, adjustment, E_WEEK_VIEW (view));
}

static void
view_selection_changed_cb (GnomeCalendar *gcal)
{
	g_signal_emit (gcal, signals[CALENDAR_SELECTION_CHANGED], 0);
}

static void
view_progress_cb (ECalModel *model,
                  const gchar *message,
                  gint percent,
                  ECalSourceType type,
                  GnomeCalendar *gcal)
{
#if 0  /* KILL-BONOBO */
	ECalendarView *view;

	view = gnome_calendar_get_calendar_view (gcal, GNOME_CAL_WEEK_VIEW);
	e_calendar_view_set_status_message (view, message, percent);
#endif
}

static void
view_done_cb (ECalModel *model,
              ECalendarStatus status,
              ECalSourceType type,
              GnomeCalendar *gcal)
{
#if 0  /* KILL-BONOBO */
	ECalendarView *view;

	view = gnome_calendar_get_calendar_view (gcal, GNOME_CAL_WEEK_VIEW);
	e_calendar_view_set_status_message (view, NULL, -1);
#endif
}

static void
gnome_calendar_notify_week_start_day_cb (GnomeCalendar *gcal)
{
	time_t start_time;

	start_time = gcal->priv->base_view_time;
	gnome_calendar_set_selected_time_range (gcal, start_time);
}

static void
gnome_calendar_update_time_range (GnomeCalendar *gcal)
{
	time_t start_time;

	start_time = gcal->priv->base_view_time;
	gnome_calendar_set_selected_time_range (gcal, start_time);
}

static void
gnome_calendar_set_shell_settings (GnomeCalendar *gcal,
                                   EShellSettings *shell_settings)
{
	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));
	g_return_if_fail (gcal->priv->shell_settings == NULL);

	gcal->priv->shell_settings = g_object_ref (shell_settings);
}

static void
gnome_calendar_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DATE_NAVIGATOR:
			gnome_calendar_set_date_navigator (
				GNOME_CALENDAR (object),
				g_value_get_object (value));
			return;

		case PROP_SHELL_SETTINGS:
			gnome_calendar_set_shell_settings (
				GNOME_CALENDAR (object),
				g_value_get_object (value));
			return;

		case PROP_VIEW:
			gnome_calendar_set_view (
				GNOME_CALENDAR (object),
				g_value_get_int (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gnome_calendar_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DATE_NAVIGATOR:
			g_value_set_object (
				value, gnome_calendar_get_date_navigator (
				GNOME_CALENDAR (object)));
			return;

		case PROP_SHELL_SETTINGS:
			g_value_set_object (
				value, gnome_calendar_get_shell_settings (
				GNOME_CALENDAR (object)));
			return;

		case PROP_VIEW:
			g_value_set_int (
				value, gnome_calendar_get_view (
				GNOME_CALENDAR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gnome_calendar_constructed (GObject *object)
{
	GnomeCalendar *gcal = GNOME_CALENDAR (object);
	EShellSettings *shell_settings;
	ECalendarView *calendar_view;
	ECalModel *model;
	GtkAdjustment *adjustment;

	shell_settings = gnome_calendar_get_shell_settings (gcal);

	/* Create the model for the views. */
	model = e_cal_model_calendar_new (shell_settings);
	e_cal_model_set_flags (model, E_CAL_MODEL_FLAGS_EXPAND_RECURRENCES);

	g_signal_connect (
		model, "cal-view-progress",
		G_CALLBACK (view_progress_cb), gcal);

	g_signal_connect (
		model, "cal-view-done",
		G_CALLBACK (view_done_cb), gcal);

	/* Day View */
	calendar_view = e_day_view_new (model);
	e_calendar_view_set_calendar (calendar_view, gcal);
	gcal->priv->views[GNOME_CAL_DAY_VIEW] = calendar_view;

	g_signal_connect_swapped (
		calendar_view, "selection-changed",
		G_CALLBACK (view_selection_changed_cb), gcal);

	/* Work Week View */
	calendar_view = e_day_view_new (model);
	e_day_view_set_work_week_view (E_DAY_VIEW (calendar_view), TRUE);
	e_day_view_set_days_shown (E_DAY_VIEW (calendar_view), 5);
	e_calendar_view_set_calendar (calendar_view, gcal);
	gcal->priv->views[GNOME_CAL_WORK_WEEK_VIEW] = calendar_view;

	g_signal_connect_swapped (
		calendar_view, "notify::working-days",
		G_CALLBACK (gnome_calendar_update_time_range), gcal);

	/* Week View */
	calendar_view = e_week_view_new (model);
	e_calendar_view_set_calendar (calendar_view, gcal);
	gcal->priv->views[GNOME_CAL_WEEK_VIEW] = calendar_view;

	g_signal_connect_swapped (
		calendar_view, "selection-changed",
		G_CALLBACK (view_selection_changed_cb), gcal);

	adjustment = gtk_range_get_adjustment (
		GTK_RANGE (E_WEEK_VIEW (calendar_view)->vscrollbar));
	g_signal_connect (
		adjustment, "value-changed",
		G_CALLBACK (week_view_adjustment_changed_cb), gcal);

	/* Month View */
	calendar_view = e_month_view_new (model);
	e_week_view_set_multi_week_view (E_WEEK_VIEW (calendar_view), TRUE);
	e_week_view_set_weeks_shown (E_WEEK_VIEW (calendar_view), 6);
	e_calendar_view_set_calendar (calendar_view, gcal);
	gcal->priv->views[GNOME_CAL_MONTH_VIEW] = calendar_view;

	g_signal_connect_swapped (
		calendar_view, "selection-changed",
		G_CALLBACK (view_selection_changed_cb), gcal);

	adjustment = gtk_range_get_adjustment (
		GTK_RANGE (E_WEEK_VIEW (calendar_view)->vscrollbar));
	g_signal_connect (
		adjustment, "value-changed",
		G_CALLBACK (month_view_adjustment_changed_cb), gcal);

	/* List View */
	calendar_view = e_cal_list_view_new (model);
	e_calendar_view_set_calendar (calendar_view, gcal);
	gcal->priv->views[GNOME_CAL_LIST_VIEW] = calendar_view;

	g_signal_connect_swapped (
		calendar_view, "selection-changed",
		G_CALLBACK (view_selection_changed_cb), gcal);

	g_signal_connect_swapped (
		model, "notify::week-start-day",
		G_CALLBACK (gnome_calendar_notify_week_start_day_cb), gcal);

	gnome_calendar_update_time_range (gcal);
}

/* Class initialization function for the gnome calendar */
static void
gnome_calendar_class_init (GnomeCalendarClass *class)
{
	GObjectClass *object_class;
	GtkObjectClass *gtk_object_class;
	GtkBindingSet *binding_set;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = gnome_calendar_set_property;
	object_class->get_property = gnome_calendar_get_property;
	object_class->constructed = gnome_calendar_constructed;

	gtk_object_class = GTK_OBJECT_CLASS (class);
	gtk_object_class->destroy = gnome_calendar_destroy;

	class->dates_shown_changed = NULL;
	class->calendar_selection_changed = NULL;
	class->source_added = NULL;
	class->source_removed = NULL;
	class->goto_date = gnome_calendar_goto_date;
	class->change_view = gnome_calendar_change_view;

	g_object_class_install_property (
		object_class,
		PROP_DATE_NAVIGATOR,
		g_param_spec_object (
			"date-navigator",
			"Date Navigator",
			NULL,
			E_TYPE_CALENDAR,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SHELL_SETTINGS,
		g_param_spec_object (
			"shell-settings",
			_("Shell Settings"),
			_("Application-wide settings"),
			E_TYPE_SHELL_SETTINGS,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_VIEW,
		g_param_spec_int (
			"view",
			"View",
			NULL,
			GNOME_CAL_DAY_VIEW,
			GNOME_CAL_LIST_VIEW,
			0,
			G_PARAM_READWRITE));

	signals[DATES_SHOWN_CHANGED] =
		g_signal_new ("dates_shown_changed",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomeCalendarClass, dates_shown_changed),
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE, 0);

	signals[CALENDAR_SELECTION_CHANGED] =
		g_signal_new ("calendar_selection_changed",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomeCalendarClass, calendar_selection_changed),
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID,
				G_TYPE_NONE, 0);

	signals[SOURCE_ADDED] =
		g_signal_new ("source_added",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GnomeCalendarClass, source_added),
			      NULL, NULL,
			      e_marshal_VOID__INT_OBJECT,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_INT, G_TYPE_OBJECT);

	signals[SOURCE_REMOVED] =
		g_signal_new ("source_removed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GnomeCalendarClass, source_removed),
			      NULL, NULL,
			      e_marshal_VOID__INT_OBJECT,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_INT, G_TYPE_OBJECT);

	signals[GOTO_DATE] =
		g_signal_new ("goto_date",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GnomeCalendarClass, goto_date),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);

	signals[CHANGE_VIEW] =
		g_signal_new ("change_view",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GnomeCalendarClass, change_view),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);

	/*
	 * Key bindings
	 */

	binding_set = gtk_binding_set_by_class (class);

	/* Alt+PageUp/PageDown, go to the first/last day of the month */
	gtk_binding_entry_add_signal (binding_set, GDK_Page_Up,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_FIRST_DAY_OF_MONTH);
	gtk_binding_entry_add_signal (binding_set, GDK_KP_Page_Up,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_FIRST_DAY_OF_MONTH);
	gtk_binding_entry_add_signal (binding_set, GDK_Page_Down,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_LAST_DAY_OF_MONTH);
	gtk_binding_entry_add_signal (binding_set, GDK_KP_Page_Down,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_LAST_DAY_OF_MONTH);

	/* Alt+Home/End, go to the first/last day of the week */
	gtk_binding_entry_add_signal (binding_set, GDK_Home,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_FIRST_DAY_OF_WEEK);
	gtk_binding_entry_add_signal (binding_set, GDK_End,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_LAST_DAY_OF_WEEK);
	gtk_binding_entry_add_signal (binding_set, GDK_KP_Home,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_FIRST_DAY_OF_WEEK);
	gtk_binding_entry_add_signal (binding_set, GDK_KP_End,
				      GDK_MOD1_MASK,
				      "goto_date", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_LAST_DAY_OF_WEEK);

	/*Alt+Left/Right, go to the same day of the previous/next week*/
	gtk_binding_entry_add_signal (binding_set,GDK_Left,
				      GDK_MOD1_MASK,
				      "goto_date",1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_SAME_DAY_OF_PREVIOUS_WEEK);
	gtk_binding_entry_add_signal (binding_set,GDK_KP_Left,
				      GDK_MOD1_MASK,
				      "goto_date",1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_SAME_DAY_OF_PREVIOUS_WEEK);
	gtk_binding_entry_add_signal (binding_set,GDK_Right,
				      GDK_MOD1_MASK,
				      "goto_date",1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_SAME_DAY_OF_NEXT_WEEK);
	gtk_binding_entry_add_signal (binding_set,GDK_KP_Right,
				      GDK_MOD1_MASK,
				      "goto_date",1,
				      G_TYPE_ENUM,
				      GNOME_CAL_GOTO_SAME_DAY_OF_NEXT_WEEK);

	/* Ctrl+Y/J/K/M/L to switch between
	 * DayView/WorkWeekView/WeekView/MonthView/ListView */
	gtk_binding_entry_add_signal (binding_set, GDK_y,
				      GDK_CONTROL_MASK,
				      "change_view", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_DAY_VIEW);
	gtk_binding_entry_add_signal (binding_set, GDK_j,
				      GDK_CONTROL_MASK,
				      "change_view", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_WORK_WEEK_VIEW);
	gtk_binding_entry_add_signal (binding_set, GDK_k,
				      GDK_CONTROL_MASK,
				      "change_view", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_WEEK_VIEW);
	gtk_binding_entry_add_signal (binding_set, GDK_m,
				      GDK_CONTROL_MASK,
				      "change_view", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_MONTH_VIEW);
	gtk_binding_entry_add_signal (binding_set, GDK_l,
				      GDK_CONTROL_MASK,
				      "change_view", 1,
				      G_TYPE_ENUM,
				      GNOME_CAL_LIST_VIEW);

	/* init the accessibility support for gnome_calendar */
	gnome_calendar_a11y_init ();

}

/* We do this check since the calendar items are downloaded from the server
 * in the open_method, since the default timezone might not be set there. */
static void
ensure_dates_are_in_default_zone (GnomeCalendar *gcal,
                                  icalcomponent *icalcomp)
{
	ECalModel *model;
	icaltimezone *timezone;
	icaltimetype dt;

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);

	if (timezone == NULL)
		return;

	dt = icalcomponent_get_dtstart (icalcomp);
	if (dt.is_utc) {
		dt = icaltime_convert_to_zone (dt, timezone);
		icalcomponent_set_dtstart (icalcomp, dt);
	}

	dt = icalcomponent_get_dtend (icalcomp);
	if (dt.is_utc) {
		dt = icaltime_convert_to_zone (dt, timezone);
		icalcomponent_set_dtend (icalcomp, dt);
	}
}

/* Callback used when the calendar query reports of an updated object */
static void
dn_e_cal_view_objects_added_cb (ECalView *query, GList *objects, gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	GList *l;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	for (l = objects; l; l = l->next) {
		ECalComponent *comp = NULL;

		ensure_dates_are_in_default_zone (gcal, l->data);
		comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (l->data))) {
			g_object_unref (comp);

			continue;
		}

		tag_calendar_by_comp (priv->date_navigator, comp, e_cal_view_get_client (query), NULL,
				      FALSE, TRUE);
		g_object_unref (comp);
	}
}

static void
dn_e_cal_view_objects_modified_cb (ECalView *query, GList *objects, gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);

	/* We have to retag the whole thing: an event may change dates
	 * and the tag_calendar_by_comp() below would not know how to
	 * untag the old dates.
	 */
	gnome_calendar_update_query (gcal);
}

/* Callback used when the calendar query reports of a removed object */
static void
dn_e_cal_view_objects_removed_cb (ECalView *query, GList *ids, gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);

	/* Just retag the whole thing */
	gnome_calendar_update_query (gcal);
}

/* Callback used when the calendar query is done */
static void
dn_e_cal_view_done_cb (ECalView *query, ECalendarStatus status, gpointer data)
{
	/* FIXME Better error reporting */
	if (status != E_CALENDAR_STATUS_OK)
		g_warning (G_STRLOC ": Query did not successfully complete");
}

ECalendarView *
gnome_calendar_get_calendar_view (GnomeCalendar *gcal,
                                  GnomeCalendarViewType view_type)
{
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);
	g_return_val_if_fail (view_type < GNOME_CAL_LAST_VIEW, NULL);

	return gcal->priv->views[view_type];
}

static void
get_times_for_views (GnomeCalendar *gcal, GnomeCalendarViewType view_type, time_t *start_time, time_t *end_time, time_t *select_time)
{
	GnomeCalendarPrivate *priv;
	ECalModel *model;
	gint shown, display_start;
	GDate date;
	gint week_start_day;
	gint weekday, first_day, last_day, days_shown, i;
	gboolean has_working_days = FALSE;
	guint offset;
	struct icaltimetype tt = icaltime_null_time ();
	icaltimezone *timezone;

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);
	week_start_day = e_cal_model_get_week_start_day (model);

	priv = gcal->priv;

	switch (view_type) {
	case GNOME_CAL_DAY_VIEW:
		shown  = e_day_view_get_days_shown (E_DAY_VIEW (priv->views[view_type]));
		*start_time = time_day_begin_with_zone (*start_time, timezone);
		*end_time = time_add_day_with_zone (*start_time, shown, timezone);
		break;
	case GNOME_CAL_WORK_WEEK_VIEW:
		/* FIXME Kind of gross, but it works */
		time_to_gdate_with_zone (&date, *start_time, timezone);

		/* The start of the work-week is the first working day after the
		   week start day. */

		/* Get the weekday corresponding to start_time, 0 (Sun) to 6 (Sat). */
		weekday = g_date_get_weekday (&date) % 7;

		/* Find the first working day in the week, 0 (Sun) to 6 (Sat). */
		first_day = (week_start_day + 1) % 7;
		for (i = 0; i < 7; i++) {
			if (E_DAY_VIEW (priv->views[view_type])->working_days & (1 << first_day)) {
				has_working_days = TRUE;
				break;
			}
			first_day = (first_day + 1) % 7;
		}

		if (has_working_days) {
			/* Now find the last working day of the week, backwards. */
			last_day = week_start_day % 7;
			for (i = 0; i < 7; i++) {
				if (E_DAY_VIEW (priv->views[view_type])->working_days & (1 << last_day))
					break;
				last_day = (last_day + 6) % 7;
			}
			/* Now calculate the days we need to show to include all the
			   working days in the week. Add 1 to make it inclusive. */
			days_shown = (last_day + 7 - first_day) % 7 + 1;
		} else {
			/* If no working days are set, just use 7. */
			days_shown = 7;
		}

		/* Calculate how many days we need to go back to the first workday. */
		if (weekday < first_day) {
			offset = (first_day - weekday) % 7;
			g_date_add_days (&date, offset);
		} else {
			offset = (weekday - first_day) % 7;
			g_date_subtract_days (&date, offset);
		}

		tt.year = g_date_get_year (&date);
		tt.month = g_date_get_month (&date);
		tt.day = g_date_get_day (&date);

		*start_time = icaltime_as_timet_with_zone (tt, timezone);
		*end_time = time_add_day_with_zone (*start_time, days_shown, timezone);

		if (select_time && E_DAY_VIEW (priv->views[view_type])->selection_start_day == -1)
			time (select_time);
		break;
	case GNOME_CAL_WEEK_VIEW:
		/* FIXME We should be using the same day of the week enum every where */
		display_start = (E_WEEK_VIEW (priv->views[view_type])->display_start_day + 1) % 7;

		*start_time = time_week_begin_with_zone (*start_time, display_start, timezone);
		*end_time = time_add_week_with_zone (*start_time, 1, timezone);

		if (select_time && E_WEEK_VIEW (priv->views[view_type])->selection_start_day == -1)
			time (select_time);
		break;
	case GNOME_CAL_MONTH_VIEW:
		shown = e_week_view_get_weeks_shown (E_WEEK_VIEW (priv->views[view_type]));
		/* FIXME We should be using the same day of the week enum every where */
		display_start = (E_WEEK_VIEW (priv->views[view_type])->display_start_day + 1) % 7;

		if (!priv->range_selected && (!E_WEEK_VIEW (priv->views[view_type])->multi_week_view || !E_WEEK_VIEW (priv->views[view_type])->month_scroll_by_week))
			*start_time = time_month_begin_with_zone (*start_time, timezone);
		*start_time = time_week_begin_with_zone (*start_time, display_start, timezone);
		*end_time = time_add_week_with_zone (*start_time, shown, timezone);

		if (select_time && E_WEEK_VIEW (priv->views[view_type])->selection_start_day == -1)
			time (select_time);
		break;
	case GNOME_CAL_LIST_VIEW:
		/* FIXME What to do here? */
		*start_time = time_month_begin_with_zone (*start_time, timezone);
		*end_time = time_add_month_with_zone (*start_time, 1, timezone);
		break;
	default:
		g_return_if_reached ();
	}
}

/* Computes the range of time that the date navigator is showing */
static void
get_date_navigator_range (GnomeCalendar *gcal, time_t *start_time, time_t *end_time)
{
	ECalModel *model;
	gint start_year, start_month, start_day;
	gint end_year, end_month, end_day;
	struct icaltimetype start_tt;
	struct icaltimetype end_tt;
	icaltimezone *timezone;

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);

	start_tt = icaltime_null_time ();
	end_tt = icaltime_null_time ();

	if (!e_calendar_item_get_date_range (
		gcal->priv->date_navigator->calitem,
		&start_year, &start_month, &start_day,
		&end_year, &end_month, &end_day)) {
		*start_time = -1;
		*end_time = -1;
		return;
	}

	start_tt.year = start_year;
	start_tt.month = start_month + 1;
	start_tt.day = start_day;

	end_tt.year = end_year;
	end_tt.month = end_month + 1;
	end_tt.day = end_day;

	icaltime_adjust (&end_tt, 1, 0, 0, 0);

	*start_time = icaltime_as_timet_with_zone (start_tt, timezone);
	*end_time = icaltime_as_timet_with_zone (end_tt, timezone);
}

/* Adjusts a given query sexp with the time range of the date navigator */
static gchar *
adjust_e_cal_view_sexp (GnomeCalendar *gcal, const gchar *sexp)
{
	time_t start_time, end_time;
	gchar *start, *end;
	gchar *new_sexp;

	get_date_navigator_range (gcal, &start_time, &end_time);
	if (start_time == -1 || end_time == -1)
		return NULL;

	start = isodate_from_time_t (start_time);
	end = isodate_from_time_t (end_time);

	new_sexp = g_strdup_printf ("(and (occur-in-time-range? (make-time \"%s\")"
				    "                           (make-time \"%s\"))"
				    "     %s)",
				    start, end,
				    sexp);
	g_free (start);
	g_free (end);

	return new_sexp;
}

struct _date_query_msg {
	Message header;
	GnomeCalendar *gcal;
};

static void
update_query_async (struct _date_query_msg *msg)
{
	GnomeCalendar *gcal = msg->gcal;
	GnomeCalendarPrivate *priv;
	ECalView *old_query;
	gchar *real_sexp;
	GList *l;

	priv = gcal->priv;

	/* free the previous queries */
	for (l = priv->dn_queries; l != NULL; l = l->next) {
		old_query = l->data;

		if (old_query) {
			g_signal_handlers_disconnect_matched (old_query, G_SIGNAL_MATCH_DATA,
							      0, 0, NULL, NULL, gcal);
			g_object_unref (old_query);
		}
	}

	g_list_free (priv->dn_queries);
	priv->dn_queries = NULL;

	g_return_if_fail (priv->sexp != NULL);

	real_sexp = adjust_e_cal_view_sexp (gcal, priv->sexp);
	if (!real_sexp) {
		g_object_unref (msg->gcal);
		g_slice_free (struct _date_query_msg, msg);
		return; /* No time range is set, so don't start a query */
	}

	/* create queries for each loaded client */
	for (l = priv->clients_list; l != NULL; l = l->next) {
		GError *error = NULL;
		gint tries = 0;

		/* don't create queries for clients not loaded yet */
		if (e_cal_get_load_state ((ECal *) l->data) != E_CAL_LOAD_LOADED)
			continue;

try_again:
		old_query = NULL;
		if (!e_cal_get_query ((ECal *) l->data, real_sexp, &old_query, &error)) {
			/* If calendar is busy try again for 3 times. */
			if (error->code == E_CALENDAR_STATUS_BUSY && tries != 10) {
				tries++;
				/*TODO chose an optimal value */
				g_usleep (500);

				g_clear_error (&error);
				goto try_again;
			}

			g_warning (G_STRLOC ": Could not create the query: %s ", error->message);
			g_clear_error (&error);

			continue;
		}

		g_signal_connect (old_query, "objects_added",
				  G_CALLBACK (dn_e_cal_view_objects_added_cb), gcal);
		g_signal_connect (old_query, "objects_modified",
				  G_CALLBACK (dn_e_cal_view_objects_modified_cb), gcal);
		g_signal_connect (old_query, "objects_removed",
				  G_CALLBACK (dn_e_cal_view_objects_removed_cb), gcal);
		g_signal_connect (old_query, "view_done",
				  G_CALLBACK (dn_e_cal_view_done_cb), gcal);

		priv->dn_queries = g_list_append (priv->dn_queries, old_query);

		e_cal_view_start (old_query);
	}

	/* free memory */
	g_free (real_sexp);
	update_todo_view (gcal);

	g_object_unref (msg->gcal);
	g_slice_free (struct _date_query_msg, msg);
}

/* Restarts a query for the date navigator in the calendar */
void
gnome_calendar_update_query (GnomeCalendar *gcal)
{
	struct _date_query_msg *msg;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	e_calendar_item_clear_marks (gcal->priv->date_navigator->calitem);

	msg = g_slice_new0 (struct _date_query_msg);
	msg->header.func = (MessageFunc) update_query_async;
	msg->gcal = g_object_ref (gcal);

	message_push ((Message *) msg);
}

void
gnome_calendar_set_search_query (GnomeCalendar *gcal,
                                 const gchar *sexp,
                                 gboolean range_search,
                                 time_t start_range,
                                 time_t end_range)
{
	GnomeCalendarPrivate *priv;
	gint i;
	time_t start, end;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	g_return_if_fail (sexp != NULL);

	priv = gcal->priv;

	/* Set the query on the date navigator */

	g_free (priv->sexp);
	priv->sexp = g_strdup (sexp);

	priv->lview_select_daten_range = range_search;
	start = start_range;
	end = end_range;

	d(g_print ("Changing the queries %s \n", sexp));

	gnome_calendar_update_query (gcal);

	i = priv->current_view_type;

	/* Set the query on the views */
	if (i == GNOME_CAL_LIST_VIEW) {
		if (!priv->lview_select_daten_range) {
			e_cal_model_set_search_query_with_time_range (e_calendar_view_get_model (priv->views[i]), sexp, start, end);
		} else {
			start = priv->base_view_time;
			get_times_for_views (gcal, GNOME_CAL_LIST_VIEW, &start, &end, NULL);

			e_cal_model_set_search_query_with_time_range (e_calendar_view_get_model (priv->views[i]), sexp, start, end);

			if (priv->current_view_type == GNOME_CAL_LIST_VIEW)
				gnome_calendar_update_date_navigator (gcal);
		}
	} else
		e_cal_model_set_search_query (e_calendar_view_get_model (priv->views[i]), sexp);

	/* Set the query on the task pad */
	update_todo_view (gcal);
}

static void
set_timezone (GnomeCalendar *gcal)
{
	ECalModel *model;
	icaltimezone *timezone;
	GList *l;

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);

	for (l = gcal->priv->clients_list; l != NULL; l = l->next) {
		ECal *client = l->data;

		if (e_cal_get_load_state (client) == E_CAL_LOAD_LOADED)
			/* FIXME Error checking */
			e_cal_set_default_timezone (client, timezone, NULL);
	}
}

struct _mupdate_todo_msg {
	Message header;
	GnomeCalendar *gcal;
};

#if 0 /* KILL-BONOBO */
static void
update_todo_view_async (struct _mupdate_todo_msg *msg)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	ECalModel *model;
	gchar *sexp = NULL;

	g_return_if_fail (msg != NULL);

	gcal = msg->gcal;
	priv = gcal->priv;

	g_mutex_lock (priv->todo_update_lock);

	/* Set the query on the task pad */
	if (priv->todo_sexp) {
		g_free (priv->todo_sexp);
		priv->todo_sexp = NULL;
	}

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));

	if ((sexp = calendar_config_get_hide_completed_tasks_sexp (FALSE)) != NULL) {
		priv->todo_sexp = g_strdup_printf ("(and %s %s)", sexp,
							priv->sexp ? priv->sexp : "");
		e_cal_model_set_search_query (model, priv->todo_sexp);
		g_free (sexp);
	} else {
		priv->todo_sexp = g_strdup (priv->sexp);
		e_cal_model_set_search_query (model, priv->todo_sexp);
	}

	g_mutex_unlock (priv->todo_update_lock);

	g_object_unref (msg->gcal);
	g_slice_free (struct _mupdate_todo_msg, msg);
}
#endif

static void
update_todo_view (GnomeCalendar *gcal)
{
#if 0 /* KILL-BONOBO */
	struct _mupdate_todo_msg *msg;

	msg = g_slice_new0 (struct _mupdate_todo_msg);
	msg->header.func = (MessageFunc) update_todo_view_async;
	msg->gcal = g_object_ref (gcal);

	message_push ((Message *) msg);
#endif
}

static void
update_memo_view (GnomeCalendar *gcal)
{
#if 0 /* KILL-BONOBO */
	GnomeCalendarPrivate *priv;
	ECalModel *model, *view_model;
	time_t start, end;
	gchar *iso_start, *iso_end;

	priv = gcal->priv;

	/* Set the query on the memo pad*/
	model = e_memo_table_get_model (E_MEMO_TABLE (priv->memo));
	view_model = e_calendar_view_get_model(priv->views[priv->current_view_type]);
	e_cal_model_get_time_range (view_model, &start, &end);

	if (start != -1 && end != -1) {
		iso_start = isodate_from_time_t (start);
		iso_end = isodate_from_time_t (end);

		if (priv->memo_sexp) {
			g_free (priv->memo_sexp);
		}

		priv->memo_sexp = g_strdup_printf ("(or (not (has-start?)) (and (occur-in-time-range? (make-time \"%s\")"
				" (make-time \"%s\"))"
				"  %s))",
				iso_start, iso_end,
				priv->sexp ? priv->sexp : "");

		e_cal_model_set_search_query (model, priv->memo_sexp);

		g_free (iso_start);
		g_free (iso_end);
	}
#endif
}

static void
process_completed_tasks (GnomeCalendar *gcal, gboolean config_changed)
{
#if 0 /* KILL-BONOBO */
	GnomeCalendarPrivate *priv;

	g_return_if_fail (GNOME_IS_CALENDAR(gcal));

	priv = gcal->priv;

	e_calendar_table_process_completed_tasks (E_CALENDAR_TABLE (priv->todo), priv->clients_list[E_CAL_SOURCE_TYPE_TODO], config_changed);
#endif
}

#if 0 /* KILL-BONOBO */
static gboolean
update_todo_view_cb (GnomeCalendar *gcal)
{
	ECalModel *model;

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (gcal->priv->todo));

	process_completed_tasks (gcal, FALSE);
	e_cal_model_tasks_update_due_tasks (E_CAL_MODEL_TASKS (model));

	return TRUE;
}
#endif

static gboolean
update_marcus_bains_line_cb (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GnomeCalendarViewType view_type;
	ECalendarView *view;
	time_t now, day_begin;

	priv = gcal->priv;

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	if (E_IS_DAY_VIEW (view))
		e_day_view_marcus_bains_update (E_DAY_VIEW (view));

	time (&now);
	day_begin = time_day_begin (now);

	/* check in the first two minutes */
	if (now >= day_begin && now <= day_begin + 120) {
		time_t start_time = 0, end_time = 0;

		g_return_val_if_fail (view != NULL, TRUE);

		e_calendar_view_get_selected_time_range (view, &start_time, &end_time);

		if (end_time >= time_add_day (day_begin, -1) && start_time <= day_begin) {
			gnome_calendar_goto (gcal, now);
		}
	}

	return TRUE;
}

static void
setup_widgets (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GtkWidget *w, *sep;
	gchar *filename;
	ETable *etable;
	GtkAdjustment *adjustment;
	GtkWidget *vbox;
	GtkWidget *label;
	ECalModel *cal_model;
	gint i;
	gchar *tmp;

	priv = gcal->priv;

	/* update_todo_view (gcal); */

	/* Timeout check to hide completed items */
#if 0 /* KILL-BONOBO */
	priv->update_timeout = g_timeout_add_full (G_PRIORITY_LOW, 60000, (GSourceFunc) update_todo_view_cb, gcal, NULL);
#endif

	/* The Marcus Bains line */
	priv->update_marcus_bains_line_timeout = g_timeout_add_full (G_PRIORITY_LOW, 60000, (GSourceFunc) update_marcus_bains_line_cb, gcal, NULL);

	update_memo_view (gcal);
}

/* Object initialization function for the gnome calendar */
static void
gnome_calendar_init (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	priv = g_new0 (GnomeCalendarPrivate, 1);
	gcal->priv = priv;

	priv->clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	if (non_intrusive_error_table == NULL)
		non_intrusive_error_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	if (non_intrusive_error_table == NULL)
		non_intrusive_error_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	priv->todo_update_lock = g_mutex_new ();

	priv->current_view_type = GNOME_CAL_DAY_VIEW;
	priv->range_selected = FALSE;
	priv->lview_select_daten_range = TRUE;

	setup_widgets (gcal);

	priv->dn_queries = NULL;
	priv->sexp = g_strdup ("#t"); /* Match all */
	priv->todo_sexp = g_strdup ("#t");
	priv->memo_sexp = g_strdup ("#t");

	priv->visible_start = -1;
	priv->visible_end = -1;
	priv->updating = FALSE;
}

static void
gnome_calendar_destroy (GtkObject *object)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	gchar *filename;
	ECalModel *cal_model;
	ECalendarView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (object));

	gcal = GNOME_CALENDAR (object);
	priv = gcal->priv;

	if (priv) {
		GList *l;
		gint i;

		if (priv->shell_settings != NULL) {
			g_object_unref (priv->shell_settings);
			priv->shell_settings = NULL;
		}

		/* Clean up the clients */
		for (l = priv->clients_list; l != NULL; l = l->next) {
			g_signal_handlers_disconnect_matched (l->data, G_SIGNAL_MATCH_DATA,
							      0, 0, NULL, NULL, gcal);
		}

		g_hash_table_destroy (priv->clients);
		g_list_free (priv->clients_list);

		priv->clients = NULL;
		priv->clients_list = NULL;

		if (priv->default_client) {
			g_signal_handlers_disconnect_matched (priv->default_client,
							      G_SIGNAL_MATCH_DATA,
							      0, 0, NULL, NULL, gcal);
			g_object_unref (priv->default_client);
		}
		priv->default_client = NULL;

		for (l = priv->notifications; l; l = l->next)
			calendar_config_remove_notification (GPOINTER_TO_UINT (l->data));
		g_list_free (priv->notifications);
		priv->notifications = NULL;

		if (priv->dn_queries) {
			for (l = priv->dn_queries; l != NULL; l = l->next) {
				g_signal_handlers_disconnect_matched ((ECalView *) l->data, G_SIGNAL_MATCH_DATA,
								      0, 0, NULL, NULL, gcal);
				g_object_unref (l->data);
			}

			g_list_free (priv->dn_queries);
			priv->dn_queries = NULL;
		}

		if (non_intrusive_error_table) {
			g_hash_table_destroy (non_intrusive_error_table);
			non_intrusive_error_table = NULL;
		}

		if (priv->sexp) {
			g_free (priv->sexp);
			priv->sexp = NULL;
		}

		if (priv->update_timeout) {
			g_source_remove (priv->update_timeout);
			priv->update_timeout = 0;
		}

		if (priv->update_marcus_bains_line_timeout) {
			g_source_remove (priv->update_marcus_bains_line_timeout);
			priv->update_marcus_bains_line_timeout = 0;
		}

		/* Disconnect all handlers */
		view = gnome_calendar_get_calendar_view (
			gcal, GNOME_CAL_WEEK_VIEW);
		cal_model = e_calendar_view_get_model (view);
		g_signal_handlers_disconnect_by_func (
			cal_model, view_progress_cb, gcal);
		g_signal_handlers_disconnect_by_func (
			cal_model, view_done_cb, gcal);

		g_mutex_free (priv->todo_update_lock);

		g_free (priv);
		gcal->priv = NULL;
	}

	if (GTK_OBJECT_CLASS (gnome_calendar_parent_class)->destroy)
		(* GTK_OBJECT_CLASS (gnome_calendar_parent_class)->destroy) (object);
}

static void
notify_selected_time_changed (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	gint i;

	priv = gcal->priv;
	for (i = 0; i < GNOME_CAL_LAST_VIEW; i++) {
		g_signal_emit_by_name (priv->views[i], "selected_time_changed");
	}
}

static void
gnome_calendar_goto_date (GnomeCalendar *gcal,
			  GnomeCalendarGotoDateType goto_date)
{
	ECalModel *model;
	time_t	 new_time = 0;
	gint week_start_day;
	gboolean need_updating = FALSE;
	icaltimezone *timezone;

	g_return_if_fail (GNOME_IS_CALENDAR(gcal));

	model = gnome_calendar_get_calendar_model (gcal);
	week_start_day = e_cal_model_get_week_start_day (model);
	timezone = e_cal_model_get_timezone (model);

	switch (goto_date) {
		/* GNOME_CAL_GOTO_TODAY and GNOME_CAL_GOTO_DATE are
		   currently not used
		*/
	case GNOME_CAL_GOTO_TODAY:
		break;
	case GNOME_CAL_GOTO_DATE:
		break;
	case GNOME_CAL_GOTO_FIRST_DAY_OF_MONTH:
		new_time = time_month_begin_with_zone (
			gcal->priv->base_view_time, timezone);
		need_updating = TRUE;
		break;
	case GNOME_CAL_GOTO_LAST_DAY_OF_MONTH:
		new_time = time_add_month_with_zone (
			gcal->priv->base_view_time, 1, timezone);
		new_time = time_month_begin_with_zone (new_time, timezone);
		new_time = time_add_day_with_zone (new_time, -1, timezone);
		need_updating = TRUE;
		break;
	case GNOME_CAL_GOTO_FIRST_DAY_OF_WEEK:
		new_time = time_week_begin_with_zone (
			gcal->priv->base_view_time, week_start_day, timezone);
		need_updating = TRUE;
		break;
	case GNOME_CAL_GOTO_LAST_DAY_OF_WEEK:
		new_time = time_week_begin_with_zone (
			gcal->priv->base_view_time, week_start_day, timezone);
		if (gcal->priv->current_view_type == GNOME_CAL_DAY_VIEW ||
		    gcal->priv->current_view_type == GNOME_CAL_WORK_WEEK_VIEW) {
			/* FIXME Shouldn't hard code work week end */
			/* goto Friday of this week */
			new_time = time_add_day_with_zone (new_time, 4, timezone);
		} else {
			/* goto Sunday of this week */
			/* FIXME Shouldn't hard code week end */
			new_time = time_add_day_with_zone (new_time, 6, timezone);
		}
		need_updating = TRUE;
		break;
	case GNOME_CAL_GOTO_SAME_DAY_OF_PREVIOUS_WEEK:
		new_time = time_add_week_with_zone (
			gcal->priv->base_view_time, -1, timezone);
		need_updating = TRUE;
		break;
	case GNOME_CAL_GOTO_SAME_DAY_OF_NEXT_WEEK:
		new_time = time_add_week_with_zone (
			gcal->priv->base_view_time, 1, timezone);
		need_updating = TRUE;
		break;
	default:
		break;
	}

	if (need_updating) {
		gnome_calendar_set_selected_time_range (gcal, new_time);
		notify_selected_time_changed (gcal);
	}
}

void
gnome_calendar_goto (GnomeCalendar *gcal, time_t new_time)
{
	GnomeCalendarPrivate *priv;
	gint i;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	g_return_if_fail (new_time != -1);

	priv = gcal->priv;

	gnome_calendar_set_selected_time_range (gcal, new_time);

	for (i = 0; i < GNOME_CAL_LAST_VIEW; i++) {
		if (E_CALENDAR_VIEW_CLASS (G_OBJECT_GET_CLASS (priv->views[i]))->set_selected_time_range)
			E_CALENDAR_VIEW_CLASS (G_OBJECT_GET_CLASS (priv->views[i]))->set_selected_time_range (priv->views[i], new_time, new_time);
	}
}

static void
update_view_times (GnomeCalendar *gcal, time_t start_time)
{
	GnomeCalendarPrivate *priv;
	ECalModel *model;
	time_t real_start_time = start_time;
	time_t end_time, select_time = 0;

	priv = gcal->priv;

	priv->base_view_time = start_time;

	model = e_calendar_view_get_model (priv->views[priv->current_view_type]);
	get_times_for_views (gcal, priv->current_view_type, &real_start_time, &end_time, &select_time);

	if (priv->current_view_type == GNOME_CAL_LIST_VIEW && !priv->lview_select_daten_range)
		return;

	e_cal_model_set_time_range (model, real_start_time, end_time);

	if (select_time != 0 && select_time >= real_start_time && select_time <= end_time)
		e_calendar_view_set_selected_time_range (priv->views[priv->current_view_type], select_time, select_time);
}

static void
gnome_calendar_direction (GnomeCalendar *gcal, gint direction)
{
	ECalModel *model;
	icaltimezone *timezone;

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);

	switch (gnome_calendar_get_view (gcal)) {
	case GNOME_CAL_DAY_VIEW:
		gcal->priv->base_view_time = time_add_day_with_zone (
			gcal->priv->base_view_time, direction, timezone);
		break;
	case GNOME_CAL_WORK_WEEK_VIEW:
	case GNOME_CAL_WEEK_VIEW:
		gcal->priv->base_view_time = time_add_week_with_zone (
			gcal->priv->base_view_time, direction, timezone);
		break;
	case GNOME_CAL_MONTH_VIEW:
	case GNOME_CAL_LIST_VIEW:
		gcal->priv->base_view_time = time_add_month_with_zone (
			gcal->priv->base_view_time, direction, timezone);
		break;
	default:
		g_return_if_reached ();
	}

	gnome_calendar_set_selected_time_range (
		gcal, gcal->priv->base_view_time);
}

void
gnome_calendar_next (GnomeCalendar *gcal)
{
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gnome_calendar_direction (gcal, 1);
}

void
gnome_calendar_previous (GnomeCalendar *gcal)
{
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gnome_calendar_direction (gcal, -1);
}

void
gnome_calendar_dayjump (GnomeCalendar *gcal, time_t time)
{
	ECalModel *model;
	icaltimezone *timezone;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);

	gcal->priv->base_view_time =
		time_day_begin_with_zone (time, timezone);

	update_view_times (gcal, gcal->priv->base_view_time);
	gnome_calendar_set_view (gcal, GNOME_CAL_DAY_VIEW);
}

void
gnome_calendar_goto_today (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	gnome_calendar_goto (gcal, time (NULL));
	gtk_widget_grab_focus (GTK_WIDGET (view));
}

/**
 * gnome_calendar_get_view:
 * @gcal: A calendar.
 *
 * Queries the type of the view that is being shown in a calendar.
 *
 * Return value: Type of the view that is currently shown.
 **/
GnomeCalendarViewType
gnome_calendar_get_view (GnomeCalendar *gcal)
{
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), GNOME_CAL_DAY_VIEW);

	return gcal->priv->current_view_type;
}

static void
set_view (GnomeCalendar *gcal, GnomeCalendarViewType view_type, gboolean range_selected)
{
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	gcal->priv->range_selected = range_selected;
	gcal->priv->current_view_type = view_type;

	view = gnome_calendar_get_calendar_view (gcal, view_type);
	gtk_widget_grab_focus (GTK_WIDGET (view));

	g_object_notify (G_OBJECT (gcal), "view");
}

/**
 * gnome_calendar_set_view:
 * @gcal: A calendar.
 * @view_type: Type of view to show.
 *
 * Sets the view that should be shown in a calendar.  If @reset_range is true,
 * this function will automatically set the number of days or weeks shown in
 * the view; otherwise the last configuration will be kept.
 **/
void
gnome_calendar_set_view (GnomeCalendar *gcal, GnomeCalendarViewType view_type)
{
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	set_view (gcal, view_type, FALSE);
}

static void
gnome_calendar_change_view (GnomeCalendar *gcal, GnomeCalendarViewType view_type)
{
	if (gnome_calendar_get_view(gcal) == view_type)
		return;

	gnome_calendar_set_view(gcal, view_type);
}

void
gnome_calendar_display_view (GnomeCalendar *gcal,
                             GnomeCalendarViewType view_type)
{
	CalendarView *cal_view;
	ECalendarView *view;
	gboolean preserve_day;
	time_t start_time;
	gint ii;

	view = gnome_calendar_get_calendar_view (gcal, view_type);

	/* Set the view without changing the selection or updating the date
	 * navigator. If a range of dates isn't selected, also reset the
	 * number of days/weeks shown to the default (i.e. 1 day for the
	 * day view or 6 weeks for the month view). */

	preserve_day = FALSE;

	switch (view_type) {
	case GNOME_CAL_DAY_VIEW:
		if (!gcal->priv->range_selected)
			e_day_view_set_days_shown (E_DAY_VIEW (view), 1);

		gtk_widget_show (GTK_WIDGET (gcal->priv->date_navigator));
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		preserve_day = TRUE;
		gtk_widget_show (GTK_WIDGET (gcal->priv->date_navigator));
		break;

	case GNOME_CAL_WEEK_VIEW:
		preserve_day = TRUE;
		gtk_widget_show (GTK_WIDGET (gcal->priv->date_navigator));
		break;

	case GNOME_CAL_MONTH_VIEW:
		if (!gcal->priv->range_selected)
			e_week_view_set_weeks_shown (E_WEEK_VIEW (view), 6);

		preserve_day = TRUE;
		gtk_widget_show (GTK_WIDGET (gcal->priv->date_navigator));
		break;

	case GNOME_CAL_LIST_VIEW:
		if (!gcal->priv->lview_select_daten_range)
			gtk_widget_hide (GTK_WIDGET (gcal->priv->date_navigator));
		else
			gtk_widget_show (GTK_WIDGET (gcal->priv->date_navigator));
		break;

	default:
		g_return_if_reached ();
	}

	set_view (gcal, view_type, gcal->priv->range_selected);

	/* XXX Move this to set_view()? */
	E_CALENDAR_VIEW (gcal->priv->views[view_type])->in_focus = TRUE;
	for (ii = 0; ii < GNOME_CAL_LAST_VIEW; ii++) {
		if (ii == view_type)
			continue;
		E_CALENDAR_VIEW (gcal->priv->views[ii])->in_focus = FALSE;
	}

	/* For the week & month views we want the selection in the date
	   navigator to be rounded to the nearest week when the arrow buttons
	   are pressed to move to the previous/next month. */
	g_object_set (
		gcal->priv->date_navigator->calitem,
		"preserve_day_when_moving", preserve_day, NULL);

	if (!gcal->priv->base_view_time)
		start_time = time (NULL);
	else
		start_time = gcal->priv->base_view_time;

	gnome_calendar_set_selected_time_range (gcal, start_time);

}

struct _mclient_msg {
	Message header;
	ECalModel *model;
	ECal *client;
};

static void
add_mclient_async (struct _mclient_msg *msg)
{
	e_cal_model_add_client (msg->model, msg->client);

	g_object_unref (msg->client);
	g_object_unref (msg->model);
	g_slice_free (struct _mclient_msg, msg);
}

static void
add_mclient (ECalModel *model, ECal *client)
{
	struct _mclient_msg *msg;

	msg = g_slice_new0 (struct _mclient_msg);
	msg->header.func = (MessageFunc) add_mclient_async;
	msg->model = g_object_ref (model);
	msg->client = g_object_ref (client);

	message_push ((Message *) msg);
}

static void
non_intrusive_error_remove(GtkWidget *w, gpointer data)
{
	g_hash_table_remove(non_intrusive_error_table, data);
}

static void
client_cal_opened_cb (ECal *ecal, ECalendarStatus status, GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	ESource *source;
	ECalModel *model;
	ECalLoadState state;
	gchar *msg;
	GtkWidget *w = NULL;
	gchar *id;

	priv = gcal->priv;

	source = e_cal_get_source (ecal);
	state = e_cal_get_load_state (ecal);

	if (status == E_CALENDAR_STATUS_AUTHENTICATION_FAILED || status == E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED)
		auth_cal_forget_password (ecal);

	switch (status) {
	case E_CALENDAR_STATUS_OK:
		break;
	case E_CALENDAR_STATUS_BUSY:
		if (state == E_CAL_LOAD_NOT_LOADED)
			e_cal_open_async (ecal, FALSE);
		return;
	case E_CALENDAR_STATUS_INVALID_SERVER_VERSION:
		id = g_strdup ("calendar:server-version");

		if (g_hash_table_lookup(non_intrusive_error_table, id)) {
			/* We already have it */
			g_message("Error occurred while existing dialog active:\n");
			return;
		}

		w = e_error_new(NULL, "calendar:server-version", NULL);
		/*e_calendar_utils_show_error_silent (w);  KILL-BONOBO */
		g_hash_table_insert (non_intrusive_error_table, id, g_object_ref(w));
		g_signal_connect(w, "destroy", G_CALLBACK(non_intrusive_error_remove), id);

		status = E_CALENDAR_STATUS_OK;
		break;
	case E_CALENDAR_STATUS_AUTHENTICATION_FAILED:
		/* try to reopen calendar - it'll ask for a password once again */
		e_cal_open_async (ecal, FALSE);
		return;
	case E_CALENDAR_STATUS_REPOSITORY_OFFLINE:
		/* check to see if we have dialog already running for this operation */
		id = g_strdup ("calendar:unable-to-load-the-calendar");

		if (g_hash_table_lookup(non_intrusive_error_table, id)) {
			/* We already have it */
			g_message("Error occurred while existing dialog active:\n");
			return;
		}

		w = e_error_new(GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gcal))), "calendar:unable-to-load-the-calendar", e_cal_get_error_message (status), NULL);
		/*e_calendar_utils_show_error_silent (w);  KILL-BONOBO */
		g_hash_table_insert (non_intrusive_error_table, id, g_object_ref(w));
		g_signal_connect(w, "destroy", G_CALLBACK(non_intrusive_error_remove), id);
	default:
		/* Make sure the source doesn't disappear on us */
		g_object_ref (source);

		priv->clients_list = g_list_remove (priv->clients_list, ecal);
		g_hash_table_remove (priv->clients, e_source_peek_uid (source));

		g_signal_emit (gcal, signals[SOURCE_REMOVED], 0, source);
		g_object_unref (source);

		g_warning ("Unable to load the calendar %s \n", e_cal_get_error_message (status));

		return;
	}

	g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, client_cal_opened_cb, NULL);

	msg = g_strdup_printf (_("Loading appointments at %s"), e_cal_get_uri (ecal));
	/*e_calendar_view_set_status_message (E_CALENDAR_VIEW (priv->week_view), msg, -1);  KILL-BONOBO */
	g_free (msg);

	/* add client to the views */
	model = e_calendar_view_get_model (priv->views[priv->current_view_type]);
	add_mclient (model, ecal);

	/* update date navigator query */
	gnome_calendar_update_query (gcal);

	/*e_calendar_view_set_status_message (E_CALENDAR_VIEW (priv->week_view), NULL, -1);  KILL-BONOBO */
}

static void
default_client_cal_opened_cb (ECal *ecal, ECalendarStatus status, GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	ESource *source;
	ECalLoadState state;

	priv = gcal->priv;

	source = e_cal_get_source (ecal);
	state = e_cal_get_load_state (ecal);

	if (status == E_CALENDAR_STATUS_AUTHENTICATION_FAILED || status == E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED)
		auth_cal_forget_password (ecal);

	switch (status) {
	case E_CALENDAR_STATUS_OK:
		break;
	case E_CALENDAR_STATUS_BUSY:
		if (state == E_CAL_LOAD_NOT_LOADED)
			e_cal_open_async (ecal, FALSE);
		return;
	case E_CALENDAR_STATUS_AUTHENTICATION_FAILED:
		/* try to reopen calendar - it'll ask for a password once again */
		e_cal_open_async (ecal, FALSE);
		return;
	case E_CALENDAR_STATUS_INVALID_SERVER_VERSION:
		e_error_run (NULL, "calendar:server-version", NULL);
	default:
		/* Make sure the source doesn't disappear on us */
		g_object_ref (source);

		g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, gcal);

		/* FIXME should we do this to prevent multiple error dialogs? */
		priv->clients_list = g_list_remove (priv->clients_list, ecal);
		g_hash_table_remove (priv->clients, e_source_peek_uid (source));

		/* FIXME Is there a better way to handle this? */
		if (priv->default_client)
			g_object_unref (priv->default_client);
		priv->default_client = NULL;

		g_signal_emit (gcal, signals[SOURCE_REMOVED], 0, source);
		g_object_unref (source);

		g_warning ("Unable to load the calendar %s \n", e_cal_get_error_message (status));

		return;
	}

	g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, default_client_cal_opened_cb, NULL);

	e_cal_model_set_default_client (
		e_calendar_view_get_model (
		E_CALENDAR_VIEW (priv->views[priv->current_view_type])),
		ecal);
}

typedef void (*open_func) (ECal *, ECalendarStatus, GnomeCalendar *);

static gboolean
open_ecal (GnomeCalendar *gcal, ECal *cal, gboolean only_if_exists, open_func of)
{
	GnomeCalendarPrivate *priv;
	gchar *msg;
	icaltimezone *zone;

	priv = gcal->priv;

	zone = calendar_config_get_icaltimezone ();
	e_cal_set_default_timezone (cal, zone, NULL);

	msg = g_strdup_printf (_("Opening %s"), e_cal_get_uri (cal));
	/*e_calendar_view_set_status_message (E_CALENDAR_VIEW (priv->week_view), msg, -1);  KILL-BONOBO */

	g_free (msg);

	g_signal_connect (G_OBJECT (cal), "cal_opened", G_CALLBACK (of), gcal);
	e_cal_open_async (cal, only_if_exists);

	return TRUE;
}

/* Callback when we get an error message from the backend */
static void
backend_error_cb (ECal *client, const gchar *message, gpointer data)
{
	GnomeCalendar *gcal;
	GtkDialog *dialog;
	gchar *uristr;
	gchar *id;

	gcal = GNOME_CALENDAR (data);

	uristr = get_uri_without_password (e_cal_get_uri (client));
	id = g_strdup ("calendar:error-on-loading-the-calendar");

	if (g_hash_table_lookup(non_intrusive_error_table,id)) {
		/* We already have it */
		g_message("Error occurred while existing dialog active:\n");
		return;
	}

	dialog = (GtkDialog *)e_error_new(GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gcal))), "calendar:error-on-loading-the-calendar", uristr, message, NULL);
	/* e_calendar_utils_show_error_silent(GTK_WIDGET (dialog));  KILL-BONOBO */

	g_hash_table_insert (non_intrusive_error_table, id, g_object_ref(dialog));
	g_signal_connect(GTK_WIDGET (dialog), "destroy", G_CALLBACK(non_intrusive_error_remove), id);

	g_free (uristr);
}

/* Callback when the backend dies */
static void
backend_died_cb (ECal *ecal, gpointer data)
{
	GnomeCalendar *gcal;
	GnomeCalendarPrivate *priv;
	ESource *source;
	const gchar *id;
	GtkWidget *w = NULL;

	gcal = GNOME_CALENDAR (data);
	priv = gcal->priv;

	/* FIXME What about default sources? */

	/* Make sure the source doesn't go away on us since we use it below */
	source = g_object_ref (e_cal_get_source (ecal));

	priv->clients_list = g_list_remove (priv->clients_list, ecal);
	g_hash_table_remove (priv->clients, e_source_peek_uid (source));

	id = g_strdup ("calendar:calendar-crashed");

	/* e_calendar_view_set_status_message (E_CALENDAR_VIEW (priv->week_view), NULL, -1); KILL-BONOBO */

	g_signal_emit (gcal, signals[SOURCE_REMOVED], 0, source);

	g_object_unref (source);

	if (g_hash_table_lookup(non_intrusive_error_table,id)) {
		/* We already have it */
		g_message("Error occurred while existing dialog active:\n");
		return;
	}

	w = e_error_new(GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gcal))), "calendar:backend_died", NULL);
	/* e_calendar_utils_show_error_silent (w); KILL-BONOBO */
	g_hash_table_insert (non_intrusive_error_table, (gpointer) id, g_object_ref(w));
	g_signal_connect((GtkObject *)w, "destroy", G_CALLBACK(non_intrusive_error_remove), (gpointer) id);
}

GtkWidget *
gnome_calendar_new (EShellSettings *shell_settings)
{
	g_return_val_if_fail (E_IS_SHELL_SETTINGS (shell_settings), NULL);

	return g_object_new (
		GNOME_TYPE_CALENDAR,
		"shell-settings", shell_settings, NULL);
}

EShellSettings *
gnome_calendar_get_shell_settings (GnomeCalendar *gcal)
{
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	return gcal->priv->shell_settings;
}

ECalendar *
gnome_calendar_get_date_navigator (GnomeCalendar *gcal)
{
	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	return gcal->priv->date_navigator;
}

void
gnome_calendar_set_date_navigator (GnomeCalendar *gcal,
                                   ECalendar *date_navigator)
{
	ECalendarItem *calitem;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	if (date_navigator != NULL) {
		g_return_if_fail (E_IS_CALENDAR (date_navigator));
		g_object_ref (date_navigator);
	}

	/* FIXME This ignores the possibility that a date navigator
	 *       object already exists.  But this function should
	 *       only be called once anyway. */
	if (gcal->priv->date_navigator != NULL)
		g_object_unref (gcal->priv->date_navigator);

	gcal->priv->date_navigator = date_navigator;
	calitem = date_navigator->calitem;

	g_signal_connect (
		calitem, "selection-changed",
		G_CALLBACK (gnome_calendar_on_date_navigator_selection_changed),
		gcal);

	g_object_notify (G_OBJECT (gcal), "date-navigator");
}

/**
 * gnome_calendar_get_calendar_model:
 * @gcal: A calendar view.
 *
 * Queries the calendar model object that a calendar view is using.
 *
 * Return value: A calendar client interface object.
 **/
ECalModel *
gnome_calendar_get_calendar_model (GnomeCalendar *gcal)
{
	ECalendarView *calendar_view;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	/* XXX This is kind of silly.  The views all share the same
	 *     model now.  We should just keep our own reference. */
	calendar_view = gcal->priv->views[gcal->priv->current_view_type];

	return e_calendar_view_get_model (calendar_view);
}

/**
 * gnome_calendar_get_default_client
 */
ECal *
gnome_calendar_get_default_client (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), NULL);

	priv = gcal->priv;

	return e_cal_model_get_default_client (e_calendar_view_get_model (gcal->priv->views[priv->current_view_type]));
}

/**
 * gnome_calendar_add_source:
 * @gcal: A GnomeCalendar.
 * @source: #ESource to add to the calendar views.
 *
 * Adds the given calendar source to the calendar views.
 *
 * Returns: TRUE if successful, FALSE if error.
 */
gboolean
gnome_calendar_add_source (GnomeCalendar *gcal, ESource *source)
{
	GnomeCalendarPrivate *priv;
	ECal *client;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	priv = gcal->priv;

	client = g_hash_table_lookup (priv->clients, e_source_peek_uid (source));
	if (client) {
		/* We already have it */

		return TRUE;
	} else {
		ESource *default_source;

		if (priv->default_client) {
			default_source = e_cal_get_source (priv->default_client);

			g_message ("Check if default client matches (%s %s)", e_source_peek_uid (default_source), e_source_peek_uid (source));
			/* We don't have it but the default client is it */
			if (!strcmp (e_source_peek_uid (default_source), e_source_peek_uid (source)))
				client = g_object_ref (priv->default_client);
		}

		/* Create a new one */
		if (!client) {
			client = auth_new_cal_from_source (source, E_CAL_SOURCE_TYPE_EVENT);
			if (!client)
				return FALSE;
		}
	}

	g_signal_connect (G_OBJECT (client), "backend_error", G_CALLBACK (backend_error_cb), gcal);
	g_signal_connect (G_OBJECT (client), "backend_died", G_CALLBACK (backend_died_cb), gcal);

	/* add the client to internal structure */
	g_hash_table_insert (priv->clients, g_strdup (e_source_peek_uid (source)), client);
	priv->clients_list = g_list_prepend (priv->clients_list, client);

	g_signal_emit (gcal, signals[SOURCE_ADDED], 0, source);

	open_ecal (gcal, client, FALSE, client_cal_opened_cb);

	return TRUE;
}

/**
 * gnome_calendar_remove_source
 * @gcal: A #GnomeCalendar.
 * @source: #ESource to be removed from the clients.
 *
 * Removes the given source from the list of clients being shown by the
 * calendar views.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean
gnome_calendar_remove_source (GnomeCalendar *gcal, ESource *source)
{
	gboolean result;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	result = gnome_calendar_remove_source_by_uid (gcal, e_source_peek_uid (source));
	if (result)
		g_signal_emit (gcal, signals[SOURCE_REMOVED], 0, source);

	return result;
}

gboolean
gnome_calendar_remove_source_by_uid (GnomeCalendar *gcal, const gchar *uid)
{
	GnomeCalendarPrivate *priv;
	ECal *client;
	ECalModel *model;
	GList *l;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	priv = gcal->priv;

	client = g_hash_table_lookup (priv->clients, uid);
	if (!client)
		return TRUE;

	priv->clients_list = g_list_remove (priv->clients_list, client);
	g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA,
					      0, 0, NULL, NULL, gcal);

	/* remove the query for this client */
	for (l = priv->dn_queries; l != NULL; l = l->next) {
		ECalView *query = l->data;

		if (query && (client == e_cal_view_get_client (query))) {
			g_signal_handlers_disconnect_matched (query, G_SIGNAL_MATCH_DATA,
							      0, 0, NULL, NULL, gcal);
			priv->dn_queries = g_list_remove (priv->dn_queries, query);
			g_object_unref (query);
			break;
		}
	}

	model = e_calendar_view_get_model (priv->views[priv->current_view_type]);
	e_cal_model_remove_client (model, client);

	/* update date navigator query */
	gnome_calendar_update_query (gcal);

	g_hash_table_remove (priv->clients, uid);

	return TRUE;
}

/**
 * gnome_calendar_set_default_source:
 * @gcal: A calendar view
 * @source: The #ESource to use as default
 *
 * Set the default uri on the given calendar view, the default uri
 * will be used as the default when creating events in the view.

 *
 * Return value: TRUE if the uri was already added and is set, FALSE
 * otherwise
 **/
gboolean
gnome_calendar_set_default_source (GnomeCalendar *gcal, ESource *source)
{
	GnomeCalendarPrivate *priv;
	ECal *client;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	priv = gcal->priv;

	client = g_hash_table_lookup (priv->clients, e_source_peek_uid (source));

	if (priv->default_client)
		g_object_unref (priv->default_client);

	if (client) {
		priv->default_client = g_object_ref (client);
	} else {
		priv->default_client = auth_new_cal_from_source (source, E_CAL_SOURCE_TYPE_EVENT);
		if (!priv->default_client)
			return FALSE;
	}

	open_ecal (gcal, priv->default_client, FALSE, default_client_cal_opened_cb);

	return TRUE;
}

void
gnome_calendar_set_selected_time_range (GnomeCalendar *gcal,
					time_t	       start_time)
{
	update_view_times (gcal, start_time);
	gnome_calendar_update_date_navigator (gcal);
	gnome_calendar_notify_dates_shown_changed (gcal);
}

/**
 * gnome_calendar_get_selected_time_range:
 * @gcal: A calendar view.
 * @start_time: Return value for the start of the time selection.
 * @end_time: Return value for the end of the time selection.
 *
 * Queries the time selection range on the calendar view.
 **/
void
gnome_calendar_get_selected_time_range (GnomeCalendar *gcal,
					time_t	 *start_time,
					time_t	 *end_time)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;
	ECalModel *model;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);
	model = e_calendar_view_get_model (view);

	e_cal_model_get_time_range (model, start_time, end_time);
}

/**
 * gnome_calendar_new_task:
 * @gcal: An Evolution calendar.
 * @param dtstart Start time of the task, in same timezone as model.
 * @param dtend End time of the task, in same timezone as model.
 *
 * Opens a task editor dialog for a new task. dtstart or dtend can be NULL.
 **/
#if 0 /* KILL-BONOBO */
void
gnome_calendar_new_task		(GnomeCalendar *gcal, time_t *dtstart, time_t *dtend)
{
	GnomeCalendarPrivate *priv;
	ECal *ecal;
	ECalModel *model;
	CompEditor *editor;
	ECalComponent *comp;
	icalcomponent *icalcomp;
	const gchar *category;
	guint32 flags = 0;
	ECalComponentDateTime dt;
	struct icaltimetype itt;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;
	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->todo));
	ecal = e_cal_model_get_default_client (model);
	if (!ecal)
		return;

	flags |= COMP_EDITOR_NEW_ITEM;
	editor = task_editor_new (ecal, flags);

	icalcomp = e_cal_model_create_component_with_defaults (model, FALSE);
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	dt.value = &itt;
	dt.tzid = icaltimezone_get_tzid (e_cal_model_get_timezone (model));

	if (dtstart) {
		itt = icaltime_from_timet_with_zone (*dtstart, FALSE, e_cal_model_get_timezone (model));
		e_cal_component_set_dtstart (comp, &dt);
	}

	if (dtend) {
		itt = icaltime_from_timet_with_zone (*dtend, FALSE, e_cal_model_get_timezone (model));
		e_cal_component_set_due (comp, &dt); /* task uses 'due' not 'dtend' */
	}

	if (dtstart || dtend)
		e_cal_component_commit_sequence (comp);

	comp_editor_edit_comp (editor, comp);
	g_object_unref (comp);

	gtk_window_present (GTK_WINDOW (editor));
}
#endif

/* Returns the selected time range for the current view. Note that this may be
   different from the fields in the GnomeCalendar, since the view may clip
   this or choose a more appropriate time. */
void
gnome_calendar_get_current_time_range (GnomeCalendar *gcal,
				       time_t	 *start_time,
				       time_t	 *end_time)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	e_calendar_view_get_selected_time_range (view, start_time, end_time);
}

/* Gets the visible time range for the current view. Returns FALSE if no
   time range has been set yet. */
gboolean
gnome_calendar_get_visible_time_range (GnomeCalendar *gcal,
				       time_t	 *start_time,
				       time_t	 *end_time)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), FALSE);

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	return e_calendar_view_get_visible_time_range (
		view, start_time, end_time);
}

/* This updates the month shown and the days selected in the calendar, if
   necessary. */
static void
gnome_calendar_update_date_navigator (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	ECalModel *model;
	time_t start, end;
	gint week_start_day;
	GDate start_date, end_date;
	icaltimezone *timezone;

	priv = gcal->priv;

	/* If the ECalendar is not yet set, we just return. */
	if (priv->date_navigator == NULL)
		return;

	/* If the ECalendar isn't visible, we just return. */
	if (!GTK_WIDGET_VISIBLE (priv->date_navigator))
		return;

	if (priv->current_view_type == GNOME_CAL_LIST_VIEW && !priv->lview_select_daten_range)
		return;

	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);
	week_start_day = e_cal_model_get_week_start_day (model);
	e_cal_model_get_time_range (model, &start, &end);

	time_to_gdate_with_zone (&start_date, start, timezone);
	if (priv->current_view_type == GNOME_CAL_MONTH_VIEW) {
		EWeekView *week_view = E_WEEK_VIEW (priv->views[priv->current_view_type]);

		if (week_start_day == 0
		    && (!week_view->multi_week_view || week_view->compress_weekend))
			g_date_add_days (&start_date, 1);
	}
	time_to_gdate_with_zone (&end_date, end, timezone);
	g_date_subtract_days (&end_date, 1);

	e_calendar_item_set_selection (priv->date_navigator->calitem,
				       &start_date, &end_date);
}

static void
gnome_calendar_on_date_navigator_selection_changed (ECalendarItem *calitem, GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	GnomeCalendarViewType view_type;
	ECalendarView *view;
	ECalModel *model;
	GDate start_date, end_date, new_start_date, new_end_date;
	gint new_days_shown;
	gint week_start_day;
	gboolean starts_on_week_start_day;
	time_t new_time, start, end;
	struct icaltimetype tt;
	icaltimezone *timezone;

	priv = gcal->priv;

	starts_on_week_start_day = FALSE;

	view_type = gnome_calendar_get_view (gcal);
	model = gnome_calendar_get_calendar_model (gcal);
	timezone = e_cal_model_get_timezone (model);
	week_start_day = e_cal_model_get_week_start_day (model);
	e_cal_model_get_time_range (model, &start, &end);

	time_to_gdate_with_zone (&start_date, start, timezone);
	if (view_type == GNOME_CAL_MONTH_VIEW) {
		ECalendarView *calendar_view;
		EWeekView *week_view;

		calendar_view = gnome_calendar_get_calendar_view (gcal, view_type);
		week_view = E_WEEK_VIEW (calendar_view);

		if (week_start_day == 0 && (!week_view->multi_week_view || week_view->compress_weekend))
			g_date_add_days (&start_date, 1);
	}
	time_to_gdate_with_zone (&end_date, end, timezone);
	g_date_subtract_days (&end_date, 1);

	e_calendar_item_get_selection (calitem, &new_start_date, &new_end_date);

	/* If the selection hasn't changed just return. */
	if (!g_date_compare (&start_date, &new_start_date)
	    && !g_date_compare (&end_date, &new_end_date)) {
		return;
	}

	new_days_shown = g_date_get_julian (&new_end_date) - g_date_get_julian (&new_start_date) + 1;

	/* If a complete week is selected we show the Week view.
	   Note that if weekends are compressed and the week start day is set
	   to Sunday we don't actually show complete weeks in the Week view,
	   so this may need tweaking. */
	if (g_date_get_weekday (&new_start_date) % 7 == week_start_day)
		starts_on_week_start_day = TRUE;

	/* Update selection to be in the new time range */
	tt = icaltime_null_time ();
	tt.year = g_date_get_year (&new_start_date);
	tt.month  = g_date_get_month (&new_start_date);
	tt.day = g_date_get_day (&new_start_date);
	new_time = icaltime_as_timet_with_zone (tt, timezone);

	/* Switch views as appropriate, and change the number of days or weeks
	   shown. */
	if (new_days_shown > 9) {
		/* FIXME Gross hack so that the view times are updated properly */
		priv->range_selected = TRUE;

		if (priv->current_view_type != GNOME_CAL_LIST_VIEW) {
			view = gnome_calendar_get_calendar_view (
				gcal, GNOME_CAL_MONTH_VIEW);
			e_week_view_set_weeks_shown (
				E_WEEK_VIEW (view), (new_days_shown + 6) / 7);
			view_type = GNOME_CAL_MONTH_VIEW;
		} else
			view_type = GNOME_CAL_LIST_VIEW;
	} else if (new_days_shown == 7 && starts_on_week_start_day) {
		view_type = GNOME_CAL_WEEK_VIEW;
	} else {
		view = gnome_calendar_get_calendar_view (
			gcal, GNOME_CAL_DAY_VIEW);
		e_day_view_set_days_shown (E_DAY_VIEW (view), new_days_shown);

		if (new_days_shown == 5 && starts_on_week_start_day
		    && priv->current_view_type == GNOME_CAL_WORK_WEEK_VIEW)
			view_type = GNOME_CAL_WORK_WEEK_VIEW;
		else
			view_type = GNOME_CAL_DAY_VIEW;
	}

	/* Make the views display things properly */
	update_view_times (gcal, new_time);
	set_view (gcal, view_type, TRUE);

	gnome_calendar_notify_dates_shown_changed (gcal);
}

static gboolean
gnome_calendar_hpane_resized (GtkWidget *w, GdkEventButton *e, GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	ECalendarView *view;
	gint times_width;

	priv = gcal->priv;

	if (priv->current_view_type == GNOME_CAL_MONTH_VIEW && !priv->range_selected) {
		priv->hpane_pos_month_view = gtk_paned_get_position (GTK_PANED (priv->hpane));
		calendar_config_set_month_hpane_pos (priv->hpane_pos_month_view);
	} else {
		priv->hpane_pos = gtk_paned_get_position (GTK_PANED (priv->hpane));
		calendar_config_set_hpane_pos (priv->hpane_pos);
	}

	/* adjust the size of the EDayView's time column */
	view = gnome_calendar_get_calendar_view (gcal, GNOME_CAL_DAY_VIEW);
	times_width = e_day_view_time_item_get_column_width (
		E_DAY_VIEW_TIME_ITEM (E_DAY_VIEW (view)->time_canvas_item));
	if (times_width < priv->hpane_pos - 20)
		gtk_widget_set_size_request (E_DAY_VIEW (view)->time_canvas, times_width, -1);
	else
		gtk_widget_set_size_request (E_DAY_VIEW (view)->time_canvas, priv->hpane_pos - 20, -1);

	return FALSE;
}
void
gnome_calendar_cut_clipboard (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	e_calendar_view_cut_clipboard (view);
}

void
gnome_calendar_copy_clipboard (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	e_calendar_view_copy_clipboard (view);
}

void
gnome_calendar_paste_clipboard (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	e_calendar_view_paste_clipboard (view);
}

static void
gnome_calendar_notify_dates_shown_changed (GnomeCalendar *gcal)
{
	GnomeCalendarPrivate *priv;
	time_t start_time, end_time;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	/* If no time range is set yet, just return. */
	if (!gnome_calendar_get_visible_time_range (gcal, &start_time,
						    &end_time))
		return;

	/* We check if the visible date range has changed, and only emit the
	   signal if it has. (This makes sure we only change the folder title
	   bar label in the shell when we need to.) */
	if (priv->visible_start != start_time
	    || priv->visible_end != end_time) {
		priv->visible_start = start_time;
		priv->visible_end = end_time;

		g_signal_emit (gcal, signals[DATES_SHOWN_CHANGED], 0);
	}
	update_memo_view (gcal);
}

/* Returns the number of selected events (0 or 1 at present). */
gint
gnome_calendar_get_num_events_selected (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;
	gint retval = 0;

	g_return_val_if_fail (GNOME_IS_CALENDAR (gcal), 0);

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	if (E_IS_DAY_VIEW (view))
		retval = e_day_view_get_num_events_selected (E_DAY_VIEW (view));
	else
		retval = e_week_view_get_num_events_selected (E_WEEK_VIEW (view));

	return retval;
}

void
gnome_calendar_delete_selection (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	e_calendar_view_delete_selected_events (view);
}

void
gnome_calendar_delete_selected_occurrence (GnomeCalendar *gcal)
{
	GnomeCalendarViewType view_type;
	ECalendarView *view;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	view_type = gnome_calendar_get_view (gcal);
	view = gnome_calendar_get_calendar_view (gcal, view_type);

	e_calendar_view_delete_selected_occurrence (view);
}

static gboolean
check_instance_cb (ECalComponent *comp,
		   time_t instance_start,
		   time_t instance_end,
		   gpointer data)
{
	gboolean *remove = data;

	*remove = FALSE;

	return FALSE;
}

void
gnome_calendar_purge (GnomeCalendar *gcal, time_t older_than)
{
	GnomeCalendarPrivate *priv;
	gchar *sexp, *start, *end;
	GList *l;

	g_return_if_fail (GNOME_IS_CALENDAR (gcal));

	priv = gcal->priv;

	start = isodate_from_time_t (0);
	end = isodate_from_time_t (older_than);
	sexp = g_strdup_printf ("(occur-in-time-range? (make-time \"%s\")"
				"                      (make-time \"%s\"))",
				start, end);

	/*e_calendar_view_set_status_message (E_CALENDAR_VIEW (priv->week_view), _("Purging"), -1);  KILL-BONOBO */

	/* FIXME Confirm expunge */
	for (l = priv->clients_list; l != NULL; l = l->next) {
		ECal *client = l->data;
		GList *objects, *m;
		gboolean read_only;

		if (!e_cal_is_read_only (client, &read_only, NULL) || read_only)
			continue;

		if (!e_cal_get_object_list (client, sexp, &objects, NULL)) {
			g_warning (G_STRLOC ": Could not get the objects");

			continue;
		}

		for (m = objects; m; m = m->next) {
			gboolean remove = TRUE;

			/* FIXME write occur-before and occur-after
			 * sexp funcs so we don't have to use the max
			 * gint */
			if (!e_cal_get_static_capability (client, CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER))
				e_cal_generate_instances_for_object (client, m->data,
							     older_than, G_MAXINT32,
							     (ECalRecurInstanceFn) check_instance_cb,
							     &remove);

			/* FIXME Better error handling */
			if (remove) {
				const gchar *uid = icalcomponent_get_uid (m->data);
				GError *error = NULL;

				if (e_cal_util_component_is_instance (m->data) || e_cal_util_component_has_recurrences (m->data)) {
					gchar *rid = NULL;
					struct icaltimetype recur_id = icalcomponent_get_recurrenceid (m->data);

					if (!icaltime_is_null_time (recur_id) )
						rid = icaltime_as_ical_string_r (recur_id);

					e_cal_remove_object_with_mod (client, uid, rid, CALOBJ_MOD_ALL, &error);
					g_free (rid);
				} else {
					e_cal_remove_object (client, uid, &error);
				}

				if (error) {
					g_warning ("Unable to purge events %s \n", error->message);
					g_error_free (error);
				}
			}
		}

		g_list_foreach (objects, (GFunc) icalcomponent_free, NULL);
		g_list_free (objects);
	}

	/* e_calendar_view_set_status_message (E_CALENDAR_VIEW (priv->week_view), NULL, -1);  KILL-BONOBO */

	g_free (sexp);
	g_free (start);
	g_free (end);

}

void
gnome_calendar_edit_appointment (GnomeCalendar *gcal,
				 const gchar * src_uid,
				 const gchar * comp_uid,
				 const gchar * comp_rid)
{
	ECal *client = NULL;
	GList *l;
	icalcomponent* icalcomp = NULL;
	icalproperty *attendee_prop = NULL;

	if (!src_uid || !comp_uid)
		return;

	for (l = gcal->priv->clients_list; l != NULL; l = l->next) {
		ESource *client_src;

		client = l->data;
		client_src = e_cal_get_source (client);

		if (!strcmp (src_uid, e_source_peek_uid (client_src)))
			break;
	}

	if (!client)
		return;

	e_cal_get_object (client, comp_uid, comp_rid, &icalcomp, NULL);

	if (!icalcomp)
		return;

	attendee_prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
	e_calendar_view_edit_appointment (gcal->priv->views[gcal->priv->current_view_type],
					  client, icalcomp, attendee_prop ? TRUE:FALSE);
	icalcomponent_free (icalcomp);
}
