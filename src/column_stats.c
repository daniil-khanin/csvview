/**
 * column_stats.c
 *
 * Implementation of the column statistics window with calculation of all metrics
 */

#include "column_stats.h"
#include "utils.h"          // get_column_value, trim, col_letter, etc.
#include "csv_mmap.h"
#include "filtering.h"      // apply_filter

#include <ncurses.h>        // newwin, mvwprintw, wrefresh, etc.
#include <stdlib.h>         // malloc, realloc, free, qsort
#include <string.h>         // strcpy, strncpy, strcmp, strlen
#include <stdio.h>          // snprintf, sscanf
#include <math.h>           // INFINITY, strtod
#include <limits.h>         // for INT_MAX/INT_MIN if needed

// ────────────────────────────────────────────────
// Helper structures (internal to this module only)
// ────────────────────────────────────────────────

typedef struct {
    char *value;
    long count;
} Freq;

typedef struct {
    char label[16];
    long count;
    int sort_key;           // for sorting: year*100 + month, etc.
} DateGroup;

#define MAX_UNIQUE 100000

/* Hash table for O(1) frequency lookup instead of O(n) linear scan */
#define FREQ_HASH_BITS 18
#define FREQ_HASH_SIZE (1 << FREQ_HASH_BITS)
#define FREQ_HASH_MASK (FREQ_HASH_SIZE - 1)

static unsigned int freq_hash_fn(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

/* Find or insert value in freqs[]. Returns 1 on success, 0 if table full. */
static int freq_lookup_insert(Freq *freqs, long *freq_count, long *ht, const char *val)
{
    unsigned int slot = freq_hash_fn(val) & FREQ_HASH_MASK;
    for (int probe = 0; probe < 512; probe++, slot = (slot + 1) & FREQ_HASH_MASK) {
        long idx = ht[slot];
        if (idx < 0) {
            /* Empty — insert new entry */
            if (*freq_count >= MAX_UNIQUE) return 0;
            idx = *freq_count;
            freqs[idx].value = strdup(val);
            freqs[idx].count = 1;
            ht[slot] = idx;
            (*freq_count)++;
            return 1;
        }
        if (strcmp(freqs[idx].value, val) == 0) {
            freqs[idx].count++;
            return 1;
        }
    }
    /* Fallback linear scan (shouldn't happen with good hash distribution) */
    for (long j = 0; j < *freq_count; j++) {
        if (strcmp(freqs[j].value, val) == 0) { freqs[j].count++; return 1; }
    }
    if (*freq_count < MAX_UNIQUE) {
        freqs[*freq_count].value = strdup(val);
        freqs[*freq_count].count = 1;
        (*freq_count)++;
    }
    return 1;
}

// ────────────────────────────────────────────────
// Comparison functions for sorting Freq arrays
// ────────────────────────────────────────────────

static int cmp_freq_count_desc(const void *a, const void *b) {
    const Freq *fa = a, *fb = b;
    if (fb->count != fa->count) return (fb->count > fa->count) ? 1 : -1;
    return strcmp(fa->value, fb->value);
}
static int cmp_freq_count_asc(const void *a, const void *b) {
    const Freq *fa = a, *fb = b;
    if (fa->count != fb->count) return (fa->count > fb->count) ? 1 : -1;
    return strcmp(fa->value, fb->value);
}
static int cmp_freq_alpha_asc(const void *a, const void *b) {
    return strcmp(((const Freq*)a)->value, ((const Freq*)b)->value);
}
static int cmp_freq_alpha_desc(const void *a, const void *b) {
    return strcmp(((const Freq*)b)->value, ((const Freq*)a)->value);
}

// ────────────────────────────────────────────────
// Sorting helper for double arrays
// ────────────────────────────────────────────────

static int cmp_double_asc(const void *a, const void *b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

// Interpolated percentile on a sorted array (0 <= p <= 100)
static double percentile_sorted(const double *s, long n, double p) {
    if (n <= 0) return 0.0;
    if (n == 1) return s[0];
    double pos = (p / 100.0) * (n - 1);
    long lo = (long)pos;
    double frac = pos - lo;
    if (lo + 1 >= n) return s[n - 1];
    return s[lo] * (1.0 - frac) + s[lo + 1] * frac;
}

// ────────────────────────────────────────────────
// [p] Percentile breakdown popup
// ────────────────────────────────────────────────

static void show_percentile_breakdown(const double *vals, long n, const char *col_name)
{
    if (n <= 0) return;

    double *sorted = malloc(n * sizeof(double));
    if (!sorted) return;
    memcpy(sorted, vals, n * sizeof(double));
    qsort(sorted, n, sizeof(double), cmp_double_asc);

    static const double pcts[] = {1,5,10,25,50,75,90,95,99};
    static const char  *plbls[] = {"P1","P5","P10","P25","P50 (Median)","P75","P90","P95","P99"};
    int np = (int)(sizeof(pcts)/sizeof(pcts[0]));

    double p25 = percentile_sorted(sorted, n, 25.0);
    double p75 = percentile_sorted(sorted, n, 75.0);
    double iqr = p75 - p25;
    double mean = 0.0;
    for (long i = 0; i < n; i++) mean += sorted[i];
    mean /= n;
    double var = 0.0;
    for (long i = 0; i < n; i++) { double d = sorted[i] - mean; var += d*d; }
    double stddev = (n > 1) ? sqrt(var / (n - 1)) : 0.0;

    int pw = 48, ph = np + 13;
    if (ph > LINES - 4) ph = LINES - 4;
    int sy = (LINES - ph) / 2;
    int sx = (COLS - pw) / 2;
    WINDOW *win = newwin(ph, pw, sy, sx);
    if (!win) { free(sorted); return; }
    wbkgd(win, COLOR_PAIR(1));
    keypad(win, TRUE);
    wattron(win, COLOR_PAIR(6)); box(win, 0, 0); wattroff(win, COLOR_PAIR(6));
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 0, (pw - 22) / 2, " Percentile Breakdown ");
    wattroff(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 1, 2, "Column: %s  (n=%ld)", col_name, n);

    int y = 3;
    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "%-14s %14s", "Percentile", "Value");
    wattroff(win, A_BOLD);
    wattron(win, COLOR_PAIR(6)); mvwhline(win, y++, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));

    for (int i = 0; i < np; i++) {
        double v = percentile_sorted(sorted, n, pcts[i]);
        if (i == 4) wattron(win, COLOR_PAIR(3) | A_BOLD); // P50
        mvwprintw(win, y++, 2, "%-14s %14.4g", plbls[i], v);
        if (i == 4) wattroff(win, COLOR_PAIR(3) | A_BOLD);
    }

    wattron(win, COLOR_PAIR(6)); mvwhline(win, y++, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));
    mvwprintw(win, y++, 2, "%-14s %14.4g", "IQR", iqr);
    mvwprintw(win, y++, 2, "%-14s %14.4g", "Std Dev", stddev);
    mvwprintw(win, y++, 2, "%-14s %14.4g", "Mean", mean);

    mvwprintw(win, ph - 2, 2, "[any key] Close");
    wrefresh(win);
    wgetch(win);

    free(sorted);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

// ────────────────────────────────────────────────
// [T] Value trends sparkline popup
// ────────────────────────────────────────────────

static void show_value_trends(const double *vals, long n, const char *col_name)
{
    if (n <= 0) return;

    /* Divide rows into up to NBLOCKS blocks */
    int NBLOCKS = 20;
    if (n < NBLOCKS) NBLOCKS = (int)n;

    double block_mean[20], block_min[20], block_max[20];
    long block_n[20];

    for (int b = 0; b < NBLOCKS; b++) {
        long i0 = (long)(b * (double)n / NBLOCKS);
        long i1 = (long)((b + 1) * (double)n / NBLOCKS);
        if (i1 > n) i1 = n;
        if (i1 <= i0) { block_n[b] = 0; block_mean[b] = block_min[b] = block_max[b] = 0; continue; }
        double s = 0.0, bmin = vals[i0], bmax = vals[i0];
        for (long i = i0; i < i1; i++) {
            s += vals[i];
            if (vals[i] < bmin) bmin = vals[i];
            if (vals[i] > bmax) bmax = vals[i];
        }
        block_n[b] = i1 - i0;
        block_mean[b] = s / block_n[b];
        block_min[b] = bmin;
        block_max[b] = bmax;
    }

    /* Find global min/max of block means */
    double gmin = block_mean[0], gmax = block_mean[0];
    for (int b = 1; b < NBLOCKS; b++) {
        if (block_mean[b] < gmin) gmin = block_mean[b];
        if (block_mean[b] > gmax) gmax = block_mean[b];
    }
    double grange = gmax - gmin;
    if (grange == 0) grange = 1;

    /* Braille-style block chars for sparkline */
    static const char *sparks[] = {"▁","▂","▃","▄","▅","▆","▇","█"};

    int pw = 82, ph = 14 + NBLOCKS / 2;
    if (ph > LINES - 4) ph = LINES - 4;
    int sy = (LINES - ph) / 2;
    int sx = (COLS - pw) / 2;
    WINDOW *win = newwin(ph, pw, sy, sx);
    if (!win) return;
    wbkgd(win, COLOR_PAIR(1));
    keypad(win, TRUE);
    wattron(win, COLOR_PAIR(6)); box(win, 0, 0); wattroff(win, COLOR_PAIR(6));
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 0, (pw - 16) / 2, " Value Trends ");
    wattroff(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 1, 2, "Column: %s  (n=%ld, %d blocks)", col_name, n, NBLOCKS);

    int y = 3;
    /* Inline sparkline */
    wattron(win, A_BOLD);
    mvwprintw(win, y, 2, "Trend: ");
    wmove(win, y, 9);
    for (int b = 0; b < NBLOCKS; b++) {
        double norm = (block_mean[b] - gmin) / grange;
        int si = (int)(norm * 7.0 + 0.5);
        if (si < 0) si = 0; if (si > 7) si = 7;
        waddstr(win, sparks[si]);
    }
    wattroff(win, A_BOLD);
    y += 2;

    wattron(win, COLOR_PAIR(6)); mvwhline(win, y++, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));
    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "%-5s %8s %10s %10s %10s  %-16s", "Block", "Count", "Mean", "Min", "Max", "Rows");
    wattroff(win, A_BOLD);
    wattron(win, COLOR_PAIR(6)); mvwhline(win, y++, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));

    int data_y0 = y;
    int page_sz = ph - data_y0 - 3;
    if (page_sz < 1) page_sz = 1;
    long cur = 0, top = 0;

    for (;;) {
        if (cur >= NBLOCKS) cur = NBLOCKS - 1;
        if (cur < 0) cur = 0;
        if (cur < top) top = cur;
        if (cur >= top + page_sz) top = cur - page_sz + 1;

        for (int r = 0; r < page_sz; r++) {
            long b = top + r;
            int ry = data_y0 + r;
            mvwhline(win, ry, 1, ' ', pw - 2);
            if (b >= NBLOCKS) continue;

            /* Show block row range */
            long i0 = (long)(b * (double)n / NBLOCKS);
            long i1 = (long)((b + 1) * (double)n / NBLOCKS);

            if (b == cur) wattron(win, COLOR_PAIR(2));
            mvwprintw(win, ry, 2, "%-5ld %8ld %10.4g %10.4g %10.4g  %ld-%ld",
                      b + 1, block_n[b], block_mean[b], block_min[b], block_max[b],
                      i0 + 1, i1);
            if (b == cur) wattroff(win, COLOR_PAIR(2));
        }

        wattron(win, COLOR_PAIR(6)); mvwhline(win, data_y0 + page_sz, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));
        mvwprintw(win, data_y0 + page_sz + 1, 2, "j/k Navigate  |  [q] Close");
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 'q' || ch == 'Q' || ch == 27) break;
        else if (ch == KEY_UP   || ch == 'k') cur--;
        else if (ch == KEY_DOWN || ch == 'j') cur++;
        else if (ch == KEY_PPAGE) cur -= page_sz;
        else if (ch == KEY_NPAGE) cur += page_sz;
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

// ────────────────────────────────────────────────
// Full interactive frequency list with drilldown
// ────────────────────────────────────────────────

#define FREQ_SORT_COUNT_DESC  0
#define FREQ_SORT_COUNT_ASC   1
#define FREQ_SORT_ALPHA_ASC   2
#define FREQ_SORT_ALPHA_DESC  3

static const char *freq_sort_names[] = {
    "Count (High->Low)",
    "Count (Low->High)",
    "Alpha (A->Z)",
    "Alpha (Z->A)"
};

typedef int (*FreqCmpFn)(const void*, const void*);
static FreqCmpFn freq_cmps[] = {
    cmp_freq_count_desc,
    cmp_freq_count_asc,
    cmp_freq_alpha_asc,
    cmp_freq_alpha_desc
};

static void show_freq_list(Freq *freqs, long freq_count, long valid_count,
                           const char *col_name)
{
    if (freq_count <= 0) return;

    int sort_mode = FREQ_SORT_COUNT_DESC;   /* already sorted this way */

    WINDOW *win = newwin(LINES, COLS, 0, 0);
    if (!win) return;
    wbkgd(win, COLOR_PAIR(1));
    keypad(win, TRUE);

    /* Layout */
    int data_y0  = 6;                        /* first data row */
    int data_y1  = LINES - 4;               /* last data row (exclusive) */
    int page_sz  = data_y1 - data_y0;
    if (page_sz < 1) page_sz = 1;

    /* Column widths — adapt to terminal */
    int cnt_w  = 12;
    int pct_w  = 7;   /* "100.0%" */
    /* val_w: leave room for rank(6) + cnt + pct + 3 spaces + bar_min(10) + 2 border */
    int val_w  = COLS - 2 - 6 - cnt_w - pct_w - 3 - 10;
    if (val_w > 44) val_w = 44;
    if (val_w < 12) val_w = 12;
    int bar_w  = COLS - 2 - 6 - val_w - cnt_w - pct_w - 3;
    if (bar_w > 45) bar_w = 45;
    if (bar_w <  5) bar_w =  5;

    long cur  = 0;
    long top  = 0;
    char bar_buf[64];

    for (;;) {
        /* Clamp cursor & scroll */
        if (cur >= freq_count) cur = freq_count - 1;
        if (cur < 0) cur = 0;
        if (cur < top) top = cur;
        if (cur >= top + page_sz) top = cur - page_sz + 1;

        /* Max count for bar scaling */
        long max_cnt = 1;
        for (long i = 0; i < freq_count; i++)
            if (freqs[i].count > max_cnt) max_cnt = freqs[i].count;

        werase(win);
        wattron(win, COLOR_PAIR(6));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(6));

        /* Title */
        wattron(win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(win, 1, 2, "Frequency List: %s", col_name);
        wattroff(win, COLOR_PAIR(3) | A_BOLD);
        char info_buf[64];
        snprintf(info_buf, sizeof(info_buf), "%ld unique, %ld total", freq_count, valid_count);
        mvwprintw(win, 1, COLS - 2 - (int)strlen(info_buf), "%s", info_buf);

        /* Sort hint */
        mvwprintw(win, 2, 2, "Sort: %s   [s] cycle", freq_sort_names[sort_mode]);

        /* Divider + header */
        wattron(win, COLOR_PAIR(6));
        mvwhline(win, 3, 1, ACS_HLINE, COLS - 2);
        wattroff(win, COLOR_PAIR(6));

        wattron(win, A_BOLD);
        mvwprintw(win, 4, 2, "%-5s %-*s %*s %*s  %-*s",
                  "#", val_w, "Value", cnt_w, "Count", pct_w, "Pct%", bar_w, "Bar");
        wattroff(win, A_BOLD);

        wattron(win, COLOR_PAIR(6));
        mvwhline(win, 5, 1, ACS_HLINE, COLS - 2);
        wattroff(win, COLOR_PAIR(6));

        /* Data rows */
        for (int r = 0; r < page_sz; r++) {
            long idx = top + r;
            if (idx >= freq_count) break;
            int ry = data_y0 + r;

            /* Explicitly clear the row to avoid stale multi-byte chars */
            mvwhline(win, ry, 1, ' ', COLS - 2);

            /* Bar */
            int blen = (int)(bar_w * (double)freqs[idx].count / max_cnt + 0.5);
            if (blen > bar_w) blen = bar_w;
            memset(bar_buf, '#', blen);
            memset(bar_buf + blen, ' ', bar_w - blen);
            bar_buf[bar_w] = '\0';

            double pct = valid_count > 0
                ? (double)freqs[idx].count / valid_count * 100.0 : 0.0;

            if (idx == cur) wattron(win, COLOR_PAIR(2));

            /* Fixed x positions — avoid cursor drift after RTL text */
            int x_rank  = 2;
            int x_val   = x_rank + 6;          /* 5 digits + 1 space */
            int x_count = x_val + val_w + 1;
            int x_pct   = x_count + cnt_w + 1;
            int x_bar   = x_pct + pct_w + 2;

            /* Rank */
            mvwprintw(win, ry, x_rank, "%-5ld", idx + 1);

            /* Value cell. Purely RTL (no ASCII alphanums): right-align with
               FSI/PDI to isolate BiDi context — same as main table.
               Mixed or LTR: left-align without BiDi markers.
               Count/pct/bar use absolute x — immune to cursor drift.
               The mvwhline above clears stale multi-byte cells from prior
               frames so FSI/PDI bytes don't bleed into adjacent rows. */
            char *tv = truncate_for_display(freqs[idx].value, val_w);
            int tv_dw = utf8_display_width(tv);
            int has_ltr = 0;
            for (const char *cp = tv; *cp; cp++)
                if ((unsigned char)*cp < 0x80 && isalnum((unsigned char)*cp))
                    { has_ltr = 1; break; }
            if (str_has_rtl(tv) && !has_ltr) {
                /* Purely RTL: right-align with FSI/PDI isolation */
                int pad = (val_w > tv_dw) ? val_w - tv_dw : 0;
                mvwprintw(win, ry, x_val, "%*s\xE2\x81\xA8%s\xE2\x81\xA9",
                          pad, "", tv);
            } else {
                /* LTR / mixed: left-align, pad to val_w display columns */
                mvwprintw(win, ry, x_val, "%s", tv);
                for (int p = tv_dw; p < val_w; p++) waddch(win, ' ');
            }
            free(tv);

            /* Count, pct%, bar — absolute positions, immune to cursor drift */
            mvwprintw(win, ry, x_count, "%*ld", cnt_w, freqs[idx].count);
            mvwprintw(win, ry, x_pct,   "%*.1f%%", pct_w - 1, pct);
            mvwprintw(win, ry, x_bar,   "%s", bar_buf);

            if (idx == cur) wattroff(win, COLOR_PAIR(2));
        }

        /* Bottom divider + hint */
        wattron(win, COLOR_PAIR(6));
        mvwhline(win, data_y1, 1, ACS_HLINE, COLS - 2);
        wattroff(win, COLOR_PAIR(6));

        mvwprintw(win, data_y1 + 1, 2,
            "Row %ld/%ld  |  j/k/arrows Navigate  |  [s] Sort  |  [Enter] Drilldown filter  |  [q] Close",
            cur + 1, freq_count);

        wrefresh(win);

        int ch = wgetch(win);

        if (ch == 'q' || ch == 'Q' || ch == 27) {
            break;
        } else if (ch == KEY_UP || ch == 'k') {
            cur--;
        } else if (ch == KEY_DOWN || ch == 'j') {
            cur++;
        } else if (ch == KEY_PPAGE) {
            cur -= page_sz;
        } else if (ch == KEY_NPAGE) {
            cur += page_sz;
        } else if (ch == KEY_HOME || ch == 'g') {
            cur = 0;
        } else if (ch == KEY_END || ch == 'G') {
            cur = freq_count - 1;
        } else if (ch == 's' || ch == 'S') {
            sort_mode = (sort_mode + 1) % 4;
            qsort(freqs, freq_count, sizeof(Freq), freq_cmps[sort_mode]);
            cur = 0; top = 0;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            /* Drilldown: set filter and apply */
            if (cur >= 0 && cur < freq_count) {
                if (strchr(col_name, ' '))
                    snprintf(filter_query, sizeof(filter_query),
                             "`%s` = \"%s\"", col_name, freqs[cur].value);
                else
                    snprintf(filter_query, sizeof(filter_query),
                             "%s = \"%s\"", col_name, freqs[cur].value);
                in_filter_mode  = 1;
                cur_display_row = 0;
                top_display_row = 0;
                apply_filter(rows, f, row_count);
            }
            break;
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

// ────────────────────────────────────────────────
// Main column statistics function
// ────────────────────────────────────────────────

void show_column_stats(int col_idx)
{
    // Validate column index
    if (col_idx < 0 || col_idx >= col_count) {
        return;
    }

    // Create a centered statistics window
    int sy = (LINES - STATS_H) / 2;
    int sx = (COLS - STATS_W) / 2;
    WINDOW *win = newwin(STATS_H, STATS_W, sy, sx);
    if (!win) {
        return;
    }
    wbkgd(win, COLOR_PAIR(1));

    scrollok(win, TRUE);
    wattron(win, COLOR_PAIR(6));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(6));

    // Window title
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 0, (STATS_W - 20) / 2, " Column Statistics ");
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // Column name
    char col_name[64];
    if (use_headers && column_names[col_idx]) {
        strncpy(col_name, column_names[col_idx], sizeof(col_name)-1);
        col_name[sizeof(col_name)-1] = '\0';
    } else {
        col_letter(col_idx, col_name);
    }

    mvwprintw(win, 1, 2, "Column: %s (%s)",
              col_name,
              col_types[col_idx] == COL_NUM ? "Number" :
              col_types[col_idx] == COL_DATE ? "Date" : "String");

    wrefresh(win);

    // Show current filter if one is active
    if (filter_active && filter_query[0] != '\0') {
        char filtered_str[128];
        snprintf(filtered_str, sizeof(filtered_str), "Filtered by: %s", filter_query);
        wattron(win, COLOR_PAIR(5));  // Filter input color (replace with your own)
        mvwprintw(win, 2, 2, "%s", filtered_str);
        wattroff(win, COLOR_PAIR(5));
    }

    // ────────────────────────────────────────────────
    // Calculate statistics — single pass over all visible rows
    // ────────────────────────────────────────────────

    long total_cells = row_count - (use_headers ? 1 : 0);
    if (filter_active) {
        total_cells = filtered_count;
    }

    long valid_count = 0;
    long empty_count = 0;
    double sum_val = 0.0;
    double min_val = INFINITY;
    double max_val = -INFINITY;

    // For numbers — store only valid values (for median and histogram)
    double *numeric_values = NULL;
    long numeric_capacity = 0;
    long numeric_count = 0;

    // For dates — frequency counts by month
    #define MAX_DATE_GROUPS 360
    DateGroup monthly_groups[MAX_DATE_GROUPS];
    int monthly_group_count = 0;
    int min_year = 9999, min_month = 99;
    int max_year = 0, max_month = 0;

    // Frequencies for top-10 and mode (always as strings)
    Freq *freqs = calloc(MAX_UNIQUE, sizeof(Freq));
    long freq_count = 0;
    /* Hash table for O(1) freq lookup (initialized to -1 = empty) */
    long *freq_ht = malloc((size_t)FREQ_HASH_SIZE * sizeof(long));
    if (freq_ht) memset(freq_ht, 0xFF, (size_t)FREQ_HASH_SIZE * sizeof(long));

    long count_processed = 0;
    int display_count = filter_active ? filtered_count : row_count;

    for (int i = 0; i < display_count; i++)
    {
        int real_row = get_real_row(i);
        if (real_row < 0 || real_row >= row_count) {
            continue;
        }

        /* Fast row access via mmap or fseek fallback */
        char mmap_buf[MAX_LINE_LEN];
        if (!rows[real_row].line_cache) {
            if (g_mmap_base) {
                char *ml = csv_mmap_get_line(rows[real_row].offset, mmap_buf, sizeof(mmap_buf));
                rows[real_row].line_cache = strdup(ml ? ml : "");
            } else {
                fseek(f, rows[real_row].offset, SEEK_SET);
                char *line = malloc(MAX_LINE_LEN + 1);
                if (fgets(line, MAX_LINE_LEN + 1, f)) {
                    line[strcspn(line, "\n")] = '\0';
                    rows[real_row].line_cache = line;
                } else {
                    free(line);
                    rows[real_row].line_cache = strdup("");
                }
            }
        }

        // Get cell value
        char *cell = get_column_value(rows[real_row].line_cache,
                                      column_names[col_idx] ? column_names[col_idx] : "",
                                      use_headers);

        // Empty or NULL cell
        if (!cell || !*cell || strcmp(trim(cell), "") == 0)
        {
            empty_count++;
            free(cell);
            count_processed++;
            continue;
        }

        valid_count++;

        // Value frequencies (always as string)
        if (freq_ht) {
            freq_lookup_insert(freqs, &freq_count, freq_ht, cell);
        } else {
            /* Fallback linear scan if ht alloc failed */
            int found = 0;
            for (long j = 0; j < freq_count; j++) {
                if (strcmp(freqs[j].value, cell) == 0) { freqs[j].count++; found = 1; break; }
            }
            if (!found && freq_count < MAX_UNIQUE) {
                freqs[freq_count].value = strdup(cell);
                freqs[freq_count].count = 1;
                freq_count++;
            }
        }

        // ── Type-specific processing ──
        if (col_types[col_idx] == COL_NUM)
        {
            char *endptr;
            double val = parse_double(cell, &endptr);
            if (endptr != cell && *endptr == '\0')
            {
                sum_val += val;
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;

                // Store for median and histogram
                if (numeric_count >= numeric_capacity)
                {
                    long new_cap = numeric_capacity ? numeric_capacity * 2 : 32768;
                    double *new_ptr = realloc(numeric_values, new_cap * sizeof(double));
                    if (new_ptr)
                    {
                        numeric_values = new_ptr;
                        numeric_capacity = new_cap;
                    }
                    else
                    {
                        goto skip_num_alloc;
                    }
                }
                numeric_values[numeric_count++] = val;
            }
        }
        else if (col_types[col_idx] == COL_DATE && strlen(cell) >= 7)
        {
            int year = 0, month = 0;
            if (sscanf(cell, "%d-%d", &year, &month) == 2 &&
                year >= 1900 && year <= 9999 && month >= 1 && month <= 12)
            {
                // Update global date range
                if (year < min_year || (year == min_year && month < min_month))
                {
                    min_year = year;
                    min_month = month;
                }
                if (year > max_year || (year == max_year && month > max_month))
                {
                    max_year = year;
                    max_month = month;
                }

                // Month key
                char label[16];
                snprintf(label, sizeof(label), "%04d-%02d", year, month);

                // Add or increment group counter
                int found = 0;
                for (int g = 0; g < monthly_group_count; g++)
                {
                    if (strcmp(monthly_groups[g].label, label) == 0)
                    {
                        monthly_groups[g].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && monthly_group_count < MAX_DATE_GROUPS)
                {
                    strcpy(monthly_groups[monthly_group_count].label, label);
                    monthly_groups[monthly_group_count].count = 1;
                    monthly_groups[monthly_group_count].sort_key = year * 100 + month;
                    monthly_group_count++;
                }
            }
        }

        free(cell);
        count_processed++;

        // Show processing progress (every 1% for performance)
        if (count_processed % (total_cells / 100 + 1) == 0)
        {
            int pct = (int)((count_processed * 100LL) / total_cells);
            mvwprintw(win, STATS_H - 3, 2, "Processing %ld / %ld (%d%%)", count_processed, total_cells, pct);
            wrefresh(win);
        }
    }

skip_num_alloc:

    // ────────────────────────────────────────────────
    // Sort frequencies (top-10) and date groups
    // ────────────────────────────────────────────────

    // Sort frequencies in descending order by count
    qsort(freqs, freq_count, sizeof(Freq), cmp_freq_count_desc);

    // Sort monthly groups chronologically
    for (int i = 0; i < monthly_group_count - 1; i++)
    {
        for (int j = i + 1; j < monthly_group_count; j++)
        {
            if (monthly_groups[i].sort_key > monthly_groups[j].sort_key)
            {
                DateGroup tmp = monthly_groups[i];
                monthly_groups[i] = monthly_groups[j];
                monthly_groups[j] = tmp;
            }
        }
    }

    // ────────────────────────────────────────────────
    // Render left panel — numeric metrics and top-10
    // ────────────────────────────────────────────────

    int y = (filter_active && filter_query[0] != '\0') ? 4 : 3;
    const int label_width = 18;
    const int num_width   = 15;

    mvwprintw(win, y++, 2, "%-*s%*ld", label_width, "Total cells:", num_width, total_cells);
    mvwprintw(win, y++, 2, "%-*s%*ld (%.1f%%)", label_width, "Valid:", num_width, valid_count,
              total_cells > 0 ? (double)valid_count / total_cells * 100 : 0);
    mvwprintw(win, y++, 2, "%-*s%*ld", label_width, "Empty/Invalid:", num_width, empty_count);

    if (col_types[col_idx] == COL_NUM)
    {
        double median = 0.0;
        if (numeric_count > 0)
        {
            qsort(numeric_values, numeric_count, sizeof(double), compare_double);
            if (numeric_count % 2 == 1) {
                median = numeric_values[numeric_count / 2];
            } else {
                median = (numeric_values[numeric_count / 2 - 1] + numeric_values[numeric_count / 2]) / 2.0;
            }
        }

        long mode_count = freq_count > 0 ? freqs[0].count : 0;
        char mode_val[64] = "N/A";
        if (freq_count > 0) {
            strncpy(mode_val, freqs[0].value, sizeof(mode_val)-1);
            mode_val[sizeof(mode_val)-1] = '\0';
        }

        double mode_pct = valid_count > 0 ? (double)mode_count / valid_count * 100 : 0;

        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Sum:", num_width, sum_val);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Mean:", num_width,
                  valid_count > 0 ? sum_val / valid_count : 0);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Median:", num_width, median);
        mvwprintw(win, y++, 2, "%-*s%*.8s %ld (%.1f%%)", label_width, "Mode:", num_width,
                  mode_val, mode_count, mode_pct);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Min:", num_width,
                  min_val < INFINITY ? min_val : 0);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Max:", num_width,
                  max_val > -INFINITY ? max_val : 0);
    }

    // Top-10 values
    y += 1;
    mvwprintw(win, y++, 2, "Top 10 most frequent values:");

    long sum_top = 0;
    const int top_val_w = 20;
    for (int t = 0; t < 10 && t < freq_count; t++)
    {
        sum_top += freqs[t].count;
        double pct = valid_count > 0 ? (double)freqs[t].count / valid_count * 100 : 0;
        /* Print value padded by display columns (handles UTF-8/Cyrillic) */
        wmove(win, y++, 4);
        char *tv = truncate_for_display(freqs[t].value, top_val_w);
        waddstr(win, tv);
        int dw = utf8_display_width(tv);
        free(tv);
        for (int p = dw; p < top_val_w; p++) waddch(win, ' ');
        wprintw(win, " %*ld (%.1f%%)", 12, freqs[t].count, pct);
    }

    if (freq_count > 10)
    {
        double pct_others = valid_count > 0 ? (double)(valid_count - sum_top) / valid_count * 100 : 0;
        mvwprintw(win, y++, 4, "...");
        mvwprintw(win, y++, 4, "%-*s %*ld (%.1f%%)", 20, "Others",
                  12, valid_count - sum_top, pct_others);
    }

    // ────────────────────────────────────────────────
    // Right panel — histogram
    // ────────────────────────────────────────────────

    int hist_y = (filter_active && filter_query[0] != '\0') ? 4 : 3;
    int hist_x = LEFT_W + 3;

    mvwprintw(win, hist_y++, hist_x, "Histogram:");

    if (valid_count == 0)
    {
        mvwprintw(win, hist_y++, hist_x, "(No data to display)");
    }
    else if (col_types[col_idx] == COL_NUM && numeric_count > 0)
    {
        double range = max_val - min_val;
        if (range < 1e-9) range = 1.0;
        double bin_step = range / MAX_BINS;
        long bins[MAX_BINS] = {0};

        for (long k = 0; k < numeric_count; k++)
        {
            int bin_idx = (int)((numeric_values[k] - min_val) / bin_step);
            if (bin_idx < 0) bin_idx = 0;
            if (bin_idx >= MAX_BINS) bin_idx = MAX_BINS - 1;
            bins[bin_idx]++;
        }

        long max_bin = 0;
        for (int b = 0; b < MAX_BINS; b++) {
            if (bins[b] > max_bin) max_bin = bins[b];
        }
        if (max_bin == 0) max_bin = 1;

        int BAR_WIDTH = 35;
        char bar[BAR_WIDTH + 1];

        for (int b = 0; b < MAX_BINS; b++)
        {
            if (bins[b] == 0) continue;

            double low  = min_val + b * bin_step;
            double high = low + bin_step;

            int bar_len = (int)(BAR_WIDTH * (double)bins[b] / max_bin + 0.5);
            memset(bar, '#', bar_len);
            bar[bar_len] = '\0';

            mvwprintw(win, hist_y++, hist_x, "%12.2f → %12.2f |%-*s %8ld",
                      low, high, BAR_WIDTH, bar, bins[b]);
        }
    }
    else if (col_types[col_idx] == COL_DATE && monthly_group_count > 0)
    {
        // Determine grouping scale
        long total_months = (max_year - min_year) * 12LL + (max_month - min_month) + 1;
        int group_type = 0; // 0=months, 1=quarters, 2=years, 3=centuries

        if (total_months <= 20) {
            group_type = 0;
        } else if (total_months <= 80) {
            group_type = 1;
        } else if ((max_year - min_year + 1) <= 20) {
            group_type = 2;
        } else {
            group_type = 3;
        }

        // Aggregate groups
        DateGroup agg_groups[MAX_DATE_GROUPS];
        int agg_count = 0;
        memset(agg_groups, 0, sizeof(agg_groups));

        for (int m = 0; m < monthly_group_count; m++)
        {
            int year  = monthly_groups[m].sort_key / 100;
            int month = monthly_groups[m].sort_key % 100;

            char agg_label[16] = {0};
            int agg_sort_key = 0;

            if (group_type == 0) {
                strcpy(agg_label, monthly_groups[m].label);
                agg_sort_key = monthly_groups[m].sort_key;
            } else if (group_type == 1) {
                int q = (month - 1) / 3 + 1;
                snprintf(agg_label, sizeof(agg_label), "%04d Q%d", year, q);
                agg_sort_key = year * 100 + q;
            } else if (group_type == 2) {
                snprintf(agg_label, sizeof(agg_label), "%04d", year);
                agg_sort_key = year;
            } else {
                int century = year / 100;
                snprintf(agg_label, sizeof(agg_label), "%dXX", century);
                agg_sort_key = century;
            }

            int found = 0;
            for (int g = 0; g < agg_count; g++)
            {
                if (agg_groups[g].sort_key == agg_sort_key)
                {
                    agg_groups[g].count += monthly_groups[m].count;
                    found = 1;
                    break;
                }
            }

            if (!found && agg_count < MAX_DATE_GROUPS)
            {
                strcpy(agg_groups[agg_count].label, agg_label);
                agg_groups[agg_count].count = monthly_groups[m].count;
                agg_groups[agg_count].sort_key = agg_sort_key;
                agg_count++;
            }
        }

        // Sort aggregated groups
        for (int i = 0; i < agg_count - 1; i++)
        {
            for (int j = i + 1; j < agg_count; j++)
            {
                if (agg_groups[i].sort_key > agg_groups[j].sort_key)
                {
                    DateGroup tmp = agg_groups[i];
                    agg_groups[i] = agg_groups[j];
                    agg_groups[j] = tmp;
                }
            }
        }

        // Render aggregated groups
        long max_bin = 0;
        for (int g = 0; g < agg_count; g++) {
            if (agg_groups[g].count > max_bin) max_bin = agg_groups[g].count;
        }
        if (max_bin == 0) max_bin = 1;

        int BAR_WIDTH = 35;
        char bar[BAR_WIDTH + 1];

        for (int g = 0; g < agg_count; g++)
        {
            int bar_len = (int)(BAR_WIDTH * (double)agg_groups[g].count / max_bin + 0.5);
            memset(bar, '#', bar_len);
            bar[bar_len] = '\0';

            mvwprintw(win, hist_y++, hist_x, "%-12s |%-*s %8ld",
                      agg_groups[g].label, BAR_WIDTH, bar, agg_groups[g].count);
        }

        const char *group_name = (group_type == 0) ? "by month" :
                                 (group_type == 1) ? "by quarter" :
                                 (group_type == 2) ? "by year" : "by century";
        mvwprintw(win, hist_y++, hist_x, "(%s)", group_name);
    }
    else
    {
        mvwprintw(win, hist_y++, hist_x, "(Not applicable for this column type)");
    }

    // Close hint
    if (col_types[col_idx] == COL_NUM && numeric_count > 0) {
        if (freq_count > 0)
            mvwprintw(win, STATS_H - 2, 2, "[f] Freq  [p] Percentiles  [e] Trends  [any] Close");
        else
            mvwprintw(win, STATS_H - 2, 2, "[p] Percentiles  [e] Trends  [any key] Close");
    } else if (freq_count > 0) {
        mvwprintw(win, STATS_H - 2, 2, "[f] Full frequency list   [any key] Close");
    } else {
        mvwprintw(win, STATS_H - 2, 2, "Press any key to close");
    }
    wrefresh(win);

    // Wait for keypress
    int stats_ch = wgetch(win);

    delwin(win);
    touchwin(stdscr);
    refresh();

    if ((stats_ch == 'f' || stats_ch == 'F') && freq_count > 0) {
        show_freq_list(freqs, freq_count, valid_count, col_name);
    } else if (stats_ch == 'p' && col_types[col_idx] == COL_NUM && numeric_count > 0) {
        show_percentile_breakdown(numeric_values, numeric_count, col_name);
    } else if (stats_ch == 'e' && col_types[col_idx] == COL_NUM && numeric_count > 0) {
        show_value_trends(numeric_values, numeric_count, col_name);
    }

    // ────────────────────────────────────────────────
    // Free memory
    // ────────────────────────────────────────────────

    free(numeric_values);

    for (long i = 0; i < freq_count; i++) {
        free(freqs[i].value);
    }
    free(freqs);
    free(freq_ht);
}

// ────────────────────────────────────────────────
// Correlation matrix   :corr
// ────────────────────────────────────────────────

/* Returns 1 and fills *out_x / *out_y if user pressed Enter on a cell.
   Returns 0 otherwise. */
int show_correlation_matrix(int *out_x_col, int *out_y_col)
{
    /* Collect numeric column indices */
    int ncols[MAX_COLS];
    int nc = 0;
    for (int c = 0; c < col_count && nc < MAX_COLS; c++) {
        if (!col_hidden[c] && col_types[c] == COL_NUM) ncols[nc++] = c;
    }
    if (nc < 2) {
        /* Show a brief message and return */
        WINDOW *mw = newwin(5, 44, (LINES - 5) / 2, (COLS - 44) / 2);
        if (mw) {
            wbkgd(mw, COLOR_PAIR(1));
            wattron(mw, COLOR_PAIR(6)); box(mw, 0, 0); wattroff(mw, COLOR_PAIR(6));
            mvwprintw(mw, 2, 3, "Need at least 2 numeric columns.");
            mvwprintw(mw, 3, 3, "[any key] Close");
            wrefresh(mw); wgetch(mw); delwin(mw);
            touchwin(stdscr); refresh();
        }
        return 0;
    }
    if (nc > 16) nc = 16;  /* cap to keep display manageable */

    /* Collect column values: two-pass over rows to keep memory bounded.
       Pass 1: allocate arrays and fill. */
    int display_count = filter_active ? filtered_count : row_count;
    long nrows = display_count;

    /* Allocate nc arrays of doubles */
    double **col_vals = calloc(nc, sizeof(double*));
    long    *col_ns   = calloc(nc, sizeof(long));
    if (!col_vals || !col_ns) { free(col_vals); free(col_ns); return 0; }
    for (int ci = 0; ci < nc; ci++) {
        col_vals[ci] = malloc(nrows * sizeof(double));
        if (!col_vals[ci]) {
            for (int k = 0; k < ci; k++) free(col_vals[k]);
            free(col_vals); free(col_ns); return 0;
        }
    }

    /* Show progress message for large files */
    if (nrows > 50000) {
        move(LINES - 1, 0);
        attron(COLOR_PAIR(5));
        printw(" Computing correlations... ");
        attroff(COLOR_PAIR(5));
        refresh();
    }

    for (long i = 0; i < nrows; i++) {
        int real_row = get_real_row((int)i);
        if (real_row < 0 || real_row >= row_count) continue;

        char mmap_buf[MAX_LINE_LEN];
        if (!rows[real_row].line_cache) {
            if (g_mmap_base) {
                char *ml = csv_mmap_get_line(rows[real_row].offset, mmap_buf, sizeof(mmap_buf));
                rows[real_row].line_cache = strdup(ml ? ml : "");
            } else {
                fseek(f, rows[real_row].offset, SEEK_SET);
                char *line = malloc(MAX_LINE_LEN + 1);
                if (fgets(line, MAX_LINE_LEN + 1, f)) {
                    line[strcspn(line, "\n")] = '\0';
                    rows[real_row].line_cache = line;
                } else { free(line); rows[real_row].line_cache = strdup(""); }
            }
        }

        for (int ci = 0; ci < nc; ci++) {
            char *cell = get_column_value(rows[real_row].line_cache,
                                          column_names[ncols[ci]] ? column_names[ncols[ci]] : "",
                                          use_headers);
            if (cell && *cell) {
                char *ep;
                double v = parse_double(cell, &ep);
                if (ep != cell && *ep == '\0') {
                    col_vals[ci][col_ns[ci]++] = v;
                }
            }
            free(cell);
        }
    }

    /* Compute means */
    double *means = calloc(nc, sizeof(double));
    if (!means) goto corr_cleanup_nomatrix;
    for (int ci = 0; ci < nc; ci++) {
        for (long k = 0; k < col_ns[ci]; k++) means[ci] += col_vals[ci][k];
        if (col_ns[ci] > 0) means[ci] /= col_ns[ci];
    }

    /* Compute Pearson r matrix (nc x nc).  For pairs use pairwise-complete: */
    double *rmat = calloc(nc * nc, sizeof(double));
    if (!rmat) { free(means); goto corr_cleanup_nomatrix; }

    for (int a = 0; a < nc; a++) {
        rmat[a * nc + a] = 1.0;  /* diagonal */
        for (int b = a + 1; b < nc; b++) {
            /* Only use rows where BOTH columns have values.
               Simplification: use the shorter array length (same rows visited in order). */
            long nn = col_ns[a] < col_ns[b] ? col_ns[a] : col_ns[b];
            if (nn < 2) { rmat[a*nc+b] = rmat[b*nc+a] = 0.0; continue; }
            double sxy = 0, sx2 = 0, sy2 = 0;
            for (long k = 0; k < nn; k++) {
                double dx = col_vals[a][k] - means[a];
                double dy = col_vals[b][k] - means[b];
                sxy += dx * dy;
                sx2 += dx * dx;
                sy2 += dy * dy;
            }
            double denom = sqrt(sx2 * sy2);
            rmat[a*nc+b] = rmat[b*nc+a] = (denom > 0) ? sxy / denom : 0.0;
        }
    }

    /* ── Interactive display ── */
    {
        int ret = 0;
        int cur_a = 0, cur_b = (nc > 1) ? 1 : 0;

        /* Column label width: col name truncated to 8 chars */
        int CELL_W = 7;   /* 6 digits + sign: "-0.9999" */
        int LBL_W  = 9;   /* label column on left */
        int pw = 2 + LBL_W + nc * (CELL_W + 1) + 2;
        if (pw < 72) pw = 72;   /* minimum to fit bottom hint */
        if (pw > COLS) pw = COLS;
        int ph = nc + 11;       /* rows: border+title+blank+header+hline+nc+hline+legend+blank+hint+border */
        if (ph < 12) ph = 12;
        if (ph > LINES - 2) ph = LINES - 2;
        int sy2 = (LINES - ph) / 2;
        int sx2 = (COLS - pw) / 2;

        WINDOW *win = newwin(ph, pw, sy2, sx2);
        if (!win) { free(rmat); free(means); goto corr_cleanup_nomatrix; }
        wbkgd(win, COLOR_PAIR(1));
        keypad(win, TRUE);

        for (;;) {
            werase(win);
            wattron(win, COLOR_PAIR(6)); box(win, 0, 0); wattroff(win, COLOR_PAIR(6));
            wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, 0, (pw - 22) / 2, " Correlation Matrix ");
            wattroff(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, 1, 2, "Pearson r  (n cols: %d)", nc);

            /* Header row */
            int y = 3, x0 = 2 + LBL_W;
            wattron(win, A_BOLD);
            for (int b = 0; b < nc; b++) {
                char lbl[10] = "";
                if (use_headers && column_names[ncols[b]])
                    strncpy(lbl, column_names[ncols[b]], CELL_W);
                else
                    col_letter(ncols[b], lbl);
                lbl[CELL_W] = '\0';
                mvwprintw(win, y, x0 + b * (CELL_W + 1), "%*s", CELL_W, lbl);
            }
            wattroff(win, A_BOLD);
            y++;
            wattron(win, COLOR_PAIR(6)); mvwhline(win, y++, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));

            /* Data rows */
            for (int a = 0; a < nc; a++) {
                char rlbl[10] = "";
                if (use_headers && column_names[ncols[a]])
                    strncpy(rlbl, column_names[ncols[a]], LBL_W - 1);
                else
                    col_letter(ncols[a], rlbl);
                rlbl[LBL_W - 1] = '\0';

                wattron(win, A_BOLD);
                mvwprintw(win, y, 2, "%-*s", LBL_W - 1, rlbl);
                wattroff(win, A_BOLD);

                for (int b = 0; b < nc; b++) {
                    double r = rmat[a * nc + b];
                    int is_cur = (a == cur_a && b == cur_b);

                    /* Color by strength */
                    int pair;
                    if (a == b) {
                        pair = 6;  /* diagonal: gray/border */
                    } else if (r >= 0.7) {
                        pair = 3;  /* strong positive: accent */
                    } else if (r <= -0.7) {
                        pair = 5;  /* strong negative: warning/red */
                    } else if (r >= 0.3 || r <= -0.3) {
                        pair = 2;  /* moderate: selection color */
                    } else {
                        pair = 1;  /* weak: normal */
                    }

                    int attr = (is_cur ? A_REVERSE : 0);
                    wattron(win, COLOR_PAIR(pair) | attr);
                    if (a == b)
                        mvwprintw(win, y, x0 + b * (CELL_W + 1), "%*s", CELL_W, "  1.0  ");
                    else
                        mvwprintw(win, y, x0 + b * (CELL_W + 1), "%*.4f", CELL_W, r);
                    wattroff(win, COLOR_PAIR(pair) | attr);
                }
                y++;
            }

            wattron(win, COLOR_PAIR(6)); mvwhline(win, y++, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));

            /* Separator + legend — fixed rows so they never overlap the hint */
            wattron(win, COLOR_PAIR(6)); mvwhline(win, ph - 5, 1, ACS_HLINE, pw - 2); wattroff(win, COLOR_PAIR(6));
            {
                int ly = ph - 4;
                mvwhline(win, ly, 1, ' ', pw - 2);
                int lx = 2;
                wattron(win, COLOR_PAIR(3));
                mvwprintw(win, ly, lx, ">=0.7 strong+");
                wattroff(win, COLOR_PAIR(3));
                lx += 14;
                wattron(win, COLOR_PAIR(2));
                mvwprintw(win, ly, lx, "+/-0.3 moderate");
                wattroff(win, COLOR_PAIR(2));
                lx += 16;
                wattron(win, COLOR_PAIR(5));
                mvwprintw(win, ly, lx, "<=-0.7 strong-");
                wattroff(win, COLOR_PAIR(5));
            }

            /* Cursor info */
            char cn_a[16] = "", cn_b[16] = "";
            if (use_headers && column_names[ncols[cur_a]]) strncpy(cn_a, column_names[ncols[cur_a]], 15);
            else col_letter(ncols[cur_a], cn_a);
            if (use_headers && column_names[ncols[cur_b]]) strncpy(cn_b, column_names[ncols[cur_b]], 15);
            else col_letter(ncols[cur_b], cn_b);
            mvwprintw(win, ph - 2, 2,
                "r=%.4f  hjkl Navigate  Enter Scatter  q Close",
                rmat[cur_a * nc + cur_b]);
            mvwprintw(win, ph - 2, pw - 2 - (int)strlen(cn_a) - 1 - (int)strlen(cn_b) - 1,
                "%s/%s", cn_a, cn_b);

            wrefresh(win);

            int ch = wgetch(win);
            if (ch == 'q' || ch == 'Q' || ch == 27) { break; }
            else if (ch == KEY_UP   || ch == 'k') { if (cur_a > 0) cur_a--; }
            else if (ch == KEY_DOWN || ch == 'j') { if (cur_a < nc - 1) cur_a++; }
            else if (ch == KEY_LEFT || ch == 'h') { if (cur_b > 0) cur_b--; }
            else if (ch == KEY_RIGHT|| ch == 'l') { if (cur_b < nc - 1) cur_b++; }
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (cur_a != cur_b) {
                    *out_x_col = ncols[cur_b];
                    *out_y_col = ncols[cur_a];
                    ret = 1;
                    break;
                }
            }
        }

        delwin(win);
        touchwin(stdscr);
        refresh();
        free(rmat);
        free(means);

        for (int ci = 0; ci < nc; ci++) free(col_vals[ci]);
        free(col_vals);
        free(col_ns);
        return ret;
    }

corr_cleanup_nomatrix:
    for (int ci = 0; ci < nc; ci++) free(col_vals[ci]);
    free(col_vals);
    free(col_ns);
    return 0;
}

// ────────────────────────────────────────────────
// Outlier report   :outliers
// ────────────────────────────────────────────────

typedef struct {
    int    col_idx;
    long   display_row;   /* display index (not real row) */
    double value;
    double zscore;
    char   col_name[32];
} Outlier;

int show_outlier_report(double threshold)
{
    int jump_row = -1;
    if (threshold <= 0) threshold = 3.0;

    /* Collect numeric columns */
    int ncols[MAX_COLS];
    int nc = 0;
    for (int c = 0; c < col_count && nc < MAX_COLS; c++) {
        if (!col_hidden[c] && col_types[c] == COL_NUM) ncols[nc++] = c;
    }
    if (nc == 0) {
        WINDOW *mw = newwin(5, 40, (LINES - 5) / 2, (COLS - 40) / 2);
        if (mw) {
            wbkgd(mw, COLOR_PAIR(1));
            wattron(mw, COLOR_PAIR(6)); box(mw, 0, 0); wattroff(mw, COLOR_PAIR(6));
            mvwprintw(mw, 2, 3, "No numeric columns found.");
            mvwprintw(mw, 3, 3, "[any key] Close");
            wrefresh(mw); wgetch(mw); delwin(mw);
            touchwin(stdscr); refresh();
        }
        return -1;
    }

    int display_count = filter_active ? filtered_count : row_count;
    long nrows = display_count;

    /* Per-column: compute mean and variance in one pass */
    double *col_mean = NULL, *col_var = NULL, *col_std = NULL;
    long   *col_n    = NULL;
    col_mean = calloc(nc, sizeof(double));
    col_var  = calloc(nc, sizeof(double));
    col_n    = calloc(nc, sizeof(long));
    if (!col_mean || !col_var || !col_n) goto outlier_cleanup_early;

    /* Show progress for large files */
    if (nrows > 50000) {
        move(LINES - 1, 0);
        attron(COLOR_PAIR(5));
        printw(" Computing outliers... ");
        attroff(COLOR_PAIR(5));
        refresh();
    }

    for (long i = 0; i < nrows; i++) {
        int real_row = get_real_row((int)i);
        if (real_row < 0 || real_row >= row_count) continue;

        char mmap_buf[MAX_LINE_LEN];
        if (!rows[real_row].line_cache) {
            if (g_mmap_base) {
                char *ml = csv_mmap_get_line(rows[real_row].offset, mmap_buf, sizeof(mmap_buf));
                rows[real_row].line_cache = strdup(ml ? ml : "");
            } else {
                fseek(f, rows[real_row].offset, SEEK_SET);
                char *line = malloc(MAX_LINE_LEN + 1);
                if (fgets(line, MAX_LINE_LEN + 1, f)) {
                    line[strcspn(line, "\n")] = '\0';
                    rows[real_row].line_cache = line;
                } else { free(line); rows[real_row].line_cache = strdup(""); }
            }
        }

        for (int ci = 0; ci < nc; ci++) {
            char *cell = get_column_value(rows[real_row].line_cache,
                                          column_names[ncols[ci]] ? column_names[ncols[ci]] : "",
                                          use_headers);
            if (cell && *cell) {
                char *ep;
                double v = parse_double(cell, &ep);
                if (ep != cell && *ep == '\0') {
                    col_n[ci]++;
                    double delta = v - col_mean[ci];
                    col_mean[ci] += delta / col_n[ci];
                    col_var[ci] += delta * (v - col_mean[ci]); /* Welford */
                }
            }
            free(cell);
        }
    }

    /* Compute std dev */
    col_std = calloc(nc, sizeof(double));
    if (!col_std) goto outlier_cleanup_early;
    for (int ci = 0; ci < nc; ci++) {
        col_std[ci] = (col_n[ci] > 1) ? sqrt(col_var[ci] / (col_n[ci] - 1)) : 0.0;
    }

    /* Second pass: collect outliers */
    long outlier_cap = 10000;
    Outlier *outliers = malloc(outlier_cap * sizeof(Outlier));
    long outlier_count = 0;
    if (!outliers) goto outlier_cleanup_early;

    for (long i = 0; i < nrows && outlier_count < outlier_cap; i++) {
        int real_row = get_real_row((int)i);
        if (real_row < 0 || real_row >= row_count) continue;
        if (!rows[real_row].line_cache) continue;

        for (int ci = 0; ci < nc; ci++) {
            if (col_std[ci] == 0) continue;
            char *cell = get_column_value(rows[real_row].line_cache,
                                          column_names[ncols[ci]] ? column_names[ncols[ci]] : "",
                                          use_headers);
            if (cell && *cell) {
                char *ep;
                double v = parse_double(cell, &ep);
                if (ep != cell && *ep == '\0') {
                    double z = (v - col_mean[ci]) / col_std[ci];
                    if (z < 0) z = -z;
                    if (z >= threshold) {
                        Outlier *o = &outliers[outlier_count++];
                        o->col_idx    = ncols[ci];
                        o->display_row = i;
                        o->value      = v;
                        o->zscore     = z;
                        if (use_headers && column_names[ncols[ci]])
                            strncpy(o->col_name, column_names[ncols[ci]], 31);
                        else
                            col_letter(ncols[ci], o->col_name);
                        o->col_name[31] = '\0';
                        if (outlier_count >= outlier_cap) { free(cell); break; }
                    }
                }
            }
            free(cell);
        }
    }

    if (outlier_count == 0) {
        WINDOW *mw = newwin(5, 52, (LINES - 5) / 2, (COLS - 52) / 2);
        if (mw) {
            wbkgd(mw, COLOR_PAIR(1));
            wattron(mw, COLOR_PAIR(6)); box(mw, 0, 0); wattroff(mw, COLOR_PAIR(6));
            mvwprintw(mw, 2, 3, "No outliers found (threshold: %.1f sigma).", threshold);
            mvwprintw(mw, 3, 3, "[any key] Close");
            wrefresh(mw); wgetch(mw); delwin(mw);
            touchwin(stdscr); refresh();
        }
        free(outliers); free(col_std); col_std = NULL;
        goto outlier_cleanup_early;
    }

    /* ── Interactive list ── */
    {
        WINDOW *win = newwin(LINES, COLS, 0, 0);
        if (!win) { free(outliers); free(col_std); col_std = NULL; goto outlier_cleanup_early; }
        wbkgd(win, COLOR_PAIR(1));
        keypad(win, TRUE);

        int data_y0 = 6, data_y1 = LINES - 3;
        int page_sz = data_y1 - data_y0;
        if (page_sz < 1) page_sz = 1;

        int val_w = 16, z_w = 8;
        long cur = 0, top = 0;

        for (;;) {
            if (cur >= outlier_count) cur = outlier_count - 1;
            if (cur < 0) cur = 0;
            if (cur < top) top = cur;
            if (cur >= top + page_sz) top = cur - page_sz + 1;

            werase(win);
            wattron(win, COLOR_PAIR(6)); box(win, 0, 0); wattroff(win, COLOR_PAIR(6));
            wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, 1, 2, "Outlier Report  (threshold: %.1f σ, found: %ld)", threshold, outlier_count);
            wattroff(win, COLOR_PAIR(3) | A_BOLD);

            wattron(win, COLOR_PAIR(6)); mvwhline(win, 3, 1, ACS_HLINE, COLS - 2); wattroff(win, COLOR_PAIR(6));
            wattron(win, A_BOLD);
            mvwprintw(win, 4, 2, "%-8s %-12s %-12s %*s %*s",
                      "DispRow", "Col", "ColName",
                      val_w, "Value", z_w, "Z-score");
            wattroff(win, A_BOLD);
            wattron(win, COLOR_PAIR(6)); mvwhline(win, 5, 1, ACS_HLINE, COLS - 2); wattroff(win, COLOR_PAIR(6));

            for (int r = 0; r < page_sz; r++) {
                long idx = top + r;
                if (idx >= outlier_count) break;
                Outlier *o = &outliers[idx];
                int ry = data_y0 + r;
                mvwhline(win, ry, 1, ' ', COLS - 2);

                if (idx == cur) wattron(win, COLOR_PAIR(2));

                /* Z-score color: > 5 = strong warning */
                char z_str[16];
                snprintf(z_str, sizeof(z_str), "%.2f", o->zscore);

                mvwprintw(win, ry, 2, "%-8ld %-12d %-12s %*.4g",
                          o->display_row + 1,
                          o->col_idx + 1,
                          o->col_name,
                          val_w, o->value);
                /* Z-score with color */
                int zx = 2 + 8 + 1 + 12 + 1 + 12 + 1 + val_w + 1;
                if (idx != cur) {
                    int zpair = (o->zscore >= 5.0) ? 5 : 3;
                    wattron(win, COLOR_PAIR(zpair));
                }
                mvwprintw(win, ry, zx, "%*s", z_w, z_str);
                if (idx != cur) wattroff(win, COLOR_PAIR((o->zscore >= 5.0) ? 5 : 3));

                if (idx == cur) wattroff(win, COLOR_PAIR(2));
            }

            wattron(win, COLOR_PAIR(6)); mvwhline(win, data_y1, 1, ACS_HLINE, COLS - 2); wattroff(win, COLOR_PAIR(6));
            mvwprintw(win, data_y1 + 1, 2,
                "Row %ld/%ld  |  j/k Navigate  |  [Enter] Jump to row  |  [f] Filter by column  |  [q] Close",
                cur + 1, outlier_count);

            wrefresh(win);
            int ch = wgetch(win);

            if (ch == 'q' || ch == 'Q' || ch == 27) {
                break;
            } else if (ch == KEY_UP   || ch == 'k') { cur--; }
            else if (ch == KEY_DOWN || ch == 'j') { cur++; }
            else if (ch == KEY_PPAGE) { cur -= page_sz; }
            else if (ch == KEY_NPAGE) { cur += page_sz; }
            else if (ch == KEY_HOME || ch == 'g') { cur = 0; }
            else if (ch == KEY_END  || ch == 'G') { cur = outlier_count - 1; }
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                /* Return the display row to the caller so it can navigate */
                if (cur >= 0 && cur < outlier_count) {
                    long dr = outliers[cur].display_row;
                    jump_row = (int)(dr < INT_MAX ? dr : INT_MAX);
                }
                break;
            } else if (ch == 'f' || ch == 'F') {
                /* Filter by this column */
                if (cur >= 0 && cur < outlier_count) {
                    Outlier *o = &outliers[cur];
                    char val_str[64];
                    snprintf(val_str, sizeof(val_str), "%.10g", o->value);
                    if (strchr(o->col_name, ' '))
                        snprintf(filter_query, sizeof(filter_query),
                                 "`%s` = \"%s\"", o->col_name, val_str);
                    else
                        snprintf(filter_query, sizeof(filter_query),
                                 "%s = \"%s\"", o->col_name, val_str);
                    in_filter_mode  = 1;
                    cur_display_row = 0;
                    top_display_row = 0;
                    apply_filter(rows, f, row_count);
                }
                break;
            }
        }

        delwin(win);
        touchwin(stdscr);
        refresh();
        free(outliers);
        free(col_std);
        col_std = NULL;
    }

outlier_cleanup_early:
    free(col_mean);
    free(col_var);
    free(col_std);
    free(col_n);
    return jump_row;
}