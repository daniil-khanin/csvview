#include "split_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#define MAX_SPLIT_FILES 10000
#define SPLIT_LINE_BUF  65536
#define SPLIT_VAL_BUF   512
#define SPLIT_HASH_SIZE 8192

// ── Structures ────────────────────────────────────────────────────────────────

typedef struct SplitEntry {
    char           *key;        // column value
    char           *filename;   // path to the output file
    FILE           *fp;         // open file descriptor
    long            row_count;
    struct SplitEntry *next;    // chain in hash bucket
} SplitEntry;

static SplitEntry  *split_buckets[SPLIT_HASH_SIZE];
static SplitEntry  *split_entries[MAX_SPLIT_FILES]; // for the final summary output
static int          split_entry_count = 0;

// ── String hash ───────────────────────────────────────────────────────────────

static unsigned int split_hash(const char *s) {
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h % SPLIT_HASH_SIZE;
}

// ── Extract the N-th field from a CSV line (0-based) ─────────────────────────

static void get_csv_col(const char *line, int col_idx, char *buf, int bufsz) {
    int col = 0;
    const char *p = line;

    // skip col_idx fields
    while (col < col_idx) {
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && *(p + 1) == '"') { p += 2; continue; }
                if (*p == '"') { p++; break; }
                p++;
            }
        } else {
            while (*p && *p != ',') p++;
        }
        if (*p == ',') p++;
        col++;
        if (!*p) { buf[0] = '\0'; return; }
    }

    // read the target field
    int i = 0;
    if (*p == '"') {
        p++;
        while (*p && i < bufsz - 1) {
            if (*p == '"' && *(p + 1) == '"') { buf[i++] = '"'; p += 2; continue; }
            if (*p == '"') { p++; break; }
            buf[i++] = *p++;
        }
    } else {
        while (*p && *p != ',' && *p != '\n' && *p != '\r' && i < bufsz - 1)
            buf[i++] = *p++;
    }
    buf[i] = '\0';
}

// ── Remove the N-th field from a CSV line (0-based), result in buf ───────────

static void remove_csv_col(const char *line, int col_idx, char *buf, int bufsz) {
    int o = 0, col = 0;
    const char *p = line;

    while (*p && *p != '\n' && *p != '\r') {
        // Boundaries of the current field
        const char *field_start = p;

        // Skip the field
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && *(p + 1) == '"') { p += 2; continue; }
                if (*p == '"') { p++; break; }
                p++;
            }
        } else {
            while (*p && *p != ',') p++;
        }
        const char *field_end = p; // points to ',' or end of line

        if (col != col_idx) {
            // Write separator before the field (except before the first kept field)
            if (o > 0 && o < bufsz - 1) buf[o++] = ',';
            int flen = (int)(field_end - field_start);
            if (o + flen < bufsz - 1) {
                memcpy(buf + o, field_start, flen);
                o += flen;
            }
        }

        if (*p == ',') p++;
        col++;
    }
    buf[o] = '\0';
}

// ── Replace unsafe characters for use in a filename ───────────────────────────

static void sanitize_for_filename(const char *val, char *out, int outsz) {
    int i = 0, o = 0;
    while (val[i] && o < outsz - 1) {
        unsigned char c = (unsigned char)val[i++];
        if (isalnum(c) || c == '-' || c == '.' || c == '_')
            out[o++] = (char)c;
        else
            out[o++] = '_';
    }
    if (o == 0) { out[0] = '_'; o = 1; }
    out[o] = '\0';
}

// ── Find column index by name or 1-based number ───────────────────────────────

static int find_col_index(const char *header, const char *by_col) {
    // First try to parse as a number (1-based)
    char *endp;
    long n = strtol(by_col, &endp, 10);
    if (*endp == '\0' && n > 0) return (int)(n - 1);

    // Search by name in the header
    char colname[512];
    int idx = 0;
    const char *p = header;
    while (*p) {
        int i = 0;
        if (*p == '"') {
            p++;
            while (*p && i < 511) {
                if (*p == '"' && *(p + 1) == '"') { colname[i++] = '"'; p += 2; continue; }
                if (*p == '"') { p++; break; }
                colname[i++] = *p++;
            }
        } else {
            while (*p && *p != ',' && *p != '\n' && *p != '\r' && i < 511)
                colname[i++] = *p++;
        }
        colname[i] = '\0';

        if (strcmp(colname, by_col) == 0) return idx;

        if (*p != ',') break;
        p++;
        idx++;
    }
    return -1;
}

// ── Base filename without extension ───────────────────────────────────────────

static void get_base_prefix(const char *input_path, const char *output_dir,
                             char *prefix, int prefix_sz) {
    const char *slash = strrchr(input_path, '/');
    const char *fname = slash ? slash + 1 : input_path;
    const char *dot   = strrchr(fname, '.');
    int namelen = dot ? (int)(dot - fname) : (int)strlen(fname);

    if (output_dir && *output_dir) {
        mkdir(output_dir, 0755);
        snprintf(prefix, prefix_sz, "%s/%.*s", output_dir, namelen, fname);
    } else if (slash) {
        int dirlen = (int)(slash - input_path + 1);
        snprintf(prefix, prefix_sz, "%.*s%.*s", dirlen, input_path, namelen, fname);
    } else {
        snprintf(prefix, prefix_sz, "%.*s", namelen, fname);
    }
}

// ── Main function ─────────────────────────────────────────────────────────────

int split_file(const char *input_path, const char *by_col, const char *output_dir, int drop_col) {
    memset(split_buckets, 0, sizeof(split_buckets));
    split_entry_count = 0;

    FILE *in = fopen(input_path, "r");
    if (!in) {
        fprintf(stderr, "Error: cannot open '%s'\n", input_path);
        return 1;
    }

    char *line = malloc(SPLIT_LINE_BUF);
    if (!line) { fclose(in); return 1; }

    // Read the header
    if (!fgets(line, SPLIT_LINE_BUF, in)) {
        fprintf(stderr, "Error: file is empty: %s\n", input_path);
        fclose(in); free(line); return 1;
    }
    line[strcspn(line, "\n")] = '\0';
    char *header = strdup(line);

    int col_idx = find_col_index(header, by_col);
    if (col_idx < 0) {
        fprintf(stderr, "Error: column '%s' not found\nHeader: %s\n", by_col, header);
        fclose(in); free(line); free(header); return 1;
    }

    printf("Splitting '%s' by column %d (\"%s\")%s\n\n",
           input_path, col_idx + 1, by_col,
           drop_col ? " [column will be removed from output]" : "");

    // Header to write (with the column removed or original)
    char *write_header;
    char *stripped_header = NULL;
    if (drop_col) {
        stripped_header = malloc(SPLIT_LINE_BUF);
        remove_csv_col(header, col_idx, stripped_header, SPLIT_LINE_BUF);
        write_header = stripped_header;
    } else {
        write_header = header;
    }

    // Prefix for output filenames
    char dir_prefix[1024];
    get_base_prefix(input_path, output_dir, dir_prefix, sizeof(dir_prefix));

    // Process rows
    long total_rows  = 0;
    int  error_flag  = 0;
    char val_buf[SPLIT_VAL_BUF];
    char safe_val[SPLIT_VAL_BUF];
    char *row_buf = drop_col ? malloc(SPLIT_LINE_BUF) : NULL;

    while (fgets(line, SPLIT_LINE_BUF, in)) {
        line[strcspn(line, "\n")] = '\0';
        if (!*line) continue;

        get_csv_col(line, col_idx, val_buf, sizeof(val_buf));

        // Look up the entry in the hash table
        unsigned int h = split_hash(val_buf);
        SplitEntry *e = split_buckets[h];
        while (e && strcmp(e->key, val_buf) != 0) e = e->next;

        if (!e) {
            if (split_entry_count >= MAX_SPLIT_FILES) {
                fprintf(stderr,
                    "\nError: more than %d unique values — aborting.\n"
                    "Make sure you're splitting by a low-cardinality column.\n",
                    MAX_SPLIT_FILES);
                error_flag = 1;
                break;
            }

            sanitize_for_filename(*val_buf ? val_buf : "empty", safe_val, sizeof(safe_val));

            char filepath[2048];
            snprintf(filepath, sizeof(filepath), "%s_%s.csv", dir_prefix, safe_val);

            FILE *fp = fopen(filepath, "w");
            if (!fp) {
                fprintf(stderr, "\nError: cannot create '%s'\n", filepath);
                error_flag = 1;
                break;
            }
            fprintf(fp, "%s\n", write_header);

            e = malloc(sizeof(SplitEntry));
            e->key       = strdup(val_buf);
            e->filename  = strdup(filepath);
            e->fp        = fp;
            e->row_count = 0;
            e->next      = split_buckets[h];
            split_buckets[h]              = e;
            split_entries[split_entry_count++] = e;
        }

        if (drop_col) {
            remove_csv_col(line, col_idx, row_buf, SPLIT_LINE_BUF);
            fprintf(e->fp, "%s\n", row_buf);
        } else {
            fprintf(e->fp, "%s\n", line);
        }
        e->row_count++;
        total_rows++;

        if (total_rows % 500000 == 0)
            printf("  ... %ld rows processed\n", total_rows);
    }

    fclose(in);
    free(line);
    free(header);
    free(stripped_header);
    free(row_buf);

    // Close files and print summary
    printf("\n%-36s  %s\n", "Value", "Rows");
    printf("%-36s  %s\n",
           "------------------------------------",
           "--------");

    for (int i = 0; i < split_entry_count; i++) {
        SplitEntry *e = split_entries[i];
        fclose(e->fp);
        const char *display_key = *e->key ? e->key : "(empty)";
        printf("  %-34s  %ld\n", display_key, e->row_count);
        free(e->key);
        free(e->filename);
        free(e);
    }

    printf("\n");
    if (!error_flag)
        printf("Done: %d files, %ld rows total\n", split_entry_count, total_rows);

    return error_flag;
}
