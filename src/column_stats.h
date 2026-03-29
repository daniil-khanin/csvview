/**
 * column_stats.h
 *
 * Interface for the column statistics module
 * Declaration of the display and calculation function for the selected column's statistics
 */

#ifndef COLUMN_STATS_H
#define COLUMN_STATS_H

#include "csvview_defs.h"   // RowIndex, ColType, ColumnFormat and global variables
#include "utils.h"          // get_column_value, trim, col_letter, etc.
#include "sorting.h"

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Shows the statistics window for the selected column with actual calculations
 *
 * Displays detailed statistics for column col_idx:
 *   - general metrics (total cells, valid, empty)
 *   - for numbers: sum, mean, median, min/max, mode
 *   - for dates: distribution by month/quarter/year/century
 *   - for all types: top-10 most frequent values + histogram
 *
 * Calculation is done in a single pass over all visible rows (respecting the filter).
 * Supports lazy loading of rows into cache.
 *
 * @param col_idx   Column index (0-based) for which to display statistics
 *
 * @note
 *   - Window is centered on screen (STATS_W x STATS_H)
 *   - Shows processing progress for large row counts
 *   - For numbers, builds a histogram with MAX_BINS bins
 *   - For dates, automatically selects scale (month/quarter/year/century)
 *   - Closes on any keypress
 *
 * @warning
 *   - Depends on globals: col_count, column_names, col_types, col_formats,
 *     use_headers, filter_active, filtered_count, filtered_rows, row_count,
 *     rows, f, MAX_LINE_LEN, MAX_BINS, etc.
 *   - Allocates temporary memory for numeric_values and freqs — freed at the end
 *   - For very large files may take time and memory
 *   - If the column does not exist — simply returns
 *
 * @see
 *   - get_column_value() — retrieve cell value
 *   - get_real_row() — convert visible index to real index
 *   - draw_table_body() — call site of this function (typically triggered by key 'D')
 */
void show_column_stats(int col_idx);

/**
 * @brief Shows an N×N Pearson r correlation matrix for all visible numeric columns.
 *
 * Navigate with arrows/hjkl. Press Enter on a non-diagonal cell to launch
 * a scatter plot; the function returns 1 and fills *out_x_col / *out_y_col.
 * Returns 0 if the user closes with q/Esc.
 */
int show_correlation_matrix(int *out_x_col, int *out_y_col);

/**
 * @brief Shows a scrollable outlier report for all visible numeric columns.
 *
 * Rows with |z-score| >= threshold are listed.
 * Enter jumps to the row; 'f' applies a filter for that exact value.
 *
 * @param threshold  Z-score threshold (pass 0 to use default 3.0)
 */
/* Returns the display row index to jump to, or -1 if no jump requested. */
int show_outlier_report(double threshold);

#endif /* COLUMN_STATS_H */