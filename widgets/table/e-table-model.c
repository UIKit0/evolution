/*
 * e-table-model.c: a Table Model
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999 Helix Code, Inc.
 */
#include <config.h>
#include <gtk/gtksignal.h>
#include "e-table-model.h"

#define ETM_CLASS(e) ((ETableModelClass *)((GtkObject *)e)->klass)

static GtkObjectClass *e_table_model_parent_class;

enum {
	MODEL_CHANGED,
	ROW_SELECTION,
	LAST_SIGNAL
};

static guint etm_signals [LAST_SIGNAL] = { 0, };

int
e_table_model_column_count (ETableModel *etable)
{
	return ETM_CLASS (etable)->column_count (etable);
}

const char *
e_table_model_column_name (ETableModel *etable, int col)
{
	return ETM_CLASS (etable)->column_name (etable, col);
}

int
e_table_model_row_count (ETableModel *etable)
{
	return ETM_CLASS (etable)->row_count (etable);
}

void *
e_table_model_value_at (ETableModel *etable, int col, int row)
{
	return ETM_CLASS (etable)->value_at (etable, col, row);
}

void
e_table_model_set_value_at (ETableModel *etable, int col, int row, void *data)
{
	return ETM_CLASS (etable)->set_value_at (etable, col, row, data);
}

gboolean
e_table_model_is_cell_editable (ETableModel *etable, int col, int row)
{
	return ETM_CLASS (etable)->is_cell_editable (etable, col, row);
}

static void
e_table_model_destroy (GtkObject *object)
{
	GSList *l;
	
	ETableModel *etable = (ETableModel *) object;

	if (e_table_model_parent_class->destroy)
		(*e_table_model_parent_class->destroy)(object);
}

static void
e_table_model_class_init (GtkObjectClass *object_class)
{
	e_table_model_parent_class = gtk_type_class (gtk_object_get_type ());
	
	object_class->destroy = e_table_model_destroy;

	etm_signals [MODEL_CHANGED] =
		gtk_signal_new ("model_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETableModelClass, model_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	etm_signals [ROW_SELECTION] =
		gtk_signal_new ("row_selection",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETableModelClass, row_selection),
				gtk_marshal_NONE__INT_INT,
				GTK_TYPE_NONE, 2, GTK_TYPE_INT, GTK_TYPE_INT);
	
	gtk_object_class_add_signals (object_class, etm_signals, LAST_SIGNAL);
}

static void
e_table_model_init (ETableModel *etm)
{
	etm->row_selected = -1;
}

GtkType
e_table_model_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"ETableModel",
			sizeof (ETableModel),
			sizeof (ETableModelClass),
			(GtkClassInitFunc) e_table_model_class_init,
			(GtkObjectInitFunc) e_table_model_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_object_get_type (), &info);
	}

	return type;
}

void
e_table_model_changed (ETableModel *e_table_model)
{
	gtk_signal_emit (GTK_OBJECT (e_table_model),
			 etm_signals [MODEL_CHANGED]);
}

#if 0
int
e_table_model_max_col_width (ETableModel *etm, int col)
{
	const int nvals = e_table_model_row_count (etm);
	int max = 0;
	int row;
	
	for (row = 0; row < nvals; row++){
		int w;
		
		w = e_table_model_cell_width (etm, col, i);

		if (w > max)
			max = w;
	}

	return max;
}
#endif

void
e_table_model_select_row (ETableModel *etm, int row)
{
	gtk_signal_emit (GTK_OBJECT (etm), etm_signals [ROW_SELECTION], row, 1);
	etm->row_selected = row;
}

void
e_table_model_unselect_row (ETableModel *etm, int row)
{
	if (etm->row_selected != -1){
		gtk_signal_emit (
			GTK_OBJECT (etm), etm_signals [ROW_SELECTION],
			etm->row_selected, 0);
	}
	
	etm->row_selected = -1;
}

gint
e_table_model_get_selected_row (ETableModel *etm)
{
	return etm->row_selected;
}

