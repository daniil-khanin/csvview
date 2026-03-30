/**
 * format_ndjson.c
 *
 * NDJSON format driver (one JSON object per line).
 * Supports full read/edit/save via the json_parse module.
 */

#include "file_format.h"
#include "json_parse.h"
#include "csvview_defs.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rebuild all row line_caches after a column rename.
   Each JSON object in the file has its key names replaced with the
   current column_names[] — values are preserved by positional order. */
static void ndjson_rebuild_header(void)
{
    char buf[MAX_LINE_LEN];

    for (int r = 0; r < row_count; r++) {
        const char *src = rows[r].line_cache;
        if (!src) {
            if (fseek(f, rows[r].offset, SEEK_SET) == 0 &&
                fgets(buf, sizeof(buf), f)) {
                buf[strcspn(buf, "\r\n")] = '\0';
                src = buf;
            } else {
                continue;
            }
        }

        /* Extract values by position, ignoring old key names */
        int count = 0;
        char **fields = ndjson_parse_positional(src, &count);
        if (!fields) continue;

        /* Rebuild JSON object with current column_names[] as keys */
        char *new_line = ndjson_build_row(fields, count, column_names, col_types);
        free_csv_fields(fields, count);

        if (new_line) {
            free(rows[r].line_cache);
            rows[r].line_cache = new_line;
        }
    }
}

FileFormatDriver ndjson_driver = {
    .id             = FMT_NDJSON,
    .name           = "ndjson",
    .can_edit       = 1,
    .has_header_row = 0,
    .parse_row      = ndjson_parse_row,
    .build_row      = ndjson_build_row,
    .rebuild_header = ndjson_rebuild_header,
};
