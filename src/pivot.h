/**
 * pivot.h
 *
 * Interface for the pivot table module of csvview
 * Contains all functions for hash table operations, aggregation, settings, and pivot rendering
 */

#ifndef PIVOT_H
#define PIVOT_H

#include "csvview_defs.h"   // Agg, PivotSettings, ColType, RowIndex and global variables
#include "utils.h"          // get_column_value, col_name_to_num, etc.

/**
 * @brief Computes a hash of a string (used for HashMap)
 * @param str   String to hash
 * @return      64-bit hash
 */
unsigned long hash_string(const char *str);

/**
 * @brief Creates a new hash table
 * @param size  Initial size (number of buckets)
 * @return      Pointer to HashMap or NULL on error
 */
HashMap *hash_map_create(int size);

/**
 * @brief Inserts or updates a value by key
 * @param map   Hash table
 * @param key   Key (string)
 * @param value Value (pointer)
 */
void hash_map_put(HashMap *map, const char *key, void *value);

/**
 * @brief Retrieves a value by key
 * @param map   Hash table
 * @param key   Key
 * @return      Value or NULL if key not found
 */
void *hash_map_get(HashMap *map, const char *key);

/**
 * @brief Returns an array of all keys in the hash table
 * @param map       Hash table
 * @param count     [out] Number of keys
 * @return          Array of strings (caller must free each element and the array itself)
 */
char **hash_map_keys(HashMap *map, int *count);

/**
 * @brief Frees all memory used by the hash table
 * @param map   Hash table (may be NULL)
 */
void hash_map_destroy(HashMap *map);

// ────────────────────────────────────────────────
// Comparison functions for qsort
// ────────────────────────────────────────────────

/**
 * @brief Case-sensitive string comparison for qsort
 */
int compare_str(const void *a, const void *b);

/**
 * @brief Date key comparison for qsort (supports YYYY-MM, YYYY-Qn, YYYY, century)
 */
int compare_date_keys(const void *a, const void *b);

// ────────────────────────────────────────────────
// Aggregation and grouping keys
// ────────────────────────────────────────────────

/**
 * @brief Returns the grouping key for a cell value (especially for dates)
 * @param val       Cell value
 * @param type      Column type
 * @param grouping  Granularity ("Month", "Quarter", "Year", "Century", "Auto")
 * @param col_idx   Column index (for date_format)
 * @return          New key string (malloc'd) — caller must free()
 */
char *get_group_key(const char *val, ColType type, const char *grouping, int col_idx);

/**
 * @brief Updates aggregation with a single cell value
 * @param agg         Aggregation structure
 * @param val         Cell value
 * @param value_type  Value type (COL_NUM or other)
 */
void update_agg(Agg *agg, const char *val, ColType value_type);

/**
 * @brief Formats an aggregation result for display
 * @param agg           Aggregation
 * @param aggregation   Type ("SUM", "AVG", "COUNT", etc.)
 * @param value_type    Value type
 * @return              Static string buffer (do not free!)
 */
char *get_agg_display(const Agg *agg, const char *aggregation, ColType value_type);

// ────────────────────────────────────────────────
// Pivot settings
// ────────────────────────────────────────────────

/**
 * @brief Loads pivot settings from <csv_filename>.pivot
 * @param csv_filename  File name
 * @param settings      Structure to populate
 * @return              1 on success, 0 if file not found or error
 */
int load_pivot_settings(const char *csv_filename, PivotSettings *settings);

/**
 * @brief Saves pivot settings to <csv_filename>.pivot
 * @param csv_filename  File name
 * @param settings      Settings
 */
void save_pivot_settings(const char *csv_filename, const PivotSettings *settings);

// ────────────────────────────────────────────────
// Rendering and UI
// ────────────────────────────────────────────────

/**
 * @brief Draws the table border (used inside pivot)
 * @param y      Vertical position
 * @param x      Horizontal position
 * @param height Height
 * @param width  Width
 */
void draw_table_frame(int y, int x, int height, int width);

/**
 * @brief Shows the pivot table settings window
 * @param settings      Settings (modified in place)
 * @param csv_filename  File name
 * @param height        Screen height
 * @param width         Screen width
 */
void show_pivot_settings_window(PivotSettings *settings,
                                const char *csv_filename,
                                int height, int width);

/**
 * @brief Builds and displays the pivot table from settings
 * @param settings      Settings
 * @param csv_filename  File name
 * @param height        Screen height
 * @param width         Screen width
 */
void build_and_show_pivot(PivotSettings *settings,
                          const char *csv_filename,
                          int height, int width);

#endif /* PIVOT_H */