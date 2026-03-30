/**
 * format_csv.c
 *
 * CSV/TSV/PSV/ECSV format driver.
 * Thin wrappers around the existing parse_csv_line / build_csv_line helpers.
 */

#include "file_format.h"
#include "utils.h"
#include "csvview_defs.h"
#include <stdlib.h>
#include <string.h>

/* csv_delimiter is defined in csvview.c and declared extern in csvview_defs.h */

static char **csv_parse_row(const char *row_buf, int *out_count)
{
    return parse_csv_line(row_buf, out_count);
}

static char *csv_build_row(char **fields, int count,
                            char **col_names, ColType *col_types)
{
    (void)col_names;
    (void)col_types;
    return build_csv_line(fields, count, csv_delimiter);
}

static void csv_rebuild_header(void)
{
    if (row_count == 0 || !use_headers) return;
    char *new_header = build_csv_line(column_names, col_count, csv_delimiter);
    if (new_header) {
        free(rows[0].line_cache);
        rows[0].line_cache = new_header;
    }
}

FileFormatDriver csv_driver = {
    .id             = FMT_CSV,
    .name           = "csv",
    .can_edit       = 1,
    .has_header_row = 1,
    .parse_row      = csv_parse_row,
    .build_row      = csv_build_row,
    .rebuild_header = csv_rebuild_header,
};
