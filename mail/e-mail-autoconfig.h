/*
 * e-mail-autoconfig.h
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
 */

#ifndef E_MAIL_AUTOCONFIG_H
#define E_MAIL_AUTOCONFIG_H

#include <gio/gio.h>
#include <libedataserver/e-source.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_AUTOCONFIG \
	(e_mail_autoconfig_get_type ())
#define E_MAIL_AUTOCONFIG(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_AUTOCONFIG, EMailAutoconfig))
#define E_MAIL_AUTOCONFIG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_AUTOCONFIG, EMailAutoconfigClass))
#define E_IS_MAIL_AUTOCONFIG(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_AUTOCONFIG))
#define E_IS_MAIL_AUTOCONFIG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_AUTOCONFIG))
#define E_MAIL_AUTOCONFIG_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_AUTOCONFIG, EMailAutoconfigClass))

G_BEGIN_DECLS

typedef struct _EMailAutoconfig EMailAutoconfig;
typedef struct _EMailAutoconfigClass EMailAutoconfigClass;
typedef struct _EMailAutoconfigPrivate EMailAutoconfigPrivate;

struct _EMailAutoconfig {
	GObject parent;
	EMailAutoconfigPrivate *priv;
};

struct _EMailAutoconfigClass {
	GObjectClass parent_class;
};

GType		e_mail_autoconfig_get_type	(void) G_GNUC_CONST;
EMailAutoconfig *
		e_mail_autoconfig_new_sync	(const gchar *email_address,
						 GCancellable *cancellable,
						 GError **error);
void		e_mail_autoconfig_new		(const gchar *email_address,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
EMailAutoconfig *
		e_mail_autoconfig_finish	(GAsyncResult *result,
						 GError **error);
const gchar *	e_mail_autoconfig_get_email_address
						(EMailAutoconfig *autoconfig);
const gchar *	e_mail_autoconfig_get_markup_content
						(EMailAutoconfig *autoconfig);
gboolean	e_mail_autoconfig_set_imap_details
						(EMailAutoconfig *autoconfig,
						 ESource *imap_source);
gboolean	e_mail_autoconfig_set_pop3_details
						(EMailAutoconfig *autoconfig,
						 ESource *pop3_source);
gboolean	e_mail_autoconfig_set_smtp_details
						(EMailAutoconfig *autoconfig,
						 ESource *smtp_source);

G_END_DECLS

#endif /* E_MAIL_AUTOCONFIG_H */
