/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* test-reflow.c
 *
 * Copyright (C) 2000 Helix Code, Inc.
 * Author: Chris Lahey <clahey@helixcode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */



#include "config.h"

#include <gnome.h>
#include "e-reflow.h"
#include "e-minicard.h"

/* This is a horrible thing to do, but it is just a test. */
GnomeCanvasItem *reflow;
GnomeCanvasItem *rect;
GtkAllocation last_alloc;

static void destroy_callback(GtkWidget *app, gpointer data)
{
  exit(0);
}

static void allocate_callback(GtkWidget *canvas, GtkAllocation *allocation, gpointer data)
{
  double width;
  last_alloc = *allocation;
  gnome_canvas_item_set( reflow,
			 "height", (double) allocation->height,
			 NULL );
  gtk_object_get(GTK_OBJECT(reflow),
		 "width", &width,
		 NULL);
  width = MAX(width, allocation->width);
  gnome_canvas_set_scroll_region(GNOME_CANVAS( canvas ), 0, 0, width, allocation->height );
  gnome_canvas_item_set( rect,
			 "x2", (double) width,
			 "y2", (double) allocation->height,
			 NULL );
}

static void resize(GnomeCanvasItem *item, gpointer data)
{
	double width;
	gtk_object_get(GTK_OBJECT(reflow),
		 "width", &width,
		       NULL);
	width = MAX(width, last_alloc.width);
	gnome_canvas_set_scroll_region(item->canvas , 0, 0, width, last_alloc.height );
	gnome_canvas_item_set( rect,
			       "x2", (double) width,
			       "y2", (double) last_alloc.height,
			       NULL );	
}

static void about_callback( GtkWidget *widget, gpointer data )
{
  
  const gchar *authors[] =
  {
    "Christopher James Lahey <clahey@umich.edu>",
    NULL
  };

  GtkWidget *about =
    gnome_about_new ( _( "Reflow Test" ), VERSION,
		      _( "Copyright (C) 2000, Helix Code, Inc." ),
		      authors,
		      _( "This should test the reflow canvas item" ),
		      NULL);
  gtk_widget_show (about);                                            
}

int main( int argc, char *argv[] )
{
  GtkWidget *app;
  GtkWidget *canvas;
  GtkWidget *vbox;
  GtkWidget *scrollbar;
  int i;

  /*  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
      textdomain (PACKAGE);*/

  gnome_init( "Reflow Test", VERSION, argc, argv);
  app = gnome_app_new("Reflow Test", NULL);

  vbox = gtk_vbox_new(FALSE, 0);

  canvas = gnome_canvas_new();
  rect = gnome_canvas_item_new( gnome_canvas_root( GNOME_CANVAS( canvas ) ),
				gnome_canvas_rect_get_type(),
				"x1", (double) 0,
				"y1", (double) 0,
				"x2", (double) 100,
				"y2", (double) 100,
				"fill_color", "white",
				NULL );
  reflow = gnome_canvas_item_new( gnome_canvas_root( GNOME_CANVAS( canvas ) ),
				  e_reflow_get_type(),
				  "x", (double) 0,
				  "y", (double) 0,
				  "height", (double) 100,
				  NULL );
  gtk_signal_connect( GTK_OBJECT( reflow ), "resize",
		      GTK_SIGNAL_FUNC( resize ),
		      ( gpointer ) app);
  for ( i = 0; i < 200; i++ )
    {
      GnomeCanvasItem *item;
      item = gnome_canvas_item_new( GNOME_CANVAS_GROUP(reflow),
				    e_minicard_get_type(),
				    NULL);
      e_reflow_add_item(E_REFLOW(reflow), item);
    }
  gnome_canvas_set_scroll_region ( GNOME_CANVAS( canvas ),
				   0, 0,
				   100, 100 );

  gtk_box_pack_start(GTK_BOX(vbox), canvas, TRUE, TRUE, 0);

  scrollbar = gtk_hscrollbar_new(gtk_layout_get_hadjustment(GTK_LAYOUT(canvas)));

  gtk_box_pack_start(GTK_BOX(vbox), scrollbar, FALSE, FALSE, 0);

  gnome_app_set_contents( GNOME_APP( app ), vbox );

  /* Connect the signals */
  gtk_signal_connect( GTK_OBJECT( app ), "destroy",
		      GTK_SIGNAL_FUNC( destroy_callback ),
		      ( gpointer ) app );

  gtk_signal_connect( GTK_OBJECT( canvas ), "size_allocate",
		      GTK_SIGNAL_FUNC( allocate_callback ),
		      ( gpointer ) app );

  gtk_widget_show_all( app );

  gtk_main(); 

  /* Not reached. */
  return 0;
}
