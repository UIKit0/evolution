/*
 * e-task-shell-view-actions.c
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

#include "e-task-shell-view-private.h"

static void
action_gal_save_custom_view_cb (GtkAction *action,
                                ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	EShellView *shell_view;
	GalViewInstance *view_instance;

	/* All shell views respond to the activation of this action,
	 * which is defined by EShellWindow.  But only the currently
	 * active shell view proceeds with saving the custom view. */
	shell_view = E_SHELL_VIEW (task_shell_view);
	if (!e_shell_view_is_active (shell_view))
		return;

	task_shell_content = task_shell_view->priv->task_shell_content;
	view_instance = e_task_shell_content_get_view_instance (task_shell_content);
	gal_view_instance_save_as (view_instance);
}

static void
action_search_filter_cb (GtkRadioAction *action,
                         GtkRadioAction *current,
                         EShellView *shell_view)
{
	e_shell_view_execute_search (shell_view);
}

static void
action_task_assign_cb (GtkAction *action,
                       ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	GSList *list;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;
	g_slist_free (list);

	/* XXX We only open the first selected task. */
	e_task_shell_view_open_task (task_shell_view, comp_data);

	/* FIXME Need to actually assign the task. */
}

static void
action_task_clipboard_copy_cb (GtkAction *action,
                               ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	e_calendar_table_copy_clipboard (task_table);
}

static void
action_task_clipboard_cut_cb (GtkAction *action,
                              ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	e_calendar_table_cut_clipboard (task_table);
}

static void
action_task_clipboard_paste_cb (GtkAction *action,
                                ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	e_calendar_table_paste_clipboard (task_table);
}

static void
action_task_delete_cb (GtkAction *action,
                       ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalComponentPreview *task_preview;
	ECalendarTable *task_table;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);
	task_preview = e_task_shell_content_get_task_preview (task_shell_content);

	e_task_shell_view_set_status_message (
		task_shell_view, _("Deleting selected tasks..."), -1.0);
	e_calendar_table_delete_selected (task_table);
	e_task_shell_view_set_status_message (task_shell_view, NULL, -1.0);

	e_web_view_clear (E_WEB_VIEW (task_preview));
}

static void
action_task_forward_cb (GtkAction *action,
                        ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	ECalComponent *comp;
	icalcomponent *clone;
	GSList *list;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;
	g_slist_free (list);

	/* XXX We only forward the first selected task. */
	comp = e_cal_component_new ();
	clone = icalcomponent_new_clone (comp_data->icalcomp);
	e_cal_component_set_icalcomponent (comp, clone);
	itip_send_comp (
		E_CAL_COMPONENT_METHOD_PUBLISH, comp,
		comp_data->client, NULL, NULL, NULL, TRUE, FALSE);
	g_object_unref (comp);
}

static void
action_task_list_copy_cb (GtkAction *action,
                          ETaskShellView *task_shell_view)
{
	ETaskShellSidebar *task_shell_sidebar;
	EShellWindow *shell_window;
	EShellView *shell_view;
	ESourceSelector *selector;
	ESource *source;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	task_shell_sidebar = task_shell_view->priv->task_shell_sidebar;
	selector = e_task_shell_sidebar_get_selector (task_shell_sidebar);
	source = e_source_selector_peek_primary_selection (selector);
	g_return_if_fail (E_IS_SOURCE (source));

	copy_source_dialog (
		GTK_WINDOW (shell_window),
		source, E_CAL_SOURCE_TYPE_TODO);
}

static void
action_task_list_delete_cb (GtkAction *action,
                            ETaskShellView *task_shell_view)
{
	ETaskShellBackend *task_shell_backend;
	ETaskShellContent *task_shell_content;
	ETaskShellSidebar *task_shell_sidebar;
	EShellWindow *shell_window;
	EShellView *shell_view;
	ECalendarTable *task_table;
	ECal *client;
	ECalModel *model;
	ESourceSelector *selector;
	ESourceGroup *source_group;
	ESourceList *source_list;
	ESource *source;
	gint response;
	gchar *uri;
	GError *error = NULL;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	task_shell_backend = task_shell_view->priv->task_shell_backend;
	source_list = e_task_shell_backend_get_source_list (task_shell_backend);

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);
	model = e_calendar_table_get_model (task_table);

	task_shell_sidebar = task_shell_view->priv->task_shell_sidebar;
	selector = e_task_shell_sidebar_get_selector (task_shell_sidebar);
	source = e_source_selector_peek_primary_selection (selector);
	g_return_if_fail (E_IS_SOURCE (source));

	/* Ask for confirmation. */
	response = e_error_run (
		GTK_WINDOW (shell_window),
		"calendar:prompt-delete-task-list",
		e_source_peek_name (source));
	if (response != GTK_RESPONSE_YES)
		return;

	uri = e_source_get_uri (source);
	client = e_cal_model_get_client_for_uri (model, uri);
	if (client == NULL)
		client = e_cal_new_from_uri (uri, E_CAL_SOURCE_TYPE_JOURNAL);
	g_free (uri);

	g_return_if_fail (client != NULL);

	if (!e_cal_remove (client, &error)) {
		g_warning ("%s", error->message);
		g_error_free (error);
		return;
	}

	if (e_source_selector_source_is_selected (selector, source)) {
		e_task_shell_sidebar_remove_source (
			task_shell_sidebar, source);
		e_source_selector_unselect_source (selector, source);
	}

	source_group = e_source_peek_group (source);
	e_source_group_remove_source (source_group, source);

	if (!e_source_list_sync (source_list, &error)) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
}

static void
action_task_list_new_cb (GtkAction *action,
                         ETaskShellView *task_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	calendar_setup_new_task_list (GTK_WINDOW (shell_window));
}

static void
action_task_list_print_cb (GtkAction *action,
                           ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ETable *table;
	GtkPrintOperationAction print_action;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);
	table = e_calendar_table_get_table (task_table);

	print_action = GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
	print_table (table, _("Print Tasks"), _("Tasks"), print_action);
}

static void
action_task_list_print_preview_cb (GtkAction *action,
                                   ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ETable *table;
	GtkPrintOperationAction print_action;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);
	table = e_calendar_table_get_table (task_table);

	print_action = GTK_PRINT_OPERATION_ACTION_PREVIEW;
	print_table (table, _("Print Tasks"), _("Tasks"), print_action);
}

static void
action_task_list_properties_cb (GtkAction *action,
                                ETaskShellView *task_shell_view)
{
	ETaskShellSidebar *task_shell_sidebar;
	EShellView *shell_view;
	EShellWindow *shell_window;
	ESource *source;
	ESourceSelector *selector;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	task_shell_sidebar = task_shell_view->priv->task_shell_sidebar;
	selector = e_task_shell_sidebar_get_selector (task_shell_sidebar);
	source = e_source_selector_peek_primary_selection (selector);
	g_return_if_fail (E_IS_SOURCE (source));

	calendar_setup_edit_task_list (GTK_WINDOW (shell_window), source);
}

static void
action_task_list_rename_cb (GtkAction *action,
                            ETaskShellView *task_shell_view)
{
	ETaskShellSidebar *task_shell_sidebar;
	ESourceSelector *selector;

	task_shell_sidebar = task_shell_view->priv->task_shell_sidebar;
	selector = e_task_shell_sidebar_get_selector (task_shell_sidebar);

	e_source_selector_edit_primary_selection (selector);
}

static void
action_task_list_select_one_cb (GtkAction *action,
                                ETaskShellView *task_shell_view)
{
	ETaskShellSidebar *task_shell_sidebar;
	ESourceSelector *selector;
	ESource *primary;
	GSList *list, *iter;

	/* XXX ESourceSelector should provide a function for this. */

	task_shell_sidebar = task_shell_view->priv->task_shell_sidebar;
	selector = e_task_shell_sidebar_get_selector (task_shell_sidebar);
	primary = e_source_selector_peek_primary_selection (selector);
	g_return_if_fail (primary != NULL);

	list = e_source_selector_get_selection (selector);
	for (iter = list; iter != NULL; iter = iter->next) {
		ESource *source = iter->data;

		if (source == primary)
			continue;

		e_source_selector_unselect_source (selector, source);
	}
	e_source_selector_free_selection (list);

	e_source_selector_select_source (selector, primary);
}

static void
action_task_mark_complete_cb (GtkAction *action,
                              ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModel *model;
	GSList *list, *iter;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);
	list = e_calendar_table_get_selected (task_table);
	model = e_calendar_table_get_model (task_table);

	for (iter = list; iter != NULL; iter = iter->next) {
		ECalModelComponent *comp_data = iter->data;
		e_cal_model_tasks_mark_comp_complete (
			E_CAL_MODEL_TASKS (model), comp_data);
	}

	g_slist_free (list);
}

static void
action_task_mark_incomplete_cb (GtkAction *action,
                                ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModel *model;
	GSList *list, *iter;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);
	list = e_calendar_table_get_selected (task_table);
	model = e_calendar_table_get_model (task_table);

	for (iter = list; iter != NULL; iter = iter->next) {
		ECalModelComponent *comp_data = iter->data;
		e_cal_model_tasks_mark_comp_incomplete (
			E_CAL_MODEL_TASKS (model), comp_data);
	}

	g_slist_free (list);
}

static void
action_task_new_cb (GtkAction *action,
                    ETaskShellView *task_shell_view)
{
	EShell *shell;
	EShellView *shell_view;
	EShellWindow *shell_window;
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	ECal *client;
	ECalComponent *comp;
	CompEditor *editor;
	GSList *list;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;
	g_slist_free (list);

	client = comp_data->client;
	editor = task_editor_new (client, shell, COMP_EDITOR_NEW_ITEM);
	comp = cal_comp_task_new_with_defaults (client);
	comp_editor_edit_comp (editor, comp);

	gtk_window_present (GTK_WINDOW (editor));

	g_object_unref (comp);
	g_object_unref (client);
}

static void
action_task_open_cb (GtkAction *action,
                     ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	GSList *list;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;
	g_slist_free (list);

	/* XXX We only open the first selected task. */
	e_task_shell_view_open_task (task_shell_view, comp_data);
}

static void
action_task_open_url_cb (GtkAction *action,
                         ETaskShellView *task_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	icalproperty *prop;
	const gchar *uri;
	GSList *list;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;

	/* XXX We only open the URI of the first selected task. */
	prop = icalcomponent_get_first_property (
		comp_data->icalcomp, ICAL_URL_PROPERTY);
	g_return_if_fail (prop == NULL);

	uri = icalproperty_get_url (prop);
	e_show_uri (GTK_WINDOW (shell_window), uri);
}

static void
action_task_preview_cb (GtkToggleAction *action,
                        ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	gboolean visible;

	task_shell_content = task_shell_view->priv->task_shell_content;
	visible = gtk_toggle_action_get_active (action);
	e_task_shell_content_set_preview_visible (task_shell_content, visible);
}

static void
action_task_print_cb (GtkAction *action,
                      ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	ECalComponent *comp;
	icalcomponent *clone;
	GtkPrintOperationAction print_action;
	GSList *list;

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;
	g_slist_free (list);

	/* XXX We only print the first selected task. */
	comp = e_cal_component_new ();
	clone = icalcomponent_new_clone (comp_data->icalcomp);
	print_action = GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
	e_cal_component_set_icalcomponent (comp, clone);
	print_comp (comp, comp_data->client, print_action);
	g_object_unref (comp);
}

static void
action_task_purge_cb (GtkAction *action,
                      ETaskShellView *task_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	GtkWidget *dialog;
	GtkWidget *widget;
	gboolean active;
	gint response;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	if (!e_task_shell_view_get_confirm_purge (task_shell_view))
		goto purge;

	/* XXX This needs reworked.  The dialog looks like ass. */

	dialog = gtk_message_dialog_new (
		GTK_WINDOW (shell_window),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_WARNING,
		GTK_BUTTONS_YES_NO,
		"%s", _("This operation will permanently erase all tasks "
		"marked as completed. If you continue, you will not be able "
		"to recover these tasks.\n\nReally erase these tasks?"));

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_NO);

	widget = gtk_check_button_new_with_label (_("Do not ask me again"));
	gtk_box_pack_start (
		GTK_BOX (GTK_DIALOG (dialog)->vbox), widget, TRUE, TRUE, 6);
	gtk_widget_show (widget);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	gtk_widget_destroy (dialog);

	if (response != GTK_RESPONSE_YES)
		return;

	if (active)
		e_task_shell_view_set_confirm_purge (task_shell_view, FALSE);

purge:

	/* FIXME KILL-BONOBO */
        ;
}

static void
action_task_save_as_cb (GtkAction *action,
                        ETaskShellView *task_shell_view)
{
	EShell *shell;
	EShellView *shell_view;
	EShellWindow *shell_window;
	EShellBackend *shell_backend;
	ETaskShellContent *task_shell_content;
	ECalendarTable *task_table;
	ECalModelComponent *comp_data;
	EActivity *activity;
	GSList *list;
	GFile *file;
	gchar *string;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);
	shell_backend = e_shell_view_get_shell_backend (shell_view);
	shell = e_shell_window_get_shell (shell_window);

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_table = e_task_shell_content_get_task_table (task_shell_content);

	list = e_calendar_table_get_selected (task_table);
	g_return_if_fail (list != NULL);
	comp_data = list->data;
	g_slist_free (list);

	file = e_shell_run_save_dialog (
		shell, _("Save as iCalendar"), NULL, NULL, NULL);
	if (file == NULL)
		return;

	/* XXX We only save the first selected task. */
	string = e_cal_get_component_as_string (
		comp_data->client, comp_data->icalcomp);
	if (string == NULL) {
		g_warning ("Could not convert task to a string");
		g_object_unref (file);
		return;
	}

	/* XXX No callback means errors are discarded. */
	activity = e_file_replace_contents_async (
		file, string, strlen (string), NULL, FALSE,
		G_FILE_CREATE_NONE, (GAsyncReadyCallback) NULL, NULL);
	e_shell_backend_add_activity (shell_backend, activity);

	/* Free the string when the activity is finalized. */
	g_object_set_data_full (
		G_OBJECT (activity),
		"file-content", string,
		(GDestroyNotify) g_free);

	g_object_unref (file);
}

static void
action_task_view_cb (GtkRadioAction *action,
                     GtkRadioAction *current,
                     ETaskShellView *task_shell_view)
{
	ETaskShellContent *task_shell_content;
	GtkOrientable *orientable;
	GtkOrientation orientation;

	task_shell_content = task_shell_view->priv->task_shell_content;
	orientable = GTK_ORIENTABLE (task_shell_content);

	switch (gtk_radio_action_get_current_value (action)) {
		case 0:
			orientation = GTK_ORIENTATION_VERTICAL;
			break;
		case 1:
			orientation = GTK_ORIENTATION_HORIZONTAL;
			break;
		default:
			g_return_if_reached ();
	}

	gtk_orientable_set_orientation (orientable, orientation);
}

static GtkActionEntry task_entries[] = {

	{ "task-assign",
	  NULL,
	  N_("_Assign Task"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_assign_cb) },

	{ "task-clipboard-copy",
	  GTK_STOCK_COPY,
	  NULL,
	  NULL,
	  N_("Copy selected tasks"),
	  G_CALLBACK (action_task_clipboard_copy_cb) },

	{ "task-clipboard-cut",
	  GTK_STOCK_CUT,
	  NULL,
	  NULL,
	  N_("Cut selected tasks"),
	  G_CALLBACK (action_task_clipboard_cut_cb) },

	{ "task-clipboard-paste",
	  GTK_STOCK_PASTE,
	  NULL,
	  NULL,
	  N_("Paste tasks from the clipboard"),
	  G_CALLBACK (action_task_clipboard_paste_cb) },

	{ "task-delete",
	  GTK_STOCK_DELETE,
	  N_("_Delete Task"),
	  NULL,
	  N_("Delete selected tasks"),
	  G_CALLBACK (action_task_delete_cb) },

	{ "task-forward",
	  "mail-forward",
	  N_("_Forward as iCalendar..."),
	  "<Control>f",
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_forward_cb) },

	{ "task-list-copy",
	  GTK_STOCK_COPY,
	  N_("Copy..."),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_list_copy_cb) },

	{ "task-list-delete",
	  GTK_STOCK_DELETE,
	  N_("_Delete"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_list_delete_cb) },

	{ "task-list-new",
	  "stock_todo",
	  N_("_New Task List"),
	  NULL,
	  N_("Create a new task list"),
	  G_CALLBACK (action_task_list_new_cb) },

	{ "task-list-properties",
	  GTK_STOCK_PROPERTIES,
	  NULL,
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_list_properties_cb) },

	{ "task-list-rename",
	  NULL,
	  N_("_Rename..."),
	  "F2",
	  N_("Rename the selected task list"),
	  G_CALLBACK (action_task_list_rename_cb) },

	{ "task-list-select-one",
	  "stock_check-filled",
	  N_("Show _Only This Task List"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_list_select_one_cb) },

	{ "task-mark-complete",
	  NULL,
	  N_("_Mark as Complete"),
	  "<Control>k",
	  N_("Mark selected tasks as complete"),
	  G_CALLBACK (action_task_mark_complete_cb) },

	{ "task-mark-incomplete",
	  NULL,
	  N_("Mar_k as Incomplete"),
	  NULL,
	  N_("Mark selected tasks as incomplete"),
	  G_CALLBACK (action_task_mark_incomplete_cb) },

	{ "task-new",
	  "stock_task",
	  N_("New _Task"),
	  NULL,
	  N_("Create a new task"),
	  G_CALLBACK (action_task_new_cb) },

	{ "task-open",
	  GTK_STOCK_OPEN,
	  N_("_Open Task"),
	  "<Control>o",
	  N_("View the selected task"),
	  G_CALLBACK (action_task_open_cb) },

	{ "task-open-url",
	  "applications-internet",
	  N_("Open _Web Page"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_open_url_cb) },

	{ "task-purge",
	  NULL,
	  N_("Purg_e"),
	  "<Control>e",
	  N_("Delete completed tasks"),
	  G_CALLBACK (action_task_purge_cb) },

	/*** Menus ***/

	{ "task-actions-menu",
	  NULL,
	  N_("_Actions"),
	  NULL,
	  NULL,
	  NULL },

	{ "task-preview-menu",
	  NULL,
	  N_("_Preview"),
	  NULL,
	  NULL,
	  NULL }
};

static EPopupActionEntry task_popup_entries[] = {

	{ "task-list-popup-copy",
	  NULL,
	  "task-list-copy" },

	{ "task-list-popup-delete",
	  NULL,
	  "task-list-delete" },

	{ "task-list-popup-properties",
	  NULL,
	  "task-list-properties" },

	{ "task-list-popup-rename",
	  NULL,
	  "task-list-rename" },

	{ "task-list-popup-select-one",
	  NULL,
	  "task-list-select-one" },

	{ "task-popup-assign",
	  NULL,
	  "task-assign" },

	{ "task-popup-clipboard-copy",
	  NULL,
	  "task-clipboard-copy" },

	{ "task-popup-clipboard-cut",
	  NULL,
	  "task-clipboard-cut" },

	{ "task-popup-clipboard-paste",
	  NULL,
	  "task-clipboard-paste" },

	{ "task-popup-delete",
	  NULL,
	  "task-delete" },

	{ "task-popup-forward",
	  NULL,
	  "task-forward" },

	{ "task-popup-mark-complete",
	  NULL,
	  "task-mark-complete" },

	{ "task-popup-mark-incomplete",
	  NULL,
	  "task-mark-incomplete" },

	{ "task-popup-open",
	  NULL,
	  "task-open" },

	{ "task-popup-open-url",
	  NULL,
	  "task-open-url" }
};

static GtkToggleActionEntry task_toggle_entries[] = {

	{ "task-preview",
	  NULL,
	  N_("Task _Preview"),
	  "<Control>m",
	  N_("Show task preview pane"),
	  G_CALLBACK (action_task_preview_cb),
	  TRUE }
};

static GtkRadioActionEntry task_view_entries[] = {

	/* This action represents the inital active memo view.
	 * It should not be visible in the UI, nor should it be
	 * possible to switch to it from another shell view. */
	{ "task-view-initial",
	  NULL,
	  NULL,
	  NULL,
	  NULL,
	  -1 },

	{ "task-view-classic",
	  NULL,
	  N_("_Classic View"),
	  NULL,
	  N_("Show task preview below the task list"),
	  0 },

	{ "task-view-vertical",
	  NULL,
	  N_("_Vertical View"),
	  NULL,
	  N_("Show task preview alongside the task list"),
	  1 }
};

static GtkRadioActionEntry task_filter_entries[] = {

	{ "task-filter-active-tasks",
	  NULL,
	  N_("Active Tasks"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_ACTIVE_TASKS },

	{ "task-filter-any-category",
	  NULL,
	  N_("Any Category"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_ANY_CATEGORY },

	{ "task-filter-completed-tasks",
	  NULL,
	  N_("Completed Tasks"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_COMPLETED_TASKS },

	{ "task-filter-next-7-days-tasks",
	  NULL,
	  N_("Next 7 Days' Tasks"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_NEXT_7_DAYS_TASKS },

	{ "task-filter-overdue-tasks",
	  NULL,
	  N_("Overdue Tasks"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_OVERDUE_TASKS },

	{ "task-filter-tasks-with-attachments",
	  NULL,
	  N_("Tasks with Attachments"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_TASKS_WITH_ATTACHMENTS },

	{ "task-filter-unmatched",
	  NULL,
	  N_("Unmatched"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_FILTER_UNMATCHED }
};

static GtkRadioActionEntry task_search_entries[] = {

	{ "task-search-advanced-hidden",
	  NULL,
	  N_("Advanced search"),
	  NULL,
	  NULL,
	  TASK_SEARCH_ADVANCED },

	{ "task-search-any-field-contains",
          NULL,
	  N_("Any field contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_SEARCH_ANY_FIELD_CONTAINS },

	{ "task-search-description-contains",
	  NULL,
	  N_("Description contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_SEARCH_DESCRIPTION_CONTAINS },

	{ "task-search-summary-contains",
	  NULL,
	  N_("Summary contains"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  TASK_SEARCH_SUMMARY_CONTAINS }
};

static GtkActionEntry lockdown_printing_entries[] = {

	{ "task-list-print",
	  GTK_STOCK_PRINT,
	  NULL,
	  "<Control>p",
	  N_("Print the list of tasks"),
	  G_CALLBACK (action_task_list_print_cb) },

	{ "task-list-print-preview",
	  GTK_STOCK_PRINT_PREVIEW,
	  NULL,
	  NULL,
	  N_("Preview the list of tasks to be printed"),
	  G_CALLBACK (action_task_list_print_preview_cb) },

	{ "task-print",
	  GTK_STOCK_PRINT,
	  NULL,
	  NULL,
	  N_("Print the selected task"),
	  G_CALLBACK (action_task_print_cb) }
};

static EPopupActionEntry lockdown_printing_popup_entries[] = {

	{ "task-popup-print",
	  NULL,
	  "task-print" }
};

static GtkActionEntry lockdown_save_to_disk_entries[] = {

	{ "task-save-as",
	  GTK_STOCK_SAVE_AS,
	  N_("_Save as iCalendar..."),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  G_CALLBACK (action_task_save_as_cb) }
};

static EPopupActionEntry lockdown_save_to_disk_popup_entries[] = {

	{ "task-popup-save-as",
	  NULL,
	  "task-save-as" },
};

void
e_task_shell_view_actions_init (ETaskShellView *task_shell_view)
{
	EShellView *shell_view;
	EShellWindow *shell_window;
	ETaskShellContent *task_shell_content;
	ECalComponentPreview *task_preview;
	GtkActionGroup *action_group;
	GConfBridge *bridge;
	GtkAction *action;
	GtkRadioAction *radio_action;
	GObject *object;
	const gchar *key;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	task_shell_content = task_shell_view->priv->task_shell_content;
	task_preview = e_task_shell_content_get_task_preview (task_shell_content);

	/* Task Actions */
	action_group = ACTION_GROUP (TASKS);
	gtk_action_group_add_actions (
		action_group, task_entries,
		G_N_ELEMENTS (task_entries), task_shell_view);
	e_action_group_add_popup_actions (
		action_group, task_popup_entries,
		G_N_ELEMENTS (task_popup_entries));
	gtk_action_group_add_toggle_actions (
		action_group, task_toggle_entries,
		G_N_ELEMENTS (task_toggle_entries), task_shell_view);
	gtk_action_group_add_radio_actions (
		action_group, task_view_entries,
		G_N_ELEMENTS (task_view_entries), -1,
		G_CALLBACK (action_task_view_cb), task_shell_view);
	gtk_action_group_add_radio_actions (
		action_group, task_search_entries,
		G_N_ELEMENTS (task_search_entries),
		-1, NULL, NULL);

	/* Advanced Search action */
	radio_action = GTK_RADIO_ACTION (ACTION (TASK_SEARCH_ADVANCED_HIDDEN));
	e_shell_content_set_search_radio_action (e_shell_view_get_shell_content (shell_view), radio_action);
	gtk_action_set_visible (GTK_ACTION (radio_action), FALSE);
	gtk_radio_action_set_current_value (radio_action, TASK_SEARCH_SUMMARY_CONTAINS);

	/* Lockdown Printing Actions */
	action_group = ACTION_GROUP (LOCKDOWN_PRINTING);
	gtk_action_group_add_actions (
		action_group, lockdown_printing_entries,
		G_N_ELEMENTS (lockdown_printing_entries),
		task_shell_view);
	e_action_group_add_popup_actions (
		action_group, lockdown_printing_popup_entries,
		G_N_ELEMENTS (lockdown_printing_popup_entries));

	/* Lockdown Save-to-Disk Actions */
	action_group = ACTION_GROUP (LOCKDOWN_SAVE_TO_DISK);
	gtk_action_group_add_actions (
		action_group, lockdown_save_to_disk_entries,
		G_N_ELEMENTS (lockdown_save_to_disk_entries),
		task_shell_view);
	e_action_group_add_popup_actions (
		action_group, lockdown_save_to_disk_popup_entries,
		G_N_ELEMENTS (lockdown_save_to_disk_popup_entries));

	/* Bind GObject properties to GConf keys. */

	bridge = gconf_bridge_get ();

	object = G_OBJECT (ACTION (TASK_PREVIEW));
	key = "/apps/evolution/calendar/display/show_task_preview";
	gconf_bridge_bind_property (bridge, key, object, "active");

	object = G_OBJECT (ACTION (TASK_VIEW_VERTICAL));
	key = "/apps/evolution/calendar/display/task_layout";
	gconf_bridge_bind_property (bridge, key, object, "current-value");

	/* Fine tuning. */

	action = ACTION (TASK_DELETE);
	g_object_set (action, "short-label", _("Delete"), NULL);

	g_signal_connect (
		ACTION (GAL_SAVE_CUSTOM_VIEW), "activate",
		G_CALLBACK (action_gal_save_custom_view_cb), task_shell_view);

	e_binding_new (
		ACTION (TASK_PREVIEW), "active",
		ACTION (TASK_VIEW_CLASSIC), "sensitive");

	e_binding_new (
		ACTION (TASK_PREVIEW), "active",
		ACTION (TASK_VIEW_VERTICAL), "sensitive");

	e_web_view_set_open_proxy (
		E_WEB_VIEW (task_preview), ACTION (TASK_OPEN));

	e_web_view_set_print_proxy (
		E_WEB_VIEW (task_preview), ACTION (TASK_PRINT));

	e_web_view_set_save_as_proxy (
		E_WEB_VIEW (task_preview), ACTION (TASK_SAVE_AS));
}

void
e_task_shell_view_update_search_filter (ETaskShellView *task_shell_view)
{
	EShellContent *shell_content;
	EShellWindow *shell_window;
	EShellView *shell_view;
	GtkActionGroup *action_group;
	GtkRadioAction *radio_action;
	GList *list, *iter;
	GSList *group;
	gint ii;

	shell_view = E_SHELL_VIEW (task_shell_view);
	shell_content = e_shell_view_get_shell_content (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	action_group = ACTION_GROUP (TASKS_FILTER);
	e_action_group_remove_all_actions (action_group);

	/* Add the standard filter actions. */
	gtk_action_group_add_radio_actions (
		action_group, task_filter_entries,
		G_N_ELEMENTS (task_filter_entries),
		TASK_FILTER_ANY_CATEGORY,
		G_CALLBACK (action_search_filter_cb),
		task_shell_view);

	/* Retrieve the radio group from an action we just added. */
	list = gtk_action_group_list_actions (action_group);
	radio_action = GTK_RADIO_ACTION (list->data);
	group = gtk_radio_action_get_group (radio_action);
	g_list_free (list);

	/* Build the category actions. */

	list = e_categories_get_list ();
	for (iter = list, ii = 0; iter != NULL; iter = iter->next, ii++) {
		const gchar *category_name = iter->data;
		const gchar *filename;
		GtkAction *action;
		gchar *action_name;

		action_name = g_strdup_printf (
			"task-filter-category-%d", ii);
		radio_action = gtk_radio_action_new (
			action_name, category_name, NULL, NULL, ii);
		g_free (action_name);

		/* Convert the category icon file to a themed icon name. */
		filename = e_categories_get_icon_file_for (category_name);
		if (filename != NULL && *filename != '\0') {
			gchar *basename;
			gchar *cp;

			basename = g_path_get_basename (filename);

			/* Lose the file extension. */
			if ((cp = strrchr (basename, '.')) != NULL)
				*cp = '\0';

			g_object_set (
				radio_action, "icon-name", basename, NULL);

			g_free (basename);
		}

		gtk_radio_action_set_group (radio_action, group);
		group = gtk_radio_action_get_group (radio_action);

		/* The action group takes ownership of the action. */
		action = GTK_ACTION (radio_action);
		gtk_action_group_add_action (action_group, action);
		g_object_unref (radio_action);
	}
	g_list_free (list);

	/* Use any action in the group; doesn't matter which. */
	e_shell_content_set_filter_action (shell_content, radio_action);

	ii = TASK_FILTER_UNMATCHED;
	e_shell_content_add_filter_separator_after (shell_content, ii);

	ii = TASK_FILTER_TASKS_WITH_ATTACHMENTS;
	e_shell_content_add_filter_separator_after (shell_content, ii);
}
