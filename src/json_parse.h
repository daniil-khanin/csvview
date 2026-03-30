/**
 * json_parse.h
 *
 * Minimal JSON parser for NDJSON support.
 * Handles objects with string/number/bool/null values.
 * Nested objects and arrays are captured as opaque strings.
 */

#ifndef JSON_PARSE_H
#define JSON_PARSE_H

#include "csvview_defs.h"
#include <stdio.h>

/**
 * Parse an NDJSON row (JSON object string) into an array of field values.
 * Fields are ordered according to the global column_names[] / col_count.
 * Missing keys produce an empty string "".
 * Returns malloc'd char**; caller frees with free_csv_fields().
 */
char **ndjson_parse_row(const char *buf, int *out_count);

/**
 * Parse a JSON object string extracting values in positional (key-order) order,
 * ignoring key names. Used when rebuilding rows after a column rename so that
 * old key names in the file are replaced with the new column_names[].
 * Returns malloc'd char**; caller frees with free_csv_fields().
 */
char **ndjson_parse_positional(const char *obj, int *out_count);

/**
 * Build a JSON object string from an array of field values.
 * col_names provides the key names; col_types guides value quoting
 * (COL_NUM values are written without quotes when they are valid numbers).
 * Returns malloc'd string; caller free()s it.
 */
char *ndjson_build_row(char **fields, int count,
                       char **col_names, ColType *col_types);

/**
 * Scan the first N lines of fp to discover all JSON keys in union order.
 * Populates col_names[] (caller must free each strdup'd entry).
 * Returns the number of columns found.
 */
int ndjson_discover_columns(FILE *fp, char **col_names, int max_cols);

#endif /* JSON_PARSE_H */
