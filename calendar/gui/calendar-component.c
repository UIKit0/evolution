/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* calendar-component.c
 *
 * Copyright (C) 2003  Ettore Perazzoli
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
 * Authors: Ettore Perazzoli <ettore@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-exception.h>
#include "calendar-config.h"
#include "calendar-component.h"
#include "calendar-commands.h"
#include "gnome-cal.h"
#include "migration.h"
#include "e-comp-editor-registry.h"
#include "comp-util.h"
#include "dialogs/new-calendar.h"
#include "dialogs/comp-editor.h"
#include "dialogs/copy-source-dialog.h"
#include "dialogs/event-editor.h"
#include "widgets/misc/e-source-selector.h"


/* IDs for user creatable items */
#define CREATE_EVENT_ID "event"
#define CREATE_MEETING_ID "meeting"
#define CREATE_ALLDAY_EVENT_ID "allday-event"

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _CalendarComponentPrivate {
	char *config_directory;

	GConfClient *gconf_client;
	ESourceList *source_list;
	GSList *source_selection;
	
	GnomeCalendar *calendar;
	GtkWidget *source_selector;

	guint selected_not;	
};

/* FIXME This should be gnome cal likely */
extern ECompEditorRegistry *comp_editor_registry;

/* Utility functions.  */

static void
add_uri_for_source (ESource *source, GnomeCalendar *calendar)
{
	char *uri = e_source_get_uri (source);

	gnome_calendar_add_event_uri (calendar, uri);
	g_free (uri);
}

static void
remove_uri_for_source (ESource *source, GnomeCalendar *calendar)
{
	char *uri = e_source_get_uri (source);

	gnome_calendar_remove_event_uri (calendar, uri);
	g_free (uri);
}

static gboolean
is_in_selection (GSList *selection, ESource *source)
{
	GSList *l;
	
	for (l = selection; l; l = l->next) {
		ESource *selected_source = l->data;
		
		if (!strcmp (e_source_peek_uid (selected_source), e_source_peek_uid (source)))
			return TRUE;
	}

	return FALSE;
}

static gboolean
is_in_uids (GSList *uids, ESource *source)
{
	GSList *l;
	
	for (l = uids; l; l = l->next) {
		const char *uid = l->data;
		
		if (!strcmp (uid, e_source_peek_uid (source)))
			return TRUE;
	}

	return FALSE;
}

static ESource *
find_first_source (ESourceList *source_list)
{
	GSList *groups, *sources, *l, *m;
			
	groups = e_source_list_peek_groups (source_list);
	for (l = groups; l; l = l->next) {
		ESourceGroup *group = l->data;
				
		sources = e_source_group_peek_sources (group);
		for (m = sources; m; m = m->next) {
			ESource *source = m->data;

			return source;
		}				
	}

	return NULL;
}

static void
update_uris_for_selection (CalendarComponent *calendar_component)
{
	CalendarComponentPrivate *priv;
	GSList *selection, *l, *uids_selected = NULL;
	
	priv = calendar_component->priv;
	
	selection = e_source_selector_get_selection (E_SOURCE_SELECTOR (priv->source_selector));

	for (l = priv->source_selection; l; l = l->next) {
		ESource *old_selected_source = l->data;

		if (!is_in_selection (selection, old_selected_source))
			remove_uri_for_source (old_selected_source, priv->calendar);
	}	
	
	for (l = selection; l; l = l->next) {
		ESource *selected_source = l->data;
		
		add_uri_for_source (selected_source, priv->calendar);
		uids_selected = g_slist_append (uids_selected, (char *)e_source_peek_uid (selected_source));
	}
	
	e_source_selector_free_selection (priv->source_selection);
	priv->source_selection = selection;

	/* Save the selection for next time we start up */
	calendar_config_set_calendars_selected (uids_selected);
	g_slist_free (uids_selected);
}

static void
update_uri_for_primary_selection (CalendarComponent *calendar_component)
{
	CalendarComponentPrivate *priv;
	ESource *source;
	char *uri;

	priv = calendar_component->priv;
	
	source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (priv->source_selector));
	if (!source)
		return;

	/* Set the default */
	uri = e_source_get_uri (source);
	gnome_calendar_set_default_uri (priv->calendar, uri);
	g_free (uri);
	
	/* Save the selection for next time we start up */
	calendar_config_set_primary_calendar (e_source_peek_uid (source));
}

static void
update_selection (CalendarComponent *calendar_component)
{
	CalendarComponentPrivate *priv;
	GSList *selection, *uids_selected, *l;

	priv = calendar_component->priv;

	/* Get the selection in gconf */
	uids_selected = calendar_config_get_calendars_selected ();

	/* Remove any that aren't there any more */
	selection = e_source_selector_get_selection (E_SOURCE_SELECTOR (priv->source_selector));

	for (l = selection; l; l = l->next) {
		ESource *source = l->data;

		if (!is_in_uids (uids_selected, source)) 
			e_source_selector_unselect_source (E_SOURCE_SELECTOR (priv->source_selector), source);
	}
	
	e_source_selector_free_selection (selection);

	/* Make sure the whole selection is there */
	for (l = uids_selected; l; l = l->next) {
		char *uid = l->data;
		ESource *source;

		source = e_source_list_peek_source_by_uid (priv->source_list, uid);
		if (source) 
			e_source_selector_select_source (E_SOURCE_SELECTOR (priv->source_selector), source);
		
		g_free (uid);
	}
	g_slist_free (uids_selected);
}

static void
update_primary_selection (CalendarComponent *calendar_component)
{
	CalendarComponentPrivate *priv;
	ESource *source;
	char *uid;

	priv = calendar_component->priv;

	uid = calendar_config_get_primary_calendar ();
	if (uid) {
		source = e_source_list_peek_source_by_uid (priv->source_list, uid);
		g_free (uid);
	
		e_source_selector_set_primary_selection (E_SOURCE_SELECTOR (priv->source_selector), source);
	} else {
		ESource *source;
		
		/* Try to create a default if there isn't one */
		source = find_first_source (priv->source_list);
		if (source)
			e_source_selector_set_primary_selection (E_SOURCE_SELECTOR (priv->source_selector), source);
	}
}

/* FIXME This is duplicated from comp-editor-factory.c, should it go in comp-util? */
static ECalComponent *
get_default_event (ECal *client, gboolean all_day) 
{
	ECalComponent *comp;
	struct icaltimetype itt;
	ECalComponentDateTime dt;
	char *location;
	icaltimezone *zone;

	comp = cal_comp_event_new_with_defaults (client);

	location = calendar_config_get_timezone ();
	zone = icaltimezone_get_builtin_timezone (location);

	if (all_day) {
		itt = icaltime_from_timet_with_zone (time (NULL), 1, zone);

		dt.value = &itt;
		dt.tzid = icaltimezone_get_tzid (zone);
		
		e_cal_component_set_dtstart (comp, &dt);
		e_cal_component_set_dtend (comp, &dt);		
	} else {
		itt = icaltime_current_time_with_zone (zone);
		icaltime_adjust (&itt, 0, 1, -itt.minute, -itt.second);
		
		dt.value = &itt;
		dt.tzid = icaltimezone_get_tzid (zone);
		
		e_cal_component_set_dtstart (comp, &dt);
		icaltime_adjust (&itt, 0, 1, 0, 0);
		e_cal_component_set_dtend (comp, &dt);
	}

	e_cal_component_commit_sequence (comp);

	return comp;
}

/* Callbacks.  */
static void
add_popup_menu_item (GtkMenu *menu, const char *label, const char *pixmap,
		     GCallback callback, gpointer user_data, gboolean sensitive)
{
	GtkWidget *item, *image;

	if (pixmap) {
		item = gtk_image_menu_item_new_with_label (label);

		/* load the image */
		image = gtk_image_new_from_stock (pixmap, GTK_ICON_SIZE_MENU);
		if (!image)
			image = gtk_image_new_from_file (pixmap);

		if (image) {
			gtk_widget_show (image);
			gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		}
	} else {
		item = gtk_menu_item_new_with_label (label);
	}

	if (callback)
		g_signal_connect (G_OBJECT (item), "activate", callback, user_data);

	if (!sensitive)
		gtk_widget_set_sensitive (item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
}

static void
copy_calendar_cb (GtkWidget *widget, CalendarComponent *comp)
{
	ESource *selected_source;
	CalendarComponentPrivate *priv;

	priv = comp->priv;
	
	selected_source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (priv->source_selector));
	if (!selected_source)
		return;

	copy_source_dialog (GTK_WINDOW (gtk_widget_get_toplevel (widget)), selected_source, CALOBJ_TYPE_EVENT);
}

static void
delete_calendar_cb (GtkWidget *widget, CalendarComponent *comp)
{
	ESource *selected_source;
	CalendarComponentPrivate *priv;
	GtkWidget *dialog;

	priv = comp->priv;
	
	selected_source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (priv->source_selector));
	if (!selected_source)
		return;

	/* create the confirmation dialog */
	dialog = gtk_message_dialog_new (
		GTK_WINDOW (gtk_widget_get_toplevel (widget)),
		GTK_DIALOG_MODAL,
		GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_YES_NO,
		_("Calendar '%s' will be removed. Are you sure you want to continue?"),
		e_source_peek_name (selected_source));
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
		if (e_source_selector_source_is_selected (E_SOURCE_SELECTOR (priv->source_selector),
							  selected_source))
			e_source_selector_unselect_source (E_SOURCE_SELECTOR (priv->source_selector),
							   selected_source);
		
		e_source_group_remove_source (e_source_peek_group (selected_source), selected_source);

		/* FIXME: remove the calendar.ics file and the directory */
	}

	gtk_widget_destroy (dialog);
}

static void
new_calendar_cb (GtkWidget *widget, CalendarComponent *comp)
{
	new_calendar_dialog (GTK_WINDOW (gtk_widget_get_toplevel (widget)));
}

static void
rename_calendar_cb (GtkWidget *widget, CalendarComponent *comp)
{
	CalendarComponentPrivate *priv;
	ESource *selected_source;
	GtkWidget *dialog, *entry;

	priv = comp->priv;
	
	selected_source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (priv->source_selector));
	if (!selected_source)
		return;

	/* create the dialog to prompt the user for the new name */
	dialog = gtk_message_dialog_new (gtk_widget_get_toplevel (widget),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_OK_CANCEL,
					 _("Rename this calendar to"));
	entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry), e_source_peek_name (selected_source));
	gtk_widget_show (entry);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), entry, TRUE, FALSE, 6);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
		e_source_set_name (selected_source, gtk_entry_get_text (GTK_ENTRY (entry)));

	gtk_widget_destroy (dialog);
}

static void
fill_popup_menu_cb (ESourceSelector *selector, GtkMenu *menu, CalendarComponent *comp)
{
	gboolean sensitive;

	sensitive = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (comp->priv->source_selector)) ?
		TRUE : FALSE;

	add_popup_menu_item (menu, _("New Calendar"), GTK_STOCK_NEW, G_CALLBACK (new_calendar_cb), comp, TRUE);
	add_popup_menu_item (menu, _("Copy"), NULL, G_CALLBACK (copy_calendar_cb), comp, sensitive);
	add_popup_menu_item (menu, _("Rename"), NULL, G_CALLBACK (rename_calendar_cb), comp, sensitive);
	add_popup_menu_item (menu, _("Delete"), GTK_STOCK_DELETE, G_CALLBACK (delete_calendar_cb), comp, sensitive);
}

static void
source_selection_changed_cb (ESourceSelector *selector, 
			     CalendarComponent *calendar_component)
{
	update_uris_for_selection (calendar_component);
}

static void
primary_source_selection_changed_cb (ESourceSelector *selector,
				     CalendarComponent *calendar_component)
{
	update_uri_for_primary_selection (calendar_component);
}

static void
config_selection_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	update_selection (data);
}

/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	CalendarComponentPrivate *priv = CALENDAR_COMPONENT (object)->priv;

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	if (priv->source_selection != NULL) {
		e_source_selector_free_selection (priv->source_selection);
		priv->source_selection = NULL;
	}
	
	if (priv->gconf_client != NULL) {
		g_object_unref (priv->gconf_client);
		priv->gconf_client = NULL;
	}

	if (priv->selected_not) {
		calendar_config_remove_notification (priv->selected_not);
		priv->selected_not = 0;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	CalendarComponentPrivate *priv = CALENDAR_COMPONENT (object)->priv;

	g_free (priv->config_directory);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Evolution::Component CORBA methods.  */

static void
control_activate_cb (BonoboControl *control, gboolean activate, gpointer data)
{
	GnomeCalendar *gcal;

	gcal = GNOME_CALENDAR (data);

	if (activate)
		calendar_control_activate (control, gcal);
	else
		calendar_control_deactivate (control, gcal);
}

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *corba_sidebar_control,
		     Bonobo_Control *corba_view_control,
		     Bonobo_Control *corba_statusbar_control,
		     CORBA_Environment *ev)
{
	CalendarComponent *calendar_component = CALENDAR_COMPONENT (bonobo_object_from_servant (servant));
	CalendarComponentPrivate *priv;
	GtkWidget *selector_scrolled_window;
	BonoboControl *sidebar_control;
	BonoboControl *view_control;
	
	priv = calendar_component->priv;
	
	/* Create sidebar selector */
	priv->source_selector = e_source_selector_new (calendar_component->priv->source_list);
	gtk_widget_show (priv->source_selector);

	selector_scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (selector_scrolled_window), priv->source_selector);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (selector_scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (selector_scrolled_window),
					     GTK_SHADOW_IN);
	gtk_widget_show (selector_scrolled_window);

	sidebar_control = bonobo_control_new (selector_scrolled_window);

	/* Create main calendar view */
	/* FIXME Instead of returning, we should make a control with a
	 * label describing the problem */
	priv->calendar = GNOME_CALENDAR (gnome_calendar_new ());
	if (!priv->calendar) {
		g_warning (G_STRLOC ": could not create the calendar widget!");
		bonobo_exception_set (ev, ex_GNOME_Evolution_Component_Failed);
		return;
	}
	
	gtk_widget_show (GTK_WIDGET (priv->calendar));

	view_control = bonobo_control_new (GTK_WIDGET (priv->calendar));
	if (!view_control) {
		g_warning (G_STRLOC ": could not create the control!");
		bonobo_exception_set (ev, ex_GNOME_Evolution_Component_Failed);
		return;
	}
	g_object_set_data (G_OBJECT (priv->calendar), "control", view_control);

	g_signal_connect (view_control, "activate", G_CALLBACK (control_activate_cb), priv->calendar);

	g_signal_connect_object (priv->source_selector, "selection_changed",
				 G_CALLBACK (source_selection_changed_cb), 
				 G_OBJECT (calendar_component), 0);
	g_signal_connect_object (priv->source_selector, "primary_selection_changed",
				 G_CALLBACK (primary_source_selection_changed_cb), 
				 G_OBJECT (calendar_component), 0);
	g_signal_connect_object (priv->source_selector, "fill_popup_menu",
				 G_CALLBACK (fill_popup_menu_cb),
				 G_OBJECT (calendar_component), 0);

	/* Load the selection from the last run */
	update_selection (calendar_component);	
	update_primary_selection (calendar_component);

	/* If it gets fiddled with, ie from another evolution window, update it */
	priv->selected_not = calendar_config_add_notification_calendars_selected (config_selection_changed_cb, 
										  calendar_component);
	
	/* Return the controls */
	*corba_sidebar_control = CORBA_Object_duplicate (BONOBO_OBJREF (sidebar_control), ev);
	*corba_view_control = CORBA_Object_duplicate (BONOBO_OBJREF (view_control), ev);

	/* FIXME temporary for testing.  */
	{
		GtkWidget *label = gtk_label_new ("Hey hey this is the calendar");
		BonoboControl *control;

		gtk_widget_show (label);
		control = bonobo_control_new (label);
		*corba_statusbar_control = CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);
	}
}


static GNOME_Evolution_CreatableItemTypeList *
impl__get_userCreatableItems (PortableServer_Servant servant,
			      CORBA_Environment *ev)
{
	GNOME_Evolution_CreatableItemTypeList *list = GNOME_Evolution_CreatableItemTypeList__alloc ();

	list->_length  = 3;
	list->_maximum = list->_length;
	list->_buffer  = GNOME_Evolution_CreatableItemTypeList_allocbuf (list->_length);

	CORBA_sequence_set_release (list, FALSE);

	list->_buffer[0].id = CREATE_EVENT_ID;
	list->_buffer[0].description = _("New appointment");
	list->_buffer[0].menuDescription = _("_Appointment");
	list->_buffer[0].tooltip = _("Create a new appointment");
	list->_buffer[0].menuShortcut = 'a';
	list->_buffer[0].iconName = "new_appointment.xpm";

	list->_buffer[1].id = CREATE_MEETING_ID;
	list->_buffer[1].description = _("New meeting");
	list->_buffer[1].menuDescription = _("M_eeting");
	list->_buffer[1].tooltip = _("Create a new meeting request");
	list->_buffer[1].menuShortcut = 'e';
	list->_buffer[1].iconName = "meeting-request-16.png";

	list->_buffer[2].id = CREATE_ALLDAY_EVENT_ID;
	list->_buffer[2].description = _("New all day appointment");
	list->_buffer[2].menuDescription = _("All _Day Appointment");
	list->_buffer[2].tooltip = _("Create a new all-day appointment");
	list->_buffer[2].menuShortcut = 'd';
	list->_buffer[2].iconName = "new_all_day_event.png";

	return list;
}

static void
impl_requestCreateItem (PortableServer_Servant servant,
			const CORBA_char *item_type_name,
			CORBA_Environment *ev)
{
	CalendarComponent *calendar_component = CALENDAR_COMPONENT (bonobo_object_from_servant (servant));
	CalendarComponentPrivate *priv;
	ECal *ecal;
	ECalComponent *comp;
	EventEditor *editor;
	gboolean is_meeting = FALSE;
	
	priv = calendar_component->priv;
	
	ecal = gnome_calendar_get_default_client (priv->calendar);
	if (!ecal) {
		/* FIXME We should display a gui dialog or something */
		bonobo_exception_set (ev, ex_GNOME_Evolution_Component_UnknownType);
		g_warning (G_STRLOC ": No default client");
	}
		
	editor = event_editor_new (ecal);
	
	if (strcmp (item_type_name, CREATE_EVENT_ID) == 0) {
		comp = get_default_event (ecal, FALSE);
 	} else if (strcmp (item_type_name, CREATE_ALLDAY_EVENT_ID) == 0) {
		comp = get_default_event (ecal, TRUE);
	} else if (strcmp (item_type_name, CREATE_MEETING_ID) == 0) {
		comp = get_default_event (ecal, FALSE);
		is_meeting = TRUE;
	} else {
		bonobo_exception_set (ev, ex_GNOME_Evolution_Component_UnknownType);
		return;
	}	

	comp_editor_edit_comp (COMP_EDITOR (editor), comp);
	if (is_meeting)
		event_editor_show_meeting (editor);
	comp_editor_focus (COMP_EDITOR (editor));

	e_comp_editor_registry_add (comp_editor_registry, COMP_EDITOR (editor), TRUE);
}


/* Initialization.  */

static void
calendar_component_class_init (CalendarComponentClass *class)
{
	POA_GNOME_Evolution_Component__epv *epv = &class->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	epv->createControls          = impl_createControls;
	epv->_get_userCreatableItems = impl__get_userCreatableItems;
	epv->requestCreateItem       = impl_requestCreateItem;

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;
}

static void
calendar_component_init (CalendarComponent *component)
{
	CalendarComponentPrivate *priv;
	GSList *groups;

	priv = g_new0 (CalendarComponentPrivate, 1);

	priv->config_directory = g_build_filename (g_get_home_dir (),
						   ".evolution", "calendar", "config",
						   NULL);

	/* EPFIXME: Should use a custom one instead?  Also we should add
	 * calendar_component_peek_gconf_client().  */
	priv->gconf_client = gconf_client_get_default ();

	priv->source_list = e_source_list_new_for_gconf (priv->gconf_client,
							 "/apps/evolution/calendar/sources");

	/* create default calendars if there are no groups */
	groups = e_source_list_peek_groups (priv->source_list);
	if (!groups) {
		ESourceGroup *group;
		ESource *source;
		char *base_uri, *new_dir;

		/* create the local source group */
		base_uri = g_build_filename (g_get_home_dir (),
					     "/.evolution/calendar/local/OnThisComputer/",
					     NULL);
		group = e_source_group_new (_("On This Computer"), base_uri);
		e_source_list_add_group (priv->source_list, group, -1);

		/* migrate calendars from older setup */
		if (!migrate_old_calendars (group)) {
			/* create default calendars */
			new_dir = g_build_filename (base_uri, "Personal/", NULL);
			if (!e_mkdir_hier (new_dir, 0700)) {
				source = e_source_new (_("Personal"), "Personal");
				e_source_group_add_source (group, source, -1);
			}
			g_free (new_dir);

			new_dir = g_build_filename (base_uri, "Work/", NULL);
			if (!e_mkdir_hier (new_dir, 0700)) {
				source = e_source_new (_("Work"), "Work");
				e_source_group_add_source (group, source, -1);
			}
			g_free (new_dir);
		}

		g_free (base_uri);

		/* create the remote source group */
		group = e_source_group_new (_("On The Web"), "webcal://");
		e_source_list_add_group (priv->source_list, group, -1);
	}

	component->priv = priv;
}


/* Public API.  */

CalendarComponent *
calendar_component_peek (void)
{
	static CalendarComponent *component = NULL;

	if (component == NULL) {
		component = g_object_new (calendar_component_get_type (), NULL);

		if (e_mkdir_hier (calendar_component_peek_config_directory (component), 0777) != 0) {
			g_warning (G_STRLOC ": Cannot create directory %s: %s",
				   calendar_component_peek_config_directory (component),
				   g_strerror (errno));
			g_object_unref (component);
			component = NULL;
		}
	}

	return component;
}

const char *
calendar_component_peek_config_directory (CalendarComponent *component)
{
	return component->priv->config_directory;
}


BONOBO_TYPE_FUNC_FULL (CalendarComponent, GNOME_Evolution_Component, PARENT_TYPE, calendar_component)
