/**
 * ui_draw.h
 *
 * Interface for the table rendering module and UI elements
 * Contains functions for drawing menus, cells, borders, headers, table body, and the status bar
 */

#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "csvview_defs.h"   // RowIndex, globals (cur_col, cur_display_row, etc.)
#include "utils.h"          // col_letter, format_cell_value, etc.

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Draws the menu bar at the bottom of the screen
 *
 * @param y         Vertical position (usually LINES - 1)
 * @param x         Horizontal position (usually 0)
 * @param w         Screen width (COLS)
 * @param menu_type 1 = main menu, 2 = context menu
 */
void draw_menu(int y, int x, int w, int menu_type);

/**
 * @brief Draws an enlarged window showing the content of a single cell
 *
 * @param y           Vertical position of the top-left corner
 * @param x           Horizontal position of the top-left corner
 * @param col_name    Column name
 * @param row_num     Row number (visible)
 * @param raw_content Raw cell content
 * @param width       Window width
 */
void draw_cell_view(int y, const char *col_name, int row_num, const char *raw_content, int width);

/**
 * @brief Draws the outer border of the table
 *
 * @param top     Vertical position of the top line
 * @param height  Border height
 * @param width   Border width
 */
void draw_table_border(int top, int height, int width);

/**
 * @brief Draws the table header row
 *
 * Highlights the current column and the sort arrow (if any)
 *
 * @param top          Vertical position of the header row
 * @param offset       Unused (kept for compatibility)
 * @param visible_cols Number of visible columns
 * @param left_col     First visible column
 * @param cur_col      Current (selected) column
 */
void draw_table_headers(int top, int offset, int visible_cols,
                        int left_col, int cur_col);

/**
 * @brief Draws the table body (the visible portion of rows)
 *
 * Supports:
 *   - highlighting of the current row/column/cell
 *   - cell formatting (format_cell_value)
 *   - lazy row loading
 *   - horizontal scrolling
 *
 * @param top             Vertical position where the body starts
 * @param offset          Unused (kept for compatibility)
 * @param visible_rows    Number of visible rows
 * @param top_display_row First visible index
 * @param cur_display_row Current visible cursor index
 * @param cur_col         Current column
 * @param left_col        First visible column
 * @param visible_cols    Number of visible columns
 * @param rows            Array of row indices
 * @param f               FILE* of the source file
 * @param row_count       Total number of rows
 */
void draw_table_body(int top, int offset, int visible_rows,
                     int top_display_row, int cur_display_row, int cur_col,
                     int left_col, int visible_cols,
                     RowIndex *rows, FILE *f, int row_count);

/**
 * @brief Draws the status bar at the bottom of the screen
 *
 * @param y           Vertical position (usually LINES - 1)
 * @param x           Horizontal position (usually 0)
 * @param filename    Name of the open file
 * @param row_count   Number of rows
 * @param size_str    File size as a string (e.g. "unknown")
 */
void draw_status_bar(int y, int x, const char *filename,
                     int row_count, const char *size_str);

/**
 * @brief Shows a window with the list of saved filters
 *
 * Displays a list of all saved filters (`saved_filters[]`).
 * Supports:
 *   - ↑↓ navigation
 *   - Enter — insert filter into filter_query and apply
 *   - Shift+Enter — apply without inserting (if needed)
 *   - D/d — delete the selected filter (from memory and the .csvf file)
 *   - Esc/q — close the window
 *
 * @param csv_filename  Name of the source CSV file (for .csvf)
 *
 * @note
 *   - If there are no saved filters — shows a message and returns
 *   - Window is centered; size adapts to the number of filters
 *   - After selecting/applying, calls apply_filter() and refreshes the status bar
 *   - Deleting a filter rewrites the entire .csvf file (preserving other lines)
 *
 * @warning
 *   - Depends on globals: saved_filters, saved_filter_count, filter_query,
 *     in_filter_mode, filter_active, rows, f, row_count
 *   - Rewrites the .csvf file on deletion — be careful with large files
 *   - Does not check write permissions on the file
 *
 * @see
 *   - apply_filter() — called after a filter is selected
 *   - save_filter() / load_saved_filters() — persistence helpers
 */
void show_saved_filters_window(const char *csv_filename);

// -----------------------------------------------------------------------------
// Gets the cell content for a given column number (cur_col) from a line
// Returns a new string (must be freed), always correctly handles ,, and "text, with comma"
// -----------------------------------------------------------------------------
char *get_cell_content(const char *line, int target_col);

/**
 * @brief Draws one frame of the spinner [ |/-\ ] in the right corner of the status bar
 *
 * Call every ~5000 rows during long-running operations.
 * Cycles through the characters | / - \ in order.
 */
void spinner_tick(void);

/**
 * @brief Clears the spinner (erases the characters)
 *
 * Call after a long-running operation completes.
 */
void spinner_clear(void);

/**
 * @brief Shows a window for browsing comment lines (lines starting with '#')
 *
 * Available when skip_comments=1 and comment_count>0.
 * Invoked by pressing '#' in the main view.
 */
void show_comments_window(void);

#endif /* UI_DRAW_H */