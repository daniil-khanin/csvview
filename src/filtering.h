/**
 * filtering.h
 *
 * Filtering module interface for csvview
 * Function declarations for working with filters
 */

#ifndef FILTERING_H
#define FILTERING_H

#include "csvview_defs.h"   // for RowIndex, FilterExpr and global variables

/**
 * Applies the current filter (filter_query) to the table
 * Populates filtered_rows[] and filtered_count
 */
void apply_filter(RowIndex *rows, FILE *f, int row_count);

/**
 * Loads saved filters from file <csv_filename>
 * (expects lines of the form "filter: query")
 */
void load_saved_filters(const char *csv_filename);

/**
 * Saves a filter to file <csv_filename>.csvf and into memory
 */
void save_filter(const char *csv_filename, const char *query);

#endif /* FILTERING_H */