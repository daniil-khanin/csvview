#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/stat.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <search.h>
#include <wchar.h>
#include <unistd.h>

#include "csvview_defs.h"
#include "utils.h"
#include "filtering.h"
#include "sorting.h"
#include "search.h"
#include "column_stats.h"
#include "column_format.h"
#include "ui_draw.h"
#include "table_edit.h"
#include "pivot.h"
#include "graph.h"
#include "graph_svg.h"
#include "help.h"
#include "concat_files.h"
#include "split_file.h"
#include "dedup.h"
#include "profile.h"
#include "bookmarks.h"
#include "csv_mmap.h"
#include "themes.h"
#include "splash.h"

// ────────────────────────────────────────────────
// Global variables — definitions (initialization)
// ────────────────────────────────────────────────

// 1. Filters and saved queries
char *saved_filters[MAX_SAVED_FILTERS] = {NULL};
int   saved_filter_count = 0;

// 2. Search
SearchResult search_results[MAX_SEARCH_RESULTS] = {{0}};
int          search_count    = 0;
int          search_index    = -1;
char         search_query[256] = "";
int          in_search_mode  = 0;

// 3. Filtering
int *filtered_rows           = NULL;
int  filtered_count          = 0;
char filter_query[256]       = "";
int  in_filter_mode          = 0;
int  filter_active           = 0;

// 4. Navigation and current table state
int  cur_display_row  = 0;     // cursor position in the visible list
int  top_display_row  = 0;     // first visible row
int  cur_real_row     = 0;     // real row number in the file
int  cur_col          = 0;
int  left_col         = 0;
char file_size_str[32] = "unknown";

// 5. Columns and metadata
char       *column_names[MAX_COLS] = {NULL};
ColType     col_types[MAX_COLS]    = {0};
ColumnFormat col_formats[MAX_COLS]  = {{0}};
int         col_count              = 0;
int         use_headers            = 0;
int         col_widths[MAX_COLS]   = {0};

// 6. Sorting
int *sorted_rows          = NULL;
int sorted_count          = 0;
int sort_col              = -1;     // -1 = no sorting
int sort_order            = 0;      // 1 = asc, -1 = desc, 0 = none
SortLevel sort_levels[MAX_SORT_LEVELS];
int       sort_level_count = 0;

// 7. File data
int       row_count = 0;
RowIndex *rows      = NULL;
FILE     *f         = NULL;

// 8. Graphs and visualization
int       in_graph_mode           = 0;
int       graph_col_list[10]      = {0};
int       graph_col_count         = 0;
int       graph_marked[MAX_COLS]  = {0};
int       graph_series_hidden[10] = {0}; /* 1 = series N is hidden (toggled by 1-9 in graph mode) */
int       graph_dual_yaxis        = 0;   /* 1 = dual Y axis: series 0 left, rest right */
int       graph_scatter_mode      = 0;   /* 1 = scatter plot active */
int       graph_scatter_x_col     = -1; /* X column for scatter plot */
int       current_graph           = 0;
int       graph_start             = 0;
int       graph_scroll_step       = 1;
int       using_date_x            = 0;
int       date_col                = -1;
int       date_x_col              = -1;
int       current_graph_color_pair = 5;

GraphType  graph_type             = GRAPH_LINE;
GraphScale graph_scale            = SCALE_LINEAR;

int       graph_cursor_pos        = 0;
int       graph_visible_points    = 0;
int       graph_anomaly_count     = 0;
double   *graph_anomalies         = NULL;
bool      show_anomalies          = false;
bool      show_graph_cursor       = false;
int       min_max_show            = 0;

// 9. State saving (for undo/temporary storage)
int   save_sort_col       = -1;
int   save_sort_order     = 0;
int   save_sort_level_count = 0;
int   save_sorted_count   = 0;
int  *save_sorted_rows    = NULL;
int   save_filtered_count = 0;
int  *save_filtered_rows  = NULL;

// 10. Field delimiter (default ',' — CSV)
char csv_delimiter = ',';
// Set to 1 when delimiter was specified explicitly (--sep= or known extension).
// Prevents .csvf from overriding it in preload_delimiter().
static int sep_explicit = 0;

// 11. Column freeze (first N always visible)
int freeze_cols = 0;
int col_hidden[MAX_COLS] = {0};
int relative_line_numbers = 0;

// 12a. Bookmarks (vim-style): 26 letters a-z, store display row (-1 = not set)
int bookmarks[26];

// 12. Drill-down from pivot: filter for returning to the main table
char pivot_drilldown_filter[512] = "";

// 13b. Comments: lines starting with '#' are collected during indexing if skip_comments=1
int   skip_comments                     = 0;
char *comment_lines[MAX_COMMENT_LINES]  = {NULL};
int   comment_count                     = 0;

// 13. Undo stack for cell editing
#define UNDO_MAX 20
typedef struct {
    int   real_row;
    char *old_line;
} UndoEntry;

static UndoEntry undo_stack[UNDO_MAX];
static int undo_top = 0;  // 0 = empty

static void undo_push(int real_row, const char *old_line)
{
    if (undo_top == UNDO_MAX) {
        // Stack is full — shift entries, removing the oldest one
        free(undo_stack[0].old_line);
        memmove(&undo_stack[0], &undo_stack[1], (UNDO_MAX - 1) * sizeof(UndoEntry));
        undo_top--;
    }
    undo_stack[undo_top].real_row = real_row;
    undo_stack[undo_top].old_line = strdup(old_line ? old_line : "");
    undo_top++;
}

static int undo_pop(int *real_row_out, char **old_line_out)
{
    if (undo_top == 0) return 0;
    undo_top--;
    *real_row_out = undo_stack[undo_top].real_row;
    *old_line_out = undo_stack[undo_top].old_line;
    undo_stack[undo_top].old_line = NULL;
    return 1;
}

// ────────────────────────────────────────────────
// Dynamic construction of file row index
// ────────────────────────────────────────────────

static RowIndex *build_row_index(FILE *fp, int *out_count)
{
    // Reset old comments
    for (int i = 0; i < comment_count; i++) { free(comment_lines[i]); comment_lines[i] = NULL; }
    comment_count = 0;

    size_t cap = 4096;
    RowIndex *r = malloc(cap * sizeof(RowIndex));
    if (!r) return NULL;

    int  cnt = 0;
    long pos = 0;
    char buf[MAX_LINE_LEN];
    int  in_preamble = 1;   // no data lines encountered yet

    while (fgets(buf, sizeof(buf), fp)) {
        long line_start = pos;
        pos += (long)strlen(buf);

        // Collect comment lines (starting with '#')
        if (skip_comments && buf[0] == '#') {
            buf[strcspn(buf, "\r\n")] = '\0';
            if (comment_count < MAX_COMMENT_LINES)
                comment_lines[comment_count++] = strdup(buf);
            continue;
        }

        // Skip blank lines in the preamble (between comments and data)
        if (skip_comments && in_preamble && buf[strspn(buf, " \t\r\n")] == '\0')
            continue;

        in_preamble = 0;

        if (cnt >= MAX_ROWS) break;
        if ((size_t)cnt >= cap) {
            cap *= 2;
            if (cap > (size_t)(MAX_ROWS + 1)) cap = (size_t)(MAX_ROWS + 1);
            RowIndex *tmp = realloc(r, cap * sizeof(RowIndex));
            if (!tmp) { free(r); return NULL; }
            r = tmp;
        }
        r[cnt].offset     = line_start;
        r[cnt].line_cache = NULL;
        cnt++;
    }

    // +1 spare: table_edit.c accesses rows[row_count] when adding rows
    RowIndex *tmp = realloc(r, ((size_t)cnt + 1) * sizeof(RowIndex));
    if (tmp) r = tmp;

    *out_count = cnt;
    rewind(fp);
    return r;
}

// ────────────────────────────────────────────────
// Dynamic allocation of filter/sort arrays
// ────────────────────────────────────────────────

static int alloc_row_arrays(int count)
{
    free(filtered_rows);
    free(sorted_rows);
    filtered_rows = calloc((size_t)count, sizeof(int));
    sorted_rows   = calloc((size_t)count, sizeof(int));
    if (!filtered_rows || !sorted_rows) {
        free(filtered_rows); filtered_rows = NULL;
        free(sorted_rows);   sorted_rows   = NULL;
        return 0;
    }
    return 1;
}

// ────────────────────────────────────────────────
// Rebuild column names from rows[0] (called on reload)
// ────────────────────────────────────────────────
static void reparse_column_names(void)
{
    for (int i = 0; i < col_count; i++) { free(column_names[i]); column_names[i] = NULL; }
    col_count = 0;

    if (!rows[0].line_cache) {
        fseek(f, rows[0].offset, SEEK_SET);
        char line_buf[MAX_LINE_LEN];
        if (fgets(line_buf, sizeof(line_buf), f)) {
            line_buf[strcspn(line_buf, "\r\n")] = '\0';
            char *end = line_buf + strlen(line_buf) - 1;
            while (end >= line_buf &&
                   (*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0))
                *end-- = '\0';
            rows[0].line_cache = strdup(line_buf);
        } else {
            rows[0].line_cache = strdup("");
        }
    }

    int hdr_count = 0;
    char **hdr_fields = parse_csv_line(rows[0].line_cache, &hdr_count);
    if (hdr_fields) {
        while (col_count < hdr_count && col_count < MAX_COLS) {
            char *t = hdr_fields[col_count];
            while (*t == ' ' || *t == '\t' || (unsigned char)*t == 0xA0) t++;
            char *end = t + strlen(t) - 1;
            while (end >= t &&
                   (*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0))
                *end-- = '\0';
            column_names[col_count] = strdup(t);
            if (!column_names[col_count]) column_names[col_count] = strdup("");
            col_types[col_count] = COL_STR;
            col_count++;
        }
        free_csv_fields(hdr_fields, hdr_count);
    }
    init_column_formats();
}

// ────────────────────────────────────────────────
// File history (~/.csvview_history)
// ────────────────────────────────────────────────
#define HISTORY_FILE  "/.csvview_history"
#define HISTORY_MAX   20

typedef struct {
    char   path[4096];
    time_t last_open;   /* 0 = unknown (old format) */
    long   row_count;   /* 0 = unknown */
} HistEntry;

/* Parse a history line.
   New format: "timestamp\trow_count\tpath"
   Old format: plain path */
static void history_parse_line(const char *line, HistEntry *e)
{
    memset(e, 0, sizeof(*e));
    char *end;
    long ts = strtol(line, &end, 10);
    if (end != line && *end == '\t') {
        e->last_open = (time_t)ts;
        const char *p2 = end + 1;
        long rc = strtol(p2, &end, 10);
        if (end != p2 && *end == '\t') {
            e->row_count = rc;
            strncpy(e->path, end + 1, sizeof(e->path) - 1);
            return;
        }
    }
    /* Old format: plain path */
    strncpy(e->path, line, sizeof(e->path) - 1);
}

static void history_write_entry(FILE *fh, const HistEntry *e)
{
    fprintf(fh, "%ld\t%ld\t%s\n", (long)e->last_open, e->row_count, e->path);
}

static void history_add(const char *path, long row_count)
{
    char hist_path[512];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(hist_path, sizeof(hist_path), "%s%s", home, HISTORY_FILE);

    char real[4096];
    if (!realpath(path, real)) return;

    /* Read existing entries, skipping duplicate of new path */
    HistEntry entries[HISTORY_MAX];
    int count = 0;
    FILE *fh = fopen(hist_path, "r");
    if (fh) {
        char line[4096 + 64];
        while (count < HISTORY_MAX && fgets(line, sizeof(line), fh)) {
            int len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            if (len == 0) continue;
            HistEntry e;
            history_parse_line(line, &e);
            if (strcmp(e.path, real) != 0)
                entries[count++] = e;
        }
        fclose(fh);
    }

    /* Write: new entry first, then old entries */
    fh = fopen(hist_path, "w");
    if (!fh) return;
    HistEntry cur;
    memset(&cur, 0, sizeof(cur));
    strncpy(cur.path, real, sizeof(cur.path) - 1);
    cur.last_open = time(NULL);
    cur.row_count = row_count;
    history_write_entry(fh, &cur);
    int written = 1;
    for (int i = 0; i < count && written < HISTORY_MAX; i++, written++)
        history_write_entry(fh, &entries[i]);
    fclose(fh);
}

/* Format file size into buf */
static void hist_fmt_size(off_t sz, char *buf, int size)
{
    if (sz < 0) { snprintf(buf, size, "—"); return; }
    if (sz < 1024LL * 1024)
        snprintf(buf, size, "%.0f KB", sz / 1024.0);
    else if (sz < 1024LL * 1024 * 1024)
        snprintf(buf, size, "%.1f MB", sz / (1024.0 * 1024.0));
    else
        snprintf(buf, size, "%.2f GB", sz / (1024.0 * 1024.0 * 1024.0));
}

/* Format a long integer with thousand separators */
static void hist_fmt_number(long n, char *buf, int size)
{
    if (n <= 0) { snprintf(buf, size, "—"); return; }
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%ld", n);
    int len = strlen(tmp);
    int out = 0;
    for (int i = 0; i < len && out < size - 2; i++) {
        if (i > 0 && (len - i) % 3 == 0) buf[out++] = ',';
        buf[out++] = tmp[i];
    }
    buf[out] = '\0';
}

/* ─── Preview helpers for history picker ─────── */
#define PREV_ROWS  40
#define PREV_LINE  8192
#define PREV_COLS  60

typedef struct {
    char path[4096];
    char lines[PREV_ROWS][PREV_LINE];
    int  row_count;
    char delim;
    int  col_count;
    int  col_widths[PREV_COLS];
    int  ok;           /* 1 = loaded (even if empty/unreadable) */
} PrevCache;

static char prev_detect_delim(const char *line, const char *fpath)
{
    const char *ext = strrchr(fpath, '.');
    if (ext) {
        if (strcasecmp(ext, ".tsv") == 0) return '\t';
        if (strcasecmp(ext, ".psv") == 0) return '|';
    }
    int t = 0, pip = 0, com = 0, sem = 0;
    for (const char *q = line; *q; q++) {
        if      (*q == '\t') t++;
        else if (*q == '|')  pip++;
        else if (*q == ',')  com++;
        else if (*q == ';')  sem++;
    }
    if (t   > 0 && t   >= pip && t   >= com && t   >= sem) return '\t';
    if (pip > 0 && pip >= com && pip >= sem)                return '|';
    if (sem > 0 && sem >= com)                              return ';';
    return ',';
}

static void prev_load(PrevCache *pc, const char *path)
{
    if (pc->ok && strcmp(pc->path, path) == 0) return;
    memset(pc, 0, sizeof(*pc));
    strncpy(pc->path, path, sizeof(pc->path) - 1);

    FILE *pf = fopen(path, "r");
    if (!pf) { pc->ok = 1; return; }

    int n = 0;
    while (n < PREV_ROWS && fgets(pc->lines[n], PREV_LINE, pf)) {
        int len = strlen(pc->lines[n]);
        while (len > 0 && (pc->lines[n][len-1] == '\n' || pc->lines[n][len-1] == '\r'))
            pc->lines[n][--len] = '\0';
        if (pc->lines[n][0] == '#') continue;  /* skip comment lines */
        n++;
    }
    fclose(pf);
    pc->row_count = n;
    if (n == 0) { pc->ok = 1; return; }

    pc->delim = prev_detect_delim(pc->lines[0], path);

    /* Compute per-column max widths by scanning all loaded rows */
    for (int r = 0; r < n; r++) {
        const char *p = pc->lines[r];
        int c = 0;
        while (*p && c < PREV_COLS) {
            int w = 0;
            if (*p == '"') {
                p++;
                while (*p) {
                    if (*p == '"' && *(p+1) == '"') { w++; p += 2; }
                    else if (*p == '"')             { p++; break; }
                    else                            { w++; p++; }
                }
                if (*p == pc->delim) p++;
            } else {
                while (*p && *p != pc->delim) { w++; p++; }
                if (*p == pc->delim) p++;
            }
            if (w > pc->col_widths[c]) pc->col_widths[c] = w;
            c++;
        }
        if (c > pc->col_count) pc->col_count = c;
    }
    for (int c = 0; c < pc->col_count; c++) {
        if (pc->col_widths[c] > 25) pc->col_widths[c] = 25;
        if (pc->col_widths[c] < 1)  pc->col_widths[c] = 1;
    }
    pc->ok = 1;
}

/* Draw one preview row at screen y, parsing fields from line on the fly.
   is_hdr=1 → bold+COLOR_PAIR(5), data → COLOR_PAIR(1). */
static void prev_draw_row(int y, const char *line, char delim,
                           const int *widths, int vis_cols, int is_hdr, int max_x)
{
    int x = 1;
    const char *p = line;
    for (int c = 0; c < vis_cols && x < max_x; c++) {
        /* Extract field into tmp (capped at 128 chars) */
        char tmp[128];
        int tn = 0;
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && *(p+1) == '"') { if (tn < 127) tmp[tn++] = '"'; p += 2; }
                else if (*p == '"')             { p++; break; }
                else                            { if (tn < 127) tmp[tn++] = *p++; else p++; }
            }
            if (*p == delim) p++;
        } else {
            while (*p && *p != delim) { if (tn < 127) tmp[tn++] = *p; p++; }
            if (*p == delim) p++;
        }
        tmp[tn] = '\0';

        int w = widths[c];
        if (x + w > max_x) w = max_x - x;
        if (w <= 0) break;

        if (is_hdr) attron(A_BOLD | COLOR_PAIR(5));
        else        attron(COLOR_PAIR(1));
        mvprintw(y, x, "%-*.*s", w, w, tmp);
        if (is_hdr) attroff(A_BOLD | COLOR_PAIR(5));
        else        attroff(COLOR_PAIR(1));
        x += w;

        /* Column separator */
        if (c < vis_cols - 1 && x + 3 <= max_x) {
            attron(COLOR_PAIR(6));
            mvaddch(y, x,   ' ');
            mvaddch(y, x+1, ACS_VLINE);
            mvaddch(y, x+2, ' ');
            attroff(COLOR_PAIR(6));
            x += 3;
        }
        if (!*p) break;
    }
}

/* ────────────────────────────────────────────── */

/* Returns malloc'd path selected by user, or NULL if cancelled.
   Runs its own ncurses session (caller hasn't called initscr yet). */
static char *show_history_picker(void)
{
    char hist_path[512];
    const char *home = getenv("HOME");
    if (!home) return NULL;
    snprintf(hist_path, sizeof(hist_path), "%s%s", home, HISTORY_FILE);

    /* Read history */
    HistEntry entries[HISTORY_MAX];
    int count = 0;
    FILE *fh = fopen(hist_path, "r");
    if (fh) {
        char line[4096 + 64];
        while (count < HISTORY_MAX && fgets(line, sizeof(line), fh)) {
            int len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            if (len > 0) {
                history_parse_line(line, &entries[count]);
                count++;
            }
        }
        fclose(fh);
    }

    if (count == 0) return NULL;

    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        theme_apply(current_theme);
        bkgd(COLOR_PAIR(1));
    }

    /* Pre-compute formatted strings (avoid stat() on every keypress) */
    char date_strs[HISTORY_MAX][20];
    char size_strs[HISTORY_MAX][16];
    char rows_strs[HISTORY_MAX][16];
    for (int i = 0; i < count; i++) {
        if (entries[i].last_open > 0) {
            struct tm *tm = localtime(&entries[i].last_open);
            strftime(date_strs[i], sizeof(date_strs[i]), "%Y-%m-%d %H:%M", tm);
        } else {
            strcpy(date_strs[i], "\xe2\x80\x94");  /* — */
        }
        struct stat st;
        if (stat(entries[i].path, &st) == 0)
            hist_fmt_size(st.st_size, size_strs[i], sizeof(size_strs[i]));
        else
            strcpy(size_strs[i], "\xe2\x80\x94");
        hist_fmt_number(entries[i].row_count, rows_strs[i], sizeof(rows_strs[i]));
    }

    /* Column layout: 2  [date 16]  2  [size 8]  2  [rows 10]  2  [path...] */
    const int date_x = 2;
    const int size_x = date_x + 16 + 2;
    const int rows_x = size_x +  8 + 2;
    const int name_x = rows_x + 10 + 2;

    int sel = 0, top_idx = 0, prev_sel = -1;
    static PrevCache pc;
    memset(&pc, 0, sizeof(pc));
    char *result = NULL;
    char err_msg[512] = "";

    while (1) {
        /* Layout: recalculate each frame to handle potential resize */
        int split     = LINES / 2;          /* row of preview top border */
        int list_rows = split - 2;          /* rows 2..(split-1) for file list */
        if (list_rows < 1) list_rows = 1;
        int prev_top  = split + 1;          /* first preview content row */
        int prev_bot  = LINES - 2;          /* bottom border row */
        int prev_rows = prev_bot - prev_top; /* number of content rows */
        if (prev_rows < 0) prev_rows = 0;

        /* Scroll list to keep sel in view */
        if (sel < top_idx) top_idx = sel;
        if (sel >= top_idx + list_rows) top_idx = sel - list_rows + 1;

        /* Load preview for selected file (cached — only reads when sel changes) */
        if (sel != prev_sel) {
            prev_load(&pc, entries[sel].path);
            prev_sel = sel;
        }

        clear();

        /* ── Title bar ── */
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(0, 2, "Recent files  (↑↓/jk — select | Enter — open | d — delete | q/Esc — quit)");
        char counter[16];
        snprintf(counter, sizeof(counter), "[%d/%d]", sel + 1, count);
        mvprintw(0, COLS - (int)strlen(counter) - 2, "%s", counter);
        attroff(COLOR_PAIR(5) | A_BOLD);

        /* ── Column headers ── */
        attron(COLOR_PAIR(6));
        mvprintw(1, date_x, "%-16s", "Last opened");
        mvprintw(1, size_x, "%8s",   "Size");
        mvprintw(1, rows_x, "%10s",  "Rows");
        if (name_x < COLS - 4) mvprintw(1, name_x, "File");
        attroff(COLOR_PAIR(6));

        /* ── File list (scrollable) ── */
        for (int i = 0; i < list_rows && (top_idx + i) < count; i++) {
            int idx = top_idx + i;
            int row = 2 + i;
            int name_w = COLS - name_x - 1;
            if (name_w < 4) name_w = 4;
            const char *name = entries[idx].path;
            int nlen = strlen(name);
            int trunc = (nlen > name_w);
            if (trunc) name = name + nlen - (name_w - 3);

            if (idx == sel) {
                attron(COLOR_PAIR(2) | A_BOLD);
                mvhline(row, 0, ' ', COLS);
                mvprintw(row, date_x, "%-16s", date_strs[idx]);
                mvprintw(row, size_x, "%8s",   size_strs[idx]);
                mvprintw(row, rows_x, "%10s",  rows_strs[idx]);
                if (name_x < COLS - 1)
                    mvprintw(row, name_x, trunc ? "...%.*s" : "%.*s",
                             name_w - (trunc ? 3 : 0), name);
                attroff(COLOR_PAIR(2) | A_BOLD);
            } else {
                attron(COLOR_PAIR(1));
                mvprintw(row, date_x, "%-16s", date_strs[idx]);
                attroff(COLOR_PAIR(1));
                attron(COLOR_PAIR(6));
                mvprintw(row, size_x, "%8s",   size_strs[idx]);
                mvprintw(row, rows_x, "%10s",  rows_strs[idx]);
                attroff(COLOR_PAIR(6));
                attron(COLOR_PAIR(1));
                if (name_x < COLS - 1)
                    mvprintw(row, name_x, trunc ? "...%.*s" : "%.*s",
                             name_w - (trunc ? 3 : 0), name);
                attroff(COLOR_PAIR(1));
            }
        }

        /* Scroll hints */
        if (top_idx > 0) {
            attron(COLOR_PAIR(3));
            mvprintw(2, COLS - 11, " \xe2\x86\x91 %d more", top_idx);
            attroff(COLOR_PAIR(3));
        }
        if (top_idx + list_rows < count) {
            attron(COLOR_PAIR(3));
            mvprintw(split - 1, COLS - 11, " \xe2\x86\x93 %d more", count - top_idx - list_rows);
            attroff(COLOR_PAIR(3));
        }

        /* ── Preview top border ── */
        attron(COLOR_PAIR(6));
        mvhline(split, 0, ACS_HLINE, COLS);
        const char *bname = strrchr(entries[sel].path, '/');
        bname = bname ? bname + 1 : entries[sel].path;
        char plabel[80];
        snprintf(plabel, sizeof(plabel), " Preview: %.*s ", 56, bname);
        mvprintw(split, 2, "%s", plabel);
        attroff(COLOR_PAIR(6));

        /* ── Preview content ── */
        if (prev_rows > 0) {
            if (!pc.ok || pc.row_count == 0) {
                attron(COLOR_PAIR(6));
                mvprintw(prev_top, 2, access(entries[sel].path, R_OK) == 0
                                      ? "(empty file)" : "(cannot read file)");
                attroff(COLOR_PAIR(6));
            } else {
                /* Determine how many columns fit horizontally */
                int vis_cols = 0, total_w = 1;
                for (int c = 0; c < pc.col_count; c++) {
                    int add = pc.col_widths[c] + (c < pc.col_count - 1 ? 3 : 0);
                    if (total_w + add > COLS - 1) break;
                    total_w += add;
                    vis_cols++;
                }
                if (vis_cols == 0 && pc.col_count > 0) vis_cols = 1;

                int shown = pc.row_count < prev_rows ? pc.row_count : prev_rows;
                for (int r = 0; r < shown; r++) {
                    int y = prev_top + r;
                    /* Header row background */
                    if (r == 0) {
                        attron(COLOR_PAIR(5));
                        mvhline(y, 0, ' ', COLS);
                        attroff(COLOR_PAIR(5));
                    }
                    prev_draw_row(y, pc.lines[r], pc.delim,
                                  pc.col_widths, vis_cols, r == 0, COLS - 1);
                }
            }
        }

        /* ── Preview bottom border ── */
        attron(COLOR_PAIR(6));
        mvhline(prev_bot, 0, ACS_HLINE, COLS);
        attroff(COLOR_PAIR(6));

        /* ── Status / hint bar ── */
        if (err_msg[0]) {
            attron(COLOR_PAIR(11) | A_BOLD);
            mvprintw(LINES - 1, 2, "%s", err_msg);
            attroff(COLOR_PAIR(11) | A_BOLD);
        } else {
            attron(COLOR_PAIR(6));
            mvprintw(LINES - 1, 2, "No history? Pass a filename directly: csvview <file.csv>");
            attroff(COLOR_PAIR(6));
        }

        refresh();

        int ch = getch();
        err_msg[0] = '\0';

        if      (ch == KEY_UP   || ch == 'k') { if (sel > 0) sel--; }
        else if (ch == KEY_DOWN || ch == 'j') { if (sel < count - 1) sel++; }
        else if (ch == KEY_HOME)              { sel = 0; }
        else if (ch == KEY_END)               { sel = count - 1; }
        else if (ch == '\n' || ch == KEY_ENTER || ch == '\r') {
            if (access(entries[sel].path, F_OK) != 0) {
                snprintf(err_msg, sizeof(err_msg),
                         "File not found: %s  (removed from history)", entries[sel].path);
                for (int i = sel; i < count - 1; i++) {
                    entries[i] = entries[i + 1];
                    memcpy(date_strs[i], date_strs[i+1], sizeof(date_strs[i]));
                    memcpy(size_strs[i], size_strs[i+1], sizeof(size_strs[i]));
                    memcpy(rows_strs[i], rows_strs[i+1], sizeof(rows_strs[i]));
                }
                count--;
                if (count == 0) break;
                if (sel >= count) sel = count - 1;
                prev_sel = -1;  /* force preview reload */
                FILE *fw = fopen(hist_path, "w");
                if (fw) {
                    for (int i = 0; i < count; i++) history_write_entry(fw, &entries[i]);
                    fclose(fw);
                }
            } else {
                result = strdup(entries[sel].path);
                break;
            }
        }
        else if (ch == 'd') {
            for (int i = sel; i < count - 1; i++) {
                entries[i] = entries[i + 1];
                memcpy(date_strs[i], date_strs[i+1], sizeof(date_strs[i]));
                memcpy(size_strs[i], size_strs[i+1], sizeof(size_strs[i]));
                memcpy(rows_strs[i], rows_strs[i+1], sizeof(rows_strs[i]));
            }
            count--;
            if (count == 0) break;
            if (sel >= count) sel = count - 1;
            prev_sel = -1;
            FILE *fw = fopen(hist_path, "w");
            if (fw) {
                for (int i = 0; i < count; i++) history_write_entry(fw, &entries[i]);
                fclose(fw);
            }
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q') {
            break;
        }
    }

    endwin();
    return result;
}

// ────────────────────────────────────────────────
// Column-name autocomplete input
// ────────────────────────────────────────────────

/* Return full column name that starts with word[0..wlen-1], or NULL */
static const char *ac_match(const char *word, int wlen)
{
    if (wlen <= 0) return NULL;
    for (int i = 0; i < col_count; i++) {
        if (column_names[i] && (int)strlen(column_names[i]) > wlen &&
            strncasecmp(column_names[i], word, wlen) == 0)
            return column_names[i];
    }
    return NULL;
}

/* Read a line with ghost-text column-name autocomplete.
 * Caller draws the label; y,x is where the input itself starts.
 * Returns 1 on Enter (buf filled), 0 on Esc (buf cleared). */
/* border_x > 0: redraw ACS_VLINE at that column after clearing (preserves box frame) */
static int ac_readline(char *buf, int maxlen, int y, int x, int border_x)
{
    int pos = 0;
    buf[0] = '\0';
    curs_set(1);
    noecho();

    while (1) {
        /* Find start of current token (scan back over word chars) */
        int ts = pos;
        while (ts > 0 && (isalnum((unsigned char)buf[ts-1]) || buf[ts-1] == '_'))
            ts--;

        const char *full = (pos > ts) ? ac_match(buf + ts, pos - ts) : NULL;
        const char *ghost = full ? full + (pos - ts) : NULL; /* suffix to display */

        /* Redraw: input + ghost */
        move(y, x);
        addstr(buf);
        if (ghost) {
            attron(A_DIM);
            addstr(ghost);
            attroff(A_DIM);
        }
        clrtoeol();
        if (border_x > 0) {
            attron(COLOR_PAIR(6));
            mvaddch(y, border_x, ACS_VLINE);
            attroff(COLOR_PAIR(6));
        }
        move(y, x + pos);
        refresh();

        int key = getch();

        if (key == '\n' || key == KEY_ENTER) {
            break;
        } else if (key == 27) {
            buf[0] = '\0';
            curs_set(0);
            return 0;
        } else if (key == '\t') {
            if (full) {
                /* Replace token with canonical column name */
                int full_len = (int)strlen(full);
                int after_len = (int)strlen(buf + pos);
                if (ts + full_len + after_len < maxlen - 1) {
                    memmove(buf + ts + full_len, buf + pos, after_len + 1);
                    memcpy(buf + ts, full, full_len);
                    pos = ts + full_len;
                }
            }
        } else if ((key == KEY_BACKSPACE || key == 127) && pos > 0) {
            buf[--pos] = '\0';
        } else if (key >= 32 && key <= 126 && pos < maxlen - 1) {
            buf[pos++] = (char)key;
            buf[pos] = '\0';
        }
    }

    curs_set(0);
    return 1;
}

// ────────────────────────────────────────────────
// :dedup progress callback (TUI)
// ────────────────────────────────────────────────
static int         g_dedup_height   = 0;
static const char *g_dedup_filename = NULL;

static void g_dedup_progress_cb(long done, long total, int pass, void *ud)
{
    (void)ud;
    if (done == 0 || total <= 0) return;
    draw_status_bar(g_dedup_height - 1, 1, g_dedup_filename, row_count, file_size_str);
    attron(COLOR_PAIR(3));
    int pct = (int)(100LL * done / total);
    if (pass == 1)
        printw(" | Dedup pass 1/2... %3d%%", pct);
    else
        printw(" | Dedup...          %3d%%", pct);
    attroff(COLOR_PAIR(3));
    refresh();
}

// ────────────────────────────────────────────────
// Bookmarks helpers
// ────────────────────────────────────────────────

/* Return display index for the given real row, or -1 if not visible.
 * Works for filtered, sorted, and plain views. */
static int find_display_for_real(int real_row, int display_count)
{
    for (int d = 0; d < display_count; d++) {
        if (get_real_row(d) == real_row) return d;
    }
    return -1;
}

/* Scroll view so that target display row is visible. */
static void bookmark_scroll(int target, int *cur, int *top, int visible)
{
    *cur = target;
    if (*cur < *top)
        *top = *cur;
    else if (*cur >= *top + visible)
        *top = *cur - visible + 1;
    if (*top < 0) *top = 0;
}

// ────────────────────────────────────────────────
// main
// ────────────────────────────────────────────────
static const char *program_path = NULL;

int main(int argc, char *argv[]) {
    program_path = argv[0];
    for (int i = 0; i < 26; i++) bookmarks[i] = -1;
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "/h") == 0)) {
        show_help(0);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        show_version(argv[0]);
        return 0;
    }

    /* Load saved theme first; CLI --theme= overrides it below */
    theme_load_config();

    int concat_mode  = 0;
    int split_mode   = 0;
    int split_drop   = 0;
    int dedup_mode   = 0;
    int profile_mode = 0;
    int dedup_keep_last = 0;
    char *dedup_by      = NULL;
    char *concat_column = NULL;
    char *split_by      = NULL;
    char *output_dir    = NULL;
    char *user_output   = NULL;
    char **input_files  = malloc(argc * sizeof(char*));
    int input_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cat") == 0) {
            concat_mode = 1;
        } else if (strcmp(argv[i], "--split") == 0) {
            split_mode = 1;
        } else if (strcmp(argv[i], "--profile") == 0) {
            profile_mode = 1;
        } else if (strcmp(argv[i], "--dedup") == 0) {
            dedup_mode = 1;
        } else if (strcmp(argv[i], "--keep=last") == 0) {
            dedup_keep_last = 1;
        } else if (strcmp(argv[i], "--keep=first") == 0) {
            dedup_keep_last = 0;
        } else if (strcmp(argv[i], "--drop-col") == 0) {
            split_drop = 1;
        } else if (strncmp(argv[i], "--by=", 5) == 0) {
            split_by   = argv[i] + 5;
            dedup_by   = argv[i] + 5;
        } else if (strncmp(argv[i], "--output-dir=", 13) == 0) {
            output_dir = argv[i] + 13;
        } else if (strncmp(argv[i], "--column=", 9) == 0) {
            concat_column = argv[i] + 9;
        } else if (strncmp(argv[i], "--output=", 9) == 0) {
            user_output = argv[i] + 9;
        } else if (strncmp(argv[i], "--sep=", 6) == 0) {
            const char *sep = argv[i] + 6;
            if (strcmp(sep, "\\t") == 0 || strcmp(sep, "tab") == 0)
                csv_delimiter = '\t';
            else if (strcmp(sep, "pipe") == 0)
                csv_delimiter = '|';
            else if (*sep)
                csv_delimiter = *sep;
            sep_explicit = 1;
        } else if (strncmp(argv[i], "--theme=", 8) == 0) {
            const Theme *t = theme_by_name(argv[i] + 8);
            if (t) current_theme = t;
        } else if (argv[i][0] != '-') {
            input_files[input_count++] = argv[i];
        }
    }

    char *file_to_open = NULL;

    if (split_mode) {
        if (input_count == 0) {
            fprintf(stderr, "Error: --split requires an input file\n");
            fprintf(stderr, "Usage: csvview --split --by=<column> [--output-dir=<dir>] file.csv\n");
            free(input_files);
            return 1;
        }
        if (!split_by) {
            fprintf(stderr, "Error: --split requires --by=<column>\n");
            fprintf(stderr, "Usage: csvview --split --by=<column> [--output-dir=<dir>] file.csv\n");
            free(input_files);
            return 1;
        }
        int ret = split_file(input_files[0], split_by, output_dir, split_drop);
        free(input_files);
        return ret;
    }

    if (profile_mode) {
        if (input_count == 0) {
            fprintf(stderr, "Error: --profile requires an input file\n");
            fprintf(stderr, "Usage: csvview --profile [--sep=CHAR] file.csv\n");
            free(input_files);
            return 1;
        }
        const char *ext = strrchr(input_files[0], '.');
        if (ext) {
            if (strcmp(ext, ".tsv") == 0) csv_delimiter = '\t';
            else if (strcmp(ext, ".psv") == 0) csv_delimiter = '|';
        }
        int ret = profile_file(input_files[0], csv_delimiter);
        free(input_files);
        return ret;
    }

    if (dedup_mode) {
        if (input_count == 0) {
            fprintf(stderr, "Error: --dedup requires an input file\n");
            fprintf(stderr, "Usage: csvview --dedup [--by=col1,col2] [--keep=last] [--output=file.csv] input.csv\n");
            free(input_files);
            return 1;
        }
        /* auto-detect delimiter from extension if not set via --sep */
        const char *ext = strrchr(input_files[0], '.');
        if (ext) {
            if (strcmp(ext, ".tsv") == 0) csv_delimiter = '\t';
            else if (strcmp(ext, ".psv") == 0) csv_delimiter = '|';
        }
        int ret = dedup_file(input_files[0], dedup_by, dedup_keep_last,
                             user_output, csv_delimiter);
        free(input_files);
        return ret;
    }

    if (concat_mode) {
        if (input_count == 0) {
            fprintf(stderr, "Error: --cat requires input files\n");
            free(input_files);
            return 1;
        }

        char *generated = NULL;
        int ret = concat_and_save_files(
            input_files, input_count,
            concat_column ? concat_column : "source_file",
            user_output,
            &generated
        );

        free(input_files);

        if (ret != 0) {
            fprintf(stderr, "Concat failed (code %d)\n", ret);
            free(generated);
            return 1;
        }

        file_to_open = generated;
        printf("Merged file: %s\n", file_to_open);
    } else {
        if (input_count == 0) {
            char *hist_pick = show_history_picker();
            if (!hist_pick) {
                free(input_files);
                fprintf(stderr, "Usage: %s <file.csv>\n", argv[0]);
                return 1;
            }
            file_to_open = hist_pick;
        } else {
            file_to_open = strdup(input_files[0]);
        }
        free(input_files);
    }

    // Auto-detect delimiter from file extension (if --sep not specified)
    if (csv_delimiter == ',') {
        const char *ext = strrchr(file_to_open, '.');
        if (ext) {
            if (strcasecmp(ext, ".tsv") == 0)       { csv_delimiter = '\t'; sep_explicit = 1; }
            else if (strcasecmp(ext, ".psv") == 0)  { csv_delimiter = '|';  sep_explicit = 1; }
            else if (strcasecmp(ext, ".ecsv") == 0) { csv_delimiter = ' ';  sep_explicit = 1; }
        }
    }

    f = fopen(file_to_open, "r");
    if (!f) { perror("fopen"); return 1; }
    csv_mmap_open(file_to_open);

    struct stat st;
    if (fstat(fileno(f), &st) == 0) {
        long long size = st.st_size;
        if (size < 1024LL * 1024) snprintf(file_size_str, sizeof(file_size_str), "%.0f KB", size / 1024.0);
        else if (size < 1024LL * 1024 * 1024) snprintf(file_size_str, sizeof(file_size_str), "%.1f MB", size / (1024.0 * 1024.0));
        else snprintf(file_size_str, sizeof(file_size_str), "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }

    // Pre-load delimiter and skip_comments from .csvf before building row index.
    // Save delimiter first — if it was set explicitly (--sep or known extension),
    // do not let the .csvf file override it.
    char sep_before_preload = csv_delimiter;
    int skip_comments_explicit = preload_delimiter(file_to_open);
    if (sep_explicit) csv_delimiter = sep_before_preload;

    // Auto-detect comment lines if not explicitly set in .csvf
    if (!skip_comments_explicit && !skip_comments) {
        fseek(f, 0, SEEK_SET);
        char peek[512];
        while (fgets(peek, sizeof(peek), f)) {
            if (peek[0] == '#') { skip_comments = 1; break; }
            if (peek[strspn(peek, " \t\r\n")] != '\0') break; /* first real data line */
        }
        fseek(f, 0, SEEK_SET);
    }

    rows = build_row_index(f, &row_count);
    if (!rows) { perror("malloc"); return 1; }
    if (!alloc_row_arrays(row_count)) { perror("malloc"); return 1; }
    history_add(file_to_open, row_count);

    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        theme_apply(current_theme);
        bkgd(COLOR_PAIR(1));
    }

    if (col_count == 0) {  // guard against double-parsing
        if (!rows[0].line_cache) {
            fseek(f, rows[0].offset, SEEK_SET);
            char line_buf[MAX_LINE_LEN];
            if (fgets(line_buf, sizeof(line_buf), f)) {
                // Strip ALL possible line endings and garbage
                line_buf[strcspn(line_buf, "\r\n")] = '\0';
                // Additionally strip trailing spaces (including non-breaking space)
                char *end = line_buf + strlen(line_buf) - 1;
                while (end >= line_buf &&
                       (*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0)) {
                    *end-- = '\0';
                }
                rows[0].line_cache = strdup(line_buf);
            } else {
                rows[0].line_cache = strdup("");
            }
        }

        col_count = 0;
        int hdr_count = 0;
        char **hdr_fields = parse_csv_line(rows[0].line_cache, &hdr_count);

        if (hdr_fields) {
            while (col_count < hdr_count && col_count < MAX_COLS) {
                char *t = hdr_fields[col_count];
                // Strip leading spaces/tabs/non-breaking spaces
                while (*t == ' ' || *t == '\t' || (unsigned char)*t == 0xA0) t++;
                // Strip trailing spaces/tabs/non-breaking spaces
                char *end = t + strlen(t) - 1;
                while (end >= t &&
                       (*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0))
                    *end-- = '\0';

                column_names[col_count] = strdup(t);
                if (!column_names[col_count])
                    column_names[col_count] = strdup("(memory error)");
                col_types[col_count] = COL_STR;
                col_count++;
            }
            free_csv_fields(hdr_fields, hdr_count);
        }

        // Initialize formats after col_count is determined
        init_column_formats();
    }

    int settings_loaded = load_column_settings(file_to_open);

    if (settings_loaded == 0) {
        // No settings file — first run, show setup wizard.
        // When comments are skipped (e.g. ECSV), the first non-comment line
        // is always the column header row.
        if (skip_comments) use_headers = 1;
        auto_detect_column_types();
        if (show_column_setup(file_to_open)) {
            endwin();
            return 0;
        }
    } else if (settings_loaded == 2) {
        // Partial load: col_count changed due to skip_comments.
        // Global settings applied, but col_count was different.
        // First line after comments is the header.
        if (skip_comments) use_headers = 1;
        auto_detect_column_types();
        save_column_settings(file_to_open);
    }

    char cfg_path[1024];
    snprintf(cfg_path, sizeof(cfg_path), "%s.csvf", file_to_open);
    load_saved_filters(cfg_path);

    int height, width;
    getmaxyx(stdscr, height, width);

    int cur_display_row = 0;
    int top_display_row = 0;
    int cur_real_row = use_headers ? 1 : 0;
    int cur_col = 0;
    int left_col = freeze_cols;

    // Move cursor to the first visible column
    while (cur_col < col_count - 1 && col_hidden[cur_col]) cur_col++;
    while (left_col < col_count && col_hidden[left_col]) left_col++;

    char current_cell_content[256] = "(empty)";
    char col_name[256];

    while (1) {
        clear();

        int table_top = 3;
        int table_height = height - table_top - 1;
        int table_width = width - 4;
        int visible_rows = table_height - 3;
        // Compute width of frozen columns (hidden ones not counted)
        int frozen_px = 0;
        for (int fc = 0; fc < freeze_cols && fc < col_count; fc++)
            if (!col_hidden[fc]) frozen_px += col_widths[fc] + 2;
        if (freeze_cols > 0 && freeze_cols < col_count) frozen_px += 1; // separator

        // visible_cols = number of scrollable (non-frozen) columns
        int scrollable_area = table_width - ROW_NUMBER_WIDTH - 2 - frozen_px;
        int visible_cols = (scrollable_area > 0) ? (scrollable_area / CELL_WIDTH) : 0;
        int max_sc = col_count - freeze_cols;
        if (max_sc < 0) max_sc = 0;
        if (visible_cols > max_sc) visible_cols = max_sc;
        if (visible_cols < 0) visible_cols = 0;

        int display_count = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));

        draw_menu(0, 1, width, 1);
        draw_table_border(table_top, table_height, width);
        draw_table_headers(table_top, ROW_DATA_OFFSET, visible_cols, left_col, cur_col);

        if (in_graph_mode) {
            if (graph_col_count > 1) {
                // Multi-series: compute Y scale(s)
                // In dual-yaxis mode: first visible -> left axis, rest -> right axis
                // In normal mode: all series share one scale
                double gmin = INFINITY, gmax = -INFINITY;
                double rmin = INFINITY, rmax = -INFINITY;
                int vis_idx = 0; // visible series index (0 = first visible)
                for (int s = 0; s < graph_col_count; s++) {
                    if (graph_series_hidden[s]) continue;
                    int pc = 0; bool agg = false; char dfmt[32];
                    double *vals = extract_plot_values(graph_col_list[s], rows, f, row_count, &pc, &agg, dfmt);
                    if (!vals) { vis_idx++; continue; }
                    int zs = (graph_zoom_start > 0) ? graph_zoom_start : 0;
                    int ze = (graph_zoom_end > 0 && graph_zoom_end <= pc) ? graph_zoom_end : pc;
                    if (zs >= ze) { zs = 0; ze = pc; }
                    int is_right = (graph_dual_yaxis && vis_idx > 0) ? 1 : 0;
                    for (int i = zs; i < ze; i++) {
                        if (is_right) {
                            if (vals[i] < rmin) rmin = vals[i];
                            if (vals[i] > rmax) rmax = vals[i];
                        } else {
                            if (vals[i] < gmin) gmin = vals[i];
                            if (vals[i] > gmax) gmax = vals[i];
                        }
                        // in non-dual mode also fill gmin/gmax for all
                        if (!graph_dual_yaxis) {
                            // already handled above
                        }
                    }
                    if (!graph_dual_yaxis) {
                        // no-op, gmin/gmax already updated
                    }
                    free(vals);
                    vis_idx++;
                }
                if (isinf(gmin) || isinf(gmax)) { gmin = 0; gmax = 1; }
                if (gmin == gmax) { gmax += 1.0; gmin -= 1.0; }
                graph_global_min = gmin;
                graph_global_max = gmax;
                // Right axis
                if (graph_dual_yaxis && !isinf(rmin) && !isinf(rmax)) {
                    if (rmin == rmax) { rmax += 1.0; rmin -= 1.0; }
                    graph_right_min = rmin;
                    graph_right_max = rmax;
                } else {
                    graph_right_min = NAN;
                    graph_right_max = NAN;
                }
                graph_right_axis_drawn = 0;
                // Note: when min_max_show != 0, each series jumps to its OWN min/max
                // independently — we collect per-series (Y, X) pairs below for the tooltip
                double ms_cursor_ys[10];
                char   ms_cursor_xs[10][64];
                int    ms_series_idx[10];  /* which graph_col_list index */
                int    ms_cursor_n = 0;
                memset(ms_cursor_xs, 0, sizeof(ms_cursor_xs));
                int    first_visible = 1;
                int    vis_draw = 0;
                for (int s = 0; s < graph_col_count; s++) {
                    if (graph_series_hidden[s]) continue;
                    current_graph_color_pair = GRAPH_COLOR_BASE + (s % 7);
                    graph_overlay_mode = first_visible ? 1 : 2;
                    graph_use_right_axis = (graph_dual_yaxis && vis_draw > 0) ? 1 : 0;
                    first_visible = 0;
                    vis_draw++;
                    graph_draw_cursor_overlay = show_graph_cursor ? 1 : 0;
                    graph_last_cursor_y = NAN;
                    draw_graph(graph_col_list[s], height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                    if (!isnan(graph_last_cursor_y) && ms_cursor_n < 10) {
                        ms_series_idx[ms_cursor_n] = s;
                        ms_cursor_ys[ms_cursor_n] = graph_last_cursor_y;
                        strncpy(ms_cursor_xs[ms_cursor_n], graph_last_cursor_x, 63);
                        ms_cursor_n++;
                    }
                }
                graph_overlay_mode = 0;
                graph_draw_cursor_overlay = 0;
                graph_use_right_axis = 0;
                graph_global_min = NAN;
                graph_global_max = NAN;
                graph_right_min = NAN;
                graph_right_max = NAN;
                // Multi-series tooltip (top line, cursor mode only)
                // min/max mode: labels are drawn near each @ marker by draw_graph itself
                if (show_graph_cursor && ms_cursor_n > 0 && min_max_show == 0) {
                    int tx = ROW_DATA_OFFSET + 2;
                    attron(COLOR_PAIR(1));
                    mvprintw(3, tx, "X: %s  ", ms_cursor_xs[0]);
                    attroff(COLOR_PAIR(1));
                    tx += (int)strlen(ms_cursor_xs[0]) + 6;
                    for (int i = 0; i < ms_cursor_n; i++) {
                        int s = ms_series_idx[i];
                        int cp = GRAPH_COLOR_BASE + (s % 7);
                        attron(COLOR_PAIR(cp) | A_BOLD);
                        mvprintw(3, tx, "%d:%.4g  ", s + 1, ms_cursor_ys[i]);
                        attroff(COLOR_PAIR(cp) | A_BOLD);
                        tx += 14;
                    }
                }
                // Draw color legend (dim if hidden, key hint [N] to toggle)
                int lx = ROW_DATA_OFFSET + 2;
                for (int s = 0; s < graph_col_count; s++) {
                    int cp = GRAPH_COLOR_BASE + (s % 7);
                    char cn[20] = "";
                    if (use_headers && column_names[graph_col_list[s]])
                        snprintf(cn, sizeof(cn), "%.14s", column_names[graph_col_list[s]]);
                    else
                        col_letter(graph_col_list[s], cn);
                    if (graph_series_hidden[s]) {
                        attron(A_DIM);
                        mvprintw(height - 3, lx, "[%d]-%s", s + 1, cn);
                        attroff(A_DIM);
                    } else {
                        attron(COLOR_PAIR(cp) | A_BOLD);
                        mvprintw(height - 3, lx, "[%d]-%s", s + 1, cn);
                        attroff(COLOR_PAIR(cp) | A_BOLD);
                    }
                    lx += (int)strlen(cn) + 6;
                }
            } else if (graph_scatter_mode && graph_scatter_x_col >= 0) {
                // Scatter plot: single or multi-Y against the same X column
                if (graph_col_count > 1) {
                    // Multi-series: shared Y scale, each Y in own color
                    double gmin = INFINITY, gmax = -INFINITY;
                    for (int s = 0; s < graph_col_count; s++) {
                        if (graph_series_hidden[s]) continue;
                        int pc = 0; bool agg = false; char dfmt[32];
                        double *vals = extract_plot_values(graph_col_list[s], rows, f, row_count, &pc, &agg, dfmt);
                        if (!vals) continue;
                        for (int i = 0; i < pc; i++) {
                            if (vals[i] < gmin) gmin = vals[i];
                            if (vals[i] > gmax) gmax = vals[i];
                        }
                        free(vals);
                    }
                    if (isinf(gmin) || isinf(gmax)) { gmin = 0; gmax = 1; }
                    if (gmin == gmax) { gmax += 1; gmin -= 1; }
                    graph_global_min = gmin; graph_global_max = gmax;
                    graph_right_min = NAN; graph_right_max = NAN;
                    graph_right_axis_drawn = 0;
                    int first_vis = 1;
                    for (int s = 0; s < graph_col_count; s++) {
                        if (graph_series_hidden[s]) continue;
                        current_graph_color_pair = GRAPH_COLOR_BASE + (s % 7);
                        graph_overlay_mode = first_vis ? 1 : 2;
                        graph_use_right_axis = 0;
                        first_vis = 0;
                        draw_scatter(graph_scatter_x_col, graph_col_list[s],
                                     height, width, rows, f, row_count, graph_cursor_pos);
                    }
                    graph_overlay_mode = 0;
                    graph_use_right_axis = 0;
                    graph_global_min = NAN; graph_global_max = NAN;
                    // Legend
                    int lx = ROW_DATA_OFFSET + 2;
                    for (int s = 0; s < graph_col_count; s++) {
                        int cp = GRAPH_COLOR_BASE + (s % 7);
                        char cn[20] = "";
                        if (use_headers && column_names[graph_col_list[s]])
                            snprintf(cn, sizeof(cn), "%.14s", column_names[graph_col_list[s]]);
                        else col_letter(graph_col_list[s], cn);
                        if (graph_series_hidden[s]) attron(A_DIM);
                        else attron(COLOR_PAIR(cp) | A_BOLD);
                        mvprintw(height - 3, lx, "[%d]-%s", s + 1, cn);
                        if (graph_series_hidden[s]) attroff(A_DIM);
                        else attroff(COLOR_PAIR(cp) | A_BOLD);
                        lx += (int)strlen(cn) + 6;
                    }
                } else {
                    current_graph_color_pair = GRAPH_COLOR_BASE;
                    draw_scatter(graph_scatter_x_col, graph_col_list[current_graph],
                                 height, width, rows, f, row_count, graph_cursor_pos);
                }
            } else {
                int col = graph_col_list[current_graph];
                draw_graph(col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
            }
        } else {
            draw_table_body(table_top, ROW_DATA_OFFSET, visible_rows, top_display_row, cur_display_row, cur_col, left_col, visible_cols, rows, f, row_count);
        }

        current_cell_content[0] = '\0';
        if (cur_real_row < row_count) {
            if (!rows[cur_real_row].line_cache) {
                fseek(f, rows[cur_real_row].offset, SEEK_SET);
                char line_buf[MAX_LINE_LEN];
                if (fgets(line_buf, sizeof(line_buf), f)) {
                    line_buf[strcspn(line_buf, "\r\n")] = '\0';
                    rows[cur_real_row].line_cache = strdup(line_buf);
                } else {
                    rows[cur_real_row].line_cache = strdup("");
                }
            }

            // Call the helper function here
            char *cell_content = get_cell_content(rows[cur_real_row].line_cache, cur_col);

            // Guard against overly long content
            if (strlen(cell_content) > 200) {
                strcpy(current_cell_content, "(very long text)");
            } else {
                strncpy(current_cell_content, cell_content, sizeof(current_cell_content)-1);
                current_cell_content[sizeof(current_cell_content)-1] = '\0';
            }

            free(cell_content);
        }
        if (use_headers && column_names[cur_col]) {
            strncpy(col_name, column_names[cur_col], sizeof(col_name) - 1);
            col_name[sizeof(col_name) - 1] = '\0';
        } else {
            col_letter(cur_col, col_name);
        }


        draw_cell_view(2, col_name, cur_display_row + 1, current_cell_content, width);
        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);

        if (in_graph_mode) {
            int col = graph_col_list[current_graph];
            char col_buf[16];
            if (column_names[col]) {
                strncpy(col_buf, column_names[col], sizeof(col_buf) - 1);
                col_buf[sizeof(col_buf) - 1] = '\0';
            } else {
                col_letter(col, col_buf);
            }

            clrtoeol();
        } else if (search_count > 0) {
            attron(COLOR_PAIR(3));
            printw(" | Found: %d/%d", search_index + 1, search_count);
            attroff(COLOR_PAIR(3));
        } else if (in_filter_mode) {
            attron(COLOR_PAIR(3));
            printw(" | Filtered: %d", filtered_count);
            attroff(COLOR_PAIR(3));
        }

        /* Bookmark indicator: show if current row has a bookmark */
        {
            int cur_real_bm = get_real_row(cur_display_row);
            for (int bi = 0; bi < 26; bi++) {
                if (bookmarks[bi] == cur_real_bm) {
                    attron(COLOR_PAIR(3));
                    printw(" [bm:%c]", 'a' + bi);
                    attroff(COLOR_PAIR(3));
                    break;
                }
            }
        }

        refresh();

        int ch = getch();

        if (in_graph_mode) {
            if (ch == 'q' || ch == 27) {  // Esc
                in_graph_mode = 0;
                graph_scatter_mode = 0;
                graph_scatter_x_col = -1;
                if (using_date_x) {
                    sort_col = save_sort_col; sort_level_count = save_sort_level_count;
                    sort_order = save_sort_order;
                    if (filter_active) {
                        memcpy(filtered_rows, save_filtered_rows, sizeof(int) * save_filtered_count);
                        filtered_count = save_filtered_count;
                        free(save_filtered_rows);
                        save_filtered_rows = NULL;
                    } else {
                        memcpy(sorted_rows, save_sorted_rows, sizeof(int) * save_sorted_count);
                        sorted_count = save_sorted_count;
                        free(save_sorted_rows);
                        save_sorted_rows = NULL;
                    }
                }
                graph_col_count = 0;
                continue;
            } else if (ch == KEY_LEFT || ch == 'h') {
                min_max_show = 0;
                if (graph_cursor_pos > 0) {
                    graph_cursor_pos--;
                } else {
                    // Cursor at left edge — try to scroll the window left
                    int total = graph_total_points;
                    int cur_s = graph_zoom_start;
                    int cur_e = (graph_zoom_end > 0 && graph_zoom_end <= total) ? graph_zoom_end : total;
                    if (cur_s > 0) {
                        int vp = graph_visible_points > 0 ? graph_visible_points : 1;
                        int scroll = (cur_e - cur_s + vp - 1) / vp;
                        if (scroll < 1) scroll = 1;
                        cur_s -= scroll;
                        cur_e -= scroll;
                        if (cur_s < 0) { cur_e -= cur_s; cur_s = 0; }
                        graph_zoom_start = cur_s;
                        graph_zoom_end   = (cur_e >= total) ? -1 : cur_e;
                    }
                    // cursor_pos stays at 0 (left edge)
                }
            } else if (ch == KEY_RIGHT || ch == 'l') {
                min_max_show = 0;
                if (graph_cursor_pos < graph_visible_points - 1) {
                    graph_cursor_pos++;
                } else {
                    // Cursor at right edge — try to scroll the window right
                    int total = graph_total_points;
                    int cur_s = graph_zoom_start;
                    int cur_e = (graph_zoom_end > 0 && graph_zoom_end <= total) ? graph_zoom_end : total;
                    if (cur_e < total) {
                        int vp = graph_visible_points > 0 ? graph_visible_points : 1;
                        int scroll = (cur_e - cur_s + vp - 1) / vp;
                        if (scroll < 1) scroll = 1;
                        cur_s += scroll;
                        cur_e += scroll;
                        if (cur_e > total) { cur_s -= (cur_e - total); cur_e = total; }
                        if (cur_s < 0) cur_s = 0;
                        graph_zoom_start = cur_s;
                        graph_zoom_end   = (cur_e >= total) ? -1 : cur_e;
                    }
                    // cursor_pos stays at graph_visible_points - 1 (right edge)
                }
            } else if (ch == 'r') {
                // Redraw (no-op, loop will redraw)
            } else if (ch == ':') {  // Enter command mode
                char cmd_buf[128] = {0};

                attron(COLOR_PAIR(3));
                printw(" | :");
                attroff(COLOR_PAIR(3));
                refresh();
                { int cy, cx; getyx(stdscr, cy, cx); ac_readline(cmd_buf, sizeof(cmd_buf), cy, cx, 0); }

                // Split command and argument (if any)
                char *cmd = cmd_buf;
                char *arg = strchr(cmd_buf, ' ');
                if (arg) {
                    *arg = '\0';
                    arg++;
                    // strip leading spaces from argument
                    while (*arg == ' ') arg++;
                }

                if (strcmp(cmd, "gx") == 0) {
                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :gx <column_name_or_letter> | :gx off");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    char arg_copy[128];
                    strncpy(arg_copy, arg, sizeof(arg_copy) - 1);
                    arg_copy[sizeof(arg_copy) - 1] = '\0';

                    // Strip quotes if present
                    if (arg_copy[0] == '"' && arg_copy[strlen(arg_copy)-1] == '"') {
                        memmove(arg_copy, arg_copy + 1, strlen(arg_copy) - 2);
                        arg_copy[strlen(arg_copy) - 2] = '\0';
                    }

                    // Check for "off"
                    if (strcasecmp(arg_copy, "off") == 0) {
                        date_x_col = -1;
                        using_date_x = 0;

                        // Restore old sort if it was saved
                        if (save_sort_col != -999) {  // -999 as "not saved" marker
                            sort_col = save_sort_col; sort_level_count = save_sort_level_count;
                            sort_order = save_sort_order;

                            if (filter_active) {
                                // Restore filtered indices (if needed)
                                if (save_filtered_rows) {
                                    memcpy(filtered_rows, save_filtered_rows, sizeof(int) * save_filtered_count);
                                    filtered_count = save_filtered_count;
                                }
                            } else {
                                if (save_sorted_rows) {
                                    memcpy(sorted_rows, save_sorted_rows, sizeof(int) * save_sorted_count);
                                    sorted_count = save_sorted_count;
                                }
                            }
                        }

                        // Redraw the graph if we are in graph mode
                        if (in_graph_mode) {
                            clear();
                            int col = graph_col_list[current_graph];
                            draw_graph(col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                            refresh();
                        }
                        continue;
                    }

                    // Regular column selection
                    int selected_col = -1;

                    // By letter (A, B, AA...)
                    if (strlen(arg_copy) <= 3 && isupper(arg_copy[0])) {
                        selected_col = col_to_num(arg_copy);
                    }
                    // By column name
                    else {
                        for (int c = 0; c < col_count; c++) {
                            if (column_names[c] && strcasecmp(column_names[c], arg_copy) == 0) {
                                selected_col = c;
                                break;
                            }
                        }
                    }

                    if (selected_col < 0 || selected_col >= col_count || col_types[selected_col] != COL_DATE) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(1));
                        printw(" | Invalid or non-date column: %s", arg_copy);
                        attroff(COLOR_PAIR(1));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    // Save current sort (only if not already saved)
                    if (save_sort_col == -999) {  // "not saved" marker
                        save_sort_col = sort_col; save_sort_level_count = sort_level_count;
                        save_sort_order = sort_order;
                        if (filter_active) {
                            save_filtered_count = filtered_count;
                            save_filtered_rows = malloc(sizeof(int) * filtered_count);
                            if (save_filtered_rows) {
                                memcpy(save_filtered_rows, filtered_rows, sizeof(int) * filtered_count);
                            }
                        } else {
                            save_sorted_count = sorted_count;
                            save_sorted_rows = malloc(sizeof(int) * sorted_count);
                            if (save_sorted_rows) {
                                memcpy(save_sorted_rows, sorted_rows, sizeof(int) * sorted_count);
                            }
                        }
                    }

                    // Sort the table by the selected date column (ascending)
                    sort_col = selected_col;
                    sort_order = 1;

                    if (filter_active) {
                        qsort(filtered_rows, filtered_count, sizeof(int), compare_rows_by_column);
                    } else {
                        build_sorted_index();
                        sorted_count = row_count - (use_headers ? 1 : 0);
                    }

                    // Restore old sort_col / sort_order (but indices are already sorted by date!)
                    sort_col = save_sort_col; sort_level_count = save_sort_level_count;
                    sort_order = save_sort_order;

                    // Set the new X axis
                    date_x_col = selected_col;
                    using_date_x = 1;

                    // If already in graph mode — redraw immediately
                    if (in_graph_mode) {
                        clear();
                        int col = graph_col_list[current_graph];
                        draw_graph(col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                        refresh();
                    }

                    // Success message
                    char col_name[64];
                    if (column_names[selected_col]) {
                        strncpy(col_name, column_names[selected_col], sizeof(col_name)-1);
                        col_name[sizeof(col_name)-1] = '\0';
                    } else {
                        col_letter(selected_col, col_name);
                    }
                    continue;
                } else if (strcmp(cmd, "gc") == 0) {
                    if (!in_graph_mode) {
                        continue;
                    }

                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :gc red|green|blue|yellow|cyan|magenta|white|default");
                        attroff(COLOR_PAIR(3));

                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    char color_str[32];
                    strncpy(color_str, arg, sizeof(color_str) - 1);
                    color_str[sizeof(color_str) - 1] = '\0';

                    int pair_idx = GRAPH_COLOR_BASE + 6; // default white

                    if (strcasecmp(color_str, "red") == 0)     pair_idx = GRAPH_COLOR_BASE + 0;
                    else if (strcasecmp(color_str, "green") == 0)   pair_idx = GRAPH_COLOR_BASE + 1;
                    else if (strcasecmp(color_str, "blue") == 0)    pair_idx = GRAPH_COLOR_BASE + 2;
                    else if (strcasecmp(color_str, "yellow") == 0)  pair_idx = GRAPH_COLOR_BASE + 3;
                    else if (strcasecmp(color_str, "cyan") == 0)    pair_idx = GRAPH_COLOR_BASE + 4;
                    else if (strcasecmp(color_str, "magenta") == 0) pair_idx = GRAPH_COLOR_BASE + 5;
                    else if (strcasecmp(color_str, "white") == 0 ||
                             strcasecmp(color_str, "default") == 0) pair_idx = GRAPH_COLOR_BASE + 6;
                    else {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Unknown color: %s", color_str);
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    current_graph_color_pair = pair_idx;

                    // Redraw the graph immediately (no getch, just update the screen)
                    clear();
                    draw_graph(cur_col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);  // or current column
                    refresh();
                    continue;
                } else if (strcmp(cmd, "gt") == 0) {
                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :gt bar | line | dot");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    char type_str[16];
                    strncpy(type_str, arg, sizeof(type_str)-1);
                    type_str[sizeof(type_str)-1] = '\0';

                    if (strcasecmp(type_str, "line") == 0) {
                        graph_type = GRAPH_LINE;
                    } else if (strcasecmp(type_str, "bar") == 0) {
                        graph_type = GRAPH_BAR;
                    } else if (strcasecmp(type_str, "dot") == 0) {
                        graph_type = GRAPH_DOT;
                    } else {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Wrong graph type: %s (bar|line|dot)", type_str);
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    // Redraw the graph immediately
                    clear();
                    draw_graph(cur_col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                    refresh();
                    continue;
                } else if (strcmp(cmd, "gy") == 0) {
                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :gy log | linear");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    char scale_str[16];
                    strncpy(scale_str, arg, sizeof(scale_str)-1);
                    scale_str[sizeof(scale_str)-1] = '\0';

                    if (strcasecmp(scale_str, "log") == 0) {
                        graph_scale = SCALE_LOG;
                    } else if (strcasecmp(scale_str, "linear") == 0) {
                        graph_scale = SCALE_LINEAR;
                    } else {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Wrong type: %s (log|linear)", scale_str);
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    // Redraw the graph immediately
                    clear();
                    draw_graph(cur_col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                    refresh();
                    continue;
                } else if (strcmp(cmd, "ga") == 0) {
                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :ga on | off – show anomalies");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }
                                        char state[8];
                    strncpy(state, arg, sizeof(state)-1);
                    state[sizeof(state)-1] = '\0';

                    if (strcasecmp(state, "on") == 0) {
                        show_anomalies = true;
                    } else if (strcasecmp(state, "off") == 0) {
                        show_anomalies = false;
                    }
                    continue;
                } else if (strcmp(cmd, "gp") == 0) {
                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :gp on | off – show cursor");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }

                    char state[8];
                    strncpy(state, arg, sizeof(state)-1);
                    state[sizeof(state)-1] = '\0';

                    if (strcasecmp(state, "on") == 0) {
                        show_graph_cursor = true;
                    } else if (strcasecmp(state, "off") == 0) {
                        show_graph_cursor = false;
                    }
                    continue;
                } else if (strcmp(cmd, "gsvg") == 0) {
                    // :gsvg [filename] [WxH]
                    // Defaults: <file>_graph.svg, 900x500
                    char svg_path[512] = "";
                    int  svg_w = 900, svg_h = 500;

                    // Parse optional args: filename and/or WxH
                    if (arg && *arg) {
                        char abuf[512];
                        strncpy(abuf, arg, sizeof(abuf) - 1);
                        abuf[sizeof(abuf) - 1] = '\0';
                        // Look for WxH token (contains 'x' between digits)
                        char *tok = strtok(abuf, " \t");
                        while (tok) {
                            int tw = 0, th = 0;
                            if (sscanf(tok, "%dx%d", &tw, &th) == 2 && tw > 0 && th > 0) {
                                svg_w = tw; svg_h = th;
                            } else {
                                strncpy(svg_path, tok, sizeof(svg_path) - 1);
                            }
                            tok = strtok(NULL, " \t");
                        }
                    }
                    if (!svg_path[0]) {
                        // Build default name from open file
                        const char *base = file_to_open ? file_to_open : "graph";
                        const char *dot  = strrchr(base, '.');
                        if (dot) {
                            int len = (int)(dot - base);
                            if (len > (int)sizeof(svg_path) - 10) len = (int)sizeof(svg_path) - 10;
                            snprintf(svg_path, sizeof(svg_path), "%.*s_graph.svg", len, base);
                        } else {
                            snprintf(svg_path, sizeof(svg_path), "%s_graph.svg", base);
                        }
                    }
                    int ret = export_graph_svg(svg_path, svg_w, svg_h,
                                               rows, f, row_count);
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    if (ret == 0) {
                        attron(COLOR_PAIR(2));
                        printw(" | Saved: %s (%dx%d)", svg_path, svg_w, svg_h);
                        attroff(COLOR_PAIR(2));
                    } else {
                        attron(COLOR_PAIR(3));
                        printw(" | Error: could not write %s", svg_path);
                        attroff(COLOR_PAIR(3));
                    }
                    refresh(); getch(); clrtoeol();
                    continue;
                } else if (strcmp(cmd, "gsc") == 0) {
                    // Scatter plot:
                    //   :gsc x_col          — X = x_col, Y = current column
                    //   :gsc x_col y_col    — X = x_col, Y = y_col (explicit)
                    //   :gsc off            — exit scatter mode
                    #define RESOLVE_COL(name, out) do { \
                        (out) = -1; \
                        for (int _ci = 0; _ci < col_count; _ci++) { \
                            if (use_headers && column_names[_ci] && \
                                strcasecmp(column_names[_ci], (name)) == 0) \
                                { (out) = _ci; break; } \
                        } \
                        if ((out) < 0) { \
                            int _v = 0; bool _ok = true; \
                            for (int _ci = 0; (name)[_ci]; _ci++) { \
                                char _c = (char)toupper((unsigned char)(name)[_ci]); \
                                if (_c < 'A' || _c > 'Z') { _ok = false; break; } \
                                _v = _v * 26 + (_c - 'A' + 1); \
                            } \
                            if (_ok && _v > 0 && _v - 1 < col_count) (out) = _v - 1; \
                        } \
                    } while(0)

                    if (arg && strcasecmp(arg, "off") == 0) {
                        graph_scatter_mode = 0;
                        graph_scatter_x_col = -1;
                    } else if (arg && *arg) {
                        // Split arg into up to two tokens
                        char gsc_buf[256];
                        strncpy(gsc_buf, arg, sizeof(gsc_buf) - 1);
                        gsc_buf[sizeof(gsc_buf) - 1] = '\0';
                        char *tok1 = gsc_buf;
                        char *tok2 = NULL;
                        for (char *p = gsc_buf; *p; p++) {
                            if (*p == ' ' || *p == '\t') {
                                *p = '\0';
                                tok2 = p + 1;
                                while (*tok2 == ' ' || *tok2 == '\t') tok2++;
                                if (!*tok2) tok2 = NULL;
                                break;
                            }
                        }
                        int xcol = -1, ycol = -1;
                        RESOLVE_COL(tok1, xcol);
                        if (tok2) {
                            RESOLVE_COL(tok2, ycol);
                        } else {
                            ycol = cur_col;
                        }
                        if (xcol < 0) {
                            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                            attron(COLOR_PAIR(3));
                            printw(" | X column '%s' not found", tok1);
                            attroff(COLOR_PAIR(3));
                            refresh(); getch(); clrtoeol();
                        } else if (ycol < 0) {
                            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                            attron(COLOR_PAIR(3));
                            printw(" | Y column '%s' not found", tok2 ? tok2 : "");
                            attroff(COLOR_PAIR(3));
                            refresh(); getch(); clrtoeol();
                        } else if (xcol == ycol) {
                            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                            attron(COLOR_PAIR(3));
                            printw(" | X and Y are the same column — use :gsc x_col y_col");
                            attroff(COLOR_PAIR(3));
                            refresh(); getch(); clrtoeol();
                        } else {
                            graph_scatter_x_col = xcol;
                            graph_scatter_mode = 1;
                            in_graph_mode = 1;
                            graph_col_list[0] = ycol;
                            graph_col_count = 1;
                            graph_cursor_pos = 0;
                            // Initialize visible_points so the key handler immediately
                            // knows the range; the exact value will come after the first render
                            graph_visible_points = width - (ROW_DATA_OFFSET + 2) - 4;
                            graph_total_points   = graph_visible_points;
                        }
                    } else {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :gsc x_col [y_col]  |  :gsc off");
                        attroff(COLOR_PAIR(3));
                        refresh(); getch(); clrtoeol();
                    }
                    #undef RESOLVE_COL
                    continue;
                } else if (strcmp(cmd, "g2y") == 0) {
                    if (arg && strcasecmp(arg, "on") == 0)       graph_dual_yaxis = 1;
                    else if (arg && strcasecmp(arg, "off") == 0) graph_dual_yaxis = 0;
                    else graph_dual_yaxis ^= 1;  // toggle if no arg
                    continue;
                } else if (strcmp(cmd, "grid") == 0) {
                    if (!arg || !*arg) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Usage: :grid y|x|yx|off");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                        continue;
                    }
                    if (strcasecmp(arg, "y") == 0)        graph_grid = 1;
                    else if (strcasecmp(arg, "x") == 0)   graph_grid = 2;
                    else if (strcasecmp(arg, "yx") == 0 ||
                             strcasecmp(arg, "xy") == 0 ||
                             strcasecmp(arg, "on") == 0)  graph_grid = 3;
                    else if (strcasecmp(arg, "off") == 0) graph_grid = 0;
                    else {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | :grid y|x|yx|off");
                        attroff(COLOR_PAIR(3));
                        refresh();
                        getch();
                        clrtoeol();
                    }
                    continue;
                }

                clrtoeol();
                refresh();
            } else if (ch == 'm' || ch == 'M') {
                min_max_show = (ch == 'm') ? 1 : 2;
            } else if (ch >= '1' && ch <= '9') {
                // Toggle series visibility
                int idx = ch - '1';
                if (idx < graph_col_count)
                    graph_series_hidden[idx] ^= 1;
            } else if (ch == '+' || ch == '=') {
                // Zoom in: halve visible range around cursor
                int total = graph_total_points;
                if (total > 0) {
                    int cur_s = graph_zoom_start;
                    int cur_e = (graph_zoom_end > 0 && graph_zoom_end <= total) ? graph_zoom_end : total;
                    int cur_len = cur_e - cur_s;
                    int half = cur_len / 4;
                    if (half < 2) half = 2;
                    int vp = graph_visible_points > 1 ? graph_visible_points - 1 : 1;
                    int center = cur_s + (int)((double)graph_cursor_pos / vp * cur_len);
                    int new_s = center - half;
                    int new_e = center + half;
                    if (new_s < 0) { new_e -= new_s; new_s = 0; }
                    if (new_e > total) { new_s -= (new_e - total); new_e = total; }
                    if (new_s < 0) new_s = 0;
                    graph_zoom_start = new_s;
                    graph_zoom_end   = new_e;
                    graph_cursor_pos = (new_e - new_s) / 2;
                }
            } else if (ch == '-') {
                // Zoom out: double visible range
                int total = graph_total_points;
                if (total > 0) {
                    int cur_s = graph_zoom_start;
                    int cur_e = (graph_zoom_end > 0 && graph_zoom_end <= total) ? graph_zoom_end : total;
                    int cur_len = cur_e - cur_s;
                    int half = cur_len;
                    int center = (cur_s + cur_e) / 2;
                    int new_s = center - half;
                    int new_e = center + half;
                    if (new_s < 0) new_s = 0;
                    if (new_e > total) new_e = total;
                    if (new_s == 0 && new_e >= total) {
                        graph_zoom_start = 0; graph_zoom_end = -1; // full view
                    } else {
                        graph_zoom_start = new_s;
                        graph_zoom_end   = new_e;
                    }
                    graph_cursor_pos = graph_visible_points / 2;
                }
            } else if (ch == '0') {
                // Reset zoom to full view
                graph_zoom_start = 0;
                graph_zoom_end   = -1;
                graph_cursor_pos = 0;
            } else if (ch == '?') {
                show_help(1);
            }

            continue;
        }

        if (ch == 'G') {
            // G → jump to last row
            cur_display_row = display_count - 1;
            top_display_row = cur_display_row - visible_rows + 1;
            if (top_display_row < 0) top_display_row = 0;
        }
        else if (ch == 'g') {
            int ch2 = getch();
            if (ch2 == 'g') {
                // gg → first row
                cur_display_row = 0;
                top_display_row = 0;
            } else if (ch2 == '_') {
                // g_ → auto-width all visible columns
                int sample_rows = filtered_count > 0 ? filtered_count : row_count;
                if (sample_rows > 100) sample_rows = 100;
                int *rows_to_check = filtered_count > 0 ? filtered_rows : NULL;
                for (int c = 0; c < col_count; c++) {
                    if (col_hidden[c]) continue;
                    int max_len = 8;
                    for (int r = 0; r < sample_rows; r++) {
                        int real_row = rows_to_check ? rows_to_check[r] : r + (use_headers ? 1 : 0);
                        char *line = rows[real_row].line_cache;
                        if (!line) continue;
                        char *val = get_column_value(line, column_names[c] ? column_names[c] : "", use_headers);
                        if (val) {
                            int len = strlen(val);
                            if (len > max_len) max_len = len;
                            free(val);
                        }
                    }
                    col_widths[c] = max_len + 7;
                    if (col_widths[c] > 80) col_widths[c] = 80;
                }
                save_column_settings(file_to_open);
            }
            /* unknown g+key: ignore */
        }

        if (ch == 'M') {  /* M — toggle mark current column for multi-series graph */
            if (cur_col >= 0 && cur_col < col_count) {
                if (col_types[cur_col] != COL_NUM) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(1));
                    printw(" | Not a numeric column");
                    attroff(COLOR_PAIR(1));
                    refresh();
                } else {
                    graph_marked[cur_col] = !graph_marked[cur_col];
                    /* count total marked */
                    int mc = 0;
                    for (int c = 0; c < col_count; c++) if (graph_marked[c]) mc++;
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    if (graph_marked[cur_col])
                        printw(" | Marked for graph (%d total) — press Ctrl+G to draw", mc);
                    else
                        printw(" | Unmarked (%d remaining)", mc);
                    attroff(COLOR_PAIR(3));
                    refresh();
                }
            }
        }

        if (ch == ('G' & 0x1f)) {  // Ctrl+G — graph marked columns + current
            /* build col list: all marked + current (no dups) */
            graph_col_count = 0;
            for (int c = 0; c < col_count && graph_col_count < 10; c++)
                if (graph_marked[c]) graph_col_list[graph_col_count++] = c;
            /* add current if numeric and not already in list */
            if (col_types[cur_col] == COL_NUM) {
                int dup = 0;
                for (int i = 0; i < graph_col_count; i++)
                    if (graph_col_list[i] == cur_col) { dup = 1; break; }
                if (!dup && graph_col_count < 10)
                    graph_col_list[graph_col_count++] = cur_col;
            }
            if (graph_col_count == 0) {
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(1));
                printw(" | No numeric columns selected (mark with M or move to numeric column)");
                attroff(COLOR_PAIR(1));
                refresh();
                getch();
                continue;
            }
            current_graph = 0;
            graph_start = 0;
            memset(graph_series_hidden, 0, sizeof(graph_series_hidden));
            graph_zoom_start = 0;
            graph_zoom_end   = -1;
            graph_cursor_pos = 0;
            using_date_x = 0;
            date_col = -1;
            for (int c = 0; c < col_count; c++) {
                if (col_types[c] == COL_DATE) {
                    date_col = c;
                    break;
                }
            }
            if (date_col >= 0) {
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                char col_buf[16];
                if (column_names[date_col]) {
                    strncpy(col_buf, column_names[date_col], sizeof(col_buf) - 1);
                    col_buf[sizeof(col_buf) - 1] = '\0';
                } else {
                    col_letter(date_col, col_buf);
                }
                printw(" | Use date column %s as X-axis? (y/n) ", col_buf);
                attroff(COLOR_PAIR(3));
                refresh();
                int yn = getch();
                if (yn == 'y' || yn == 'Y') {
                    using_date_x = 1;
                    save_sort_col = sort_col; save_sort_level_count = sort_level_count;
                    save_sort_order = sort_order;
                    int temp_sort_col = sort_col;
                    int temp_sort_order = sort_order;
                    sort_col = date_col;
                    sort_order = 1;

                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Sorting...                                              ");
                    attroff(COLOR_PAIR(3));
                    refresh();

                    if (filter_active) {
                        save_filtered_count = filtered_count;
                        save_filtered_rows = malloc(sizeof(int) * save_filtered_count);
                        if (save_filtered_rows) {
                            memcpy(save_filtered_rows, filtered_rows, sizeof(int) * save_filtered_count);
                        }
                        qsort(filtered_rows, filtered_count, sizeof(int), compare_rows_by_column);
                    } else {
                        save_sorted_count = sorted_count;
                        save_sorted_rows = malloc(sizeof(int) * sorted_count);
                        if (save_sorted_rows) {
                            memcpy(save_sorted_rows, sorted_rows, sizeof(int) * save_sorted_count);
                        }
                        build_sorted_index();
                    }
                    sort_col = temp_sort_col;
                    sort_order = temp_sort_order;
                }
            }
            in_graph_mode = 1;
            continue;
        }

        if (ch == 'q' || ch == 27) {
            for (int i = 0; i < col_count; i++) {
                free(column_names[i]);
            }
            free(rows);
            fclose(f);
            endwin();
            return 0;
        }

        if (ch == '?') {
            show_help(1);
        }

        if (in_search_mode) {
            if (ch == 'n' || ch == 'N') {
                if (search_count == 0) continue;
                if (ch == 'n') {
                    search_index = (search_index + 1) % search_count;
                } else {
                    search_index = (search_index - 1 + search_count) % search_count;
                }
                goto_search_result(search_index, &cur_display_row, &top_display_row, &cur_col, &left_col,
                       visible_rows, visible_cols, row_count);
            } else {
                in_search_mode = 0;
                search_query[0] = '\0';
                search_count = 0;
                search_index = -1;
            }
        }

        if (ch == 'u') {
            int undo_row;
            char *undo_line;
            if (undo_pop(&undo_row, &undo_line)) {
                free(rows[undo_row].line_cache);
                rows[undo_row].line_cache = undo_line;

                // Move cursor to the changed row
                int display_pos = undo_row - (use_headers ? 1 : 0);
                if (filter_active) {
                    // Find row in filtered_rows
                    for (int i = 0; i < filtered_count; i++) {
                        if (filtered_rows[i] == undo_row) { display_pos = i; break; }
                    }
                }
                if (display_pos >= 0 && display_pos < display_count) {
                    cur_display_row = display_pos;
                    if (cur_display_row < top_display_row)
                        top_display_row = cur_display_row;
                    else if (cur_display_row >= top_display_row + visible_rows)
                        top_display_row = cur_display_row - visible_rows + 1;
                    if (top_display_row < 0) top_display_row = 0;
                }

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Undo (%d left)", undo_top);
                attroff(COLOR_PAIR(3));
                refresh();
            } else {
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(1));
                printw(" | Nothing to undo");
                attroff(COLOR_PAIR(1));
                refresh();
            }
        }

        if (ch == 's' || ch == 'S') {
            int save_ok = save_file(file_to_open, f, rows, row_count);

            // Draw normal status first (to avoid blank area)
            move(height - 1, 1);
            clrtoeol();
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);

            // Add message on the right
            if (save_ok == 0) {
                attron(COLOR_PAIR(3));
                printw(" | Saving...");
                attroff(COLOR_PAIR(3));
            } else {
                attron(COLOR_PAIR(3));
                printw(" | Error...");
                attroff(COLOR_PAIR(3));
            }

            refresh();
            napms(1500);  // 1.5 seconds — show message

            // Restore normal status (erase only right part)
            move(height - 1, width - 35);
            clrtoeol();
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            refresh();
        }        

        if (ch == '\n' || ch == KEY_ENTER || ch == 'e') {
            if (cur_real_row >= row_count) continue;

            // Guard against NULL
            if (!rows[cur_real_row].line_cache) {
                rows[cur_real_row].line_cache = strdup("");
            }

            char edit_buffer[INPUT_BUF_SIZE] = {0};
            strncpy(edit_buffer, current_cell_content, INPUT_BUF_SIZE - 1);
            int buffer_len = strlen(edit_buffer);
            int pos = buffer_len;

            echo();
            curs_set(1);

            // Output edit line with correct argument order
            mvprintw(2, 2, "%s%d: ", col_name, cur_display_row + 1);
            clrtoeol();
            printw("%s", edit_buffer);
            refresh();

            // Cursor position — row 2
            int base_x = 2 + strlen(col_name) + snprintf(NULL, 0, "%d: ", cur_display_row + 1);
            move(2, base_x + pos);

            int done = 0;
            int canceled = 0;

            while (!done) {
                int key = getch();

                if (key == '\n' || key == KEY_ENTER) {
                    done = 1;
                } else if (key == 27) { // Esc
                    canceled = 1;
                    done = 1;
                } else if (key == KEY_LEFT) {
                    if (pos > 0) {
                        pos--;
                        move(2, base_x + pos);
                        refresh();
                    }
                } else if (key == KEY_RIGHT) {
                    if (pos < buffer_len) {
                        pos++;
                        move(2, base_x + pos);
                        refresh();
                    }
                } else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && pos > 0) {
                    memmove(&edit_buffer[pos-1], &edit_buffer[pos], buffer_len - pos + 1);
                    pos--;
                    buffer_len--;
                    mvprintw(2, 2, "%s%d: ", col_name, cur_display_row + 1);
                    clrtoeol();
                    printw("%s", edit_buffer);
                    move(2, base_x + pos);
                    refresh();
                } else if (key >= 32 && key <= 126 && buffer_len < INPUT_BUF_SIZE - 1) {
                    memmove(&edit_buffer[pos+1], &edit_buffer[pos], buffer_len - pos + 1);
                    edit_buffer[pos] = (char)key;
                    pos++;
                    buffer_len++;
                    mvprintw(2, 2, "%s%d: ", col_name, cur_display_row + 1);
                    clrtoeol();
                    printw("%s", edit_buffer);
                    move(2, base_x + pos);
                    refresh();
                }
            }

            noecho();
            curs_set(0);

            // Restore old value in the cell field
            mvprintw(2, 2, "%s%d: ", col_name, cur_display_row + 1);
            clrtoeol();
            printw("%s", current_cell_content);
            refresh();

            if (!canceled && strcmp(edit_buffer, current_cell_content) != 0) {
                // Save state before change for undo
                undo_push(cur_real_row, rows[cur_real_row].line_cache);

                int field_count = 0;
                char **fields = parse_csv_line(
                    rows[cur_real_row].line_cache ? rows[cur_real_row].line_cache : "",
                    &field_count);

                if (fields && cur_col < field_count) {
                    free(fields[cur_col]);
                    fields[cur_col] = strdup(edit_buffer);
                    char *new_line = build_csv_line(fields, field_count, csv_delimiter);
                    free_csv_fields(fields, field_count);
                    if (new_line) {
                        free(rows[cur_real_row].line_cache);
                        rows[cur_real_row].line_cache = new_line;
                    }
                } else if (fields) {
                    free_csv_fields(fields, field_count);
                }
            }
        }

        if (ch == '/') {
            in_search_mode = 1;
            search_query[0] = '\0';

            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            refresh();

            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Searching...");
            attroff(COLOR_PAIR(3));
            refresh();

            echo();
            curs_set(1);

            mvprintw(2, 2, "/");
            clrtoeol();
            refresh();

            int pos = 0;
            while (1) {
                int key = getch();
                if (key == '\n' || key == KEY_ENTER) break;
                if (key == 27) { in_search_mode = 0; break; }
                if ((key == KEY_BACKSPACE || key == 127) && pos > 0) {
                    pos--;
                    search_query[pos] = '\0';
                } else if (key >= 32 && key <= 126 && pos < 255) {
                    search_query[pos++] = (char)key;
                    search_query[pos] = '\0';
                }

                mvprintw(2, 2, "/%s", search_query);
                clrtoeol();
                move(2, 3 + pos);
                refresh();
            }

            noecho();
            curs_set(0);

            if (in_search_mode && strlen(search_query) > 0) {
                perform_search(rows, f, row_count);
                search_index = 0;
                goto_search_result(search_index, &cur_display_row, &top_display_row, &cur_col, &left_col,
                       visible_rows, visible_cols, row_count);
            } else {
                in_search_mode = 0;
            }
        }

        if (ch == 'f' || ch == 'F') {
            in_filter_mode = 1;
            filter_query[0] = '\0';

            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Filtering...               ");
            attroff(COLOR_PAIR(3));
            refresh();

            mvprintw(2, 2, "F:");
            clrtoeol();
            refresh();

            if (!ac_readline(filter_query, sizeof(filter_query), 2, 4, width - 1))
                in_filter_mode = 0;

            if (in_filter_mode && strlen(filter_query) > 0) {
                cur_col = 0;
                left_col = freeze_cols;
                cur_display_row = 0;
                top_display_row = 0;

                apply_filter(rows, f, row_count);
                if (sort_col >= 0 && sort_order != 0) build_sorted_index();
                if (filtered_count > 0) {
                    cur_display_row = 0;
                    top_display_row = 0;
                    cur_real_row = filtered_rows[0];
                }
            } else {
                in_filter_mode = 0;
                filter_active = 0;
                filtered_count = 0;
            }
        }
        
        if (ch == 'p' || ch == 'P') {
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Pivoting...               ");
            attroff(COLOR_PAIR(3));
            refresh();

            pivot_drilldown_filter[0] = '\0';

            PivotSettings settings = {0};
            if (ch == 'p') {
                if (load_pivot_settings(file_to_open, &settings)) {
                    build_and_show_pivot(&settings, file_to_open, height, width);
                } else {
                    show_pivot_settings_window(&settings, file_to_open, height, width);
                }
            } else {
                show_pivot_settings_window(&settings, file_to_open, height, width);
            }
            free(settings.row_group_col);
            free(settings.col_group_col);
            free(settings.value_col);
            free(settings.aggregation);
            free(settings.date_grouping);

            // Drill-down: user pressed Enter on a pivot cell
            if (pivot_drilldown_filter[0]) {
                if (filter_active && filter_query[0]) {
                    // Combine existing filter with drill-down via AND
                    char combined[512];
                    snprintf(combined, sizeof(combined), "(%s) AND %s",
                             filter_query, pivot_drilldown_filter);
                    strncpy(filter_query, combined, sizeof(filter_query) - 1);
                } else {
                    strncpy(filter_query, pivot_drilldown_filter, sizeof(filter_query) - 1);
                }
                filter_query[sizeof(filter_query) - 1] = '\0';
                pivot_drilldown_filter[0] = '\0';

                in_filter_mode = 1;
                cur_col = 0;
                left_col = freeze_cols;
                cur_display_row = 0;
                top_display_row = 0;

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Drill-down: %s", filter_query);
                attroff(COLOR_PAIR(3));
                refresh();

                apply_filter(rows, f, row_count);
                if (sort_col >= 0 && sort_order != 0) build_sorted_index();
                if (filtered_count > 0) {
                    cur_real_row = filtered_rows[0];
                }
            }
        }        

        // Navigation through visible rows
        if (ch == KEY_DOWN || ch == 'j') {
            if (cur_display_row < display_count - 1) {
                cur_display_row++;
                if (cur_display_row >= top_display_row + visible_rows - 1) top_display_row++;
            }
        }
        else if (ch == KEY_UP || ch == 'k') {
            if (cur_display_row > 0) {
                cur_display_row--;
                if (cur_display_row < top_display_row) top_display_row = cur_display_row;
            }
        }
        else if (ch == KEY_LEFT || ch == 'h') {
            int nc = cur_col - 1;
            while (nc > 0 && col_hidden[nc]) nc--;
            if (nc >= 0 && !col_hidden[nc]) {
                cur_col = nc;
                if (cur_col >= freeze_cols && cur_col < left_col) {
                    left_col = cur_col;
                    if (left_col < freeze_cols) left_col = freeze_cols;
                }
            }
        }
        else if (ch == KEY_RIGHT || ch == 'l') {
            int nc = cur_col + 1;
            while (nc < col_count && col_hidden[nc]) nc++;
            if (nc < col_count) {
                cur_col = nc;
                if (cur_col >= freeze_cols && cur_col >= left_col + visible_cols) {
                    left_col = cur_col - visible_cols + 1;
                    if (left_col < freeze_cols) left_col = freeze_cols;
                }
            }
        }       
        else if (ch == KEY_PPAGE) {
            int scroll = visible_rows - 2;
            if (scroll < 1) scroll = 1;
            cur_display_row -= scroll;
            top_display_row -= scroll;
            if (cur_display_row < 0) cur_display_row = 0;
            if (top_display_row < 0) top_display_row = 0;
        }
        else if (ch == KEY_NPAGE) {
            int scroll = visible_rows - 2;
            if (scroll < 1) scroll = 1;
            cur_display_row += scroll;
            top_display_row += scroll;
            if (cur_display_row >= display_count) cur_display_row = display_count - 1;
            if (top_display_row > display_count - visible_rows) top_display_row = display_count - visible_rows;
            if (top_display_row < 0) top_display_row = 0;
        }
        else if (ch == KEY_HOME || ch == 'K') {
            cur_display_row = 0;
            top_display_row = 0;
        }
        else if (ch == KEY_END || ch == 'J') {
            cur_display_row = display_count - 1;
            top_display_row = cur_display_row - visible_rows + 1;
            if (top_display_row < 0) top_display_row = 0;
        }
        else if (ch == 'H') {
            cur_col = 0;
            while (cur_col < col_count - 1 && col_hidden[cur_col]) cur_col++;
            left_col = freeze_cols;
            if (left_col >= col_count) left_col = col_count > 0 ? col_count - 1 : 0;
        }
        else if (ch == 'L') {
            cur_col = col_count - 1;
            while (cur_col > 0 && col_hidden[cur_col]) cur_col--;
            left_col = cur_col - visible_cols + 1;
            if (left_col < freeze_cols) left_col = freeze_cols;
            if (left_col < 0) left_col = 0;
        }
        else if (ch == 'z' || ch == 'Z') {
            // z — freeze/unfreeze at current column
            if (freeze_cols == cur_col + 1) {
                freeze_cols = 0;  // unfreeze
            } else {
                freeze_cols = cur_col + 1;
            }
            if (freeze_cols > col_count) freeze_cols = col_count;
            if (left_col < freeze_cols) left_col = freeze_cols;
            save_column_settings(file_to_open);
        }
        else if (ch == 't' || ch == 'T') {
            clear();
            int setup_ret = show_column_setup(file_to_open);
            if (setup_ret == 1) {
                // Delimiter changed — full file reload
                for (int i = 0; i < row_count; i++) {
                    free(rows[i].line_cache);
                    rows[i].line_cache = NULL;
                }
                csv_mmap_close();
                fclose(f);
                f = fopen(file_to_open, "r");
                if (f) csv_mmap_open(file_to_open);
                free(rows);
                rows = build_row_index(f, &row_count);
                if (rows && !alloc_row_arrays(row_count)) { /* oom */ }
                sort_col = -1; sort_order = 0; sort_level_count = 0; sorted_count = 0;
                filter_active = 0; filtered_count = 0; filter_query[0] = '\0';
                cur_display_row = 0; top_display_row = 0; cur_col = 0; left_col = 0;
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Separator changed — file reloaded");
                attroff(COLOR_PAIR(3));
                refresh();
            }
        }
        else if (ch == 'w' || ch == '>') {
            if (cur_col < col_count) {
                col_widths[cur_col] += 1;
                if (col_widths[cur_col] > 100) col_widths[cur_col] = 100;
            }
            save_column_settings(file_to_open);
        } else if (ch == 'W' || ch == '<') {
            if (cur_col < col_count) {
                col_widths[cur_col] -= 1;
                if (col_widths[cur_col] < 5) col_widths[cur_col] = 5;
            }
            save_column_settings(file_to_open);
        } else if (ch == 'a' || ch == 'A' || ch == '_') {
            // Auto-fit width of the current column
            if (cur_col < col_count) {
                int max_len = 8;  // minimum for column name
                int sample_rows = filtered_count > 0 ? filtered_count : row_count;
                if (sample_rows > 100) sample_rows = 100;  // sample at most 100 rows

                int *rows_to_check = filtered_count > 0 ? filtered_rows : NULL;

                for (int r = 0; r < sample_rows; r++) {
                    int real_row = rows_to_check ? rows_to_check[r] : r + (use_headers ? 1 : 0);
                    char *line = rows[real_row].line_cache;
                    if (!line) continue;

                    char *val = get_column_value(line, column_names[cur_col] ? column_names[cur_col] : "", use_headers);
                    if (val) {
                        int len = strlen(val);
                        if (len > max_len) max_len = len;
                        free(val);
                    }
                }

                // +2 for padding, +5 for comfort
                col_widths[cur_col] = max_len + 7;
                if (col_widths[cur_col] > 80) col_widths[cur_col] = 80;
            }
            save_column_settings(file_to_open);
        }
        else if (ch == 'c') {  /* c<t/i/f/d> — quick column type/format */
            if (cur_col >= 0 && cur_col < col_count) {
                int ch2 = getch();
                const char *msg = NULL;
                if (ch2 == 't') {
                    col_types[cur_col] = COL_STR;
                    col_formats[cur_col].decimal_places = -1;
                    col_formats[cur_col].date_format[0] = '\0';
                    msg = "Text";
                } else if (ch2 == 'i') {
                    col_types[cur_col] = COL_NUM;
                    col_formats[cur_col].decimal_places = 0;
                    msg = "Integer";
                } else if (ch2 == 'f') {
                    col_types[cur_col] = COL_NUM;
                    col_formats[cur_col].decimal_places = 2;
                    msg = "Float (2 decimals)";
                } else if (ch2 == 'd') {
                    col_types[cur_col] = COL_DATE;
                    strncpy(col_formats[cur_col].date_format, "%Y-%m-%d",
                            sizeof(col_formats[cur_col].date_format) - 1);
                    msg = "Date (%Y-%m-%d)";
                }
                if (msg) {
                    save_column_settings(file_to_open);
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Column type: %s", msg);
                    attroff(COLOR_PAIR(3));
                    refresh();
                }
            }
        }
        else if (ch == '[') {
            // Sort ascending — resets any existing multi-sort
            if (cur_col >= 0 && cur_col < col_count) {
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3)); printw(" | Sorting..."); attroff(COLOR_PAIR(3));
                refresh();
                sort_col = cur_col; sort_order = 1;
                sort_levels[0].col = cur_col; sort_levels[0].order = 1;
                sort_level_count = 1;
                build_sorted_index();
                cur_display_row = 0; top_display_row = 0;
            }
        }
        else if (ch == ']') {
            // Sort descending — resets any existing multi-sort
            if (cur_col >= 0 && cur_col < col_count) {
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3)); printw(" | Sorting..."); attroff(COLOR_PAIR(3));
                refresh();
                sort_col = cur_col; sort_order = -1;
                sort_levels[0].col = cur_col; sort_levels[0].order = -1;
                sort_level_count = 1;
                build_sorted_index();
                cur_display_row = 0; top_display_row = 0;
            }
        }
        else if (ch == '{') {
            // Add current column as next ascending sort level (Shift+[)
            if (cur_col >= 0 && cur_col < col_count) {
                int found = -1;
                for (int li = 0; li < sort_level_count; li++)
                    if (sort_levels[li].col == cur_col) { found = li; break; }
                if (found >= 0) {
                    sort_levels[found].order = 1;
                } else if (sort_level_count < MAX_SORT_LEVELS) {
                    sort_levels[sort_level_count].col   = cur_col;
                    sort_levels[sort_level_count].order = 1;
                    sort_level_count++;
                }
                sort_col = sort_levels[0].col; sort_order = sort_levels[0].order;
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Sorting (%d levels)...", sort_level_count);
                attroff(COLOR_PAIR(3));
                refresh();
                build_sorted_index();
                cur_display_row = 0; top_display_row = 0;
            }
        }
        else if (ch == '}') {
            // Add current column as next descending sort level (Shift+])
            if (cur_col >= 0 && cur_col < col_count) {
                int found = -1;
                for (int li = 0; li < sort_level_count; li++)
                    if (sort_levels[li].col == cur_col) { found = li; break; }
                if (found >= 0) {
                    sort_levels[found].order = -1;
                } else if (sort_level_count < MAX_SORT_LEVELS) {
                    sort_levels[sort_level_count].col   = cur_col;
                    sort_levels[sort_level_count].order = -1;
                    sort_level_count++;
                }
                sort_col = sort_levels[0].col; sort_order = sort_levels[0].order;
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Sorting (%d levels)...", sort_level_count);
                attroff(COLOR_PAIR(3));
                refresh();
                build_sorted_index();
                cur_display_row = 0; top_display_row = 0;
            }
        }
        else if (ch == 'r') {
            // Reset all sorting
            sort_col = -1; sort_order = 0;
            sort_level_count = 0;
            sorted_count = 0;
            cur_display_row = 0; top_display_row = 0;
        }
        else if (ch == 'R') {
            // Reset filter
            in_filter_mode = 0;
            filter_active = 0;
            filtered_count = 0;
            filter_query[0] = '\0';
            cur_display_row = 0; top_display_row = 0;
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Filter cleared");
            attroff(COLOR_PAIR(3));
            refresh();
        }
        else if (ch == ('R' & 0x1f)) {  // Ctrl+R — re-read file
            // 1. Free all row caches
            for (int i = 0; i < row_count; i++) {
                free(rows[i].line_cache);
                rows[i].line_cache = NULL;
            }

            // 2. Reopen the file
            csv_mmap_close();
            fclose(f);
            f = fopen(file_to_open, "r");
            if (!f) {
                mvprintw(height - 1, 0, "Reload failed: cannot open %s", file_to_open);
                refresh();
                continue;
            }
            if (f) csv_mmap_open(file_to_open);

            // 3. Update file size
            struct stat reload_st;
            if (fstat(fileno(f), &reload_st) == 0) {
                long long size = reload_st.st_size;
                if (size < 1024LL * 1024)
                    snprintf(file_size_str, sizeof(file_size_str), "%.0f KB", size / 1024.0);
                else if (size < 1024LL * 1024 * 1024)
                    snprintf(file_size_str, sizeof(file_size_str), "%.1f MB", size / (1024.0 * 1024.0));
                else
                    snprintf(file_size_str, sizeof(file_size_str), "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
            }

            // 4. Re-index rows
            free(rows);
            rows = build_row_index(f, &row_count);
            if (!rows) {
                mvprintw(LINES - 1, 0, "Out of memory during reload");
                refresh(); getch(); continue;
            }
            if (!alloc_row_arrays(row_count)) {
                mvprintw(LINES - 1, 0, "Out of memory during reload");
                refresh(); getch(); continue;
            }

            // 5. Re-apply filter if it was active
            if (strlen(filter_query) > 0) {
                apply_filter(rows, f, row_count);
            }

            // 6. Re-apply sort if it was active
            if (sort_col >= 0) {
                if (filter_active) {
                    qsort(filtered_rows, filtered_count, sizeof(int), compare_rows_by_column);
                } else {
                    build_sorted_index();
                    sorted_count = row_count - (use_headers ? 1 : 0);
                }
            }

            // 7. Adjust cursor position (file may have become shorter)
            int new_display_count = filter_active
                ? filtered_count
                : (row_count - (use_headers ? 1 : 0));
            if (cur_display_row >= new_display_count)
                cur_display_row = new_display_count > 0 ? new_display_count - 1 : 0;
            if (top_display_row > cur_display_row)
                top_display_row = cur_display_row;
        }
        else if (ch == 'D' || ch == 'd') {
            // column statistics
            show_column_stats(cur_col);
        }
        else if (ch == '\\' || ch == 'I') {  /* \ or I — data profile for all columns */
            show_profile_window();
        }
        else if (ch == '-') {  /* - — toggle hide/show current column */
            if (cur_col >= 0 && cur_col < col_count) {
                col_hidden[cur_col] = !col_hidden[cur_col];
                if (col_hidden[cur_col]) {
                    /* move cursor to next visible column */
                    int nc = cur_col + 1;
                    while (nc < col_count && col_hidden[nc]) nc++;
                    if (nc >= col_count) {
                        nc = cur_col - 1;
                        while (nc > 0 && col_hidden[nc]) nc--;
                    }
                    if (nc >= 0 && nc < col_count) cur_col = nc;
                }
                save_column_settings(file_to_open);
            }
        }
        else if (ch == '#') {
            if (comment_count > 0) {
                show_comments_window();
            } else {
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(6));
                printw(" | No comment lines (enable with :comments on)");
                attroff(COLOR_PAIR(6));
                refresh();
            }
        }
        else if (ch == 'm') {  /* set/clear bookmark: m<a-z> */
            int label = getch();
            if (label >= 'a' && label <= 'z') {
                int bi = label - 'a';
                int cur_real = get_real_row(cur_display_row);
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                if (bookmarks[bi] == cur_real) {
                    /* toggle: already on this bookmark → clear it */
                    bookmarks[bi] = -1;
                    attron(COLOR_PAIR(6));
                    printw(" | Bookmark '%c' cleared", label);
                    attroff(COLOR_PAIR(6));
                } else {
                    bookmarks[bi] = cur_real;
                    attron(COLOR_PAIR(3));
                    printw(" | Bookmark '%c' set at row %d", label, cur_display_row + 1);
                    attroff(COLOR_PAIR(3));
                }
                save_column_settings(file_to_open);
                refresh();
            }
        }
        else if (ch == '\'') {  /* jump to bookmark: '<a-z> */
            int label = getch();
            if (label >= 'a' && label <= 'z') {
                int target_real = bookmarks[label - 'a'];
                if (target_real < 0) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(11));
                    printw(" | No bookmark '%c'", label);
                    attroff(COLOR_PAIR(11));
                    refresh();
                } else {
                    int disp = find_display_for_real(target_real, display_count);
                    if (disp >= 0) {
                        bookmark_scroll(disp, &cur_display_row, &top_display_row, visible_rows);
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Bookmark '%c' -> row %d", label, cur_display_row + 1);
                        attroff(COLOR_PAIR(3));
                        refresh();
                    } else if (filter_active) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(11));
                        printw(" | Bookmark '%c' hidden by filter. Clear? [y/n]", label);
                        attroff(COLOR_PAIR(11));
                        refresh();
                        int ans = getch();
                        if (ans == 'y' || ans == 'Y') {
                            filter_active = 0;
                            int full = row_count - (use_headers ? 1 : 0);
                            disp = find_display_for_real(target_real, full);
                            if (disp >= 0)
                                bookmark_scroll(disp, &cur_display_row, &top_display_row, visible_rows);
                        }
                    }
                }
            }
        }
        else if (ch == ':') {  // Enter command mode
            char cmd_buf[128] = {0};

            attron(COLOR_PAIR(3));
            printw(" | :");
            attroff(COLOR_PAIR(3));
            refresh();
            { int cy, cx; getyx(stdscr, cy, cx); ac_readline(cmd_buf, sizeof(cmd_buf), cy, cx, 0); }

            // Split command and argument (if any)
            char *cmd = cmd_buf;
            char *arg = strchr(cmd_buf, ' ');
            if (arg) {
                *arg = '\0';
                arg++;
                // strip leading spaces from argument
                while (*arg == ' ') arg++;
            }

            if (strcmp(cmd, "q") == 0) {
                for (int i = 0; i < col_count; i++) {
                    free(column_names[i]);
                }
                free(rows);
                fclose(f);
                endwin();
                return 0;
            } else if (strcmp(cmd, "dedup") == 0) {
                /* :dedup [col1,col2] [--keep=last]
                 * Applies deduplication as a filter on the current view. */
                const char *by_cols   = NULL;
                int         keep_last = 0;
                char        cols_buf[256] = "";

                if (arg && *arg) {
                    /* split arg into tokens: extract --keep= flag and column list */
                    char tmp[256];
                    strncpy(tmp, arg, sizeof(tmp)-1);
                    tmp[sizeof(tmp)-1] = '\0';
                    char *tok = strtok(tmp, " ");
                    while (tok) {
                        if (strcmp(tok, "--keep=last") == 0)       keep_last = 1;
                        else if (strcmp(tok, "--keep=first") == 0) keep_last = 0;
                        else {
                            if (cols_buf[0]) strncat(cols_buf, ",", sizeof(cols_buf)-strlen(cols_buf)-1);
                            strncat(cols_buf, tok, sizeof(cols_buf)-strlen(cols_buf)-1);
                        }
                        tok = strtok(NULL, " ");
                    }
                    if (cols_buf[0]) by_cols = cols_buf;
                }

                g_dedup_height   = height;
                g_dedup_filename = file_to_open;

                int new_count = 0, removed = 0;
                int *new_filter = dedup_make_filter(by_cols, keep_last, &new_count, &removed,
                                                    g_dedup_progress_cb, NULL);
                if (!new_filter) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(11));
                    printw(" | :dedup — column not found");
                    attroff(COLOR_PAIR(11));
                    refresh();
                } else {
                    free(filtered_rows);
                    filtered_rows   = new_filter;
                    filtered_count  = new_count;
                    filter_active   = 1;
                    cur_display_row = 0;
                    top_display_row = 0;

                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Dedup: %d rows kept, %d duplicates hidden (use :e to export)",
                           new_count, removed);
                    attroff(COLOR_PAIR(3));
                    refresh();
                }
            } else if (strcmp(cmd, "open") == 0) {
                /* Close current file and reopen file history picker */
                endwin();
                execlp(program_path, program_path, NULL);
                /* execlp only returns on error */
                fprintf(stderr, "Cannot reopen: %s\n", program_path);
                return 1;
            } else if (strcmp(cmd, "profile") == 0) {
                show_profile_window();
            } else if (strcmp(cmd, "theme") == 0) {
                if (arg && *arg) {
                    const Theme *t = theme_by_name(arg);
                    if (t) {
                        theme_apply(t);
                        bkgd(COLOR_PAIR(1));
                        clearok(stdscr, TRUE);
                        theme_save_config(t->name);
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Theme: %s", t->label);
                        attroff(COLOR_PAIR(3));
                        refresh();
                    } else {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(3));
                        printw(" | Unknown theme. Available: %s", theme_list_names());
                        attroff(COLOR_PAIR(3));
                        refresh();
                    }
                } else {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Themes: %s  (current: %s) [any key]",
                           theme_list_names(), current_theme->name);
                    attroff(COLOR_PAIR(3));
                    refresh();
                    getch();
                }
            } else if (strcmp(cmd, "fs") == 0 && filter_active) {
                // Save current filter_query
                //char cfg_path[1024];
                save_filter(file_to_open, filter_query);

                attron(COLOR_PAIR(3));
                printw(" | Filter saved to %s.csvf", file_to_open);
                attroff(COLOR_PAIR(3));
                refresh();
            } else if (strcmp(cmd, "fl") == 0) {
                // Show list
                show_saved_filters_window(file_to_open);
            } else if (strcmp(cmd, "cal") == 0) {
                add_column_and_save(cur_col, arg ? arg : "untitled", file_to_open);
            } else if (strcmp(cmd, "car") == 0) {
                add_column_and_save(cur_col + 1, arg ? arg : "untitled", file_to_open);
            } else if (strcmp(cmd, "cf") == 0 && arg && *arg) {
                fill_column(cur_col, arg, file_to_open);
            } else if (strcmp(cmd, "cd") == 0 && arg && *arg) {
                delete_column(cur_col, arg, file_to_open);
                cur_col = 0;
                left_col = freeze_cols;
            } else if (strcmp(cmd, "marks") == 0) {
                int target_real = show_marks_window(file_to_open);
                if (target_real >= 0) {
                    int disp = find_display_for_real(target_real, display_count);
                    if (disp >= 0) {
                        bookmark_scroll(disp, &cur_display_row, &top_display_row, visible_rows);
                    } else if (filter_active) {
                        draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                        attron(COLOR_PAIR(11));
                        printw(" | Bookmark hidden by filter. Clear? [y/n]");
                        attroff(COLOR_PAIR(11));
                        refresh();
                        int ans = getch();
                        if (ans == 'y' || ans == 'Y') {
                            filter_active = 0;
                            int full = row_count - (use_headers ? 1 : 0);
                            disp = find_display_for_real(target_real, full);
                            if (disp >= 0)
                                bookmark_scroll(disp, &cur_display_row, &top_display_row, visible_rows);
                        }
                    }
                }
            } else if (strcmp(cmd, "dm") == 0 && arg && arg[0] >= 'a' && arg[0] <= 'z') {
                int bi = arg[0] - 'a';
                if (bookmarks[bi] >= 0) {
                    bookmarks[bi] = -1;
                    save_column_settings(file_to_open);
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Bookmark '%c' deleted", arg[0]);
                    attroff(COLOR_PAIR(3));
                } else {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(11));
                    printw(" | No bookmark '%c'", arg[0]);
                    attroff(COLOR_PAIR(11));
                }
                refresh();
            } else if (strcmp(cmd, "rn") == 0) {
                relative_line_numbers = !relative_line_numbers;
                config_save_rn(relative_line_numbers);
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Line numbers: %s", relative_line_numbers ? "relative" : "absolute");
                attroff(COLOR_PAIR(3));
                refresh();
            } else if (strcmp(cmd, "comments") == 0) {
                // :comments on/off — enable/disable skipping lines starting with '#'
                int new_val = skip_comments;
                if (arg && strcmp(arg, "on") == 0)  new_val = 1;
                else if (arg && strcmp(arg, "off") == 0) new_val = 0;
                else new_val = !skip_comments;

                if (new_val != skip_comments) {
                    skip_comments = new_val;
                    // Reload file to apply/cancel filtering of '#' lines
                    for (int i = 0; i < row_count; i++) { free(rows[i].line_cache); rows[i].line_cache = NULL; }
                    csv_mmap_close();
                    if (f) { fclose(f); f = NULL; }
                    f = fopen(file_to_open, "r");
                    if (f) csv_mmap_open(file_to_open);
                    free(rows);
                    rows = build_row_index(f, &row_count);
                    if (!rows || !alloc_row_arrays(row_count)) { /* oom */ }
                    // Re-parse headers (col_count may have changed)
                    reparse_column_names();
                    int cmt_load = load_column_settings(file_to_open);
                    // load_column_settings may have overwritten skip_comments from the old file —
                    // restore the value chosen by the user
                    skip_comments = new_val;
                    // If col_count changed (0 or 2) — settings are stale,
                    // first line after comments is the header
                    if (cmt_load != 1) {
                        use_headers = 1;
                        auto_detect_column_types();
                    }
                    // Save with correct col_count and correct skip_comments
                    save_column_settings(file_to_open);
                    sort_col = -1; sort_order = 0; sort_level_count = 0; sorted_count = 0;
                    filter_active = 0; filtered_count = 0; filter_query[0] = '\0';
                    cur_display_row = 0; top_display_row = 0; cur_col = 0; left_col = 0;
                }
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Comment lines (#): %s  [%d collected]",
                       skip_comments ? "skip" : "show as data", comment_count);
                attroff(COLOR_PAIR(3));
                refresh();
            } else if (strcmp(cmd, "freeze") == 0 || strcmp(cmd, "fz") == 0) {
                // :freeze N — freeze first N columns (:freeze 0 — unfreeze)
                int n = arg ? atoi(arg) : 0;
                if (n < 0) n = 0;
                if (n > col_count) n = col_count;
                freeze_cols = n;
                if (left_col < freeze_cols) left_col = freeze_cols;
                save_column_settings(file_to_open);

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                if (freeze_cols > 0)
                    printw(" | Frozen: %d column%s", freeze_cols, freeze_cols > 1 ? "s" : "");
                else
                    printw(" | Columns unfrozen");
                attroff(COLOR_PAIR(3));
                refresh();
            } else if (strcmp(cmd, "sort") == 0 && arg && *arg) {
                // :sort col1 asc, col2 desc  — multi-level sort
                int new_count = 0;
                SortLevel new_levels[MAX_SORT_LEVELS];
                char sort_arg[256];
                strncpy(sort_arg, arg, sizeof(sort_arg) - 1);
                sort_arg[sizeof(sort_arg) - 1] = '\0';

                char *tok = sort_arg;
                while (*tok && new_count < MAX_SORT_LEVELS) {
                    while (*tok == ' ') tok++;
                    if (!*tok) break;

                    char col_tok[64] = {0};
                    int ci = 0;
                    while (*tok && *tok != ' ' && *tok != ',' && ci < 63)
                        col_tok[ci++] = *tok++;
                    col_tok[ci] = '\0';

                    while (*tok == ' ') tok++;

                    char dir_tok[16] = {0};
                    ci = 0;
                    while (*tok && *tok != ',' && *tok != ' ' && ci < 15)
                        dir_tok[ci++] = *tok++;
                    dir_tok[ci] = '\0';

                    while (*tok == ' ' || *tok == ',') tok++;

                    if (!col_tok[0]) continue;

                    int cidx = use_headers ? col_name_to_num(col_tok) : col_to_num(col_tok);
                    if (cidx < 0) cidx = col_to_num(col_tok);
                    if (cidx < 0 || cidx >= col_count) continue;

                    int order = 1;
                    if (strncasecmp(dir_tok, "desc", 4) == 0 || strcmp(dir_tok, "-1") == 0)
                        order = -1;

                    new_levels[new_count].col   = cidx;
                    new_levels[new_count].order = order;
                    new_count++;
                }

                if (new_count > 0) {
                    sort_level_count = new_count;
                    for (int li = 0; li < new_count; li++)
                        sort_levels[li] = new_levels[li];
                    sort_col   = sort_levels[0].col;
                    sort_order = sort_levels[0].order;

                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Sorting (%d levels)...", sort_level_count);
                    attroff(COLOR_PAIR(3));
                    refresh();
                    build_sorted_index();
                    cur_display_row = 0; top_display_row = 0;
                } else {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(11));
                    printw(" | :sort — column not found");
                    attroff(COLOR_PAIR(11));
                    refresh();
                }
            } else if (strcmp(cmd, "fqu") == 0) {
                // :fqu — quick filter by CURRENT cell ONLY (resets old filter)

                // Get current column name
                char col_name[64] = {0};
                if (use_headers && column_names[cur_col]) {
                    strncpy(col_name, column_names[cur_col], sizeof(col_name) - 1);
                } else {
                    col_letter(cur_col, col_name);
                }

                // Get current cell value
                int real_row = get_real_row(cur_display_row);
                if (real_row < 0 || real_row >= row_count) {
                    mvprintw(LINES - 1, 0, "Invalid row");
                    refresh();
                    getch();
                    continue;
                }

                char *cell = get_column_value(rows[real_row].line_cache,
                                              column_names[cur_col] ? column_names[cur_col] : "",
                                              use_headers);

                if (!cell || !*cell) {
                    mvprintw(LINES - 1, 0, "Empty cell — nothing to filter");
                    free(cell);
                    refresh();
                    getch();
                    continue;
                }

                // Build new filter (this one only)
                char new_filter[256] = {0};
                snprintf(new_filter, sizeof(new_filter), "%s = \"%s\"", col_name, cell);
                free(cell);

                // Reset old filter and set the new one
                strncpy(filter_query, new_filter, sizeof(filter_query) - 1);
                filter_query[sizeof(filter_query) - 1] = '\0';

                // Apply
                in_filter_mode = 1;
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Filtering...               ");
                attroff(COLOR_PAIR(3));
                refresh();

                cur_col = 0;
                left_col = freeze_cols;
                cur_display_row = 0;
                top_display_row = 0;

                apply_filter(rows, f, row_count);
                if (sort_col >= 0 && sort_order != 0) build_sorted_index();
            } else if (strncmp(cmd, "fq", 2) == 0) {
                // Quick filter by current cell with n/o/no modifiers

                char op_eq = ' ';   // default =
                char *logic = "AND"; // default AND

                // Check suffix after fq
                char suffix[4] = {0};
                if (strlen(cmd) > 2) strncpy(suffix, cmd + 2, 3);

                if (strstr(suffix, "n")) op_eq = '!'; // !=
                if (strstr(suffix, "o")) logic = "OR";

                // Get column name
                char col_name[64] = {0};
                if (use_headers && column_names[cur_col]) {
                    strncpy(col_name, column_names[cur_col], sizeof(col_name) - 1);
                } else {
                    col_letter(cur_col, col_name);
                }

                // Get cell value
                int real_row = get_real_row(cur_display_row);
                if (real_row < 0 || real_row >= row_count) {
                    mvprintw(LINES - 1, 0, "Invalid row");
                    refresh();
                    getch();
                    continue;
                }

                char *cell = get_column_value(rows[real_row].line_cache,
                                              column_names[cur_col] ? column_names[cur_col] : "",
                                              use_headers);

                if (!cell || !*cell) {
                    mvprintw(LINES - 1, 0, "Empty cell — nothing to filter");
                    free(cell);
                    refresh();
                    getch();
                    continue;
                }

                // Build condition
                char new_cond[256] = {0};
                snprintf(new_cond, sizeof(new_cond), "%s %c= \"%s\"", col_name, op_eq, cell);
                free(cell);

                // Append to existing filter
                if (filter_query[0] != '\0') {
                    char temp[512];
                    snprintf(temp, sizeof(temp), " %s %s", logic, new_cond);
                    strncat(filter_query, temp, sizeof(filter_query) - strlen(filter_query) - 1);
                } else {
                    strncpy(filter_query, new_cond, sizeof(filter_query) - 1);
                    filter_query[sizeof(filter_query) - 1] = '\0';
                }

                // Apply
                in_filter_mode = 1;
                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Filtering...               ");
                attroff(COLOR_PAIR(3));
                refresh();

                cur_col = 0;
                left_col = freeze_cols;
                cur_display_row = 0;
                top_display_row = 0;

                apply_filter(rows, f, row_count);
                if (sort_col >= 0 && sort_order != 0) build_sorted_index();
            } else if (strcmp(cmd, "dr") == 0 && arg && *arg) {
                int row_num = atoi(arg);
                if (row_num < 1 || row_num > display_count) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Invalid row number: %d (1..%d)", row_num, display_count);
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    clrtoeol();
                    refresh();
                    continue;
                }

                // Real row index in the file
                int display_pos = row_num - 1;
                int real_row = get_real_row(display_pos);

                if (real_row < 0 || real_row >= row_count) {
                    mvprintw(LINES - 1, 0, "Error: invalid real row");
                    refresh();
                    getch();
                    continue;
                }

                // Remove row from cache (free memory)
                free(rows[real_row].line_cache);
                rows[real_row].line_cache = NULL;

                // Shift rows array left
                for (int i = real_row; i < row_count - 1; i++) {
                    rows[i] = rows[i + 1];
                }
                row_count--;

                // If row was in filtered — update filtered_rows
                if (filter_active) {
                    int new_filtered_count = 0;
                    for (int i = 0; i < filtered_count; i++) {
                        if (filtered_rows[i] != real_row) {
                            if (filtered_rows[i] > real_row) filtered_rows[i]--;
                            filtered_rows[new_filtered_count++] = filtered_rows[i];
                        }
                    }
                    filtered_count = new_filtered_count;
                }

                // Rewrite file without the deleted row
                if (save_file(file_to_open, f, rows, row_count) != 0) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Failed to save file after row deletion");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    continue;
                }

                // Re-index offsets (file has changed)
                free(rows);
                rows = NULL;
                row_count = 0;

                csv_mmap_close();
                fclose(f);
                f = fopen(file_to_open, "r");
                if (!f) {
                    mvprintw(LINES - 1, 0, "Failed to reopen file");
                    refresh();
                    getch();
                    continue;
                }
                if (f) csv_mmap_open(file_to_open);

                rows = build_row_index(f, &row_count);
                if (!rows) {
                    mvprintw(LINES - 1, 0, "Out of memory");
                    refresh();
                    getch();
                    continue;
                }

                // Update and save column settings
                save_column_settings(file_to_open);

                // Adjust cursor position (if the deleted row was below cursor — nothing changes)
                if (cur_display_row >= display_count) {
                    cur_display_row = display_count - 1;
                    if (cur_display_row < 0) cur_display_row = 0;
                }

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Row %d deleted", row_num);
                attroff(COLOR_PAIR(3));

                refresh();
            } else if (strcmp(cmd, "cr") == 0) {
                if (!arg || !*arg) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Usage: :cr new_column_name");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    clrtoeol();
                    refresh();
                    continue;
                }

                // Strip quotes if present
                char new_name[64] = {0};
                if (arg[0] == '"' && arg[strlen(arg)-1] == '"') {
                    strncpy(new_name, arg + 1, strlen(arg) - 2);
                    new_name[strlen(arg) - 2] = '\0';
                } else {
                    strncpy(new_name, arg, sizeof(new_name) - 1);
                    new_name[sizeof(new_name) - 1] = '\0';
                }

                if (strlen(new_name) == 0) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Column name cannot be empty");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    continue;
                }

                // Old name for the message
                char old_name[64] = {0};
                if (use_headers && column_names[cur_col]) {
                    strncpy(old_name, column_names[cur_col], sizeof(old_name) - 1);
                } else {
                    col_letter(cur_col, old_name);
                }

                // Free old name (if any)
                if (column_names[cur_col]) {
                    free(column_names[cur_col]);
                }

                // Set the new name
                column_names[cur_col] = strdup(new_name);

                // Rewrite file (header changes)
                if (save_file(file_to_open, f, rows, row_count) != 0) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Failed to save file after rename");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    continue;
                }

                // Re-index offsets after saving
                free(rows);
                rows = NULL;
                row_count = 0;

                csv_mmap_close();
                fclose(f);
                f = fopen(file_to_open, "r");
                if (!f) {
                    mvprintw(LINES - 1, 0, "Failed to reopen file");
                    refresh();
                    getch();
                    continue;
                }
                if (f) csv_mmap_open(file_to_open);

                rows = build_row_index(f, &row_count);
                if (!rows) {
                    mvprintw(LINES - 1, 0, "Out of memory");
                    refresh();
                    getch();
                    continue;
                }

                rebuild_header_row();
                save_file(file_to_open, f, rows, row_count);    

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Column renamed: %s → %s", old_name, new_name);
                attroff(COLOR_PAIR(3));

                refresh();
            } else if (strcmp(cmd, "cs") == 0) {
                // :cs [filename] — save current column to CSV (single column)

                char filename[256] = {0};

                if (arg && *arg) {
                    // Name provided explicitly
                    strncpy(filename, arg, sizeof(filename) - 5);
                    filename[sizeof(filename) - 5] = '\0';
                } else {
                    // Name from column
                    if (use_headers && column_names[cur_col]) {
                        strncpy(filename, column_names[cur_col], sizeof(filename) - 5);
                    } else {
                        col_letter(cur_col, filename);
                    }
                }

                // Append .csv if missing
                if (strstr(filename, ".csv") == NULL) {
                    strcat(filename, ".csv");
                }

                FILE *out = fopen(filename, "w");
                if (!out) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Cannot create '%s'", filename);
                    attroff(COLOR_PAIR(3));
                    refresh();
                    getch();
                    clrtoeol();
                    refresh();
                    continue;
                }

                // Write header (if any)
                if (use_headers && column_names[cur_col]) {
                    fprintf(out, "\"%s\"\n", column_names[cur_col]);
                } else {
                    char letter_buf[16];
                    col_letter(cur_col, letter_buf);
                    fprintf(out, "\"%s\"\n", letter_buf);
                }

                // Write column values
                int row_start = use_headers ? 1 : 0;
                for (int r = row_start; r < row_count; r++) {
                    char *cell = get_column_value(rows[r].line_cache,
                                                  column_names[cur_col] ? column_names[cur_col] : "",
                                                  use_headers);

                    // Escape value
                    char escaped[1024] = {0};
                    int esc_pos = 0;
                    if (cell && *cell) {
                        if (strchr(cell, ',') || strchr(cell, '"') || strchr(cell, '\n')) {
                            escaped[esc_pos++] = '"';
                            for (const char *v = cell; *v; v++) {
                                if (*v == '"') escaped[esc_pos++] = '"';
                                escaped[esc_pos++] = *v;
                            }
                            escaped[esc_pos++] = '"';
                        } else {
                            strcpy(escaped, cell);
                        }
                    }

                    fprintf(out, "%s\n", escaped);
                    free(cell);
                }

                fclose(out);

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Column saved to '%s'", filename);
                attroff(COLOR_PAIR(3));

                refresh();
            } else if (strcmp(cmd, "e") == 0) {
                char filename[256] = {0};

                // If name is given right after the space
                if (arg && *arg) {
                    strncpy(filename, arg, sizeof(filename) - 5);
                    filename[sizeof(filename) - 5] = '\0';
                } else {
                    // Default name
                    if (filter_active) {
                        strcpy(filename, "filtered.csv");
                    } else if (sort_col >= 0) {
                        strcpy(filename, "sorted.csv");
                    } else {
                        strcpy(filename, "table.csv");
                    }

                    // Ask for filename
                    mvprintw(LINES - 1, 0, "Export to: [%s] ", filename);
                    refresh();
                    echo();
                    wgetnstr(stdscr, filename, sizeof(filename) - 1);
                    noecho();
                    clrtoeol();
                }

                // Append .csv if missing
                if (strstr(filename, ".csv") == NULL) {
                    strcat(filename, ".csv");
                }

                FILE *out = fopen(filename, "w");
                if (!out) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Cannot create '%s'", filename);
                    attroff(COLOR_PAIR(3));

                    refresh();
                    getch();
                    clrtoeol();
                    refresh();
                    continue;
                }

                // Write header (if any)
                if (use_headers && row_count > 0) {
                    char *header = rows[0].line_cache ? rows[0].line_cache : "";
                    if (!rows[0].line_cache) {
                        fseek(f, rows[0].offset, SEEK_SET);
                        char buf[MAX_LINE_LEN];
                        if (fgets(buf, sizeof(buf), f)) {
                            size_t len = strlen(buf);
                            if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
                            header = buf;
                        }
                    }
                    fprintf(out, "%s\n", header);
                }

                // Write rows (filtered or all)
                int count_exported = 0;
                int display_count = filter_active ? filtered_count : row_count;
                int row_start = use_headers ? 1 : 0;

                for (int i = 0; i < display_count; i++) {
                    int display_pos = i;
                    int real_row = get_real_row(display_pos);
                    if (real_row < row_start) continue;

                    if (!rows[real_row].line_cache) {
                        if (g_mmap_base) {
                            char mmap_buf[MAX_LINE_LEN];
                            char *ml = csv_mmap_get_line(rows[real_row].offset, mmap_buf, sizeof(mmap_buf));
                            rows[real_row].line_cache = strdup(ml ? ml : "");
                        } else {
                            fseek(f, rows[real_row].offset, SEEK_SET);
                            char line_buf[MAX_LINE_LEN];
                            if (fgets(line_buf, sizeof(line_buf), f)) {
                                line_buf[strcspn(line_buf, "\n")] = '\0';
                                rows[real_row].line_cache = strdup(line_buf);
                            } else {
                                rows[real_row].line_cache = strdup("");
                            }
                        }
                    }

                    fprintf(out, "%s\n", rows[real_row].line_cache);
                    count_exported++;
                }

                fclose(out);

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Exported %d rows to '%s'", count_exported, filename);
                attroff(COLOR_PAIR(3));

                refresh();
                getch();
                clrtoeol();
                refresh();
            } else {
                /* :+N / :-N — relative jump from current row */
                int target = -1;
                if ((cmd[0] == '+' || cmd[0] == '-') && cmd[1] != '\0') {
                    int ok = 1;
                    for (const char *p = cmd + 1; *p; p++)
                        if (*p < '0' || *p > '9') { ok = 0; break; }
                    if (ok) {
                        int delta = atoi(cmd + 1);
                        target = cur_display_row + (cmd[0] == '+' ? delta : -delta);
                    }
                }

                /* :N — absolute jump (1-based) */
                if (target < 0) {
                    int is_num = (cmd[0] != '\0');
                    for (const char *p = cmd; *p; p++)
                        if (*p < '0' || *p > '9') { is_num = 0; break; }
                    if (is_num) target = atoi(cmd) - 1;
                }

                if (target >= 0) {
                    if (target >= display_count) target = display_count - 1;
                    if (target < 0) target = 0;
                    cur_display_row = target;
                    if (cur_display_row < top_display_row)
                        top_display_row = cur_display_row;
                    else if (cur_display_row >= top_display_row + visible_rows)
                        top_display_row = cur_display_row - visible_rows + 1;
                    if (top_display_row < 0) top_display_row = 0;
                }
            }

            clrtoeol();
            refresh();
        }
        // Update real row number
        cur_real_row = get_real_row(cur_display_row);
    }

    for (int i = 0; i < col_count; i++) {
        free(column_names[i]);
    }
    free(rows);
    csv_mmap_close();
    fclose(f);
    endwin();
    return 0;
}