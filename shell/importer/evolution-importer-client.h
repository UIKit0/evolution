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
 *
 *
 * Authors:
 *		Iain Holmes  <iain@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EVOLUTION_IMPORTER_CLIENT_H
#define EVOLUTION_IMPORTER_CLIENT_H

#include <glib.h>
#include <gtk/gtkwidget.h>
#include <importer/evolution-importer.h>
#include <importer/evolution-importer-listener.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif

#define EVOLUTION_TYPE_IMPORTER_CLIENT (evolution_importer_client_get_type ())
#define EVOLUTION_IMPORTER_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVOLUTION_TYPE_IMPORTER_CLIENT, EvolutionImporterClient))
#define EVOLUTION_IMPORTER_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), EVOLUTION_TYPE_IMPORTER_CLIENT, EvolutionImporterClientClass))
#define EVOLUTION_IS_IMPORTER_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVOLUTION_TYPE_IMPORTER_CLIENT))
#define EVOLUTION_IS_IMPORTER_CLIENT_CLASS(klass) (G_TYPE_CHECK_INSTANCE_TYPE ((klass), EVOLUTION_TYPE_IMPORTER_CLIENT))


typedef struct _EvolutionImporterClient EvolutionImporterClient;
typedef struct _EvolutionImporterClientClass EvolutionImporterClientClass;

struct _EvolutionImporterClient {
	GObject parent_type;

	GNOME_Evolution_Importer objref;
};

struct _EvolutionImporterClientClass {
	GObjectClass parent_class;
};

GType evolution_importer_client_get_type (void);

EvolutionImporterClient *evolution_importer_client_new (const CORBA_Object objref);
EvolutionImporterClient *evolution_importer_client_new_from_id (const char *id);

GtkWidget *evolution_importer_client_create_control (EvolutionImporterClient *client);
gboolean evolution_importer_client_support_format (EvolutionImporterClient *client,
						   const char *filename);
gboolean evolution_importer_client_load_file (EvolutionImporterClient *client,
					      const char *filename);
void evolution_importer_client_process_item (EvolutionImporterClient *client,
					     EvolutionImporterListener *listener);
const char *evolution_importer_client_get_error (EvolutionImporterClient *client);

#ifdef __cplusplus
}
#endif

#endif
