/*
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-mail-display-popup-prefer-plain.h"
#include "mail/e-mail-display-popup-extension.h"
#include "mail/e-mail-display.h"
#include <shell/e-shell.h>
#include <shell/e-shell-window.h>
#include "mail/e-mail-browser.h"

#include <libebackend/libebackend.h>

#include <glib/gi18n-lib.h>

#define d(x)

typedef struct _EMailDisplayPopupPreferPlain {
	EExtension parent;

	WebKitDOMDocument *document;
	gchar *text_plain_id;
	gchar *text_html_id;

	GtkActionGroup *action_group;

} EMailDisplayPopupPreferPlain;

typedef struct _EMailDisplayPopupPreferPlainClass {
	EExtensionClass parent_class;
} EMailDisplayPopupPreferPlainClass;

#define E_MAIL_DISPLAY_POPUP_PREFER_PLAIN(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), e_mail_display_popup_prefer_plain_get_type(), EMailDisplayPopupPreferPlain))

GType e_mail_display_popup_prefer_plain_get_type (void);
static void e_mail_display_popup_extension_interface_init (EMailDisplayPopupExtensionInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailDisplayPopupPreferPlain,
	e_mail_display_popup_prefer_plain,
	E_TYPE_EXTENSION,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION,
		e_mail_display_popup_extension_interface_init));

static const gchar *ui_webview =
"<ui>"
"  <popup name='context'>"
"    <placeholder name='custom-actions-2'>"
"      <separator/>"
"      <menuitem action='show-plain-text-part'/>"
"      <menuitem action='show-text-html-part'/>"
"      <separator/>"
"    </placeholder>"
"  </popup>"
"</ui>";

static const gchar *ui_reader =
"<ui>"
"  <popup name='mail-preview-popup'>"
"    <placeholder name='mail-preview-popup-actions'>"
"      <separator/>"
"      <menuitem action='show-plain-text-part'/>"
"      <menuitem action='show-text-html-part'/>"
"      <separator/>"
"    </placeholder>"
"  </popup>"
"</ui>";


static void
toggle_part (GtkAction *action,
	     EMailDisplayPopupExtension *extension)
{
	EMailDisplayPopupPreferPlain *pp_extension = (EMailDisplayPopupPreferPlain *) extension;
	WebKitDOMDocument *doc = pp_extension->document;
	WebKitDOMDOMWindow *window;
	WebKitDOMElement *frame_element;
	SoupURI *soup_uri;
	GHashTable *query;
	gchar *uri;

	uri = webkit_dom_document_get_document_uri (doc);
	soup_uri = soup_uri_new (uri);
	g_free (uri);

	query = soup_form_decode (soup_uri->query);
	g_hash_table_replace (query, g_strdup ("part_id"),
		pp_extension->text_html_id ?
			pp_extension->text_html_id :
			pp_extension->text_plain_id);

	soup_uri_set_query_from_form (soup_uri, query);
	g_hash_table_destroy (query);

	uri = soup_uri_to_string (soup_uri, FALSE);
	soup_uri_free (soup_uri);

	/* Get frame's window and from the window the actual <iframe> element */
	window = webkit_dom_document_get_default_view (doc);
	frame_element = webkit_dom_dom_window_get_frame_element (window);
	webkit_dom_html_iframe_element_set_src (
		WEBKIT_DOM_HTML_IFRAME_ELEMENT (frame_element), uri);

	g_free (uri);
}

GtkActionEntry entries[] = {

	{ "show-plain-text-part",
	   NULL,
	   N_("Display plain text version"),
	   NULL,
	   N_("Display plain text version of multipart/alternative message"),
	   NULL
	},

	{ "show-text-html-part",
	  NULL,
	  N_("Display HTML version"),
	  NULL,
	  N_("Display HTML version of multipart/alternative message"),
	  NULL
	}
};

const gint ID_LEN = G_N_ELEMENTS(".alternative-prefer-plain.");

static void
set_text_plain_id (EMailDisplayPopupPreferPlain *extension,
		   const gchar *id)
{
	g_free (extension->text_plain_id);
	extension->text_plain_id = g_strdup (id);
}

static void
set_text_html_id (EMailDisplayPopupPreferPlain *extension,
		  const gchar *id)
{
	g_free (extension->text_html_id);
	extension->text_html_id = g_strdup (id);
}


static GtkActionGroup*
create_group (EMailDisplayPopupExtension *extension)
{
	EExtensible *extensible;
	EWebView *web_view;
	GtkUIManager *ui_manager;
	GtkActionGroup *group;
	GtkAction *action;
	EShell *shell;
	GtkWindow *shell_window;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));
	web_view = E_WEB_VIEW (extensible);

	group = gtk_action_group_new ("prefer-plain");
	gtk_action_group_add_actions (group, entries, G_N_ELEMENTS (entries), NULL);

	ui_manager = e_web_view_get_ui_manager (web_view);
	gtk_ui_manager_insert_action_group (ui_manager, group, 0);
	gtk_ui_manager_add_ui_from_string (ui_manager, ui_webview, -1, NULL);

	action = gtk_action_group_get_action (group, "show-plain-text-part");
	g_signal_connect (action, "activate",
		G_CALLBACK (toggle_part), extension);

	action = gtk_action_group_get_action (group, "show-text-html-part");
	g_signal_connect (action, "activate",
		G_CALLBACK (toggle_part), extension);


	shell = e_shell_get_default ();
	shell_window = e_shell_get_active_window (shell);
	if (E_IS_SHELL_WINDOW (shell_window)) {
		ui_manager = e_shell_window_get_ui_manager (E_SHELL_WINDOW (shell_window));
	} else if (E_IS_MAIL_BROWSER (shell_window)) {
		ui_manager = e_mail_browser_get_ui_manager (E_MAIL_BROWSER (shell_window));
	} else {
		return NULL;
	}

	gtk_ui_manager_insert_action_group (ui_manager, group, 0);
	gtk_ui_manager_add_ui_from_string (ui_manager, ui_reader, -1, NULL);

	return group;
}

static void
mail_display_popup_prefer_plain_update_actions (EMailDisplayPopupExtension *extension,
						WebKitHitTestResult *context)
{
	EMailDisplay *display;
	GtkAction *action;
	WebKitDOMNode *node;
	gchar *uri, *part_id, *pos, *prefix;
	SoupURI *soup_uri;
	GHashTable *query;
	EMailPartList *part_list;
	GSList *iter;
	gboolean is_text_plain;
	const gchar *action_name;
	EMailDisplayPopupPreferPlain *pp_extension;

	display = E_MAIL_DISPLAY (e_extension_get_extensible (
			E_EXTENSION (extension)));

	pp_extension = E_MAIL_DISPLAY_POPUP_PREFER_PLAIN (extension);

	if (!pp_extension->action_group)
		pp_extension->action_group = create_group (extension);

	g_object_get (context, "inner-node", &node, NULL);

	if (!node) {
		gtk_action_group_set_visible (pp_extension->action_group, FALSE);
		return;
	}

	pp_extension->document = webkit_dom_node_get_owner_document (node);
	uri = webkit_dom_document_get_document_uri (pp_extension->document);

	soup_uri = soup_uri_new (uri);
	if (!soup_uri || !soup_uri->query) {
		gtk_action_group_set_visible (pp_extension->action_group, FALSE);
		if (soup_uri)
			soup_uri_free (soup_uri);
		g_free (uri);
		return;
	}

	query = soup_form_decode (soup_uri->query);
	part_id = g_hash_table_lookup (query, "part_id");
	if (part_id == NULL) {
		gtk_action_group_set_visible (pp_extension->action_group, FALSE);
		g_hash_table_destroy (query);
		soup_uri_free (soup_uri);
		g_free (uri);
		return;
	}

	pos = strstr (part_id, ".alternative-prefer-plain.");
	if (!pos) {
		gtk_action_group_set_visible (pp_extension->action_group, FALSE);
		g_hash_table_destroy (query);
		soup_uri_free (soup_uri);
		g_free (uri);
		return;
	}

	/* Don't display the actions on any other than text/plain or text/html parts */
	if (!strstr (pos, "plain_text") && !strstr (pos, "text_html")) {
		gtk_action_group_set_visible (pp_extension->action_group, FALSE);
		g_hash_table_destroy (query);
		soup_uri_free (soup_uri);
		g_free (uri);
		return;
	}

	/* Check whether the displayed part is text_plain */
	is_text_plain = (strstr (pos + ID_LEN, "plain_text") != NULL);

	/* It is! Hide the menu action */
	if (is_text_plain) {
		action = gtk_action_group_get_action (
			pp_extension->action_group, "show-plain-text-part");
		gtk_action_set_visible (action, FALSE);
	} else {
		action = gtk_action_group_get_action (
			pp_extension->action_group, "show-text-html-part");
		gtk_action_set_visible (action, FALSE);
	}

	/* Now check whether HTML version exists, if it does enable the action */
	prefix = g_strndup (part_id, (pos - part_id) + ID_LEN - 1);

	action_name = NULL;
	part_list = e_mail_display_get_parts_list (display);
	for (iter = part_list->list; iter; iter = g_slist_next (iter)) {
		EMailPart *p = iter->data;
		if (!p)
			continue;

		if (g_str_has_prefix (p->id, prefix) &&
		    (strstr (p->id, "text_html") || strstr (p->id, "plain_text"))) {

			pos = strstr (p->id, ".alternative-prefer-plain.");

			if (is_text_plain) {
				if (strstr (pos + ID_LEN, "text_html") != NULL) {
					action_name = "show-text-html-part";
					set_text_html_id (pp_extension, p->id);
					set_text_plain_id (pp_extension, NULL);
					break;
				}
			} else {
				if (strstr (pos + ID_LEN, "plain_text") != NULL) {
					action_name = "show-plain-text-part";
					set_text_html_id (pp_extension, NULL);
					set_text_plain_id (pp_extension, p->id);
					break;
				}
			}
		}
	}

	if (action_name) {
		action = gtk_action_group_get_action (
			pp_extension->action_group, action_name);
		gtk_action_group_set_visible (pp_extension->action_group, TRUE);
		gtk_action_set_visible (action, TRUE);
	} else {
		gtk_action_group_set_visible (pp_extension->action_group, FALSE);
	}

	g_free (prefix);
	g_hash_table_destroy (query);
	soup_uri_free (soup_uri);
	g_free (uri);
}

void
e_mail_display_popup_prefer_plain_type_register (GTypeModule *type_module)
{
	e_mail_display_popup_prefer_plain_register_type (type_module);
}

static void
e_mail_display_popup_prefer_plain_class_init (EMailDisplayPopupPreferPlainClass *klass)
{
	EExtensionClass *extension_class;

	e_mail_display_popup_prefer_plain_parent_class = g_type_class_peek_parent (klass);

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_MAIL_DISPLAY;
}

static void
e_mail_display_popup_extension_interface_init (EMailDisplayPopupExtensionInterface *iface)
{
	iface->update_actions = mail_display_popup_prefer_plain_update_actions;
}

void
e_mail_display_popup_prefer_plain_class_finalize (EMailDisplayPopupPreferPlainClass *klass)
{

}


static void
e_mail_display_popup_prefer_plain_init (EMailDisplayPopupPreferPlain *extension)
{
	extension->action_group = NULL;
	extension->text_html_id = NULL;
	extension->text_plain_id = NULL;
	extension->document = NULL;
}