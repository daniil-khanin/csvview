/**
 * ui_draw.c
 *
 * Implementation of table interface and UI element rendering functions
 */

#include "ui_draw.h"
#include "utils.h"          // col_letter, format_cell_value, truncate_string, etc.
#include "csvview_defs.h"   // globals (cur_col, cur_display_row, etc.)

#include <ncurses.h>        // all ncurses functions
#include <string.h>         // strlen, strncpy
#include <stdio.h>          // snprintf

#include "column_format.h"   // for format_cell_value()
#include "sorting.h"         // for get_real_row()
#include "filtering.h"       // for apply_filter()

#include <stdint.h>          // uint32_t

/* ── RTL (right-to-left) text helpers ───────────────────────────────────────
   Terminals render text left-to-right. Arabic/Hebrew/Farsi text is stored in
   logical order (right-to-left reading), so we reverse codepoints for display
   and right-align the cell — the visual result matches natural RTL reading.    */

static uint32_t rtl_next_cp(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    if (!*s) return 0;
    uint32_t cp;
    if      (*s < 0x80) { cp = *s++; }
    else if (*s < 0xE0) { cp = (uint32_t)(*s++ & 0x1F) << 6;
                          cp |= (*s++ & 0x3F); }
    else if (*s < 0xF0) { cp = (uint32_t)(*s++ & 0x0F) << 12;
                          cp |= (uint32_t)(*s++ & 0x3F) << 6;
                          cp |= (*s++ & 0x3F); }
    else                { cp = (uint32_t)(*s++ & 0x07) << 18;
                          cp |= (uint32_t)(*s++ & 0x3F) << 12;
                          cp |= (uint32_t)(*s++ & 0x3F) << 6;
                          cp |= (*s++ & 0x3F); }
    *p = (const char *)s;
    return cp;
}

static int is_rtl_cp(uint32_t cp)
{
    return (cp >= 0x0590 && cp <= 0x05FF) ||   /* Hebrew */
           (cp >= 0x0600 && cp <= 0x06FF) ||   /* Arabic */
           (cp >= 0x0750 && cp <= 0x077F) ||   /* Arabic Supplement */
           (cp >= 0x08A0 && cp <= 0x08FF) ||   /* Arabic Extended-A */
           (cp >= 0xFB1D && cp <= 0xFB4F) ||   /* Hebrew Presentation Forms */
           (cp >= 0xFB50 && cp <= 0xFDFF) ||   /* Arabic Presentation Forms-A */
           (cp >= 0xFE70 && cp <= 0xFEFF);     /* Arabic Presentation Forms-B */
}

static int str_has_rtl(const char *s)
{
    const char *p = s;
    uint32_t cp;
    while ((cp = rtl_next_cp(&p)) != 0)
        if (is_rtl_cp(cp)) return 1;
    return 0;
}

/* ── end RTL helpers ─────────────────────────────────────────────────────── */

// ────────────────────────────────────────────────
// Menu rendering
// ────────────────────────────────────────────────

void draw_menu(int y, int x, int w, int menu_type)
{
    attron(COLOR_PAIR(1));

    if (menu_type == 1) {
        mvprintw(y, x, " q: Quit | s: Save | t: Columns | d: Stats | p: Pivot | g: Graph");
    } else {
        mvprintw(y, x, " :q Back | :e Export | :o Settings | Enter: drill-down");
    }

    mvprintw(y, w - 15, "?: Help |");

    attroff(COLOR_PAIR(1));

    attron(COLOR_PAIR(3));
    mvprintw(y, w - 6, " v.%d", CSVVIEW_VERSION);
    attroff(COLOR_PAIR(3));
}

// ────────────────────────────────────────────────
// Enlarged cell window
// ────────────────────────────────────────────────

void draw_cell_view(int y, const char *col_name, int row_num, const char *raw_content, int width)
{
    attron(COLOR_PAIR(6));
    mvaddch(y - 1, 0, ACS_ULCORNER);
    for (int i = 1; i < width - 1; i++) {
        mvaddch(y - 1, i, ACS_HLINE);
    }
    mvaddch(y - 1, width - 1, ACS_URCORNER);

    mvaddch(y, 0, ACS_VLINE);
    mvaddch(y, width - 1, ACS_VLINE);
    attroff(COLOR_PAIR(6));

    // Format the value
    char *display_content = format_cell_value(raw_content, cur_col);

    if (in_search_mode) {
        attron(COLOR_PAIR(5));
        mvprintw(y, 2, "/%s", search_query);
        attroff(COLOR_PAIR(5));
    } else if (in_filter_mode) {
        attron(COLOR_PAIR(5));
        mvprintw(y, 2, "F:%s", filter_query);
        attroff(COLOR_PAIR(5));
    } else {
        /* Truncate col_name to at most 12 display columns (UTF-8 aware) */
        #define COL_LABEL_MAX 12
        char col_trunc[64];
        {
            int cols = 0, bytes = 0;
            const char *s = col_name;
            int truncated = 0;
            while (*s && cols < COL_LABEL_MAX && bytes < (int)sizeof(col_trunc) - 4) {
                unsigned char c = (unsigned char)*s;
                int cb = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                if (bytes + cb >= (int)sizeof(col_trunc) - 4) break;
                memcpy(col_trunc + bytes, s, cb);
                bytes += cb; s += cb; cols++;
            }
            truncated = (*s != '\0' && cols == COL_LABEL_MAX);
            if (truncated) {
                /* append UTF-8 ellipsis U+2026 */
                memcpy(col_trunc + bytes, "\xe2\x80\xa6", 3);
                bytes += 3;
            }
            col_trunc[bytes] = '\0';
        }
        #undef COL_LABEL_MAX

        /* Label (col name + row number) in dim color, content in bright */
        char label[80];
        snprintf(label, sizeof(label), "%s %d: ", col_trunc, row_num);
        attron(COLOR_PAIR(6));
        mvprintw(y, 2, "%s", label);
        attroff(COLOR_PAIR(6));
        /* Use actual cursor position — correct for any multibyte encoding */
        int content_x = getcurx(stdscr);
        int content_w = width - content_x - 2;
        if (content_w > 0) {
            attron(COLOR_PAIR(5) | A_BOLD);
            mvprintw(y, content_x, "%.*s", content_w, display_content);
            attroff(COLOR_PAIR(5) | A_BOLD);
        }
    }

    free(display_content);
}

// ────────────────────────────────────────────────
// Table outer border
// ────────────────────────────────────────────────

void draw_table_border(int top, int height, int width)
{
    attron(COLOR_PAIR(6));

    mvaddch(top, 0, ACS_LTEE);
    mvaddch(top, width - 1, ACS_RTEE);
    mvaddch(top + height - 1, 0, ACS_LLCORNER);
    mvaddch(top + height - 1, width - 1, ACS_LRCORNER);

    for (int x = 1; x < width - 1; x++) {
        mvaddch(top, x, ACS_HLINE);
        mvaddch(top + height - 1, x, ACS_HLINE);
    }

    for (int y = top + 1; y < top + height - 1; y++) {
        mvaddch(y, 0, ACS_VLINE);
        mvaddch(y, width - 1, ACS_VLINE);
    }

    attroff(COLOR_PAIR(6));
}

// ────────────────────────────────────────────────
// Table headers
// ────────────────────────────────────────────────
// Helper: draw a single column header
static void draw_one_header(int top, int current_x, int col_idx, int cur_col)
{
    char name[64] = {0};
    if (use_headers && column_names[col_idx]) {
        strncpy(name, column_names[col_idx], sizeof(name) - 6);
        name[sizeof(name) - 6] = '\0';
    } else {
        col_letter(col_idx, name);
    }

    char arrow_buf[8] = "";
    int arrow_pair = 0;
    for (int lv = 0; lv < sort_level_count; lv++) {
        if (sort_levels[lv].col == col_idx) {
            const char *dir = sort_levels[lv].order > 0 ? " ↑" : " ↓";
            if (sort_level_count > 1)
                snprintf(arrow_buf, sizeof(arrow_buf), "%s%d", dir, lv + 1);
            else
                snprintf(arrow_buf, sizeof(arrow_buf), "%s", dir);
            arrow_pair = 3;
            break;
        }
    }
    const char *arrow = arrow_buf;

    const char *fmt = (col_types[col_idx] == COL_NUM) ? "%*s" : "%-*s";
    char *disp = truncate_for_display(name, col_widths[col_idx] - 2);

    int marked = graph_marked[col_idx];
    if (col_idx == cur_col)
        attron(COLOR_PAIR(3) | A_BOLD | (marked ? A_UNDERLINE : 0));
    else
        attron(COLOR_PAIR(6) | A_BOLD | (marked ? A_UNDERLINE : 0));

    mvprintw(top + 1, current_x, fmt, col_widths[col_idx] - 2, disp);

    if (*arrow) {
        int arrow_x = current_x + col_widths[col_idx] - (int)strlen(arrow);
        if (arrow_pair) attron(COLOR_PAIR(arrow_pair) | A_BOLD);
        mvprintw(top + 1, arrow_x, "%s", arrow);
        if (arrow_pair) attroff(COLOR_PAIR(arrow_pair) | A_BOLD);
    }

    attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD | A_UNDERLINE);
}

void draw_table_headers(int top, int offset __attribute__((unused)), int visible_cols, int left_col, int cur_col)
{
    attron(COLOR_PAIR(6) | A_BOLD);
    mvprintw(top + 1, 1, "%*s", ROW_NUMBER_WIDTH, " ");
    attroff(COLOR_PAIR(6) | A_BOLD);

    int current_x = ROW_NUMBER_WIDTH + 2;

    // === Frozen columns (0..freeze_cols-1) ===
    for (int fc = 0; fc < freeze_cols && fc < col_count; fc++) {
        if (col_hidden[fc]) continue;
        draw_one_header(top, current_x, fc, cur_col);
        current_x += col_widths[fc] + 2;
    }

    // === Freeze separator ===
    if (freeze_cols > 0 && freeze_cols < col_count) {
        attron(COLOR_PAIR(6) | A_BOLD);
        mvaddch(top + 1, current_x - 1, ACS_VLINE);
        attroff(COLOR_PAIR(6) | A_BOLD);
    }

    // === Scrollable columns, starting from left_col ===
    int scr_w = getmaxx(stdscr);
    int right_border = scr_w - 2; /* last usable column (border lives at scr_w-1) */
    int col_idx = left_col;
    int drawn_sc = 0;
    while (col_idx < col_count) {
        if (col_hidden[col_idx]) { col_idx++; continue; }
        if (drawn_sc >= visible_cols) break;
        if (current_x >= right_border) break; /* don't draw past the right border */
        draw_one_header(top, current_x, col_idx, cur_col);
        current_x += col_widths[col_idx] + 2;
        col_idx++;
        drawn_sc++;
    }

    attroff(COLOR_PAIR(6) | A_BOLD);
}

// ────────────────────────────────────────────────
// Table body (visible rows)
// ────────────────────────────────────────────────
void draw_table_body(int top, int offset __attribute__((unused)), int visible_rows,
                     int top_display_row, int cur_display_row, int cur_col,
                     int left_col, int visible_cols, RowIndex *rows, FILE *f, int row_count)
{
    int display_count = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));

    /* check if any bookmark is set (for gutter display) */
    int any_bm = 0;
    for (int bi = 0; bi < 26; bi++) if (bookmarks[bi] >= 0) { any_bm = 1; break; }

    for (int i = 0; i < visible_rows && top_display_row + i < display_count; i++)
    {
        int display_pos = top_display_row + i;
        int real_row = get_real_row(display_pos);

        // Highlight the row number
        int is_cur = (display_pos == cur_display_row);

        if (any_bm) {
            /* find bookmark letter for this real row */
            char bm_letter = ' ';
            for (int bi = 0; bi < 26; bi++) {
                if (bookmarks[bi] == real_row) { bm_letter = 'a' + bi; break; }
            }
            /* gutter: bookmark letter at col 1 */
            if (bm_letter != ' ') {
                attron(COLOR_PAIR(3) | A_BOLD);
            } else {
                attron(COLOR_PAIR(6));
            }
            mvprintw(top + 2 + i, 1, "%c", bm_letter);
            attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD);
            /* row number shifted right by 1, one char narrower */
            if (is_cur) attron(COLOR_PAIR(3) | A_BOLD);
            else        attron(COLOR_PAIR(6));
            if (relative_line_numbers && !is_cur) {
                int rel = display_pos - cur_display_row;
                if (rel < 0) rel = -rel;
                mvprintw(top + 2 + i, 2, "%*d", ROW_NUMBER_WIDTH - 3, rel);
            } else {
                mvprintw(top + 2 + i, 2, "%*d", ROW_NUMBER_WIDTH - 3, display_pos + 1);
            }
            attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD);
        } else {
            if (is_cur) attron(COLOR_PAIR(3) | A_BOLD);
            else        attron(COLOR_PAIR(6));
            if (relative_line_numbers && !is_cur) {
                int rel = display_pos - cur_display_row;
                if (rel < 0) rel = -rel;
                mvprintw(top + 2 + i, 1, "%*d", ROW_NUMBER_WIDTH - 2, rel);
            } else {
                mvprintw(top + 2 + i, 1, "%*d", ROW_NUMBER_WIDTH - 2, display_pos + 1);
            }
            attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD);
        }

        // Lazy row loading
        if (!rows[real_row].line_cache)
        {
            fseek(f, rows[real_row].offset, SEEK_SET);
            char line_buf[MAX_LINE_LEN];
            if (fgets(line_buf, sizeof(line_buf), f))
            {
                line_buf[strcspn(line_buf, "\r\n")] = '\0';
                rows[real_row].line_cache = strdup(line_buf);
            }
            else
            {
                rows[real_row].line_cache = strdup("");
            }
        }

        // Parse the row using the new function
        int field_count = 0;
        char **fields = parse_csv_line(rows[real_row].line_cache, &field_count);

        if (!fields) {
            // on parse error — draw an empty row
            continue;
        }

        int current_x = ROW_NUMBER_WIDTH + 2;
        int row_y = top + 2 + i;

        // === Frozen columns (0..freeze_cols-1) ===
        for (int fc = 0; fc < freeze_cols && fc < col_count; fc++) {
            if (col_hidden[fc]) continue;

            const char *raw = (fc < field_count) ? fields[fc] : "";

            char display_cell[MAX_LINE_LEN];
            strncpy(display_cell, raw, sizeof(display_cell) - 1);
            display_cell[sizeof(display_cell) - 1] = '\0';
            if (strlen(display_cell) > 200) strcpy(display_cell, "(very long text)");

            char *display_val = format_cell_value(display_cell, fc);

            int is_current_cell = (display_pos == cur_display_row && fc == cur_col);
            int is_current_row  = (display_pos == cur_display_row);
            int is_current_col  = (fc == cur_col);

            if (is_current_cell)          attron(COLOR_PAIR(2));
            else if (is_current_row || is_current_col) attron(COLOR_PAIR(1));
            else                          attron(COLOR_PAIR(8));

            char *disp = truncate_for_display(display_val, col_widths[fc] - 2);
            {
                int cell_w = col_widths[fc] - 2;
                if (col_types[fc] != COL_NUM && str_has_rtl(disp)) {
                    /* RTL text: right-align using real display columns.
                       No BiDi control characters — they render as visible
                       glyphs in many terminals, causing 1-col shift. */
                    int text_w = utf8_display_width(disp);
                    int pad = (cell_w > text_w) ? cell_w - text_w : 0;
                    mvprintw(row_y, current_x, "%*s%s", pad, "", disp);
                } else if (col_types[fc] == COL_NUM) {
                    mvprintw(row_y, current_x, "%*s", cell_w, disp);
                } else {
                    /* Left-align with display-width padding (handles multi-byte) */
                    int text_w = utf8_display_width(disp);
                    int pad = (cell_w > text_w) ? cell_w - text_w : 0;
                    mvprintw(row_y, current_x, "%s%*s", disp, pad, "");
                }
            }

            current_x += col_widths[fc] + 2;
            attroff(COLOR_PAIR(2) | COLOR_PAIR(8) | COLOR_PAIR(1));
            free(display_val);
        }

        // === Freeze separator ===
        if (freeze_cols > 0 && freeze_cols < col_count) {
            attron(COLOR_PAIR(6));
            mvaddch(row_y, current_x - 1, ACS_VLINE);
            attroff(COLOR_PAIR(6));
        }

        // === Scrollable columns, starting from left_col ===
        int scr_width = getmaxx(stdscr);
        int right_edge = scr_width - 2; /* right border lives at scr_width-1 */
        int sc_col = left_col;
        int drawn_sc = 0;
        while (sc_col < col_count) {
            if (col_hidden[sc_col]) { sc_col++; continue; }
            if (drawn_sc >= visible_cols) break;
            if (current_x >= right_edge) break; /* stop before the right border */

            const char *raw = (sc_col < field_count) ? fields[sc_col] : "";

            char display_cell[MAX_LINE_LEN];
            strncpy(display_cell, raw, sizeof(display_cell) - 1);
            display_cell[sizeof(display_cell) - 1] = '\0';
            if (strlen(display_cell) > 200) strcpy(display_cell, "(very long text)");

            char *display_val = format_cell_value(display_cell, sc_col);

            int is_current_cell = (display_pos == cur_display_row && sc_col == cur_col);
            int is_current_row  = (display_pos == cur_display_row);
            int is_current_col  = (sc_col == cur_col);

            if (is_current_cell)          attron(COLOR_PAIR(2));
            else if (is_current_row || is_current_col) attron(COLOR_PAIR(1));
            else                          attron(COLOR_PAIR(8));

            char *disp = truncate_for_display(display_val, col_widths[sc_col] - 2);
            {
                int cell_w = col_widths[sc_col] - 2;
                if (col_types[sc_col] != COL_NUM && str_has_rtl(disp)) {
                    int text_w = utf8_display_width(disp);
                    int pad = (cell_w > text_w) ? cell_w - text_w : 0;
                    mvprintw(row_y, current_x, "%*s%s", pad, "", disp);
                } else if (col_types[sc_col] == COL_NUM) {
                    mvprintw(row_y, current_x, "%*s", cell_w, disp);
                } else {
                    int text_w = utf8_display_width(disp);
                    int pad = (cell_w > text_w) ? cell_w - text_w : 0;
                    mvprintw(row_y, current_x, "%s%*s", disp, pad, "");
                }
            }

            current_x += col_widths[sc_col] + 2;
            attroff(COLOR_PAIR(2) | COLOR_PAIR(8) | COLOR_PAIR(1));
            free(display_val);
            sc_col++;
            drawn_sc++;
        }

        /* Clear from current_x up to (but not including) the right border.
           clrtoeol() would erase the border character, so we fill with spaces
           up to scr_width-2 instead. */
        if (current_x < right_edge) {
            mvprintw(row_y, current_x, "%*s", right_edge - current_x, "");
        }

        // Free memory after parsing
        for (int k = 0; k < field_count; k++) {
            free(fields[k]);
        }
        free(fields);
    }
}

// ────────────────────────────────────────────────
// Status bar at the bottom of the screen
// ────────────────────────────────────────────────
void draw_status_bar(int y, int x, const char *filename, int row_count, const char *size_str)
{
    // File name
    attron(COLOR_PAIR(6));
    mvprintw(1, 2, "[ ");
    attroff(COLOR_PAIR(6));

    attron(COLOR_PAIR(3));
    mvprintw(1, 4, "%s", filename);
    attroff(COLOR_PAIR(3));

    /* count display columns: UTF-8 continuation bytes (0x80-0xBF) don't
       advance the cursor, so skip them when computing the visual width */
    int dispw = 0;
    for (const char *p = filename; *p; p++)
        if (((unsigned char)*p & 0xC0) != 0x80) dispw++;

    attron(COLOR_PAIR(6));
    mvprintw(1, 4 + dispw, " ]");
    attroff(COLOR_PAIR(6));

    // Row count and file size
    char row_buf[32];
    format_number_with_spaces(row_count, row_buf, sizeof(row_buf));

    attron(COLOR_PAIR(6));
    mvprintw(y - 1, 2, "[ %s ]", size_str);
    int meta_x = 2 + (int)strlen(size_str) + 6;
    mvprintw(y - 1, meta_x, "[ %s ]", row_buf);
    meta_x += (int)strlen(row_buf) + 4;
    if (comment_count > 0) {
        mvaddch(y - 1, meta_x,     ACS_HLINE);
        mvaddch(y - 1, meta_x + 1, ACS_HLINE);
        mvprintw(y - 1, meta_x + 2, "[ # %d ]", comment_count);
    }
    attroff(COLOR_PAIR(6));

    attron(COLOR_PAIR(6));
    mvprintw(y, x, "❯");
    attroff(COLOR_PAIR(6));
}

// ────────────────────────────────────────────────
// Window with the list of saved filters
// ────────────────────────────────────────────────
void show_saved_filters_window(const char *csv_filename)
{
    if (saved_filter_count == 0)
    {
        draw_status_bar(LINES - 1, 1, csv_filename, row_count, file_size_str);
        attron(COLOR_PAIR(3));
        printw(" | No saved filters yet. Press any key.");
        attroff(COLOR_PAIR(3));
        refresh();
        getch();
        return;
    }

    int height = saved_filter_count + 4;
    if (height > LINES - 4) height = LINES - 4;
    int width = 86;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW *win = newwin(height, width, start_y, start_x);
    wbkgd(win, COLOR_PAIR(1));
    wattron(win, COLOR_PAIR(6));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(6));
    mvwprintw(win, 0, (width - 18) / 2, " Saved Filters ");

    int selected = 0;
    int top = 0;
    int visible = height - 3;
    int ch;

    while (1)
    {
        werase(win);
        wattron(win, COLOR_PAIR(6));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(6));
        mvwprintw(win, 0, (width - 18) / 2, " Saved Filters ");

        for (int i = 0; i < visible; i++)
        {
            int idx = top + i;
            if (idx >= saved_filter_count) break;

            if (i == selected) wattron(win, A_REVERSE);

            char disp[64];
            snprintf(disp, sizeof(disp), "%d. %s", idx + 1, saved_filters[idx]);
            mvwprintw(win, i + 1, 2, "%-*s", width - 4, disp);

            if (i == selected) wattroff(win, A_REVERSE);
        }

        mvwprintw(win, height - 1, 2,
                  "[ ↑↓: select • Enter: insert • Shift+Enter: apply • D: delete •  Q/Esc: close ]");
        wrefresh(win);

        ch = getch();

        if (ch == KEY_UP)
        {
            if (selected > 0) selected--;
            if (selected < top) top = selected;
        }
        else if (ch == KEY_DOWN)
        {
            if (selected < saved_filter_count - 1) selected++;
            if (selected >= top + visible) top = selected - visible + 1;
        }
        else if (ch == 10 || ch == KEY_ENTER || ch == 343) // 343 = Shift+Enter on some terminals
        {
            strncpy(filter_query, saved_filters[selected], sizeof(filter_query) - 1);
            filter_query[sizeof(filter_query) - 1] = '\0';
            in_filter_mode = 1;

            // Close the list window
            delwin(win);
            touchwin(stdscr);
            refresh();

            // Update the status bar and apply the filter
            draw_status_bar(LINES - 1, 1, csv_filename, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Filtering... ");
            attroff(COLOR_PAIR(3));
            refresh();

            apply_filter(rows, f, row_count);
            break;
        }
        else if (ch == 'd' || ch == 'D')
        {
            if (selected < 0 || selected >= saved_filter_count) continue;

            // Remove from memory
            free(saved_filters[selected]);
            for (int j = selected; j < saved_filter_count - 1; j++)
            {
                saved_filters[j] = saved_filters[j + 1];
            }
            saved_filter_count--;

            if (selected >= saved_filter_count) selected--;
            if (top > saved_filter_count - visible) top = saved_filter_count - visible;
            if (top < 0) top = 0;

            // Rewrite the .csvf file without the deleted filter
            char cfg_path[1024];
            snprintf(cfg_path, sizeof(cfg_path), "%s.csvf", csv_filename);

            // Read the entire file into memory
            FILE *fp_in = fopen(cfg_path, "r");
            if (!fp_in) continue;

            char **lines = NULL;
            int line_count = 0;
            int max_lines = 1024;
            lines = malloc(max_lines * sizeof(char *));
            if (!lines)
            {
                fclose(fp_in);
                continue;
            }

            char buf[1024];
            while (fgets(buf, sizeof(buf), fp_in))
            {
                buf[strcspn(buf, "\n")] = '\0';
                if (line_count >= max_lines)
                {
                    max_lines *= 2;
                    lines = realloc(lines, max_lines * sizeof(char *));
                    if (!lines) break;
                }
                lines[line_count++] = strdup(buf);
            }
            fclose(fp_in);

            // Rewrite without the deleted entry
            FILE *fp_out = fopen(cfg_path, "w");
            if (fp_out)
            {
                for (int i = 0; i < line_count; i++)
                {
                    if (strncmp(lines[i], "filter: ", 8) == 0)
                    {
                        const char *query = lines[i] + 8;
                        if (strcmp(query, saved_filters[selected]) == 0)
                        {
                            free(lines[i]);
                            continue;
                        }
                    }
                    fprintf(fp_out, "%s\n", lines[i]);
                    free(lines[i]);
                }
                fclose(fp_out);
            }
            free(lines);
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q')
        {
            break;
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

// -----------------------------------------------------------------------------
// Gets the cell content for a given column number (cur_col) from a line
// Returns a new string (must be freed), always correctly handles ,, and "text, with comma"
// -----------------------------------------------------------------------------
char *get_cell_content(const char *line, int target_col)
{
    if (!line || target_col < 0) {
        return strdup("");
    }

    char *result = malloc(MAX_LINE_LEN);
    if (!result) return strdup("");

    result[0] = '\0';

    const char *p = line;
    int col = 0;
    int in_quotes = 0;
    int pos = 0;

    while (*p && col <= target_col)
    {
        if (*p == '"')
        {
            in_quotes = !in_quotes;
            p++;
            continue;
        }

        if (*p == csv_delimiter && !in_quotes)
        {
            // End of the current field
            if (col == target_col)
            {
                result[pos] = '\0';
                // strip leading/trailing whitespace if needed
                // char *trimmed = trim(result);
                // free(result);
                // return trimmed;
                return result;
            }
            col++;
            pos = 0;
            p++;
            continue;
        }

        // Write the character to the result only if this is the target column
        if (col == target_col)
        {
            if (pos < MAX_LINE_LEN - 1)
            {
                result[pos++] = *p;
            }
        }
        p++;
    }

    // Last field (if the line did not end with a delimiter)
    if (col == target_col)
    {
        result[pos] = '\0';
        return result;
    }

    // If we reached here — the column was not found
    free(result);
    return strdup("");
}

// ────────────────────────────────────────────────
// Progress spinner (right corner of the status bar)
// ────────────────────────────────────────────────

static int spinner_state = 0;
static const char spinner_chars[] = "|/-\\";

void spinner_tick(void)
{
    attron(COLOR_PAIR(6));
    mvprintw(LINES - 2, COLS - 7, "[ %c ]", spinner_chars[spinner_state & 3]);
    attroff(COLOR_PAIR(6));
    refresh();
    spinner_state++;
}

void spinner_clear(void)
{
    mvprintw(LINES - 2, COLS - 7, "     ");
    refresh();
    spinner_state = 0;
}

// ────────────────────────────────────────────────
// Comment lines viewer window (#)
// ────────────────────────────────────────────────

void show_comments_window(void)
{
    if (comment_count == 0) return;

    int height = LINES - 4;
    int width  = COLS  - 4;
    if (width  < 40) width  = 40;
    if (height < 5)  height = 5;
    int start_y = 2;
    int start_x = 2;

    WINDOW *win = newwin(height, width, start_y, start_x);
    wbkgd(win, COLOR_PAIR(1));

    int visible = height - 3;   // data rows (excluding border + footer)
    int top     = 0;
    int cur     = 0;
    int ch;

    // Row number field width
    char num_fmt[16];
    int  num_w = 1;
    { int tmp = comment_count; while (tmp >= 10) { num_w++; tmp /= 10; } }
    snprintf(num_fmt, sizeof(num_fmt), "%%%dd", num_w);

    // Text width: width - border(2) - num_w - " # "(3) - pad(1)
    int text_w = width - 2 - num_w - 4;
    if (text_w < 10) text_w = 10;

    while (1) {
        // ── Border ──
        werase(win);
        wattron(win, COLOR_PAIR(6));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(6));

        // ── Title ──
        char title[64];
        snprintf(title, sizeof(title), " # File Comments (%d) ", comment_count);
        int tx = (width - (int)strlen(title)) / 2;
        if (tx < 1) tx = 1;
        wattron(win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(win, 0, tx, "%s", title);
        wattroff(win, COLOR_PAIR(3) | A_BOLD);

        // ── Rows ──
        for (int i = 0; i < visible; i++) {
            int idx = top + i;
            if (idx >= comment_count) break;

            const char *raw = comment_lines[idx];
            // Skip the leading '#' for cleaner display
            const char *text = raw;
            if (text[0] == '#') text++;
            // Strip the space immediately after '#' if present
            if (text[0] == ' ') text++;

            int row_y = 1 + i;
            int col_x = 1;

            if (idx == cur) wattron(win, COLOR_PAIR(2));

            // Row number
            if (idx != cur) wattron(win, COLOR_PAIR(6));
            mvwprintw(win, row_y, col_x, num_fmt, idx + 1);
            if (idx != cur) wattroff(win, COLOR_PAIR(6));

            col_x += num_w + 1;

            // The '#' character
            if (idx != cur) wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, row_y, col_x, "#");
            if (idx != cur) wattroff(win, COLOR_PAIR(3) | A_BOLD);

            col_x += 2;

            // Comment text
            if (idx != cur) wattron(win, COLOR_PAIR(1));
            mvwprintw(win, row_y, col_x, "%-.*s", text_w, text);
            if (idx != cur) wattroff(win, COLOR_PAIR(1));

            if (idx == cur) wattroff(win, COLOR_PAIR(2));
        }

        // ── Scroll bar (right edge) ──
        if (comment_count > visible) {
            int bar_h = visible * visible / comment_count;
            if (bar_h < 1) bar_h = 1;
            int bar_y = 1 + top * visible / comment_count;
            wattron(win, COLOR_PAIR(6));
            for (int i = 0; i < visible; i++)
                mvwaddch(win, 1 + i, width - 2, i >= bar_y && i < bar_y + bar_h ? ACS_BLOCK : ACS_VLINE);
            wattroff(win, COLOR_PAIR(6));
        }

        // ── Footer ──
        wattron(win, COLOR_PAIR(6));
        mvwprintw(win, height - 1, 2,
                  "[ ↑↓/jk: navigate   PgUp/PgDn: page   q/Esc: close ]");
        wattroff(win, COLOR_PAIR(6));

        wrefresh(win);

        ch = getch();

        if (ch == 'q' || ch == 27 || ch == KEY_F(1)) {
            break;
        } else if (ch == KEY_UP || ch == 'k') {
            if (cur > 0) cur--;
            if (cur < top) top = cur;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (cur < comment_count - 1) cur++;
            if (cur >= top + visible) top = cur - visible + 1;
        } else if (ch == KEY_PPAGE) {
            cur -= visible;
            if (cur < 0) cur = 0;
            top = cur;
        } else if (ch == KEY_NPAGE) {
            cur += visible;
            if (cur >= comment_count) cur = comment_count - 1;
            if (cur >= top + visible) top = cur - visible + 1;
        } else if (ch == KEY_HOME || ch == 'g') {
            cur = 0; top = 0;
        } else if (ch == KEY_END || ch == 'G') {
            cur = comment_count - 1;
            top = cur - visible + 1;
            if (top < 0) top = 0;
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}
