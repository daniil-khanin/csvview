#include "dedup.h"
#include "csvview_defs.h"
#include "csv_mmap.h"
#include "sorting.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Progress display ────────────────────────────────────────────────────── */

#define DEDUP_PROGRESS_STEP 100000
#define DEDUP_BAR_WIDTH     30

/* Call every DEDUP_PROGRESS_STEP rows.
 * total > 0 → show percentage bar; total <= 0 → show spinner with row count. */
static void dedup_progress(long done, long total)
{
    if (done % DEDUP_PROGRESS_STEP != 0) return;
    if (total > 0) {
        int pct = (int)(100LL * done / total);
        int pos = (int)((long)DEDUP_BAR_WIDTH * done / total);
        printf("\r  [");
        for (int i = 0; i < DEDUP_BAR_WIDTH; i++) {
            if      (i < pos) putchar('=');
            else if (i == pos) putchar('>');
            else               putchar(' ');
        }
        printf("] %3d%%  %ld / %ld rows", pct, done, total);
    } else {
        /* spinner: | / - \ */
        static int spin = 0;
        const char *frames = "|/-\\";
        printf("\r  %c  %ld rows processed", frames[spin++ & 3], done);
    }
    fflush(stdout);
}

static void dedup_progress_done(long kept, long removed)
{
    printf("\r  [");
    for (int i = 0; i < DEDUP_BAR_WIDTH; i++) putchar('=');
    printf("] 100%%  done                        \n");
    printf("  Kept: %ld rows  |  Removed: %ld duplicates\n", kept, removed);
    fflush(stdout);
}

/* ── FNV-1a hash table ───────────────────────────────────────────────────── */

#define DHASH_INIT 65536

typedef struct DNode {
    char         *key;
    long          last_line;   /* for keep_last pass 1 */
    struct DNode *next;
} DNode;

typedef struct {
    DNode **buckets;
    int     size;
} DHashSet;

static unsigned int d_hash(const char *k, int size)
{
    unsigned int h = 2166136261u;
    while (*k) { h ^= (unsigned char)*k++; h *= 16777619u; }
    return h & (unsigned int)(size - 1);
}

static DHashSet *dhs_new(int sz)
{
    DHashSet *s = malloc(sizeof *s);
    s->size    = sz;
    s->buckets = calloc(sz, sizeof(DNode *));
    return s;
}

/* Returns 1 if newly inserted, 0 if key already existed */
static int dhs_insert(DHashSet *s, const char *key)
{
    unsigned int h = d_hash(key, s->size);
    for (DNode *e = s->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return 0;
    DNode *e  = malloc(sizeof *e);
    e->key       = strdup(key);
    e->last_line = -1;
    e->next      = s->buckets[h];
    s->buckets[h] = e;
    return 1;
}

/* Set (or insert) last_line for key */
static void dhs_set_line(DHashSet *s, const char *key, long lineno)
{
    unsigned int h = d_hash(key, s->size);
    for (DNode *e = s->buckets[h]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) { e->last_line = lineno; return; }
    }
    DNode *e  = malloc(sizeof *e);
    e->key       = strdup(key);
    e->last_line = lineno;
    e->next      = s->buckets[h];
    s->buckets[h] = e;
}

static long dhs_get_line(DHashSet *s, const char *key)
{
    unsigned int h = d_hash(key, s->size);
    for (DNode *e = s->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->last_line;
    return -1;
}

static void dhs_free(DHashSet *s)
{
    for (int i = 0; i < s->size; i++) {
        DNode *e = s->buckets[i];
        while (e) { DNode *nx = e->next; free(e->key); free(e); e = nx; }
    }
    free(s->buckets);
    free(s);
}

/* ── CSV field extraction (CLI mode, explicit sep) ───────────────────────── */

static void csv_field_sep(const char *line, int idx, char sep, char *buf, int bufsz)
{
    int col = 0;
    const char *p = line;
    while (col < idx && *p) {
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && *(p+1) == '"') { p += 2; continue; }
                if (*p == '"') { p++; break; }
                p++;
            }
        } else {
            while (*p && *p != sep) p++;
        }
        if (*p == sep) p++;
        col++;
    }
    int i = 0;
    if (*p == '"') {
        p++;
        while (*p && i < bufsz-1) {
            if (*p == '"' && *(p+1) == '"') { buf[i++] = '"'; p += 2; continue; }
            if (*p == '"') { p++; break; }
            buf[i++] = *p++;
        }
    } else {
        while (*p && *p != sep && *p != '\n' && *p != '\r' && i < bufsz-1)
            buf[i++] = *p++;
    }
    buf[i] = '\0';
}

/* ── Column index lookup ─────────────────────────────────────────────────── */

/* CLI: find column by name in a CSV-format header string */
static int cli_find_col(const char *header, char sep, const char *name)
{
    char *end;
    long n = strtol(name, &end, 10);
    if (*end == '\0' && n > 0) return (int)(n - 1);

    char field[512];
    int idx = 0;
    const char *p = header;
    while (*p) {
        int i = 0;
        if (*p == '"') {
            p++;
            while (*p && i < 511) {
                if (*p == '"' && *(p+1) == '"') { field[i++] = '"'; p += 2; continue; }
                if (*p == '"') { p++; break; }
                field[i++] = *p++;
            }
        } else {
            while (*p && *p != sep && *p != '\n' && *p != '\r' && i < 511)
                field[i++] = *p++;
        }
        field[i] = '\0';
        if (strcasecmp(field, name) == 0) return idx;
        if (*p == sep) { p++; idx++; } else break;
    }
    return -1;
}

/* Interactive: find column by name in column_names[] */
static int iact_find_col(const char *name)
{
    char *end;
    long n = strtol(name, &end, 10);
    if (*end == '\0' && n > 0 && n <= col_count) return (int)(n - 1);
    for (int i = 0; i < col_count; i++)
        if (column_names[i] && strcasecmp(column_names[i], name) == 0) return i;
    return -1;
}

/* ── Key builders ────────────────────────────────────────────────────────── */

static void build_key_sep(const char *line, char sep,
                           int *indices, int ncols,
                           char *keybuf, int keysz)
{
    keybuf[0] = '\0';
    char val[512];
    int klen = 0;
    for (int i = 0; i < ncols; i++) {
        if (i > 0 && klen < keysz-1) keybuf[klen++] = '\x01';
        csv_field_sep(line, indices[i], sep, val, sizeof(val));
        int vlen = (int)strlen(val);
        if (klen + vlen < keysz-1) { memcpy(keybuf+klen, val, vlen); klen += vlen; }
    }
    keybuf[klen] = '\0';
}

/* Interactive uses global csv_delimiter */
static void build_key_iact(const char *line, int *indices, int ncols,
                            char *keybuf, int keysz)
{
    build_key_sep(line, csv_delimiter, indices, ncols, keybuf, keysz);
}

/* ── Parse by_cols string → int[] indices (CLI) ──────────────────────────── */

static int parse_cols_cli(const char *by_cols, const char *header, char sep,
                           int ncols_total, int *out, int maxout)
{
    if (!by_cols || !*by_cols) {
        int n = ncols_total < maxout ? ncols_total : maxout;
        for (int i = 0; i < n; i++) out[i] = i;
        return n;
    }
    char buf[1024];
    strncpy(buf, by_cols, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    int n = 0;
    char *tok = strtok(buf, ",");
    while (tok && n < maxout) {
        while (*tok == ' ') tok++;
        char *e = tok + strlen(tok) - 1;
        while (e > tok && *e == ' ') *e-- = '\0';
        int idx = cli_find_col(header, sep, tok);
        if (idx < 0) { fprintf(stderr, "Error: column '%s' not found\n", tok); return -1; }
        out[n++] = idx;
        tok = strtok(NULL, ",");
    }
    return n;
}

/* ── Parse by_cols string → int[] indices (interactive) ─────────────────── */

static int parse_cols_iact(const char *by_cols, int *out, int maxout)
{
    if (!by_cols || !*by_cols) {
        int n = col_count < maxout ? col_count : maxout;
        for (int i = 0; i < n; i++) out[i] = i;
        return n;
    }
    char buf[1024];
    strncpy(buf, by_cols, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    int n = 0;
    char *tok = strtok(buf, ",");
    while (tok && n < maxout) {
        while (*tok == ' ') tok++;
        char *e = tok + strlen(tok) - 1;
        while (e > tok && *e == ' ') *e-- = '\0';
        int idx = iact_find_col(tok);
        if (idx < 0) { fprintf(stderr, "Column '%s' not found\n", tok); return -1; }
        out[n++] = idx;
        tok = strtok(NULL, ",");
    }
    return n;
}

/* ── CLI: dedup_file ─────────────────────────────────────────────────────── */

int dedup_file(const char *input_path,
               const char *by_cols,
               int         keep_last,
               const char *output_path,
               char        sep)
{
    FILE *in = fopen(input_path, "r");
    if (!in) { fprintf(stderr, "Error: cannot open '%s'\n", input_path); return 1; }

    char *linebuf = malloc(65536);
    if (!linebuf) { fclose(in); return 1; }

    if (!fgets(linebuf, 65536, in)) {
        fprintf(stderr, "Error: file is empty\n");
        fclose(in); free(linebuf); return 1;
    }
    linebuf[strcspn(linebuf, "\r\n")] = '\0';
    char *header = strdup(linebuf);

    int ncols_total = 1;
    for (const char *p = header; *p; p++) if (*p == sep) ncols_total++;

    int col_indices[702];
    int ncols_key = parse_cols_cli(by_cols, header, sep, ncols_total, col_indices, 702);
    if (ncols_key < 0) { fclose(in); free(linebuf); free(header); return 1; }

    char auto_name[256];
    const char *outfile = output_path;
    if (!outfile || !*outfile) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(auto_name, sizeof(auto_name), "deduped_%Y%m%d_%H%M%S.csv", tm_info);
        outfile = auto_name;
    }

    if (by_cols && *by_cols)
        printf("Deduplicating '%s' by: %s | keep: %s -> '%s'\n\n",
               input_path, by_cols, keep_last ? "last" : "first", outfile);
    else
        printf("Deduplicating '%s' by: all %d columns | keep: %s -> '%s'\n\n",
               input_path, ncols_total, keep_last ? "last" : "first", outfile);

    char keybuf[8192];
    long total_rows = 0, kept_rows = 0, dupes = 0;

    if (!keep_last) {
        /* single pass: keep first occurrence */
        DHashSet *seen = dhs_new(DHASH_INIT);
        FILE *out = fopen(outfile, "w");
        if (!out) {
            fprintf(stderr, "Error: cannot create '%s'\n", outfile);
            dhs_free(seen); fclose(in); free(linebuf); free(header); return 1;
        }
        fprintf(out, "%s\n", header);

        while (fgets(linebuf, 65536, in)) {
            linebuf[strcspn(linebuf, "\r\n")] = '\0';
            if (!*linebuf) continue;
            build_key_sep(linebuf, sep, col_indices, ncols_key, keybuf, sizeof(keybuf));
            if (dhs_insert(seen, keybuf)) { fprintf(out, "%s\n", linebuf); kept_rows++; }
            else                          dupes++;
            total_rows++;
            dedup_progress(total_rows, -1);
        }
        fclose(out);
        dhs_free(seen);
    } else {
        /* two passes: keep last occurrence
         * Pass 1: record last line number for each key
         * Pass 2: write only rows whose line number matches recorded value */
        DHashSet *last = dhs_new(DHASH_INIT);

        printf("Pass 1/2: scanning...\n");
        long lineno = 0;
        while (fgets(linebuf, 65536, in)) {
            linebuf[strcspn(linebuf, "\r\n")] = '\0';
            if (!*linebuf) { lineno++; continue; }
            build_key_sep(linebuf, sep, col_indices, ncols_key, keybuf, sizeof(keybuf));
            dhs_set_line(last, keybuf, lineno++);
            total_rows++;
            dedup_progress(total_rows, -1);
        }
        printf("\n");

        rewind(in);
        fgets(linebuf, 65536, in); /* skip header */

        printf("Pass 2/2: writing output...\n");
        FILE *out = fopen(outfile, "w");
        if (!out) {
            fprintf(stderr, "Error: cannot create '%s'\n", outfile);
            dhs_free(last); fclose(in); free(linebuf); free(header); return 1;
        }
        fprintf(out, "%s\n", header);

        lineno = 0;
        long written = 0;
        while (fgets(linebuf, 65536, in)) {
            linebuf[strcspn(linebuf, "\r\n")] = '\0';
            if (!*linebuf) { lineno++; continue; }
            build_key_sep(linebuf, sep, col_indices, ncols_key, keybuf, sizeof(keybuf));
            if (dhs_get_line(last, keybuf) == lineno) { fprintf(out, "%s\n", linebuf); kept_rows++; }
            else                                        dupes++;
            lineno++;
            written++;
            dedup_progress(written, total_rows);
        }
        fclose(out);
        dhs_free(last);
    }

    dedup_progress_done(kept_rows, dupes);

    fclose(in);
    free(linebuf);
    free(header);

    printf("Output: %s\n", outfile);
    return 0;
}

/* ── Interactive: dedup_make_filter ─────────────────────────────────────── */

int *dedup_make_filter(const char        *by_cols,
                       int                keep_last,
                       int               *out_count,
                       int               *removed_out,
                       dedup_progress_fn  progress,
                       void              *ud)
{
    *out_count   = 0;
    *removed_out = 0;

    int display_count;
    if (filter_active)      display_count = filtered_count;
    else if (sort_col >= 0) display_count = sorted_count;
    else                    display_count = row_count - (use_headers ? 1 : 0);

    if (display_count <= 0) return NULL;

    int col_indices[702];
    int ncols_key = parse_cols_iact(by_cols, col_indices, 702);
    if (ncols_key < 0) return NULL;

#define IACT_PROGRESS_STEP 50000
#define IACT_PROGRESS(d, pass) \
    do { if (progress && (d) % IACT_PROGRESS_STEP == 0) \
             progress((long)(d), (long)display_count, (pass), ud); } while(0)

    int  *result  = malloc(display_count * sizeof(int));
    char  linebuf[MAX_LINE_LEN];
    char  keybuf[8192];
    int   rcount = 0, dupes = 0;

    if (!keep_last) {
        /* single pass: keep first */
        DHashSet *seen = dhs_new(DHASH_INIT);
        for (int d = 0; d < display_count; d++) {
            int real_row = get_real_row(d);
            const char *line = rows[real_row].line_cache;
            if (!line) {
                if (g_mmap_base) {
                    char *ml = csv_mmap_get_line(rows[real_row].offset,
                                                 linebuf, sizeof(linebuf));
                    if (ml && !rows[real_row].line_cache)
                        rows[real_row].line_cache = strdup(linebuf);
                    line = ml ? linebuf : "";
                } else {
                    line = "";
                }
            }
            build_key_iact(line, col_indices, ncols_key, keybuf, sizeof(keybuf));
            if (dhs_insert(seen, keybuf)) result[rcount++] = real_row;
            else                          dupes++;
            IACT_PROGRESS(d, 2);
        }
        dhs_free(seen);
    } else {
        /* two passes: keep last */
        DHashSet *last = dhs_new(DHASH_INIT);

        /* pass 1: record last display index for each key */
        for (int d = 0; d < display_count; d++) {
            int real_row = get_real_row(d);
            const char *line = rows[real_row].line_cache;
            if (!line) {
                if (g_mmap_base) {
                    char *ml = csv_mmap_get_line(rows[real_row].offset,
                                                 linebuf, sizeof(linebuf));
                    if (ml && !rows[real_row].line_cache)
                        rows[real_row].line_cache = strdup(linebuf);
                    line = ml ? linebuf : "";
                } else {
                    line = "";
                }
            }
            build_key_iact(line, col_indices, ncols_key, keybuf, sizeof(keybuf));
            dhs_set_line(last, keybuf, (long)d);
            IACT_PROGRESS(d, 1);
        }

        /* pass 2: keep rows matching their last d */
        for (int d = 0; d < display_count; d++) {
            int real_row = get_real_row(d);
            const char *line = rows[real_row].line_cache ? rows[real_row].line_cache : "";
            build_key_iact(line, col_indices, ncols_key, keybuf, sizeof(keybuf));
            if (dhs_get_line(last, keybuf) == (long)d) result[rcount++] = real_row;
            else                                         dupes++;
            IACT_PROGRESS(d, 2);
        }
        dhs_free(last);
    }

#undef IACT_PROGRESS_STEP
#undef IACT_PROGRESS

    *out_count   = rcount;
    *removed_out = dupes;
    return result;
}
