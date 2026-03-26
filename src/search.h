/**
 * search.h
 *
 * Interface for the CSV table search module
 * Declarations for search execution and navigation to a result
 */

#ifndef SEARCH_H
#define SEARCH_H

#include "csvview_defs.h"   // RowIndex, SearchResult, search_results and global variables
#include "utils.h"          // strcasestr_custom, etc.

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Searches for the substring search_query across all cells in the table
 *
 * Searches case-insensitively (strcasestr_custom) across all cells of all data rows.
 * Results are stored in the global array search_results[] and search_count.
 * Each found result is a (row, col) pair.
 *
 * @param rows          Array of row indices (with offset and line_cache)
 * @param f             Open FILE* of the source CSV
 * @param row_count     Total number of rows in the file (including the header)
 *
 * @note
 *   - Skips the header row (if use_headers == 1)
 *   - Lazily loads rows into line_cache if not already cached
 *   - Limits the number of results to MAX_SEARCH_RESULTS
 *   - After the call, search_count and search_results are ready to use
 *   - search_index is reset to -1 (no current result)
 *
 * @warning
 *   - Depends on globals: search_query, search_results, search_count, search_index,
 *     use_headers, col_count, MAX_SEARCH_RESULTS
 *   - May load many rows into cache for large files with short queries
 *   - Does not consider the active filter — searches the entire file
 *
 * @see
 *   - goto_search_result() — navigate to a found result
 *   - strcasestr_custom() — case-insensitive substring search
 */
void perform_search(RowIndex *rows, FILE *f, int row_count);

/**
 * @brief Navigates to the specified search result and scrolls the table
 *
 * Moves the cursor to the found cell (search_results[index]),
 * adjusts the visible area (top_display_row, left_col)
 * so that the target row is approximately in the middle of the screen.
 *
 * Takes into account:
 *   - active filter (looks up position in filtered_rows[])
 *   - presence of a header (use_headers)
 *
 * @param index             Index of the result in search_results[] (0..search_count-1)
 * @param cur_display_row   [out] Pointer to the current visible cursor position
 * @param top_display_row   [out] Pointer to the first visible row
 * @param cur_col           [out] Pointer to the current column
 * @param left_col          [out] Pointer to the leftmost visible column
 * @param visible_rows      Number of visible rows on screen
 * @param visible_cols      Number of visible columns on screen
 * @param row_count         Total number of rows in the file
 *
 * @note
 *   - If index is out of range — the function simply returns
 *   - If the row did not pass the current filter — does not jump
 *   - Scrolling centers the row vertically and horizontally
 *
 * @warning
 *   - Depends on globals: search_results, search_index, filter_active,
 *     filtered_rows, filtered_count, use_headers, col_count
 *   - Does NOT validate that visible_rows/visible_cols are valid (assumes they are)
 *
 * @see
 *   - perform_search() — populates the results array
 *   - draw_table_body() — uses the updated cur_display_row and top_display_row
 */
void goto_search_result(int index,
                        int *cur_display_row,
                        int *top_display_row,
                        int *cur_col,
                        int *left_col,
                        int visible_rows,
                        int visible_cols,
                        int row_count);

#endif /* SEARCH_H */
