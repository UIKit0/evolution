/*
 * session.c: handles the session information and resource manipulation
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 2000 Helix Code, Inc. http://www.helixcode.com
 */
#include <config.h>
#include <gnome.h>
#include "mail.h"
#include "mail-threads.h"
#include "e-util/e-setup.h"

CamelSession *session;
GHashTable *passwords;

/* FIXME: Will this ever be called in a non-async
 * manner? Better hope not, cause if that happens
 * we deadlock....
 */

#define ASYNC_AUTH_CALLBACK

#ifndef ASYNC_AUTH_CALLBACK
static void
request_callback (gchar *string, gpointer data)
{
	char **ans = data;

	if (string)
		*ans = g_strdup(string);
	else
		*ans = NULL;
}
#endif

char *
mail_request_dialog (const char *prompt, gboolean secret, const char *key)
{
#ifndef ASYNC_AUTH_CALLBACK
	GtkWidget *dialog;
#endif

	char *ans;

	if (!passwords)
		passwords = g_hash_table_new (g_str_hash, g_str_equal);

	ans = g_hash_table_lookup (passwords, key);
	if (ans)
		return g_strdup (ans);

#ifndef ASYNC_AUTH_CALLBACK
	/* XXX parent window? */
	dialog = gnome_request_dialog (secret, prompt, NULL, 0,
				       request_callback, &ans, NULL);
	if (!dialog)
		return NULL;
	if (gnome_dialog_run_and_close (GNOME_DIALOG (dialog)) == -1 ||
	    ans == NULL)
		return NULL;
#else
	if (!mail_op_get_password (prompt, secret, &ans))
		return NULL;
#endif

	g_hash_table_insert (passwords, g_strdup (key), g_strdup (ans));
	return ans;
}

static char *
auth_callback (CamelAuthCallbackMode mode, char *data, gboolean secret,
	       CamelService *service, char *item, CamelException *ex)
{
	char *key, *ans;

	if (!passwords)
		passwords = g_hash_table_new (g_str_hash, g_str_equal);

	key = g_strdup_printf ("%s:%s",
			       camel_url_to_string (service->url, FALSE),
			       item);

	if (mode == CAMEL_AUTHENTICATOR_TELL) {
		if (!data) {
			g_hash_table_remove (passwords, key);
			g_free (key);
		} else {
			gpointer old_key, old_data;

			if (g_hash_table_lookup_extended (passwords, key,
							  &old_key,
							  &old_data)) {
				g_hash_table_insert (passwords, old_key, data);
				g_free (old_data);
				g_free (key);
			} else
				g_hash_table_insert (passwords, key, data);
		}

		return NULL;
	}

	ans = mail_request_dialog (data, secret, key);
	g_free (key);

	if (!ans) {
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     "User canceled operation.");
	}

	return ans;
}

void
session_init (void)
{
	e_setup_base_dir ();
	camel_init ();

	session = camel_session_new (auth_callback);
}

static gboolean
free_entry (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	memset (value, 0, strlen (value));
	g_free (value);
	return TRUE;
}

void
forget_passwords (BonoboUIHandler *uih, void *user_data, const char *path)
{
	g_hash_table_foreach_remove (passwords, free_entry, NULL);
}
