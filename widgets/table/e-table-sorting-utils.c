/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>
#include <string.h>
#include <e-table-sorting-utils.h>
#include <gal/util/e-util.h>

#define d(x)

/* This takes source rows. */
static int
etsu_compare(ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int row1, int row2)
{
	int j;
	int sort_count = e_table_sort_info_sorting_get_count(sort_info);
	int comp_val = 0;
	int ascending = 1;

	for (j = 0; j < sort_count; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *col;
		col = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (col == NULL)
			col = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		comp_val = (*col->compare)(e_table_model_value_at (source, col->col_idx, row1),
					   e_table_model_value_at (source, col->col_idx, row2));
		ascending = column.ascending;
		if (comp_val != 0)
			break;
	}
	if (comp_val == 0) {
		if (row1 < row2)
			comp_val = -1;
		if (row1 > row2)
			comp_val = 1;
	}
	if (!ascending)
		comp_val = -comp_val;
	return comp_val;
}

typedef struct {
	ETableSortInfo *sort_info;
	void **vals;
	int cols;
	int *ascending;
	GCompareFunc *compare;
} ETableSortClosure;

typedef struct {
	ETableSortInfo *sort_info;
	ETreeModel *tree;
	ETableHeader *full_header;
} ETreeSortClosure;

/* FIXME: Make it not cache the second and later columns (as if anyone cares.) */

static int
e_sort_callback(const void *data1, const void *data2, gpointer user_data)
{
	gint row1 = *(int *)data1;
	gint row2 = *(int *)data2;
	ETableSortClosure *closure = user_data;
	int j;
	int sort_count = e_table_sort_info_sorting_get_count(closure->sort_info);
	int comp_val = 0;
	int ascending = 1;
	for (j = 0; j < sort_count; j++) {
		comp_val = (*(closure->compare[j]))(closure->vals[closure->cols * row1 + j], closure->vals[closure->cols * row2 + j]);
		ascending = closure->ascending[j];
		if (comp_val != 0)
			break;
	}
	if (comp_val == 0) {
		if (row1 < row2)
			comp_val = -1;
		if (row1 > row2)
			comp_val = 1;
	}
	if (!ascending)
		comp_val = -comp_val;
	return comp_val;
}

void
e_table_sorting_utils_sort(ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int *map_table, int rows)
{
	int total_rows;
	int i;
	int j;
	int cols;
	ETableSortClosure closure;

	g_return_if_fail(source != NULL);
	g_return_if_fail(E_IS_TABLE_MODEL(source));
	g_return_if_fail(sort_info != NULL);
	g_return_if_fail(E_IS_TABLE_SORT_INFO(sort_info));
	g_return_if_fail(full_header != NULL);
	g_return_if_fail(E_IS_TABLE_HEADER(full_header));

	total_rows = e_table_model_row_count(source);
	cols = e_table_sort_info_sorting_get_count(sort_info);
	closure.cols = cols;
	closure.vals = g_new(void *, total_rows * cols);
	closure.sort_info = sort_info;
	closure.ascending = g_new(int, cols);
	closure.compare = g_new(GCompareFunc, cols);
	for (j = 0; j < cols; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *col;
		col = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (col == NULL)
			col = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		for (i = 0; i < rows; i++) {
			closure.vals[map_table[i] * cols + j] = e_table_model_value_at (source, col->col_idx, map_table[i]);
		}
		closure.compare[j] = col->compare;
		closure.ascending[j] = column.ascending;
	}

	e_sort(map_table, rows, sizeof(int), e_sort_callback, &closure);

	g_free(closure.vals);
	g_free(closure.ascending);
	g_free(closure.compare);
}

gboolean
e_table_sorting_utils_affects_sort  (ETableSortInfo *sort_info,
				     ETableHeader   *full_header,
				     int             col)
{
	int j;
	int cols;

	g_return_val_if_fail(sort_info != NULL, TRUE);
	g_return_val_if_fail(E_IS_TABLE_SORT_INFO(sort_info), TRUE);
	g_return_val_if_fail(full_header != NULL, TRUE);
	g_return_val_if_fail(E_IS_TABLE_HEADER(full_header), TRUE);

	cols = e_table_sort_info_sorting_get_count(sort_info);

	for (j = 0; j < cols; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *tablecol;
		tablecol = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (tablecol == NULL)
			tablecol = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		if (col == tablecol->col_idx)
			return TRUE;
	}
	return FALSE;
}


/* FIXME: This could be done in time log n instead of time n with a binary search. */
int
e_table_sorting_utils_insert(ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int *map_table, int rows, int row)
{
	int i;

	i = 0;
	/* handle insertions when we have a 'sort group' */
	while (i < rows && etsu_compare(source, sort_info, full_header, map_table[i], row) < 0)
		i++;

	return i;
}

/* FIXME: This could be done in time log n instead of time n with a binary search. */
int
e_table_sorting_utils_check_position (ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int *map_table, int rows, int view_row)
{
	int i;
	int row;

	i = view_row;
	row = map_table[i];

	i = view_row;
	if (i < rows - 1 && etsu_compare(source, sort_info, full_header, map_table[i + 1], row) < 0) {
		i ++;
		while (i < rows - 1 && etsu_compare(source, sort_info, full_header, map_table[i], row) < 0)
			i ++;
	} else if (i > 0 && etsu_compare(source, sort_info, full_header, map_table[i - 1], row) > 0) {
		i --;
		while (i > 0 && etsu_compare(source, sort_info, full_header, map_table[i], row) > 0)
			i --;
	}
	return i;
}




/* This takes source rows. */
static int
etsu_tree_compare(ETreeModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, ETreePath path1, ETreePath path2)
{
	int j;
	int sort_count = e_table_sort_info_sorting_get_count(sort_info);
	int comp_val = 0;
	int ascending = 1;

	for (j = 0; j < sort_count; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *col;
		col = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (col == NULL)
			col = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		comp_val = (*col->compare)(e_tree_model_value_at (source, path1, col->col_idx),
					   e_tree_model_value_at (source, path2, col->col_idx));
		ascending = column.ascending;
		if (comp_val != 0)
			break;
	}
	if (!ascending)
		comp_val = -comp_val;
	return comp_val;
}

static int
e_sort_tree_callback(const void *data1, const void *data2, gpointer user_data)
{
	ETreePath path1 = *(ETreePath *)data1;
	ETreePath path2 = *(ETreePath *)data2;
	ETreeSortClosure *closure = user_data;

	return etsu_tree_compare(closure->tree, closure->sort_info, closure->full_header, path1, path2);
}

void
e_table_sorting_utils_tree_sort(ETreeModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, ETreePath *map_table, int count)
{
	ETreeSortClosure closure;
	g_return_if_fail(source != NULL);
	g_return_if_fail(E_IS_TREE_MODEL(source));
	g_return_if_fail(sort_info != NULL);
	g_return_if_fail(E_IS_TABLE_SORT_INFO(sort_info));
	g_return_if_fail(full_header != NULL);
	g_return_if_fail(E_IS_TABLE_HEADER(full_header));

	closure.tree = source;
	closure.sort_info = sort_info;
	closure.full_header = full_header;

	e_sort(map_table, count, sizeof(ETreePath), e_sort_tree_callback, &closure);
}

/* FIXME: This could be done in time log n instead of time n with a binary search. */
int
e_table_sorting_utils_tree_check_position (ETreeModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, ETreePath *map_table, int count, int old_index)
{
	int i;
	ETreePath path;

	i = old_index;
	path = map_table[i];

	if (i < count - 1 && etsu_tree_compare(source, sort_info, full_header, map_table[i + 1], path) < 0) {
		i ++;
		while (i < count - 1 && etsu_tree_compare(source, sort_info, full_header, map_table[i], path) < 0)
			i ++;
	} else if (i > 0 && etsu_tree_compare(source, sort_info, full_header, map_table[i - 1], path) > 0) {
		i --;
		while (i > 0 && etsu_tree_compare(source, sort_info, full_header, map_table[i], path) > 0)
			i --;
	}
	return i;
}

/* FIXME: This does not pay attention to making sure that it's a stable insert.  This needs to be fixed. */
int
e_table_sorting_utils_tree_insert(ETreeModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, ETreePath *map_table, int count, ETreePath path)
{
	int start;
	int end;
	ETreeSortClosure closure;

	closure.tree = source;
	closure.sort_info = sort_info;
	closure.full_header = full_header;

	e_bsearch(&path, map_table, count, sizeof(ETreePath), e_sort_tree_callback, &closure, &start, &end);
	return end;
}
