/**
 * row_detail.c
 *
 * Row detail view — popup window showing all fields of the current row
 * as a two-column table: column name | full cell value with word wrapping.
 */

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "csvview_defs.h"
#include "csv_mmap.h"
#include "utils.h"
#include "file_format.h"
#include "row_detail.h"

/* ── word-wrap helper ─────────────────────────────────────────────── */

/* A single visual line produced by word-wrapping a cell value. */
typedef struct {
    int  field_idx;     /* which column this belongs to (-1 = continuation) */
    char text[1024];    /* the wrapped line fragment */
} DetailLine;

/*
 * Build an array of DetailLines by iterating over all fields and
 * word-wrapping their values to fit in `val_width` display columns.
 * Returns the count via *out_count.  Caller must free() the result.
 */
static DetailLine *build_lines(char **fields, int fc, int val_width,
                               int *out_count)
{
    int cap = fc * 4;           /* initial estimate */
    int cnt = 0;
    DetailLine *lines = malloc(cap * sizeof(DetailLine));
    if (!lines) { *out_count = 0; return NULL; }

    for (int i = 0; i < fc && i < col_count; i++) {
        const char *val = (fields && i < fc && fields[i]) ? fields[i] : "";
        int vlen = (int)strlen(val);

        if (vlen == 0) {
            /* empty value — one line */
            if (cnt >= cap) { cap *= 2; lines = realloc(lines, cap * sizeof(DetailLine)); }
            lines[cnt].field_idx = i;
            lines[cnt].text[0] = '\0';
            cnt++;
            continue;
        }

        /* Walk through the value string, breaking at val_width display cols */
        const char *p = val;
        int first = 1;
        while (*p) {
            if (cnt >= cap) { cap *= 2; lines = realloc(lines, cap * sizeof(DetailLine)); }
            lines[cnt].field_idx = first ? i : -1;
            first = 0;

            /* Copy up to val_width display columns */
            int dcols = 0;
            int bytes = 0;
            const char *start = p;
            while (p[bytes] && dcols < val_width) {
                unsigned char c = (unsigned char)p[bytes];
                int charlen = 1;
                if (c >= 0xF0) charlen = 4;
                else if (c >= 0xE0) charlen = 3;
                else if (c >= 0xC0) charlen = 2;

                /* peek display width of this character */
                char tmp[8];
                int tl = charlen;
                if (tl > (int)sizeof(tmp)-1) tl = (int)sizeof(tmp)-1;
                memcpy(tmp, p + bytes, tl);
                tmp[tl] = '\0';
                int cw = utf8_display_width(tmp);
                if (cw == 0) cw = 1;

                if (dcols + cw > val_width) break;
                dcols += cw;
                bytes += charlen;
            }

            int copy = bytes;
            if (copy > (int)sizeof(lines[cnt].text) - 1)
                copy = (int)sizeof(lines[cnt].text) - 1;
            memcpy(lines[cnt].text, start, copy);
            lines[cnt].text[copy] = '\0';
            p += bytes;
            cnt++;
        }
    }

    *out_count = cnt;
    return lines;
}

/* ── public function ──────────────────────────────────────────────── */

void show_row_detail(int real_row)
{
    if (real_row < 0 || real_row >= row_count) return;

    /* ensure line is cached */
    if (!rows[real_row].line_cache) {
        char line_buf[MAX_LINE_LEN];
        if (csv_mmap_get_line(rows[real_row].offset, line_buf, sizeof(line_buf))) {
            rows[real_row].line_cache = strdup(line_buf);
        } else {
            rows[real_row].line_cache = strdup("");
        }
    }

    /* parse fields */
    int fc = 0;
    char **fields = g_fmt ? g_fmt->parse_row(rows[real_row].line_cache, &fc)
                          : parse_csv_line(rows[real_row].line_cache, &fc);

    /* window dimensions */
    int win_h = LINES - 4;
    if (win_h < 10) win_h = 10;
    int win_w = COLS - 8;
    if (win_w > 140) win_w = 140;
    if (win_w < 40) win_w = 40;
    int start_y = (LINES - win_h) / 2;
    int start_x = (COLS - win_w) / 2;

    /* left column width: max column name length, capped at 35% of window */
    int max_name_w = 6;  /* minimum "Column" header width */
    for (int i = 0; i < col_count && i < fc; i++) {
        const char *name = NULL;
        if ((use_headers || (g_fmt && !g_fmt->has_header_row)) && column_names[i])
            name = column_names[i];
        if (name) {
            int w = utf8_display_width(name);
            if (w > max_name_w) max_name_w = w;
        }
    }
    int left_w = max_name_w + 2;  /* +2 padding */
    int max_left = (win_w - 5) * 35 / 100;
    if (left_w > max_left) left_w = max_left;
    if (left_w < 8) left_w = 8;

    int val_width = win_w - left_w - 6;  /* right column usable chars */
    if (val_width < 10) val_width = 10;

    /* build wrapped lines */
    int total_lines = 0;
    DetailLine *dlines = build_lines(fields, fc, val_width, &total_lines);
    if (!dlines) {
        if (fields) free_csv_fields(fields, fc);
        return;
    }

    /* create window */
    WINDOW *win = newwin(win_h, win_w, start_y, start_x);
    if (!win) {
        free(dlines);
        if (fields) free_csv_fields(fields, fc);
        return;
    }
    wbkgd(win, COLOR_PAIR(1));
    keypad(win, TRUE);

    int top_line = 0;
    int visible = win_h - 4;  /* rows available for data (borders + title + hint) */
    if (visible < 1) visible = 1;

    /* display row number for title */
    int display_row_num = real_row - (use_headers ? 1 : 0) + 1;
    if (g_fmt && !g_fmt->has_header_row)
        display_row_num = real_row + 1;

    while (1) {
        werase(win);

        /* border */
        wattron(win, COLOR_PAIR(6));
        draw_rounded_box(win);
        wattroff(win, COLOR_PAIR(6));

        /* title */
        {
            char title[64];
            snprintf(title, sizeof(title), " Row %d ", display_row_num);
            wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, 0, (win_w - (int)strlen(title)) / 2, "%s", title);
            wattroff(win, COLOR_PAIR(3) | A_BOLD);
        }

        /* column header line */
        wattron(win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(win, 1, 2, "%-*s", left_w, "Column");
        mvwprintw(win, 1, 2 + left_w + 1, "Value");
        wattroff(win, COLOR_PAIR(3) | A_BOLD);

        /* separator */
        wattron(win, COLOR_PAIR(6));
        mvwhline(win, 2, 1, ACS_HLINE, win_w - 2);
        wattroff(win, COLOR_PAIR(6));

        /* render visible lines */
        int y = 3;
        for (int i = top_line; i < total_lines && y < win_h - 1; i++, y++) {
            int fi = dlines[i].field_idx;

            /* column name (only on first line of a field) */
            if (fi >= 0) {
                char namebuf[128];
                if ((use_headers || (g_fmt && !g_fmt->has_header_row)) && column_names[fi])
                    snprintf(namebuf, sizeof(namebuf), "%s", column_names[fi]);
                else
                    col_letter(fi, namebuf);

                wattron(win, COLOR_PAIR(5) | A_BOLD);
                mvwprintw(win, y, 2, "%-*.*s", left_w, left_w, namebuf);
                wattroff(win, COLOR_PAIR(5) | A_BOLD);
            } else {
                /* continuation line — blank left column */
                mvwprintw(win, y, 2, "%*s", left_w, "");
            }

            /* vertical separator */
            wattron(win, COLOR_PAIR(6));
            mvwaddch(win, y, 2 + left_w, ACS_VLINE);
            wattroff(win, COLOR_PAIR(6));

            /* value */
            wattron(win, COLOR_PAIR(1));
            mvwprintw(win, y, 2 + left_w + 2, "%s", dlines[i].text);
            wattroff(win, COLOR_PAIR(1));
        }

        /* hint bar */
        wattron(win, COLOR_PAIR(6));
        mvwprintw(win, win_h - 1, 2,
                  "[ ↑↓/jk scroll | PgUp/PgDn page | Home/End | Esc/q close ]");
        wattroff(win, COLOR_PAIR(6));

        wrefresh(win);

        /* input */
        int ch = wgetch(win);
        int max_top = total_lines - visible;
        if (max_top < 0) max_top = 0;

        if (ch == KEY_UP || ch == 'k') {
            if (top_line > 0) top_line--;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (top_line < max_top) top_line++;
        } else if (ch == KEY_PPAGE) {
            top_line -= visible;
            if (top_line < 0) top_line = 0;
        } else if (ch == KEY_NPAGE) {
            top_line += visible;
            if (top_line > max_top) top_line = max_top;
        } else if (ch == KEY_HOME) {
            top_line = 0;
        } else if (ch == KEY_END) {
            top_line = max_top;
        } else if (ch == 27 || ch == 'q' || ch == 'Q' || ch == 'v') {
            break;
        }
    }

    delwin(win);
    free(dlines);
    if (fields) free_csv_fields(fields, fc);
    touchwin(stdscr);
    refresh();
}
