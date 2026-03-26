/**
 * column_format.h
 *
 * Interface for the column formatting and display settings module.
 * Contains functions for format initialization, string truncation, number/date formatting, and the main cell formatting function.
 */

#ifndef COLUMN_FORMAT_H
#define COLUMN_FORMAT_H

#include "csvview_defs.h"   // ColType, ColumnFormat, col_formats, col_types, etc.
#include "utils.h"          // trim, col_letter, etc.

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Initializes all column formats to their default values.
 *
 * Called when loading a new file or resetting settings.
 * Sets:
 *   - truncate_len = 0
 *   - decimal_places = -1 (auto)
 *   - date_format = empty string
 *   - col_widths[i] = CELL_WIDTH
 */
void init_column_formats(void);

/**
 * @brief Truncates a string to the specified length, appending "..."
 *
 * @param str       Source string (may be NULL)
 * @param max_len   Maximum length (not counting "...")
 * @return          New string (malloc) — either a copy or truncated with "..."
 *                  Always call free() on the result
 */
char *truncate_string(const char *str, int max_len);

/**
 * @brief Formats a numeric string with the specified number of decimal places.
 *
 * Supports auto mode (decimals < 0) — preserves up to 6 digits, strips trailing zeros.
 *
 * @param raw_str   Source string (may be NULL or non-numeric)
 * @param decimals  Number of decimal places (-1 = auto)
 * @return          New string (malloc) with the formatted number
 *                  Always call free() on the result
 */
char *format_number(const char *raw_str, int decimals);

/**
 * @brief Formats a date string according to the specified format.
 *
 * Attempts to recognize several common input date formats.
 * If the format is not recognized, returns the original string.
 *
 * @param date_str      Source date string
 * @param target_format Target format (e.g. "%d.%m.%Y")
 * @return              New string (malloc) in the target format, or the original
 *                      Always call free() on the result
 */
char *format_date(const char *date_str, const char *target_format);

/**
 * @brief Main function for formatting a cell value before rendering.
 *
 * Applies column format settings (truncate, decimals, date_format)
 * according to the column type (COL_STR, COL_NUM, COL_DATE).
 *
 * @param raw_value     Raw cell value (may be NULL)
 * @param col_idx       Column index (0-based)
 * @return              Formatted string (malloc)
 *                      Always call free() on the result
 */
char *format_cell_value(const char *raw_value, int col_idx);

/**
 * @brief Saves the current column settings and filters to <csv_filename>.csvf
 *
 * File format:
 *   use_headers:N
 *   col_count:N
 *   idx:type:truncate:decimals:date_format
 *   widths:w1,w2,...
 *   filter: query1
 *   filter: query2
 *
 * @param csv_filename  Name of the source CSV file (without the .csvf extension)
 */
/**
 * @brief Automatically detects column types from a sample of the first 200 data rows.
 *
 * For each column, checks ≥90% of non-empty values:
 *   - if parseable as a number → COL_NUM
 *   - if matching a date pattern → COL_DATE (takes priority over numbers)
 *   - otherwise → COL_STR
 * Supported date formats: YYYY-MM-DD, DD.MM.YYYY, DD/MM/YYYY, YYYY/MM/DD, YYYY-MM.
 * Should only be called when no .csvf file was found (before show_column_setup).
 */
void auto_detect_column_types(void);

void save_column_settings(const char *csv_filename);

/**
 * @brief Loads column settings and filters from <csv_filename>.csvf
 *
 * If the file does not exist or the format does not match, returns 0 and settings remain at defaults.
 * Fully loads only when col_count matches the current value.
 *
 * @param csv_filename  Name of the source CSV file
 * @return 1 — full success, 2 — partial (col_count changed, global settings applied), 0 — file not found
 */
int  preload_delimiter(const char *csv_filename);  /* returns 1 if skip_comments was set explicitly */
int load_column_settings(const char *csv_filename);

/**
 * @brief Displays an interactive window for configuring column types and formats.
 *
 * Allows the user to:
 *   - change the column type (S/N/D)
 *   - set the format (string truncation, decimal places, date format)
 *   - toggle headers on/off
 *
 * Supports two modes:
 *   - initial_setup = 1 — first launch (can immediately choose with/without headers)
 *   - normal mode — editing existing settings
 *
 * After changes, calls save_column_settings() to persist to .csvf
 *
 * @param rows          Array of row indices (needed only for signature compatibility)
 * @param f             FILE* of the source file (not used directly)
 * @param row_count_ptr Pointer to row_count (not used)
 * @param initial_setup 1 = first launch (with header selection), 0 = normal mode
 * @param csv_filename  File name for saving settings
 * @return
 *   0 — changes applied and saved
 *   1 — user cancelled (Esc/q)
 *
 * @note
 *   - Window occupies almost the entire screen
 *   - Supports navigation with ↑↓ ←→ Tab, quick type selection (S/N/D), format input via Enter
 *   - On first launch, can exit immediately with header selection (H/1/2)
 *
 * @warning
 *   - Depends on globals: col_count, column_names, col_types, col_formats,
 *     col_widths, use_headers, saved_filters, etc.
 *   - Calls save_column_settings() when exiting with changes applied
 *   - If col_count == 0 — simply returns
 */
int show_column_setup(const char *csv_filename);

#endif /* COLUMN_FORMAT_H */