#include "profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ── tunables ────────────────────────────────────────────────────────────── */

#define MAX_COLS       702
#define LINE_BUF       65536
#define FREQ_HASH_SZ   65536
#define FREQ_CAP       20000   /* max unique values tracked per column  */
#define TOP_N          5       /* top-N values to show                  */

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Format a large count: 1234567 → "1.23M", 12345 → "12.3k", 123 → "123" */
static void fmt_count(long n, char *buf, int sz)
{
    if (n >= 1000000L)      snprintf(buf, sz, "%.2fM", n / 1000000.0);
    else if (n >= 1000L)    snprintf(buf, sz, "%.1fk", n / 1000.0);
    else                    snprintf(buf, sz, "%ld",   n);
}

/* Try to parse as a double; return 1 on success */
static int is_number(const char *s)
{
    if (!s || !*s) return 0;
    char *end;
    strtod(s, &end);
    while (isspace((unsigned char)*end)) end++;
    return *end == '\0';
}

/* Heuristic: looks like a date (YYYY-MM... or YYYY/MM...) */
static int is_date(const char *s)
{
    if (!s || strlen(s) < 7) return 0;
    return isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
           isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) &&
           (s[4] == '-' || s[4] == '/') &&
           isdigit((unsigned char)s[5]) && isdigit((unsigned char)s[6]);
}

/* ── frequency map ───────────────────────────────────────────────────────── */

typedef struct FNode { char *key; long count; struct FNode *next; } FNode;
typedef struct { FNode **b; int sz; long entries; int capped; } FMap;

static FMap *fmap_new(void)
{
    FMap *m = malloc(sizeof *m);
    m->sz      = FREQ_HASH_SZ;
    m->entries = 0;
    m->capped  = 0;
    m->b       = calloc(m->sz, sizeof(FNode *));
    return m;
}

static unsigned int fmap_hash(const char *k, int sz)
{
    unsigned int h = 2166136261u;
    while (*k) { h ^= (unsigned char)*k++; h *= 16777619u; }
    return h & (unsigned int)(sz - 1);
}

static void fmap_add(FMap *m, const char *key)
{
    unsigned int h = fmap_hash(key, m->sz);
    for (FNode *e = m->b[h]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) { e->count++; return; }
    }
    if (m->capped) return;
    FNode *e = malloc(sizeof *e);
    e->key  = strdup(key);
    e->count = 1;
    e->next  = m->b[h];
    m->b[h]  = e;
    if (++m->entries >= FREQ_CAP) m->capped = 1;
}

/* Fill out_keys[n] / out_counts[n] with top-n entries; returns actual count */
static int fmap_top(FMap *m, int n, char **out_keys, long *out_counts)
{
    int found = 0;
    for (int i = 0; i < m->sz; i++) {
        for (FNode *e = m->b[i]; e; e = e->next) {
            int pos = found < n ? found++ : n - 1;
            if (found - 1 < n || e->count > out_counts[n - 1]) {
                out_keys[pos]   = e->key;
                out_counts[pos] = e->count;
                /* keep sorted descending */
                for (int j = pos; j > 0 && out_counts[j] > out_counts[j-1]; j--) {
                    char *tk = out_keys[j]; out_keys[j] = out_keys[j-1]; out_keys[j-1] = tk;
                    long  tc = out_counts[j]; out_counts[j] = out_counts[j-1]; out_counts[j-1] = tc;
                }
            }
        }
    }
    return found < n ? found : n;
}

static void fmap_free(FMap *m)
{
    for (int i = 0; i < m->sz; i++) {
        FNode *e = m->b[i];
        while (e) { FNode *nx = e->next; free(e->key); free(e); e = nx; }
    }
    free(m->b);
    free(m);
}

/* ── CSV field extraction ────────────────────────────────────────────────── */

static void get_field(const char *line, int idx, char sep, char *buf, int bufsz)
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
        } else { while (*p && *p != sep) p++; }
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

/* Parse header into col_names array; returns column count */
static int parse_header(const char *line, char sep, char col_names[][256], int max_cols)
{
    int n = 0;
    const char *p = line;
    while (*p && n < max_cols) {
        int i = 0;
        if (*p == '"') {
            p++;
            while (*p && i < 255) {
                if (*p == '"' && *(p+1) == '"') { col_names[n][i++] = '"'; p += 2; continue; }
                if (*p == '"') { p++; break; }
                col_names[n][i++] = *p++;
            }
        } else {
            while (*p && *p != sep && *p != '\n' && *p != '\r' && i < 255)
                col_names[n][i++] = *p++;
        }
        col_names[n][i] = '\0';
        n++;
        if (*p == sep) p++;
        else break;
    }
    return n;
}

/* ── per-column accumulator ──────────────────────────────────────────────── */

typedef struct {
    long   total;         /* non-empty values           */
    long   nulls;         /* empty values               */
    /* type detection counters */
    long   num_ok;        /* parsed as number           */
    long   date_ok;       /* looks like a date          */
    /* numeric stats (Welford online) */
    double num_min, num_max;
    double num_mean, num_M2;  /* for stddev               */
    long   num_count;
    /* frequency map */
    FMap  *freq;
} ColAcc;

/* ── main ────────────────────────────────────────────────────────────────── */

int profile_file(const char *input_path, char sep)
{
    FILE *in = fopen(input_path, "r");
    if (!in) { fprintf(stderr, "Error: cannot open '%s'\n", input_path); return 1; }

    char *linebuf = malloc(LINE_BUF);
    if (!linebuf) { fclose(in); return 1; }

    /* read header */
    if (!fgets(linebuf, LINE_BUF, in)) {
        fprintf(stderr, "Error: file is empty\n");
        fclose(in); free(linebuf); return 1;
    }
    linebuf[strcspn(linebuf, "\r\n")] = '\0';

    char col_names[MAX_COLS][256];
    int ncols = parse_header(linebuf, sep, col_names, MAX_COLS);
    if (ncols == 0) { fprintf(stderr, "Error: no columns found\n"); fclose(in); free(linebuf); return 1; }

    /* allocate accumulators */
    ColAcc *acc = calloc(ncols, sizeof(ColAcc));
    for (int c = 0; c < ncols; c++) {
        acc[c].num_min = INFINITY;
        acc[c].num_max = -INFINITY;
        acc[c].freq    = fmap_new();
    }

    char val[512];
    long total_rows = 0;

    /* ── pass 1: scan all rows ── */
    while (fgets(linebuf, LINE_BUF, in)) {
        linebuf[strcspn(linebuf, "\r\n")] = '\0';
        if (!*linebuf) continue;
        total_rows++;

        if (total_rows % 500000 == 0) {
            printf("\r  scanning... %ld rows", total_rows);
            fflush(stdout);
        }

        for (int c = 0; c < ncols; c++) {
            get_field(linebuf, c, sep, val, sizeof(val));
            if (!*val) { acc[c].nulls++; continue; }
            acc[c].total++;

            /* type detection */
            if (is_number(val)) {
                acc[c].num_ok++;
                double v = atof(val);
                /* Welford online mean/variance */
                acc[c].num_count++;
                double delta = v - acc[c].num_mean;
                acc[c].num_mean += delta / acc[c].num_count;
                acc[c].num_M2   += delta * (v - acc[c].num_mean);
                if (v < acc[c].num_min) acc[c].num_min = v;
                if (v > acc[c].num_max) acc[c].num_max = v;
            } else if (is_date(val)) {
                acc[c].date_ok++;
            }

            /* frequency map */
            fmap_add(acc[c].freq, val);
        }
    }
    if (total_rows > 500000) printf("\r                              \r");

    /* ── pass 2: count outliers for numeric columns ── */
    long *outliers = calloc(ncols, sizeof(long));
    int   has_numeric = 0;
    double thresholds_lo[MAX_COLS], thresholds_hi[MAX_COLS];

    for (int c = 0; c < ncols; c++) {
        long nz = acc[c].total;
        if (nz == 0) continue;
        int is_num = (acc[c].num_ok * 100 / nz) >= 80;
        if (!is_num) continue;
        has_numeric = 1;
        double stddev = acc[c].num_count > 1
                        ? sqrt(acc[c].num_M2 / (acc[c].num_count - 1)) : 0.0;
        thresholds_lo[c] = acc[c].num_mean - 3.0 * stddev;
        thresholds_hi[c] = acc[c].num_mean + 3.0 * stddev;
    }

    if (has_numeric) {
        rewind(in);
        fgets(linebuf, LINE_BUF, in); /* skip header */
        long r2 = 0;
        while (fgets(linebuf, LINE_BUF, in)) {
            linebuf[strcspn(linebuf, "\r\n")] = '\0';
            if (!*linebuf) continue;
            r2++;
            if (r2 % 500000 == 0) {
                printf("\r  outliers pass... %ld rows", r2);
                fflush(stdout);
            }
            for (int c = 0; c < ncols; c++) {
                long nz = acc[c].total;
                if (nz == 0) continue;
                if ((acc[c].num_ok * 100 / nz) < 80) continue;
                get_field(linebuf, c, sep, val, sizeof(val));
                if (!*val) continue;
                double v = atof(val);
                if (v < thresholds_lo[c] || v > thresholds_hi[c]) outliers[c]++;
            }
        }
        if (r2 > 500000) printf("\r                                    \r");
    }

    fclose(in);

    /* ── output ── */
    /* detect max col name length for formatting */
    int max_name = 6; /* "Column" */
    for (int c = 0; c < ncols; c++) {
        int l = (int)strlen(col_names[c]);
        if (l > max_name) max_name = l;
    }
    if (max_name > 24) max_name = 24;

    char row_str[32], col_str[32];
    fmt_count(total_rows, row_str, sizeof(row_str));
    fmt_count(ncols, col_str, sizeof(col_str));

    /* filename without path */
    const char *fname = strrchr(input_path, '/');
    fname = fname ? fname + 1 : input_path;

    printf("\nProfile: %s  \xe2\x94\x80  %s rows x %d columns\n\n", fname, row_str, ncols);

    /* header line */
    printf(" %3s  %-*s  %-7s  %6s  %8s   %s\n",
           "#", max_name, "Column", "Type", "Nulls%", "Unique", "Stats");
    int line_width = 4 + max_name + 2 + 7 + 2 + 6 + 2 + 8 + 3 + 60;
    for (int i = 0; i < line_width; i++) printf("\xe2\x94\x80");
    printf("\n");

    for (int c = 0; c < ncols; c++) {
        long nz  = acc[c].total;
        long tot = nz + acc[c].nulls;
        if (tot == 0) tot = 1;

        /* type */
        const char *type_str;
        if (nz == 0)                                  type_str = "empty";
        else if ((acc[c].num_ok  * 100 / nz) >= 80)  type_str = "number";
        else if ((acc[c].date_ok * 100 / nz) >= 80)  type_str = "date";
        else                                           type_str = "string";

        /* nulls% */
        double null_pct = 100.0 * acc[c].nulls / tot;

        /* unique count */
        char uniq_str[32];
        if (acc[c].freq->capped)
            snprintf(uniq_str, sizeof(uniq_str), "%dk+", FREQ_CAP / 1000);
        else {
            fmt_count(acc[c].freq->entries, uniq_str, sizeof(uniq_str));
        }

        /* truncated column name */
        char name_trunc[25];
        snprintf(name_trunc, sizeof(name_trunc), "%s", col_names[c]);

        printf(" %3d  %-*s  %-7s  %5.1f%%  %8s   ",
               c + 1, max_name, name_trunc, type_str, null_pct, uniq_str);

        /* stats */
        if (strcmp(type_str, "number") == 0) {
            double stddev = acc[c].num_count > 1
                            ? sqrt(acc[c].num_M2 / (acc[c].num_count - 1)) : 0.0;
            /* choose %g format: avoid scientific notation for normal values */
            char smin[32], smax[32], smean[32], sstd[32];
            snprintf(smin,  sizeof(smin),  "%g", acc[c].num_min);
            snprintf(smax,  sizeof(smax),  "%g", acc[c].num_max);
            snprintf(smean, sizeof(smean), "%g", acc[c].num_mean);
            snprintf(sstd,  sizeof(sstd),  "%g", stddev);
            printf("min=%-10s max=%-10s mean=%-10s \xcf\x83=%-8s", smin, smax, smean, sstd);
            if (outliers[c] > 0) {
                char ob[32]; fmt_count(outliers[c], ob, sizeof(ob));
                printf("  [%s outliers >3\xcf\x83]", ob);
            }
        } else {
            /* top-N values */
            char *top_keys[TOP_N];
            long  top_counts[TOP_N];
            int   ntop = fmap_top(acc[c].freq, TOP_N, top_keys, top_counts);
            for (int i = 0; i < ntop; i++) {
                char cb[16]; fmt_count(top_counts[i], cb, sizeof(cb));
                /* truncate value to 16 chars */
                char vtrunc[17];
                snprintf(vtrunc, sizeof(vtrunc), "%s", top_keys[i]);
                if (i > 0) printf("  ");
                printf("%s(%s)", vtrunc, cb);
            }
        }
        printf("\n");
    }
    printf("\n");

    for (int c = 0; c < ncols; c++) fmap_free(acc[c].freq);
    free(acc);
    free(outliers);
    free(linebuf);
    return 0;
}
