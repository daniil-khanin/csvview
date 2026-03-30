/**
 * file_format.c
 *
 * Format registry and auto-detection.
 */

#include "file_format.h"
#include <string.h>
#include <strings.h>

/* Forward declarations of the two drivers defined in their own .c files. */
extern FileFormatDriver csv_driver;
extern FileFormatDriver ndjson_driver;

FileFormatDriver *g_fmt = NULL;

void fmt_detect(const char *filename)
{
    if (!filename) { g_fmt = &csv_driver; return; }

    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcasecmp(ext, ".ndjson") == 0 ||
            strcasecmp(ext, ".jsonl")  == 0 ||
            strcasecmp(ext, ".ldjson") == 0) {
            g_fmt = &ndjson_driver;
            return;
        }
    }

    g_fmt = &csv_driver;
}
