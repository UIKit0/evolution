/*
 * e-cal-shell-view.c
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
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-shell-view-private.h"

#include "calendar/gui/calendar-view.h"

G_DEFINE_DYNAMIC_TYPE (
	ECalShellView,
	e_cal_shell_view,
	E_TYPE_SHELL_VIEW)

static void
cal_shell_view_add_action_button (GtkBox *box,
                                  GtkAction *action)
{
	GtkWidget *button, *icon;

	button = gtk_button_new ();
	icon = gtk_action_create_icon (action, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON (button), icon);
	gtk_box_pack_start (box, button, FALSE, FALSE, 0);
	gtk_widget_show (button);

	g_object_bind_property (
		action, "visible",
		button, "visible",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		action, "sensitive",
		button, "sensitive",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		action, "tooltip",
		button, "tooltip-text",
		G_BINDING_SYNC_CREATE);

	g_signal_connect_swapped (
		button, "clicked",
		G_CALLBACK (gtk_action_activate), action);
}

static void
cal_shell_view_prepare_for_quit_cb (EShell *shell,
                                    EActivity *activity,
                                    ECalShellView *cal_shell_view)
{
	g_return_if_fail (E_IS_CAL_SHELL_VIEW (cal_shell_view));

	/* Stop running searches, if any; the activity tight
	 * on the search prevents application from quitting. */
	e_cal_shell_view_search_stop (cal_shell_view);
}

static void
cal_shell_view_dispose (GObject *object)
{
	e_cal_shell_view_private_dispose (E_CAL_SHELL_VIEW (object));

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_shell_view_parent_class)->dispose (object);
}

static void
cal_shell_view_finalize (GObject *object)
{
	e_cal_shell_view_private_finalize (E_CAL_SHELL_VIEW (object));

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_shell_view_parent_class)->finalize (object);
}

static void
cal_shell_view_constructed (GObject *object)
{
	EShell *shell;
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellSearchbar *searchbar;
	ECalShellView *cal_shell_view;
	ECalShellContent *cal_shell_content;
	GtkWidget *container;
	GtkWidget *widget;
	gulong handler_id;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_cal_shell_view_parent_class)->constructed (object);

	cal_shell_view = E_CAL_SHELL_VIEW (object);
	e_cal_shell_view_private_constructed (cal_shell_view);

	shell_view = E_SHELL_VIEW (cal_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);

	cal_shell_content = cal_shell_view->priv->cal_shell_content;
	searchbar = e_cal_shell_content_get_searchbar (cal_shell_content);
	container = e_shell_searchbar_get_search_box (searchbar);

	widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	cal_shell_view_add_action_button (
		GTK_BOX (widget), ACTION (CALENDAR_SEARCH_PREV));
	cal_shell_view_add_action_button (
		GTK_BOX (widget), ACTION (CALENDAR_SEARCH_NEXT));
	cal_shell_view_add_action_button (
		GTK_BOX (widget), ACTION (CALENDAR_SEARCH_STOP));
	gtk_container_add (GTK_CONTAINER (container), widget);
	gtk_widget_show (widget);

	handler_id = g_signal_connect (
		shell, "prepare-for-quit",
		G_CALLBACK (cal_shell_view_prepare_for_quit_cb),
		cal_shell_view);

	cal_shell_view->priv->shell = g_object_ref (shell);
	cal_shell_view->priv->prepare_for_quit_handler_id = handler_id;
}

static void
cal_shell_view_execute_search (EShellView *shell_view)
{
	ECalShellContent *cal_shell_content;
	ECalShellSidebar *cal_shell_sidebar;
	EShellWindow *shell_window;
	EShellContent *shell_content;
	EShellSidebar *shell_sidebar;
	EShellSearchbar *searchbar;
	EActionComboBox *combo_box;
	GnomeCalendar *calendar;
	ECalendar *date_navigator;
	ECalModel *model;
	GtkRadioAction *action;
	icaltimezone *timezone;
	const gchar *default_tzloc = NULL;
	struct icaltimetype current_time;
	time_t start_range;
	time_t end_range;
	time_t now_time;
	gboolean range_search;
	gchar *start, *end;
	gchar *query;
	gchar *temp;
	gint value;

	e_cal_shell_view_search_stop (E_CAL_SHELL_VIEW (shell_view));

	shell_window = e_shell_view_get_shell_window (shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);

	cal_shell_content = E_CAL_SHELL_CONTENT (shell_content);
	cal_shell_sidebar = E_CAL_SHELL_SIDEBAR (shell_sidebar);

	searchbar = e_cal_shell_content_get_searchbar (cal_shell_content);

	calendar = e_cal_shell_content_get_calendar (cal_shell_content);
	model = gnome_calendar_get_model (calendar);
	timezone = e_cal_model_get_timezone (model);
	current_time = icaltime_current_time_with_zone (timezone);
	now_time = time_day_begin (icaltime_as_timet (current_time));

	if (timezone && timezone != icaltimezone_get_utc_timezone ())
		default_tzloc = icaltimezone_get_location (timezone);
	if (!default_tzloc)
		default_tzloc = "";

	action = GTK_RADIO_ACTION (ACTION (CALENDAR_SEARCH_ANY_FIELD_CONTAINS));
	value = gtk_radio_action_get_current_value (action);

	if (value == CALENDAR_SEARCH_ADVANCED) {
		query = e_shell_view_get_search_query (shell_view);

		if (!query)
			query = g_strdup ("");
	} else {
		const gchar *format;
		const gchar *text;
		GString *string;

		text = e_shell_searchbar_get_search_text (searchbar);

		if (text == NULL || *text == '\0') {
			text = "";
			value = CALENDAR_SEARCH_SUMMARY_CONTAINS;
		}

		switch (value) {
			default:
				text = "";
				/* fall through */

			case CALENDAR_SEARCH_SUMMARY_CONTAINS:
				format = "(contains? \"summary\" %s)";
				break;

			case CALENDAR_SEARCH_DESCRIPTION_CONTAINS:
				format = "(contains? \"description\" %s)";
				break;

			case CALENDAR_SEARCH_ANY_FIELD_CONTAINS:
				format = "(contains? \"any\" %s)";
				break;
		}

		/* Build the query. */
		string = g_string_new ("");
		e_sexp_encode_string (string, text);
		query = g_strdup_printf (format, string->str);
		g_string_free (string, TRUE);
	}

	range_search = FALSE;
	start_range = end_range = 0;

	/* Apply selected filter. */
	combo_box = e_shell_searchbar_get_filter_combo_box (searchbar);
	value = e_action_combo_box_get_current_value (combo_box);
	switch (value) {
		case CALENDAR_FILTER_ANY_CATEGORY:
			break;

		case CALENDAR_FILTER_UNMATCHED:
			temp = g_strdup_printf (
				"(and (has-categories? #f) %s)", query);
			g_free (query);
			query = temp;
			break;

		case CALENDAR_FILTER_ACTIVE_APPOINTMENTS:
			/* Show a year's worth of appointments. */
			start_range = now_time;
			end_range = time_day_end (time_add_day (start_range, 365));
			start = isodate_from_time_t (start_range);
			end = isodate_from_time_t (end_range);

			temp = g_strdup_printf (
				"(and %s (occur-in-time-range? "
				"(make-time \"%s\") "
				"(make-time \"%s\") \"%s\"))",
				query, start, end, default_tzloc);
			g_free (query);
			query = temp;

			range_search = TRUE;
			break;

		case CALENDAR_FILTER_NEXT_7_DAYS_APPOINTMENTS:
			start_range = now_time;
			end_range = time_day_end (time_add_day (start_range, 7));
			start = isodate_from_time_t (start_range);
			end = isodate_from_time_t (end_range);

			temp = g_strdup_printf (
				"(and %s (occur-in-time-range? "
				"(make-time \"%s\") "
				"(make-time \"%s\") \"%s\"))",
				query, start, end, default_tzloc);
			g_free (query);
			query = temp;

			range_search = TRUE;
			break;

		case CALENDAR_FILTER_OCCURS_LESS_THAN_5_TIMES:
			temp = g_strdup_printf (
				"(and %s (< (occurrences-count?) 5))", query);
			g_free (query);
			query = temp;
			break;

		default:
		{
			GList *categories;
			const gchar *category_name;

			categories = e_util_get_searchable_categories ();
			category_name = g_list_nth_data (categories, value);
			g_list_free (categories);

			temp = g_strdup_printf (
				"(and (has-categories? \"%s\") %s)",
				category_name, query);
			g_free (query);
			query = temp;
			break;
		}
	}

	date_navigator = e_cal_shell_sidebar_get_date_navigator (cal_shell_sidebar);

	if (range_search) {
		/* Switch to list view and hide the date navigator. */
		action = GTK_RADIO_ACTION (ACTION (CALENDAR_VIEW_LIST));
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
		gtk_widget_hide (GTK_WIDGET (date_navigator));
	} else {
		/* Ensure the date navigator is visible. */
		gtk_widget_show (GTK_WIDGET (date_navigator));
	}

	/* Submit the query. */
	gnome_calendar_set_search_query (
		calendar, query, range_search, start_range, end_range);
	g_free (query);

	/* Update actions so Find Prev/Next/Stop
	 * buttons will be sensitive as expected. */
	e_shell_view_update_actions (shell_view);
}

static void
cal_shell_view_update_actions (EShellView *shell_view)
{
	ECalShellViewPrivate *priv;
	ECalShellContent *cal_shell_content;
	EShellContent *shell_content;
	EShellSidebar *shell_sidebar;
	EShellWindow *shell_window;
	EShell *shell;
	ESource *source;
	ESourceRegistry *registry;
	GnomeCalendar *calendar;
	ECalendarView *cal_view;
	EMemoTable *memo_table;
	ETaskTable *task_table;
	ECalModel *model;
	GtkAction *action;
	const gchar *model_sexp;
	gboolean is_searching;
	gboolean sensitive;
	guint32 state;

	/* Be descriptive. */
	gboolean any_events_selected;
	gboolean has_mail_identity = FALSE;
	gboolean has_primary_source;
	gboolean primary_source_is_writable;
	gboolean primary_source_is_removable;
	gboolean primary_source_is_remote_deletable;
	gboolean primary_source_in_collection;
	gboolean multiple_events_selected;
	gboolean selection_is_editable;
	gboolean selection_is_instance;
	gboolean selection_is_meeting;
	gboolean selection_is_recurring;
	gboolean selection_can_delegate;
	gboolean single_event_selected;
	gboolean refresh_supported;

	/* Chain up to parent's update_actions() method. */
	E_SHELL_VIEW_CLASS (e_cal_shell_view_parent_class)->
		update_actions (shell_view);

	priv = E_CAL_SHELL_VIEW_GET_PRIVATE (shell_view);

	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);

	registry = e_shell_get_registry (shell);
	source = e_source_registry_ref_default_mail_identity (registry);
	has_mail_identity = (source != NULL);
	if (source != NULL) {
		has_mail_identity = TRUE;
		g_object_unref (source);
	}

	cal_shell_content = priv->cal_shell_content;
	calendar = e_cal_shell_content_get_calendar (cal_shell_content);
	cal_view = gnome_calendar_get_calendar_view (calendar, gnome_calendar_get_view (calendar));
	memo_table = e_cal_shell_content_get_memo_table (cal_shell_content);
	task_table = e_cal_shell_content_get_task_table (cal_shell_content);
	model = gnome_calendar_get_model (calendar);
	model_sexp = e_cal_model_get_search_query (model);
	is_searching = model_sexp && *model_sexp &&
		g_strcmp0 (model_sexp, "#t") != 0 &&
		g_strcmp0 (model_sexp, "(contains? \"summary\"  \"\")") != 0;

	shell_content = e_shell_view_get_shell_content (shell_view);
	state = e_shell_content_check_state (shell_content);

	single_event_selected =
		(state & E_CAL_SHELL_CONTENT_SELECTION_SINGLE);
	multiple_events_selected =
		(state & E_CAL_SHELL_CONTENT_SELECTION_MULTIPLE);
	selection_is_editable =
		(state & E_CAL_SHELL_CONTENT_SELECTION_IS_EDITABLE);
	selection_is_instance =
		(state & E_CAL_SHELL_CONTENT_SELECTION_IS_INSTANCE);
	selection_is_meeting =
		(state & E_CAL_SHELL_CONTENT_SELECTION_IS_MEETING);
	selection_is_recurring =
		(state & E_CAL_SHELL_CONTENT_SELECTION_IS_RECURRING);
	selection_can_delegate =
		(state & E_CAL_SHELL_CONTENT_SELECTION_CAN_DELEGATE);

	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	state = e_shell_sidebar_check_state (shell_sidebar);

	has_primary_source =
		(state & E_CAL_SHELL_SIDEBAR_HAS_PRIMARY_SOURCE);
	primary_source_is_writable =
		(state & E_CAL_SHELL_SIDEBAR_PRIMARY_SOURCE_IS_WRITABLE);
	primary_source_is_removable =
		(state & E_CAL_SHELL_SIDEBAR_PRIMARY_SOURCE_IS_REMOVABLE);
	primary_source_is_remote_deletable =
		(state & E_CAL_SHELL_SIDEBAR_PRIMARY_SOURCE_IS_REMOTE_DELETABLE);
	primary_source_in_collection =
		(state & E_CAL_SHELL_SIDEBAR_PRIMARY_SOURCE_IN_COLLECTION);
	refresh_supported =
		(state & E_CAL_SHELL_SIDEBAR_SOURCE_SUPPORTS_REFRESH);

	any_events_selected =
		(single_event_selected || multiple_events_selected);

	action = ACTION (CALENDAR_COPY);
	sensitive = has_primary_source;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CALENDAR_DELETE);
	sensitive =
		primary_source_is_removable ||
		primary_source_is_remote_deletable;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CALENDAR_PROPERTIES);
	sensitive = primary_source_is_writable;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CALENDAR_REFRESH);
	sensitive = refresh_supported;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CALENDAR_RENAME);
	sensitive =
		primary_source_is_writable &&
		!primary_source_in_collection;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (CALENDAR_SEARCH_PREV);
	gtk_action_set_sensitive (action, is_searching);

	action = ACTION (CALENDAR_SEARCH_NEXT);
	gtk_action_set_sensitive (action, is_searching);

	action = ACTION (CALENDAR_SEARCH_STOP);
	sensitive = is_searching && priv->searching_activity != NULL;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_DELEGATE);
	sensitive =
		single_event_selected &&
		selection_is_editable &&
		selection_can_delegate &&
		selection_is_meeting;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_DELETE);
	sensitive =
		any_events_selected &&
		selection_is_editable &&
		!selection_is_recurring;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_DELETE_OCCURRENCE);
	sensitive =
		any_events_selected &&
		selection_is_editable &&
		selection_is_recurring;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_DELETE_OCCURRENCE_ALL);
	sensitive =
		any_events_selected &&
		selection_is_editable &&
		selection_is_recurring;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_FORWARD);
	sensitive = single_event_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_OCCURRENCE_MOVABLE);
	sensitive =
		single_event_selected &&
		selection_is_editable &&
		selection_is_recurring &&
		selection_is_instance;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_OPEN);
	sensitive = single_event_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_PRINT);
	sensitive = single_event_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_SAVE_AS);
	sensitive = single_event_selected;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_SCHEDULE);
	sensitive =
		single_event_selected &&
		selection_is_editable &&
		!selection_is_meeting;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_SCHEDULE_APPOINTMENT);
	sensitive =
		single_event_selected &&
		selection_is_editable &&
		selection_is_meeting;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_REPLY);
	sensitive = single_event_selected && selection_is_meeting;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_REPLY_ALL);
	sensitive = single_event_selected && selection_is_meeting;
	gtk_action_set_sensitive (action, sensitive);

	action = ACTION (EVENT_MEETING_NEW);
	gtk_action_set_visible (action, has_mail_identity);

	if ((cal_view && e_calendar_view_is_editing (cal_view)) ||
	    e_table_is_editing (E_TABLE (memo_table)) ||
	    e_table_is_editing (E_TABLE (task_table))) {
		EFocusTracker *focus_tracker;

		/* disable all clipboard actions, if any of the views is in editing mode */
		focus_tracker = e_shell_window_get_focus_tracker (shell_window);

		action = e_focus_tracker_get_cut_clipboard_action (focus_tracker);
		if (action)
			gtk_action_set_sensitive (action, FALSE);

		action = e_focus_tracker_get_copy_clipboard_action (focus_tracker);
		if (action)
			gtk_action_set_sensitive (action, FALSE);

		action = e_focus_tracker_get_paste_clipboard_action (focus_tracker);
		if (action)
			gtk_action_set_sensitive (action, FALSE);

		action = e_focus_tracker_get_delete_selection_action (focus_tracker);
		if (action)
			gtk_action_set_sensitive (action, FALSE);
	}
}

static void
e_cal_shell_view_class_init (ECalShellViewClass *class)
{
	GObjectClass *object_class;
	EShellViewClass *shell_view_class;

	g_type_class_add_private (class, sizeof (ECalShellViewPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = cal_shell_view_dispose;
	object_class->finalize = cal_shell_view_finalize;
	object_class->constructed = cal_shell_view_constructed;

	shell_view_class = E_SHELL_VIEW_CLASS (class);
	shell_view_class->label = _("Calendar");
	shell_view_class->icon_name = "x-office-calendar";
	shell_view_class->ui_definition = "evolution-calendars.ui";
	shell_view_class->ui_manager_id = "org.gnome.evolution.calendars";
	shell_view_class->search_options = "/calendar-search-options";
	shell_view_class->search_rules = "caltypes.xml";
	shell_view_class->new_shell_content = e_cal_shell_content_new;
	shell_view_class->new_shell_sidebar = e_cal_shell_sidebar_new;
	shell_view_class->execute_search = cal_shell_view_execute_search;
	shell_view_class->update_actions = cal_shell_view_update_actions;

	/* Ensure the GalView types we need are registered. */
	g_type_ensure (GAL_TYPE_VIEW_CALENDAR_DAY);
	g_type_ensure (GAL_TYPE_VIEW_CALENDAR_WORK_WEEK);
	g_type_ensure (GAL_TYPE_VIEW_CALENDAR_WEEK);
	g_type_ensure (GAL_TYPE_VIEW_CALENDAR_MONTH);
	g_type_ensure (GAL_TYPE_VIEW_ETABLE);
}

static void
e_cal_shell_view_class_finalize (ECalShellViewClass *class)
{
}

static void
e_cal_shell_view_init (ECalShellView *cal_shell_view)
{
	cal_shell_view->priv =
		E_CAL_SHELL_VIEW_GET_PRIVATE (cal_shell_view);

	e_cal_shell_view_private_init (cal_shell_view);
}

void
e_cal_shell_view_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_cal_shell_view_register_type (type_module);
}

