/**
 * table_edit.h
 *
 * Interface for the CSV table editing module
 * Adding/removing columns, filling a column with values, rebuilding the header
 */

#ifndef TABLE_EDIT_H
#define TABLE_EDIT_H

#include "csvview_defs.h"   // RowIndex, ColType, ColumnFormat and globals
#include "utils.h"          // save_file, col_letter, etc.

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Adds a new column at the specified position and saves the file
 *
 * Inserts an empty column (or one with a name) at position insert_pos.
 * Shifts all subsequent columns to the right.
 * Rewrites the file, adding a comma and an empty value (or the name in the header).
 *
 * @param insert_pos    Insertion position (0 = before the first, col_count = at the end)
 * @param new_name      Name of the new column (can be NULL → "untitled")
 * @param csv_filename  File name to save to
 *
 * @note
 *   - If col_count >= MAX_COLS — shows an error and returns
 *   - After adding, re-indexes offsets and the row cache
 *   - Saves new settings via save_column_settings()
 *
 * @warning
 *   - Rewrites the file through a temporary .tmp file
 *   - If write fails — file may remain as .tmp
 *   - Depends on globals: col_count, column_names, col_types, col_widths, col_formats
 */
void add_column_and_save(int insert_pos, const char *new_name, const char *csv_filename);

/**
 * @brief Fills an entire column with a given value or sequence
 *
 * Supports:
 *   - fixed value: "text" or "123"
 *   - sequence: num(start) or num(start,step)
 *
 * @param col_idx       Index of the column to fill
 * @param arg           Argument: "string" or num(10) / num(1,5)
 * @param csv_filename  File name to save to
 *
 * @note
 *   - For num() — fills with numbers starting from the given value
 *   - Overwrites the row cache and the file
 *   - Re-indexes offsets after filling
 *
 * @warning
 *   - If the column index is invalid — shows an error
 *   - Overwrites the file — use with caution!
 */
void fill_column(int col_idx, const char *arg, const char *csv_filename);

/**
 * @brief Deletes a column by index and saves the file
 *
 * Deletes column cur_col and shifts all subsequent columns to the left.
 * Rewrites the file without the deleted column.
 *
 * @param col_idx       Index of the column to delete
 * @param arg           Column name for verification (optional)
 * @param csv_filename  File name to save to
 *
 * @note
 *   - After deletion, moves the cursor to the previous column
 *   - Re-indexes offsets and the row cache
 *   - Saves new settings
 *
 * @warning
 *   - Rewrites the file through a temporary .tmp file
 *   - If the cursor is not on the expected column — shows a warning
 */
void delete_column(int col_idx, const char *arg, const char *csv_filename);

/**
 * @brief Rebuilds the cache of the first row (header) after column renames
 *
 * Called after a column rename or an add/delete operation.
 * Builds a new header line from column_names[] with proper escaping.
 */
void rebuild_header_row(void);

#endif /* TABLE_EDIT_H */
