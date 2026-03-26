#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <strings.h>    
#include <ctype.h>  

#include "csvview_defs.h"    

/**
 * Converts a column number (0-based) to an Excel-style letter designation (A, AA, ZZ...)
 */
void col_letter(int col, char *buf);

/**
 * Converts an Excel-style column letter designation (A, AA, ZZ...) to a number (0-based)
 */
int col_to_num(const char *label);

/**
 * Finds a column number by its text name from the headers
 */
int col_name_to_num(const char *name);

/**
 * Saves all rows to a file (creates a temporary file + rename)
 */
int save_file(const char *filename, FILE *orig_f, RowIndex *rows, int row_count);

/**
 * Case-insensitive substring search (equivalent to strcasestr)
 */
char *strcasestr_custom(const char *haystack, const char *needle);

/**
 * Removes leading and trailing whitespace characters (in-place)
 */
char *trim(char *str);

/**
 * Parses a filter string into a structured FilterExpr expression
 * (allocates memory — must call free_filter_expr after use!)
 */
int parse_filter_expression(const char *query, FilterExpr *expr);

/**
 * Extracts a cell value by column name (returns a new string — must free)
 */
char *get_column_value(const char *line, const char *col_name, int use_headers);

/**
 * Checks whether a single condition holds for a given cell value
 */
int evaluate_condition(const char *cell, const Condition *cond);

/**
 * Checks whether a row passes the entire filter (respecting AND/OR/!)
 */
int row_matches_filter(const char *line, const FilterExpr *expr);

/**
 * Frees all dynamically allocated memory inside a FilterExpr
 */
void free_filter_expr(FilterExpr *expr);

/**
 * Simple comparison of two floating-point numbers.
 * Returns a negative value if da < db,
 * positive if da > db, and zero if equal.
 */

int compare_double(const void *a, const void *b);

void format_number_with_spaces(long long num, char *buf, size_t bufsize);

char *truncate_for_display(const char *str, int max_width);

/**
 * Returns the number of terminal display columns needed for UTF-8 string s.
 * CJK/full-width characters count as 2; Arabic, Hebrew, Latin, etc. count as 1.
 */
int utf8_display_width(const char *s);

char *clean_column_name(const char *raw);

/**
 * Parses a single CSV line according to RFC 4180 rules (simple implementation).
 * Returns an array of allocated fields (each field and the array itself must be freed).
 *
 * @param line      Input string (from file or cache)
 * @param out_count [out] Number of fields parsed
 * @return          Array of char* (NULL-terminated), each element is a malloc string
 *                  Returns NULL on error, out_count = 0
 */
char **parse_csv_line(const char *line, int *out_count);

/**
 * Builds a line from an array of fields with the given delimiter (RFC 4180).
 * Fields containing the delimiter, a quote, or a newline are wrapped in "...".
 * Returns a malloc string WITHOUT a trailing \n. Must free().
 */
char *build_csv_line(char **fields, int count, char delimiter);

/**
 * Frees the array of fields returned by parse_csv_line().
 */
void free_csv_fields(char **fields, int count);

/**
 * Parse a double accepting both '.' and ',' as decimal separator.
 * Drop-in replacement for strtod().
 */
double parse_double(const char *s, char **endptr);

#endif