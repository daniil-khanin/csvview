#ifndef GRAPH_SVG_H
#define GRAPH_SVG_H

#include "csvview_defs.h"
#include <stdio.h>

/**
 * Export the current graph view to an SVG file.
 *
 * Respects all current globals: graph_scatter_mode, graph_col_list,
 * graph_series_hidden, graph_zoom_start/end, graph_grid, graph_scale,
 * graph_dual_yaxis, graph_type, show_graph_cursor, graph_cursor_pos.
 *
 * @param filename    Output file path
 * @param svg_w       SVG width in pixels (e.g. 900)
 * @param svg_h       SVG height in pixels (e.g. 500)
 * @param rows        Row index array
 * @param f           Source CSV file handle
 * @param row_count   Total rows in file
 * @return 0 on success, -1 on file error
 */
int export_graph_svg(const char *filename, int svg_w, int svg_h,
                     RowIndex *rows, FILE *f, int row_count);

/* global set by csvview.c for scatter mode */
extern int graph_scatter_mode;
extern int graph_scatter_x_col;
extern int graph_series_hidden[10];
extern int graph_dual_yaxis;

#endif /* GRAPH_SVG_H */
