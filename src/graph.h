/**
 * graph.h
 *
 * Graph module interface for csvview
 * Rendering line, bar, and dot charts by column
 */

#ifndef GRAPH_H
#define GRAPH_H

#include "csvview_defs.h"   // globals (graph_type, graph_scale, rows, f, etc.)
#include "utils.h"          // format_cell_value, col_letter, etc.

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Draws a chart for the selected column (line, bar, dot)
 *
 * Supports:
 *   - line chart (GRAPH_LINE)
 *   - bar chart (GRAPH_BAR)
 *   - dot chart (GRAPH_DOT)
 *   - logarithmic scale (SCALE_LOG)
 *   - date aggregation (if using_date_x and date_col is set)
 *   - anomaly highlighting (if show_anomalies)
 *   - cursor with value display (if show_graph_cursor)
 *
 * @param col           Column index for Y values
 * @param height        Height of the available screen area
 * @param width         Width of the available screen area
 * @param rows          Array of row indices
 * @param f             FILE* of the source file
 * @param row_count     Total number of rows
 * @param csv_filename  File name (for the status bar)
 * @param cursor_pos    Cursor position (-1 = do not show)
 *
 * @note
 *   - Uses globals: graph_type, graph_scale, using_date_x, date_col,
 *     show_anomalies, show_graph_cursor, graph_anomaly_count, graph_anomalies
 *   - Lazily loads rows into cache
 *   - Automatically determines min/max across all data
 *   - Highlights anomalies (z-score > ANOMALY_THRESHOLD)
 *   - Centers the cursor and the value beneath it
 *
 * @warning
 *   - Depends on ncurses (mvaddch, attron, etc.)
 *   - Thins out data when there are many points
 *   - If the column is invalid or there is no data, simply returns
 *
 * @see
 *   - draw_table_body() — call site (on key 'g')
 *   - format_cell_value() — value formatting
 *   - get_real_row() — index conversion
 */
void draw_graph(int col, int height, int width, RowIndex *rows, FILE *f, int row_count, int cursor_pos, int min_max_show);

/**
 * Scatter plot: one dot per row at (x_col value, y_col value).
 * Draws braille dots, X/Y axis labels, and Pearson r in the corner.
 * Supports multi-series overlay via graph_overlay_mode / graph_global_min/max.
 */
void draw_scatter(int x_col, int y_col, int height, int width,
                  RowIndex *rows, FILE *f, int row_count, int cursor_pos);

/**
 * Draws a line between two pixels in the dots array (Bresenham's algorithm).
 * Exported for use in draw_pivot_graph.
 */
void draw_bresenham(bool *dots, int w, int h, int x0, int y0, int x1, int y1);

// Added at the end of graph.h
int find_min_index(double *values, int count);
int find_max_index(double *values, int count);

/**
 * Extracts numeric values from the selected column for chart rendering.
 * Respects the active filter, sort order, and monthly aggregation (if enabled).
 *
 * @param col                   column index (0-based)
 * @param rows                  array of row indices
 * @param f                     open csv file
 * @param row_count             total number of rows in the file
 * @param out_point_count       [out] receives the number of values extracted
 * @param out_aggregate         [out] true if monthly aggregation was applied
 * @param target_date_fmt_out   [out] receives the final date format string (buffer must be ≥32 bytes)
 *
 * @return                      malloc'd double array (caller must free after use)
 *                              or NULL on error / no data
 */
double *extract_plot_values(int col, RowIndex *rows, FILE *f, int row_count, int *out_point_count, bool *out_aggregate, char *target_date_fmt_out );

/* Multi-series overlay — set before calling draw_graph in a loop */
extern double graph_global_min;
extern double graph_global_max;
extern int    graph_overlay_mode;
extern int    graph_draw_cursor_overlay; /* 1 = show cursor even in overlay passes */
extern int    graph_grid;               /* 0=off, 1=y-lines, 2=x-lines, 3=both */
extern double graph_last_cursor_y;      /* Y value at cursor after last draw_graph */
extern char   graph_last_cursor_x[64]; /* X label at cursor after last draw_graph */
extern int    graph_zoom_start;        /* first visible data point (0 = from start) */
extern int    graph_zoom_end;          /* last visible data point (-1 = all) */
extern int    graph_total_points;      /* total data points before zoom */
/* Dual Y axis — set by caller per series */
extern int    graph_use_right_axis;   /* 1 = current series uses right Y axis */
extern double graph_right_min;        /* right axis min */
extern double graph_right_max;        /* right axis max */
extern int    graph_right_axis_drawn; /* reset to 0 before loop; set by draw_graph */

#endif /* GRAPH_H */