
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/htmlengine.h>
#include <gtkhtml/htmlobject.h>
#include <gtkhtml/htmliframe.h>
#include <gtkhtml/htmlinterval.h>
#include <gtkhtml/gtkhtml-embedded.h>

#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkarrow.h>

#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>

#include <libgnomevfs/gnome-vfs-mime-handlers.h>

#if 0
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#endif

#include <camel/camel-stream.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-multipart.h>
#include <camel/camel-multipart-signed.h>
#include <camel/camel-gpg-context.h>

#include <e-util/e-msgport.h>

#include "em-format-html-display.h"
#include "em-marshal.h"
#include "e-searching-tokenizer.h"

#define EFHD_TABLE_OPEN "<table>"

struct _EMFormatHTMLDisplayPrivate {
	int dummy;
};

static int efhd_html_button_press_event (GtkWidget *widget, GdkEventButton *event, EMFormatHTMLDisplay *efh);
static void efhd_html_link_clicked (GtkHTML *html, const char *url, EMFormatHTMLDisplay *efhd);

struct _attach_puri {
	EMFormatPURI puri;

	EMFormatHTMLDisplay *format;
	const EMFormatHandler *handle;

	/* for the > and V buttons */
	GtkWidget *forward, *down;
	/* currently no way to correlate this data to the frame :( */
	GtkHTML *frame;
	CamelStream *output;
	unsigned int shown:1;
};

static void efhd_iframe_created(GtkHTML *html, GtkHTML *iframe, EMFormatHTMLDisplay *efh);
static void efhd_url_requested(GtkHTML *html, const char *url, GtkHTMLStream *handle, EMFormatHTMLDisplay *efh);
static gboolean efhd_object_requested(GtkHTML *html, GtkHTMLEmbedded *eb, EMFormatHTMLDisplay *efh);

static void efhd_format(EMFormat *, CamelMedium *);
static void efhd_format_error(EMFormat *emf, CamelStream *stream, const char *txt);
static void efhd_format_message(EMFormat *, CamelStream *, CamelMedium *);
static void efhd_format_source(EMFormat *, CamelStream *, CamelMimePart *);
static void efhd_format_attachment(EMFormat *, CamelStream *, CamelMimePart *, const char *, const EMFormatHandler *);

static void efhd_builtin_init(EMFormatHTMLDisplayClass *efhc);

enum {
	EFHD_LINK_CLICKED,
	EFHD_POPUP_EVENT,
	EFHD_LAST_SIGNAL,
};

static guint efhd_signals[EFHD_LAST_SIGNAL] = { 0 };


static EMFormatHTMLClass *efhd_parent;

static void
efhd_init(GObject *o)
{
	EMFormatHTMLDisplay *efhd = (EMFormatHTMLDisplay *)o;
	GtkStyle *style;
#define efh ((EMFormatHTML *)efhd)

	efhd->priv = g_malloc0(sizeof(*efhd->priv));

	efhd->search_tok = (ESearchingTokenizer *)e_searching_tokenizer_new();
	html_engine_set_tokenizer(efh->html->engine, (HTMLTokenizer *)efhd->search_tok);

	/* we want to convert url's etc */
	efh->text_html_flags |= CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS | CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES;

	/* FIXME: does this have to be re-done every time we draw? */

	/* My favorite thing to do... muck around with colors so we respect people's stupid themes.
	   However, we only do this if we are rendering to the screen -- we ignore the theme
	   when we are printing. */
	style = gtk_widget_get_style((GtkWidget *)efh->html);
	if (style) {
		int state = GTK_WIDGET_STATE((GtkWidget *)efh->html);
		gushort r, g, b;
#define SCALE (238)

		/* choose a suitably darker or lighter colour */
		r = style->base[state].red >> 8;
		g = style->base[state].green >> 8;
		b = style->base[state].blue >> 8;

		if (r+b+g > 128*3) {
			r = (r*SCALE) >> 8;
			g = (g*SCALE) >> 8;
			b = (b*SCALE) >> 8;
		} else {
			r = (255 - (SCALE * (255-r))) >> 8;
			g = (255 - (SCALE * (255-g))) >> 8;
			b = (255 - (SCALE * (255-b))) >> 8;
		}

		efh->header_colour = ((r<<16) | (g<< 8) | b) & 0xffffff;
		
		r = style->text[state].red >> 8;
		g = style->text[state].green >> 8;
		b = style->text[state].blue >> 8;

		efh->text_colour = ((r<<16) | (g<< 8) | b) & 0xffffff;
	}
#undef SCALE
#undef efh
}

static void
efhd_finalise(GObject *o)
{
	EMFormatHTMLDisplay *efhd = (EMFormatHTMLDisplay *)o;

	/* check pending stuff */

	g_free(efhd->priv);

	((GObjectClass *)efhd_parent)->finalize(o);
}

static gboolean
efhd_bool_accumulator(GSignalInvocationHint *ihint, GValue *out, const GValue *in, void *data)
{
	gboolean val = g_value_get_boolean(in);

	g_value_set_boolean(out, val);

	return !val;
}

static void
efhd_class_init(GObjectClass *klass)
{
	((EMFormatClass *)klass)->format = efhd_format;
	((EMFormatClass *)klass)->format_error = efhd_format_error;
	((EMFormatClass *)klass)->format_message = efhd_format_message;
	((EMFormatClass *)klass)->format_source = efhd_format_source;
	((EMFormatClass *)klass)->format_attachment = efhd_format_attachment;

	klass->finalize = efhd_finalise;

	efhd_signals[EFHD_LINK_CLICKED] = 
		g_signal_new("link_clicked",
			     G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(EMFormatHTMLDisplayClass, link_clicked),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__POINTER,
			     G_TYPE_NONE, 1, G_TYPE_POINTER);

	efhd_signals[EFHD_POPUP_EVENT] = 
		g_signal_new("popup_event",
			     G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(EMFormatHTMLDisplayClass, popup_event),
			     efhd_bool_accumulator, NULL,
			     em_marshal_BOOLEAN__BOXED_POINTER_POINTER,
			     G_TYPE_BOOLEAN, 3,
			     GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE,
			     G_TYPE_POINTER, G_TYPE_POINTER);

	efhd_builtin_init((EMFormatHTMLDisplayClass *)klass);
}

GType
em_format_html_display_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFormatHTMLDisplayClass),
			NULL, NULL,
			(GClassInitFunc)efhd_class_init,
			NULL, NULL,
			sizeof(EMFormatHTMLDisplay), 0,
			(GInstanceInitFunc)efhd_init
		};
		efhd_parent = g_type_class_ref(em_format_html_get_type());
		type = g_type_register_static(em_format_html_get_type(), "EMFormatHTMLDisplay", &info, 0);
	}

	return type;
}

EMFormatHTMLDisplay *em_format_html_display_new(void)
{
	EMFormatHTMLDisplay *efhd;

	efhd = g_object_new(em_format_html_display_get_type(), 0);

	g_signal_connect(efhd->formathtml.html, "iframe_created", G_CALLBACK(efhd_iframe_created), efhd);
	g_signal_connect(efhd->formathtml.html, "link_clicked", G_CALLBACK(efhd_html_link_clicked), efhd);
	g_signal_connect(efhd->formathtml.html, "button_press_event", G_CALLBACK(efhd_html_button_press_event), efhd);

	return efhd;
}

void em_format_html_display_goto_anchor(EMFormatHTMLDisplay *efhd, const char *name)
{
	printf("FIXME: go to anchor '%s'\n", name);
}

void
em_format_html_display_set_search(EMFormatHTMLDisplay *efhd, int type, GSList *strings)
{
	efhd->search_matches = 0;

	switch(type&3) {
	case EM_FORMAT_HTML_DISPLAY_SEARCH_PRIMARY:
		e_searching_tokenizer_set_primary_case_sensitivity(efhd->search_tok, (type&EM_FORMAT_HTML_DISPLAY_SEARCH_ICASE) == 0);
		e_searching_tokenizer_set_primary_search_string(efhd->search_tok, NULL);
		while (strings) {
			e_searching_tokenizer_add_primary_search_string(efhd->search_tok, strings->data);
			strings = strings->next;
		}
		break;
	case EM_FORMAT_HTML_DISPLAY_SEARCH_SECONDARY:
	default:
		e_searching_tokenizer_set_secondary_case_sensitivity(efhd->search_tok, (type&EM_FORMAT_HTML_DISPLAY_SEARCH_ICASE) == 0);
		e_searching_tokenizer_set_secondary_search_string(efhd->search_tok, NULL);
		while (strings) {
			e_searching_tokenizer_add_secondary_search_string(efhd->search_tok, strings->data);
			strings = strings->next;
		}
		break;
	}

	printf("redrawing with search\n");
	em_format_format_clone((EMFormat *)efhd, ((EMFormat *)efhd)->message, (EMFormat *)efhd);
}

/* ********************************************************************** */

static void
efhd_iframe_created(GtkHTML *html, GtkHTML *iframe, EMFormatHTMLDisplay *efh)
{
	const char *url;

	printf("Iframe created %p ... \n", iframe);

	g_signal_connect(iframe, "button_press_event", G_CALLBACK (efhd_html_button_press_event), efh);

	return;
}

static int
efhd_html_button_press_event (GtkWidget *widget, GdkEventButton *event, EMFormatHTMLDisplay *efhd)
{
	HTMLEngine *e;
	HTMLPoint *point;
	const char *url;
	gboolean res = FALSE;

	if (event->button != 3)
		return FALSE;

	e = ((GtkHTML *)widget)->engine;
	point = html_engine_get_point_at(e, event->x, event->y, FALSE);			
	if (point == NULL)
		return FALSE;

	printf("popup button pressed\n");

	if ( (url = html_object_get_src(point->object)) != NULL
	     || (url = html_object_get_url(point->object)) != NULL) {
		EMFormatPURI *puri;
		char *uri;

		uri = gtk_html_get_url_object_relative((GtkHTML *)widget, point->object, url);
		puri = em_format_find_puri((EMFormat *)efhd, uri);

		printf("poup event, uri = '%s' part = '%p'\n", uri, puri?puri->part:NULL);

		g_signal_emit((GtkObject *)efhd, efhd_signals[EFHD_POPUP_EVENT], 0, event, uri, puri?puri->part:NULL, &res);
		g_free(uri);
	}
	
	html_point_destroy(point);

	return res;
}

static void
efhd_html_link_clicked (GtkHTML *html, const char *url, EMFormatHTMLDisplay *efhd)
{
	printf("link clicked event '%s'\n", url);
	g_signal_emit((GObject *)efhd, efhd_signals[EFHD_LINK_CLICKED], 0, url);
}

/* ********************************************************************** */

struct _signed_puri
{
	EMFormatPURI puri;

	unsigned int shown:1;
};

static void
efhd_signed_frame(EMFormat *emf, CamelStream *stream, EMFormatPURI *emfpuri)
{
	struct _signed_puri *spuri = (struct _signed_puri *)emfpuri;
	char *classid;
	static int iconid;
	CamelMimePart *part;

	classid = g_strdup_printf("multipart-signed:///icon/%d", iconid++);

	/* wtf is this so fugly? */
	camel_stream_printf(stream,
			    "<br><table cellspacing=0 cellpadding=0>"
			    "<tr><td><table width=10 cellspacing=0 cellpadding=0>"
			    "<tr><td></td></tr></table></td>"
			    "<td><object classid=\"%s\"></object></td>"
			    "<td><table width=3 cellspacing=0 cellpadding=0>"
			    "<tr><td></td></tr></table></td>"
			    "<td><font size=-1>%s</font></td></tr>"
			    "<tr><td height=10>"
			    "<table cellspacing=0 cellpadding=0><tr>"
			    "<td height=10><a name=\"glue\"></td></tr>"
			    "</table></td></tr></table>\n",
			    classid,
			    _("This message is digitally signed. Click the lock icon for more information."));

	g_free(classid);

	camel_stream_close(stream);
}

static void
efhd_multipart_signed (EMFormat *emf, CamelStream *stream, CamelMimePart *part, const EMFormatHandler *info)
{
	char *classid;
	struct _signed_puri *puri;
	static int signedid;
	CamelMultipartSigned *mps;
	CamelMimePart *cpart;

	mps = (CamelMultipartSigned *)camel_medium_get_content_object((CamelMedium *)part);
	if (!CAMEL_IS_MULTIPART_SIGNED(mps)
	    || (cpart = camel_multipart_get_part((CamelMultipart *)mps, CAMEL_MULTIPART_SIGNED_CONTENT)) == NULL) {
		em_format_format_source(emf, stream, part);
		return;
	}

	em_format_part(emf, stream, cpart);

	if (0) {
		camel_stream_printf(stream, "inlined signature ...");
		em_format_html_multipart_signed_sign(emf, stream, part);
	} else {
		classid = g_strdup_printf("multipart-signed:///em-format-html-display/%p/%d", part, signedid++);
		puri = (struct _signed_puri *)em_format_add_puri(emf, sizeof(*puri), classid, part, efhd_signed_frame);
		camel_stream_printf(stream, "<iframe src=\"%s\" frameborder=1 marginheight=0 marginwidth=0>%s</iframe>\n", classid, _("Signature verification could not be executed"));
		g_free(classid);
	}
}

/* ********************************************************************** */

static EMFormatHandler type_builtin_table[] = {
	{ "multipart/signed", (EMFormatFunc)efhd_multipart_signed },
};

static void
efhd_builtin_init(EMFormatHTMLDisplayClass *efhc)
{
	int i;

	for (i=0;i<sizeof(type_builtin_table)/sizeof(type_builtin_table[0]);i++)
		em_format_class_add_handler((EMFormatClass *)efhc, &type_builtin_table[i]);
}

/* ********************************************************************** */

/* just inherit at the moment ...? */
static void efhd_format(EMFormat *emf, CamelMedium *part)
{
	((EMFormatClass *)efhd_parent)->format(emf, part);

	((EMFormatHTMLDisplay *)emf)->search_matches = e_searching_tokenizer_match_count(((EMFormatHTMLDisplay *)emf)->search_tok);
}

static void efhd_format_error(EMFormat *emf, CamelStream *stream, const char *txt)
{
	((EMFormatClass *)efhd_parent)->format_error(emf, stream, txt);
}

static void efhd_format_message(EMFormat *emf, CamelStream *stream, CamelMedium *part)
{
	((EMFormatClass *)efhd_parent)->format_message(emf, stream, part);
}

static void efhd_format_source(EMFormat *emf, CamelStream *stream, CamelMimePart *part)
{
	((EMFormatClass *)efhd_parent)->format_source(emf, stream, part);
}

/* ********************************************************************** */

/* if it hasn't been processed yet, format the attachment */
static void
efhd_attachment_show(GtkWidget *w, struct _attach_puri *info)
{
	printf("show attachment button called\n");

	/* FIXME: track shown state in parent */

	if (info->shown) {
		printf("hiding\n");
		info->shown = FALSE;
		if (info->frame)
			gtk_widget_hide((GtkWidget *)info->frame);
		gtk_widget_show(info->forward);
		gtk_widget_hide(info->down);
	} else {
		printf("showing\n");
		info->shown = TRUE;
		if (info->frame)
			gtk_widget_show((GtkWidget *)info->frame);
		gtk_widget_hide(info->forward);
		gtk_widget_show(info->down);

		/* have we decoded it yet? */
		if (info->output) {
			info->handle->handler((EMFormat *)info->format, info->output, info->puri.part, info->handle);
			camel_stream_close(info->output);
			camel_object_unref(info->output);
			info->output = NULL;
		}
	}

	em_format_set_inline((EMFormat *)info->format, info->puri.part, info->shown);
}

static gboolean
efhd_attachment_popup(GtkWidget *w, GdkEventButton *event, struct _attach_puri *info)
{
	GtkMenu *menu;
	GtkWidget *item;

	/* FIXME FIXME
	   How can i do this with plugins!?
	   extension point=com.ximian.evolution.mail.attachmentPopup?? */

	printf("attachment popup, button %d\n", event->button);

	if (event->button != 1 && event->button != 3) {
		/* ?? gtk_propagate_event(GTK_WIDGET (user_data), (GdkEvent *)event);*/
		return FALSE;
	}

	menu = (GtkMenu *)gtk_menu_new();
	item = gtk_menu_item_new_with_mnemonic(_("Save Attachment..."));
	gtk_menu_shell_append((GtkMenuShell *)menu, item);

	/* FIXME: bonobo component handlers? */
	if (info->handle) {
		GList *apps;

		if (info->shown) {
			item = gtk_menu_item_new_with_mnemonic(_("Hide"));
		} else {
			item = gtk_menu_item_new_with_mnemonic(_("View Inline"));
		}
		g_signal_connect(item, "activate", G_CALLBACK(efhd_attachment_show), info);
		gtk_menu_shell_append((GtkMenuShell *)menu, item);

		apps = gnome_vfs_mime_get_short_list_applications(info->handle->mime_type);
		if (apps) {
			GList *l = apps;
			GString *label = g_string_new("");

			while (l) {
				GnomeVFSMimeApplication *app = l->data;
			
				g_string_printf(label, _("Open in %s..."), app->name);
				item = gtk_menu_item_new_with_label(label->str);
				gtk_menu_shell_append((GtkMenuShell *)menu, item);
				l = l->next;
			}
			g_string_free(label, TRUE);
			g_list_free(apps);
		}
	}

	gtk_widget_show_all((GtkWidget *)menu);
	g_signal_connect(menu, "selection_done", G_CALLBACK(gtk_widget_destroy), menu);
	gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button, event->time);

	return TRUE;
}

/* attachment button callback */
static gboolean
efhd_attachment_button(EMFormatHTML *efh, GtkHTMLEmbedded *eb, EMFormatHTMLPObject *pobject)
{
	struct _attach_puri *info;
	GtkWidget *hbox, *w, *button, *mainbox;

	/* FIXME: handle default shown case */
	printf("adding attachment button/content\n");

	info = (struct _attach_puri *)em_format_find_puri((EMFormat *)efh, pobject->classid);
	g_assert(info != NULL);
	g_assert(info->forward == NULL);

	mainbox = gtk_hbox_new(FALSE, 0);

	button = gtk_button_new();
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

	if (info->handle)
		g_signal_connect(button, "clicked", G_CALLBACK(efhd_attachment_show), info);
	else
		gtk_widget_set_sensitive(button, FALSE);

	hbox = gtk_hbox_new(FALSE, 2);
	info->forward = gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD, GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start((GtkBox *)hbox, info->forward, TRUE, TRUE, 0);
	if (info->handle) {
		info->down = gtk_image_new_from_stock(GTK_STOCK_GO_DOWN, GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start((GtkBox *)hbox, info->down, TRUE, TRUE, 0);
	}
	/* FIXME: Pixmap loader */
	w = gtk_image_new();
	gtk_widget_set_size_request(w, 24, 24);
	gtk_box_pack_start((GtkBox *)hbox, w, TRUE, TRUE, 0);
	gtk_container_add((GtkContainer *)button, hbox);
	gtk_box_pack_start((GtkBox *)mainbox, button, TRUE, TRUE, 0);

	button = gtk_button_new();
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
	gtk_container_add((GtkContainer *)button, gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_ETCHED_IN));
	g_signal_connect(button, "button_press_event", G_CALLBACK(efhd_attachment_popup), info);
	gtk_box_pack_start((GtkBox *)mainbox, button, TRUE, TRUE, 0);

	gtk_widget_show_all(mainbox);

	if (info->shown)
		gtk_widget_hide(info->forward);
	else
		gtk_widget_hide(info->down);

	gtk_container_add((GtkContainer *)eb, mainbox);

	return TRUE;
}

/* frame source callback */
static void
efhd_attachment_frame(EMFormat *emf, CamelStream *stream, EMFormatPURI *puri)
{
	struct _attach_puri *info = (struct _attach_puri *)puri;

	if (info->shown) {
		info->handle->handler(emf, stream, info->puri.part, info->handle);
		camel_stream_close(stream);
	} else {
		/* FIXME: this is leaked if the object is closed without showing it
		   NB: need a virtual puri_free method? */
		info->output = stream;
		camel_object_ref(stream);
	}
}

static void
efhd_format_attachment(EMFormat *emf, CamelStream *stream, CamelMimePart *part, const char *mime_type, const EMFormatHandler *handle)
{
	char *classid;
	struct _attach_puri *info;
	const char *text;
	char *html;
	GString *stext;

	classid = g_strdup_printf("attachment:///em-format-html-display/%p", part);
	info = (struct _attach_puri *)em_format_add_puri(emf, sizeof(*info), classid, part, efhd_attachment_frame);
	em_format_html_add_pobject((EMFormatHTML *)emf, classid, efhd_attachment_button, part);
	info->format = (EMFormatHTMLDisplay *)emf;
	info->handle = handle;
	info->shown = em_format_is_inline(emf, info->puri.part) && handle != NULL;

	camel_stream_write_string(stream,
				  "<table cellspacing=0 cellpadding=0><tr><td>"
				  "<table width=10 cellspacing=0 cellpadding=0>"
				  "<tr><td></td></tr></table></td>");


	camel_stream_printf(stream, "<td><object classid=\"%s\"></object></td>", classid);

	camel_stream_write_string(stream,
				  "<td><table width=3 cellspacing=0 cellpadding=0>"
				  "<tr><td></td></tr></table></td><td><font size=-1>");

	/* output some info about it */
	/* NB: copied from em-format-html */
	/* FIXME: put this in em-format */
	/* FIXME: should we look up mime_type from object again? */
	stext = g_string_new("");
	text = gnome_vfs_mime_get_description(mime_type);
	g_string_append_printf(stext, _("%s attachment"), text?text:mime_type);
	if ((text = camel_mime_part_get_filename (part)))
		g_string_append_printf(stext, " (%s)", text);
	if ((text = camel_mime_part_get_description(part)))
		g_string_append_printf(stext, ", \"%s\"", text);

	html = camel_text_to_html(stext->str, ((EMFormatHTML *)emf)->text_html_flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);
	camel_stream_write_string(stream, html);
	g_free(html);
	g_string_free(stext, TRUE);

	camel_stream_write_string(stream, "</font></td></tr><tr></table>");

	if (handle)
		camel_stream_printf(stream, "<iframe src=\"%s\" frameborder=1 marginheight=0 marginwidth=0>%s</iframe>\n", classid, _("Attachment content could not be loaded"));

	g_free(classid);
}
