/**
 * sorting.h
 *
 * Interface for the sorting module of csvview
 * Declarations of row comparison functions and sort index construction
 */

#ifndef SORTING_H
#define SORTING_H

#include "csvview_defs.h"   // RowIndex, globals (sort_col, sorted_rows, etc.)
#include "utils.h"          // get_column_value, col_name_to_num, etc.

// ────────────────────────────────────────────────
// Public module functions
// ────────────────────────────────────────────────

/**
 * @brief Comparison function for two rows used by qsort
 * Used for sorting row indices by value in column sort_col
 * Accounts for the header (always first), numeric/string comparison,
 * and sort direction (sort_order)
 *
 * @param pa  Pointer to the index of the first row (int*)
 * @param pb  Pointer to the index of the second row (int*)
 * @return    -1, 0, 1 depending on order (taking sort_order into account)
 */
int compare_rows_by_column(const void *pa, const void *pb);

/**
 * @brief Builds the sorted_rows[] array — row indices in sorted order
 * Fills sorted_count and sorted_rows[] with data rows only (skipping the header)
 * If sort_col < 0 — resets sort (sorted_count = 0)
 */
void build_sorted_index(void);

/**
 * @brief Returns the real row index in the file by visible index
 *
 * Accounts for:
 *   - active sort (sorted_rows)
 *   - active filter (filtered_rows)
 *   - presence of a header (use_headers)
 *
 * @param display_idx   Row position on screen (0 = first visible)
 * @return              Real row index in the rows[] array (0-based)
 */
int get_real_row(int display_idx);

#endif /* SORTING_H */
