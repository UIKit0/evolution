/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-table-sort-info.c: a Table Model
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Chris Lahey (clahey@ximian.com)
 *
 * (C) 1999, 2000, 2001 Ximian, Inc.
 */
#include <config.h>
#include <gtk/gtksignal.h>
#include "e-table-sort-info.h"
#include "gal/util/e-util.h"
#include "gal/util/e-xml-utils.h"

#define ETM_CLASS(e) ((ETableSortInfoClass *)((GtkObject *)e)->klass)

#define PARENT_TYPE gtk_object_get_type ()
					  

static GtkObjectClass *e_table_sort_info_parent_class;

enum {
	SORT_INFO_CHANGED,
	GROUP_INFO_CHANGED,
	LAST_SIGNAL
};

static guint e_table_sort_info_signals [LAST_SIGNAL] = { 0, };

static void
etsi_destroy (GtkObject *object)
{
	ETableSortInfo *etsi;

	etsi = E_TABLE_SORT_INFO (object);
	
	if (etsi->groupings)
		g_free(etsi->groupings);
	if (etsi->sortings)
		g_free(etsi->sortings);
}

static void
e_table_sort_info_init (ETableSortInfo *info)
{
	info->group_count = 0;
	info->groupings = NULL;
	info->sort_count = 0;
	info->sortings = NULL;
	info->frozen = 0;
	info->sort_info_changed = 0;
	info->group_info_changed = 0;
}

static void
e_table_sort_info_class_init (ETableSortInfoClass *klass)
{
	GtkObjectClass *object_class;

	e_table_sort_info_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class = GTK_OBJECT_CLASS(klass);
	
	object_class->destroy = etsi_destroy;

	e_table_sort_info_signals [SORT_INFO_CHANGED] =
		gtk_signal_new ("sort_info_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETableSortInfoClass, sort_info_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	e_table_sort_info_signals [GROUP_INFO_CHANGED] =
		gtk_signal_new ("group_info_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETableSortInfoClass, group_info_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	klass->sort_info_changed = NULL;
	klass->group_info_changed = NULL;

	gtk_object_class_add_signals (object_class, e_table_sort_info_signals, LAST_SIGNAL);
}

E_MAKE_TYPE(e_table_sort_info, "ETableSortInfo", ETableSortInfo,
	    e_table_sort_info_class_init, e_table_sort_info_init, PARENT_TYPE);

static void
e_table_sort_info_sort_info_changed (ETableSortInfo *info)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (E_IS_TABLE_SORT_INFO (info));
	
	if (info->frozen) {
		info->sort_info_changed = 1;
	} else {
		gtk_signal_emit (GTK_OBJECT (info),
				 e_table_sort_info_signals [SORT_INFO_CHANGED]);
	}
}

static void
e_table_sort_info_group_info_changed (ETableSortInfo *info)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (E_IS_TABLE_SORT_INFO (info));
	
	if (info->frozen) {
		info->group_info_changed = 1;
	} else {
		gtk_signal_emit (GTK_OBJECT (info),
				 e_table_sort_info_signals [GROUP_INFO_CHANGED]);
	}
}

/**
 * e_table_sort_info_freeze:
 * @info: The ETableSortInfo object
 *
 * This functions allows the programmer to cluster various changes to the
 * ETableSortInfo (grouping and sorting) without having the object emit
 * "group_info_changed" or "sort_info_changed" signals on each change.
 *
 * To thaw, invoke the e_table_sort_info_thaw() function, which will
 * trigger any signals that might have been queued.
 */
void 
e_table_sort_info_freeze             (ETableSortInfo *info)
{
	info->frozen++;
}

/**
 * e_table_sort_info_thaw:
 * @info: The ETableSortInfo object
 *
 * This functions allows the programmer to cluster various changes to the
 * ETableSortInfo (grouping and sorting) without having the object emit
 * "group_info_changed" or "sort_info_changed" signals on each change.
 *
 * This function will flush any pending signals that might be emited by
 * this object.
 */
void
e_table_sort_info_thaw               (ETableSortInfo *info)
{
	info->frozen--;
	if (info->frozen != 0)
		return;
	
	if (info->sort_info_changed) {
		info->sort_info_changed = 0;
		e_table_sort_info_sort_info_changed(info);
	}
	if (info->group_info_changed) {
		info->group_info_changed = 0;
		e_table_sort_info_group_info_changed(info);
	}
}

/**
 * e_table_sort_info_grouping_get_count:
 * @info: The ETableSortInfo object
 *
 * Returns: the number of grouping criteria in the object.
 */
guint
e_table_sort_info_grouping_get_count (ETableSortInfo *info)
{
	return info->group_count;
}

static void
e_table_sort_info_grouping_real_truncate  (ETableSortInfo *info, int length)
{
	if (length < info->group_count) {
		info->group_count = length;
	}
	if (length > info->group_count) {
		info->groupings = g_realloc(info->groupings, length * sizeof(ETableSortColumn));
		info->group_count = length;
	}
}

/**
 * e_table_sort_info_grouping_truncate:
 * @info: The ETableSortInfo object
 * @lenght: position where the truncation happens.
 *
 * This routine can be used to reduce or grow the number of grouping
 * criteria in the object.  
 */
void
e_table_sort_info_grouping_truncate  (ETableSortInfo *info, int length)
{
	e_table_sort_info_grouping_real_truncate(info, length);
	e_table_sort_info_group_info_changed(info);
}

/**
 * e_table_sort_info_grouping_get_nth:
 * @info: The ETableSortInfo object
 * @n: Item information to fetch.
 *
 * Returns: the description of the @n-th grouping criteria in the @info object.
 */
ETableSortColumn
e_table_sort_info_grouping_get_nth   (ETableSortInfo *info, int n)
{
	if (n < info->group_count) {
		return info->groupings[n];
	} else {
		ETableSortColumn fake = {0, 0};
		return fake;
	}
}

/**
 * e_table_sort_info_grouping_set_nth:
 * @info: The ETableSortInfo object
 * @n: Item information to fetch.
 * @column: new values for the grouping
 *
 * Sets the grouping criteria for index @n to be given by @column (a column number and
 * whether it is ascending or descending).
 */
void
e_table_sort_info_grouping_set_nth   (ETableSortInfo *info, int n, ETableSortColumn column)
{
	if (n >= info->group_count) {
		e_table_sort_info_grouping_real_truncate(info, n + 1);
	}
	info->groupings[n] = column;
	e_table_sort_info_group_info_changed(info);
}


/**
 * e_table_sort_info_get_count:
 * @info: The ETableSortInfo object
 *
 * Returns: the number of sorting criteria in the object.
 */
guint
e_table_sort_info_sorting_get_count (ETableSortInfo *info)
{
	return info->sort_count;
}

static void
e_table_sort_info_sorting_real_truncate  (ETableSortInfo *info, int length)
{
	if (length < info->sort_count) {
		info->sort_count = length;
	}
	if (length > info->sort_count) {
		info->sortings = g_realloc(info->sortings, length * sizeof(ETableSortColumn));
		info->sort_count = length;
	}
}

/**
 * e_table_sort_info_sorting_truncate:
 * @info: The ETableSortInfo object
 * @lenght: position where the truncation happens.
 *
 * This routine can be used to reduce or grow the number of sort
 * criteria in the object.  
 */
void
e_table_sort_info_sorting_truncate  (ETableSortInfo *info, int length)
{
	e_table_sort_info_sorting_real_truncate  (info, length);
	e_table_sort_info_sort_info_changed(info);
}

/**
 * e_table_sort_info_sorting_get_nth:
 * @info: The ETableSortInfo object
 * @n: Item information to fetch.
 *
 * Returns: the description of the @n-th grouping criteria in the @info object.
 */
ETableSortColumn
e_table_sort_info_sorting_get_nth   (ETableSortInfo *info, int n)
{
	if (n < info->sort_count) {
		return info->sortings[n];
	} else {
		ETableSortColumn fake = {0, 0};
		return fake;
	}
}

/**
 * e_table_sort_info_sorting_get_nth:
 * @info: The ETableSortInfo object
 * @n: Item information to fetch.
 * @column: new values for the sorting
 *
 * Sets the sorting criteria for index @n to be given by @column (a
 * column number and whether it is ascending or descending).
 */
void
e_table_sort_info_sorting_set_nth   (ETableSortInfo *info, int n, ETableSortColumn column)
{
	if (n >= info->sort_count) {
		e_table_sort_info_sorting_real_truncate(info, n + 1);
	}
	info->sortings[n] = column;
	e_table_sort_info_sort_info_changed(info);
}

/**
 * e_table_sort_info_new:
 *
 * This creates a new e_table_sort_info object that contains no
 * grouping and no sorting defined as of yet.  This object is used
 * to keep track of multi-level sorting and multi-level grouping of
 * the ETable.  
 *
 * Returns: A new %ETableSortInfo object
 */
ETableSortInfo *
e_table_sort_info_new (void)
{
	return gtk_type_new (e_table_sort_info_get_type ());
}

/**
 * e_table_sort_info_load_from_node:
 * @info: The ETableSortInfo object
 * @node: pointer to the xmlNode that describes the sorting and grouping information
 * @state_version:
 *
 * This loads the state for the %ETableSortInfo object @info from the
 * xml node @node.
 */
void
e_table_sort_info_load_from_node (ETableSortInfo *info,
				  xmlNode        *node,
				  gdouble         state_version)
{
	int i;
	xmlNode *grouping;

	if (state_version <= 0.05) {
		i = 0;
		for (grouping = node->childs; grouping && !strcmp (grouping->name, "group"); grouping = grouping->childs) {
			ETableSortColumn column;
			column.column = e_xml_get_integer_prop_by_name (grouping, "column");
			column.ascending = e_xml_get_bool_prop_by_name (grouping, "ascending");
			e_table_sort_info_grouping_set_nth(info, i++, column);
		}
		i = 0;
		for (; grouping && !strcmp (grouping->name, "leaf"); grouping = grouping->childs) {
			ETableSortColumn column;
			column.column = e_xml_get_integer_prop_by_name (grouping, "column");
			column.ascending = e_xml_get_bool_prop_by_name (grouping, "ascending");
			e_table_sort_info_sorting_set_nth(info, i++, column);
		}
	} else {
		i = 0;
		for (grouping = node->childs; grouping && !strcmp (grouping->name, "group"); grouping = grouping->next) {
			ETableSortColumn column;
			column.column = e_xml_get_integer_prop_by_name (grouping, "column");
			column.ascending = e_xml_get_bool_prop_by_name (grouping, "ascending");
			e_table_sort_info_grouping_set_nth(info, i++, column);
		}
		i = 0;
		for (; grouping && !strcmp (grouping->name, "leaf"); grouping = grouping->next) {
			ETableSortColumn column;
			column.column = e_xml_get_integer_prop_by_name (grouping, "column");
			column.ascending = e_xml_get_bool_prop_by_name (grouping, "ascending");
			e_table_sort_info_sorting_set_nth(info, i++, column);
		}
	}
	gtk_signal_emit (GTK_OBJECT (info),
			 e_table_sort_info_signals [SORT_INFO_CHANGED]);
	
}

/**
 * e_table_sort_info_save_to_node:
 * @info: The ETableSortInfo object
 * @parent: xmlNode that will be hosting the saved state of the @info object.
 *
 * This function is used
 *
 * Returns: the node that has been appended to @parent as a child containing
 * the sorting and grouping information for this ETableSortInfo object.
 */
xmlNode *
e_table_sort_info_save_to_node (ETableSortInfo *info,
				xmlNode        *parent)
{
	xmlNode *grouping;
	xmlNode *node;
	int i;
	const int sort_count = e_table_sort_info_sorting_get_count (info);
	const int group_count = e_table_sort_info_grouping_get_count (info);
	
	grouping = xmlNewChild (parent, NULL, "grouping", NULL);

	for (i = 0; i < group_count; i++) {
		ETableSortColumn column = e_table_sort_info_grouping_get_nth(info, i);
		xmlNode *new_node = xmlNewChild(grouping, NULL, "group", NULL);

		e_xml_set_integer_prop_by_name (new_node, "column", column.column);
		e_xml_set_bool_prop_by_name (new_node, "ascending", column.ascending);
		node = new_node;
	}

	for (i = 0; i < sort_count; i++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(info, i);
		xmlNode *new_node = xmlNewChild(grouping, NULL, "leaf", NULL);
		
		e_xml_set_integer_prop_by_name (new_node, "column", column.column);
		e_xml_set_bool_prop_by_name (new_node, "ascending", column.ascending);
		node = new_node;
	}

	return grouping;
}

