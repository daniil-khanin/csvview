#include "bookmarks.h"
#include "csvview_defs.h"
#include "csv_mmap.h"
#include "column_format.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM_PREVIEW_W  14   /* max chars per preview column value */

/* Read raw line for real_row into buf: cache → mmap → fseek. */
static void bm_get_line(int real_row, char *buf, int bufsz)
{
    if (rows[real_row].line_cache) {
        strncpy(buf, rows[real_row].line_cache, bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }
    if (g_mmap_base) {
        char *ml = csv_mmap_get_line((long)rows[real_row].offset, buf, bufsz);
        if (ml) {
            if (!rows[real_row].line_cache)
                rows[real_row].line_cache = strdup(buf);
            return;
        }
    }
    if (f) {
        fseek(f, rows[real_row].offset, SEEK_SET);
        if (fgets(buf, bufsz, f)) {
            buf[strcspn(buf, "\r\n")] = '\0';
            return;
        }
    }
    buf[0] = '\0';
}

/* Extract field idx from a delimited line into out[outsz]. */
static void bm_get_field(const char *line, int idx, char *out, int outsz)
{
    int field = 0, in_q = 0;
    char *o = out, *end = out + outsz - 1;
    for (const char *p = line; *p; p++) {
        if (*p == '"') { in_q = !in_q; continue; }
        if (!in_q && *p == csv_delimiter) {
            if (field == idx) { *o = '\0'; return; }
            field++;
            o = out;
            continue;
        }
        if (field == idx && o < end) *o++ = *p;
    }
    *o = '\0';
}

/* Format row number with thousands separators: 1234567 → "1,234,567". */
static void bm_fmt_rownum(long n, char *buf, int sz)
{
    if (n >= 1000000L)
        snprintf(buf, sz, "%ld,%03ld,%03ld", n/1000000, (n/1000)%1000, n%1000);
    else if (n >= 1000L)
        snprintf(buf, sz, "%ld,%03ld", n/1000, n%1000);
    else
        snprintf(buf, sz, "%ld", n);
}

int show_marks_window(const char *csv_filename)
{
    /* Collect set bookmarks in letter order. */
    int bm_letters[26];
    int bm_count = 0;
    for (int i = 0; i < 26; i++) {
        if (bookmarks[i] >= 0)
            bm_letters[bm_count++] = i;
    }

    if (bm_count == 0) {
        attron(COLOR_PAIR(11));
        mvprintw(getmaxy(stdscr) - 1, 2, " No bookmarks set ");
        attroff(COLOR_PAIR(11));
        refresh();
        getch();
        return -1;
    }

    int scr_h = getmaxy(stdscr);
    int scr_w = getmaxx(stdscr);

    /* Window sizing: fit all rows if possible, cap at screen - 4. */
    int win_w = scr_w - 8;
    if (win_w < 60) win_w = 60;
    if (win_w > scr_w - 4) win_w = scr_w - 4;

    /* Layout: border×2 + info row + separator + data rows + footer/border */
    int max_visible = scr_h - 8;
    if (max_visible < 1) max_visible = 1;
    int visible = bm_count < max_visible ? bm_count : max_visible;
    int win_h = visible + 5;   /* top border + info + sep + data + bottom border */

    int win_y = (scr_h - win_h) / 2;
    int win_x = (scr_w - win_w) / 2;

    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    if (!win) return -1;
    wbkgd(win, COLOR_PAIR(1));
    keypad(win, TRUE);

    /* How many preview columns fit: prefix "  a  →  row XXXXXXXXX  " = 22 chars */
    int prefix_w = 22;
    int avail    = win_w - 2 - prefix_w;
    int nprev    = avail / (BM_PREVIEW_W + 2);
    if (nprev > col_count) nprev = col_count;
    if (nprev > 5) nprev = 5;

    int sel    = 0;
    int top    = 0;
    int result = -1;

    char linebuf[MAX_LINE_LEN];
    char field[BM_PREVIEW_W + 2];
    char rownum[20];

    while (1) {
        werase(win);

        /* Border */
        wattron(win, COLOR_PAIR(6));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(6));

        /* Title */
        wattron(win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(win, 0, (win_w - 12) / 2, " Bookmarks ");
        wattroff(win, COLOR_PAIR(3) | A_BOLD);

        /* Info row */
        wattron(win, COLOR_PAIR(6));
        mvwprintw(win, 1, 2, "%d bookmark%s", bm_count, bm_count == 1 ? "" : "s");
        wattroff(win, COLOR_PAIR(6));

        /* Separator with T-junctions */
        wattron(win, COLOR_PAIR(6));
        mvwaddch(win, 2, 0, ACS_LTEE);
        mvwhline(win, 2, 1, ACS_HLINE, win_w - 2);
        mvwaddch(win, 2, win_w - 1, ACS_RTEE);
        wattroff(win, COLOR_PAIR(6));

        /* Data rows */
        for (int i = 0; i < visible && (top + i) < bm_count; i++) {
            int idx      = top + i;
            int bi       = bm_letters[idx];
            int real_row = bookmarks[bi];
            int is_sel   = (idx == sel);
            int row_y    = 3 + i;

            long disp_num = (long)real_row - (use_headers ? 1 : 0) + 1;
            bm_fmt_rownum(disp_num, rownum, sizeof(rownum));

            bm_get_line(real_row, linebuf, sizeof(linebuf));

            /* Full-row highlight for selected */
            if (is_sel) {
                wattron(win, A_REVERSE);
                mvwhline(win, row_y, 1, ' ', win_w - 2);
            }

            /* Letter — accent color (works over A_REVERSE too) */
            if (!is_sel) wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, row_y, 2, "%c", 'a' + bi);
            if (!is_sel) wattroff(win, COLOR_PAIR(3) | A_BOLD);

            /* Arrow + row number */
            mvwprintw(win, row_y, 4, "->  row %-10s", rownum);

            /* Preview fields */
            int px = prefix_w + 2;
            for (int c = 0; c < nprev; c++) {
                bm_get_field(linebuf, c, field, sizeof(field));
                /* Truncate to BM_PREVIEW_W */
                if ((int)strlen(field) > BM_PREVIEW_W) {
                    field[BM_PREVIEW_W - 1] = '>';
                    field[BM_PREVIEW_W]     = '\0';
                }
                mvwprintw(win, row_y, px, "%-*s", BM_PREVIEW_W + 1, field);
                px += BM_PREVIEW_W + 2;
            }

            if (is_sel) wattroff(win, A_REVERSE);
        }

        /* Footer on bottom border */
        wattron(win, COLOR_PAIR(6));
        mvwprintw(win, win_h - 1, 2,
                  "j/k ↑↓ — select  Enter — jump  d — delete  q/Esc — close");
        wattroff(win, COLOR_PAIR(6));

        /* Scroll indicator */
        if (bm_count > visible) {
            int pct = (sel >= bm_count - 1) ? 100 : (int)(100 * sel / (bm_count - 1));
            wattron(win, COLOR_PAIR(6));
            mvwprintw(win, win_h - 1, win_w - 8, " %3d%% ", pct);
            wattroff(win, COLOR_PAIR(6));
        }

        wrefresh(win);

        int ch = wgetch(win);

        if (ch == 'q' || ch == 'Q' || ch == 27) {
            break;
        } else if (ch == KEY_UP || ch == 'k') {
            if (sel > 0) {
                sel--;
                if (sel < top) top = sel;
            }
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (sel < bm_count - 1) {
                sel++;
                if (sel >= top + visible) top = sel - visible + 1;
            }
        } else if (ch == KEY_HOME) {
            sel = 0; top = 0;
        } else if (ch == KEY_END) {
            sel = bm_count - 1;
            top = sel - visible + 1;
            if (top < 0) top = 0;
        } else if (ch == '\n' || ch == KEY_ENTER || ch == '\r') {
            result = bookmarks[bm_letters[sel]];
            break;
        } else if (ch == 'd') {
            /* Delete selected bookmark */
            int bi = bm_letters[sel];
            bookmarks[bi] = -1;
            if (csv_filename) save_column_settings(csv_filename);

            /* Rebuild active list */
            bm_count = 0;
            for (int i = 0; i < 26; i++) {
                if (bookmarks[i] >= 0)
                    bm_letters[bm_count++] = i;
            }
            if (bm_count == 0) break;   /* last bookmark deleted — close */

            if (sel >= bm_count) sel = bm_count - 1;
            visible = bm_count < max_visible ? bm_count : max_visible;
            if (top > bm_count - visible) top = bm_count - visible;
            if (top < 0) top = 0;
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();

    return result;
}
