/**
 * file_format.h
 *
 * Format driver vtable: abstracts row parsing and serialization so that
 * filtering, sorting, graphs and all other modules are format-agnostic.
 */

#ifndef FILE_FORMAT_H
#define FILE_FORMAT_H

#include "csvview_defs.h"
#include <stdio.h>

typedef enum {
    FMT_CSV,
    FMT_TSV,
    FMT_PSV,
    FMT_ECSV,
    FMT_NDJSON
} FileFormatId;

typedef struct {
    FileFormatId  id;
    const char   *name;          /* "csv", "ndjson", … */
    int           can_edit;      /* 1 = full edit/save supported */
    int           has_header_row;/* 1 = row 0 is a column-name header (CSV) */

    /* Parse a raw row string → malloc'd array of field strings.
       Same contract as parse_csv_line(): caller frees with free_csv_fields().
       For NDJSON: fields are ordered according to global column_names[]. */
    char **(*parse_row)(const char *row_buf, int *out_count);

    /* Build a raw row string from fields (malloc'd; caller free()s).
       col_names / col_types are used by NDJSON to produce correct JSON;
       CSV ignores them and uses the global csv_delimiter instead. */
    char *(*build_row)(char **fields, int count,
                       char **col_names, ColType *col_types);

    /* Rebuild in-memory row caches after column renames.
       CSV: updates rows[0].line_cache (header).
       NDJSON: rebuilds every row's line_cache to rename JSON keys. */
    void (*rebuild_header)(void);

} FileFormatDriver;

/* Active format driver — set by fmt_detect() before the main loop. */
extern FileFormatDriver *g_fmt;

/* Detect format from filename extension and set g_fmt. */
void fmt_detect(const char *filename);

#endif /* FILE_FORMAT_H */
