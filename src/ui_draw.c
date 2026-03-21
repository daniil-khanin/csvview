/**
 * ui_draw.c
 *
 * Реализация функций отрисовки интерфейса таблицы и элементов UI
 */

#include "ui_draw.h"
#include "utils.h"          // col_letter, format_cell_value, truncate_string и т.д.
#include "csvview_defs.h"   // globals (cur_col, cur_display_row и т.д.)

#include <ncurses.h>        // все функции ncurses
#include <string.h>         // strlen, strncpy
#include <stdio.h>          // snprintf

#include "column_format.h"   // для format_cell_value()
#include "sorting.h"         // для get_real_row()
#include "filtering.h"       // для apply_filter()
// ────────────────────────────────────────────────
// Отрисовка меню
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
// Увеличенное окно ячейки
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

    // Форматируем значение
    char *display_content = format_cell_value(raw_content, cur_col);

    attron(COLOR_PAIR(5));
    if (in_search_mode) {
        mvprintw(y, 2, "/%s", search_query);
    } else if (in_filter_mode) {
        mvprintw(y, 2, "F:%s", filter_query);
    } else {
        mvprintw(y, 2, "%s%d: %.*s", col_name, row_num, width - 6, display_content);
    }
    attroff(COLOR_PAIR(5));

    free(display_content);
}

// ────────────────────────────────────────────────
// Внешняя рамка таблицы
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
// Заголовки таблицы
// ────────────────────────────────────────────────
// Вспомогательная отрисовка одного заголовка столбца
static void draw_one_header(int top, int current_x, int col_idx, int cur_col)
{
    char name[64] = {0};
    if (use_headers && column_names[col_idx]) {
        strncpy(name, column_names[col_idx], sizeof(name) - 6);
        name[sizeof(name) - 6] = '\0';
    } else {
        col_letter(col_idx, name);
    }

    const char *arrow = "";
    int arrow_pair = 0;
    if (sort_col == col_idx && sort_order != 0) {
        arrow = (sort_order > 0) ? " ↑" : " ↓";
        arrow_pair = 3;
    }

    const char *fmt = (col_types[col_idx] == COL_NUM) ? "%*s" : "%-*s";
    char *disp = truncate_for_display(name, col_widths[col_idx] - 2);

    if (col_idx == cur_col)
        attron(COLOR_PAIR(3) | A_BOLD);
    else
        attron(COLOR_PAIR(6) | A_BOLD);

    mvprintw(top + 1, current_x, fmt, col_widths[col_idx] - 2, disp);

    if (*arrow) {
        int arrow_x = current_x + col_widths[col_idx] - 4;
        if (arrow_pair) attron(COLOR_PAIR(arrow_pair) | A_BOLD);
        mvprintw(top + 1, arrow_x, "%s", arrow);
        if (arrow_pair) attroff(COLOR_PAIR(arrow_pair) | A_BOLD);
    }

    attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD);
}

void draw_table_headers(int top, int offset __attribute__((unused)), int visible_cols, int left_col, int cur_col)
{
    attron(COLOR_PAIR(6) | A_BOLD);
    mvprintw(top + 1, 1, "%*s", ROW_NUMBER_WIDTH, " ");
    attroff(COLOR_PAIR(6) | A_BOLD);

    int current_x = ROW_NUMBER_WIDTH + 2;

    // === Замороженные столбцы (0..freeze_cols-1) ===
    for (int fc = 0; fc < freeze_cols && fc < col_count; fc++) {
        if (col_hidden[fc]) continue;
        draw_one_header(top, current_x, fc, cur_col);
        current_x += col_widths[fc] + 2;
    }

    // === Сепаратор заморозки ===
    if (freeze_cols > 0 && freeze_cols < col_count) {
        attron(COLOR_PAIR(6) | A_BOLD);
        mvaddch(top + 1, current_x - 1, ACS_VLINE);
        attroff(COLOR_PAIR(6) | A_BOLD);
    }

    // === Скроллируемые столбцы, начиная с left_col ===
    int col_idx = left_col;
    int drawn_sc = 0;
    while (col_idx < col_count) {
        if (col_hidden[col_idx]) { col_idx++; continue; }
        if (drawn_sc >= visible_cols) break;
        draw_one_header(top, current_x, col_idx, cur_col);
        current_x += col_widths[col_idx] + 2;
        col_idx++;
        drawn_sc++;
    }

    attroff(COLOR_PAIR(6) | A_BOLD);
}

// ────────────────────────────────────────────────
// Тело таблицы (видимые строки)
// ────────────────────────────────────────────────
void draw_table_body(int top, int offset __attribute__((unused)), int visible_rows,
                     int top_display_row, int cur_display_row, int cur_col,
                     int left_col, int visible_cols, RowIndex *rows, FILE *f, int row_count)
{
    int display_count = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));

    for (int i = 0; i < visible_rows && top_display_row + i < display_count; i++)
    {
        int display_pos = top_display_row + i;
        int real_row = get_real_row(display_pos);

        // Подсветка номера строки
        int is_cur = (display_pos == cur_display_row);
        if (is_cur) {
            attron(COLOR_PAIR(3) | A_BOLD);
        } else {
            attron(COLOR_PAIR(6));
        }
        if (relative_line_numbers && !is_cur) {
            int rel = display_pos - cur_display_row;
            if (rel < 0) rel = -rel;
            mvprintw(top + 2 + i, 1, "%*d", ROW_NUMBER_WIDTH - 2, rel);
        } else {
            mvprintw(top + 2 + i, 1, "%*d", ROW_NUMBER_WIDTH - 2, display_pos + 1);
        }
        attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD);

        // Ленивая загрузка строки
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

        // Парсим строку с помощью новой функции
        int field_count = 0;
        char **fields = parse_csv_line(rows[real_row].line_cache, &field_count);

        if (!fields) {
            // если ошибка парсинга — рисуем пустую строку
            continue;
        }

        int current_x = ROW_NUMBER_WIDTH + 2;
        int row_y = top + 2 + i;

        // === Замороженные столбцы (0..freeze_cols-1) ===
        for (int fc = 0; fc < freeze_cols && fc < col_count; fc++) {
            if (col_hidden[fc]) continue;

            const char *raw = (fc < field_count) ? fields[fc] : "";

            char display_cell[MAX_LINE_LEN];
            strncpy(display_cell, raw, sizeof(display_cell) - 1);
            display_cell[sizeof(display_cell) - 1] = '\0';
            if (strlen(display_cell) > 200) strcpy(display_cell, "(очень длинный текст)");

            char *display_val = format_cell_value(display_cell, fc);

            int is_current_cell = (display_pos == cur_display_row && fc == cur_col);
            int is_current_row  = (display_pos == cur_display_row);
            int is_current_col  = (fc == cur_col);

            if (is_current_cell)          attron(COLOR_PAIR(2));
            else if (is_current_row || is_current_col) attron(COLOR_PAIR(1));
            else                          attron(COLOR_PAIR(8));

            const char *fmt = (col_types[fc] == COL_NUM) ? "%*s" : "%-*s";
            char *disp = truncate_for_display(display_val, col_widths[fc] - 2);
            mvprintw(row_y, current_x, fmt, col_widths[fc] - 2, disp);

            current_x += col_widths[fc] + 2;
            attroff(COLOR_PAIR(2) | COLOR_PAIR(8) | COLOR_PAIR(1));
            free(display_val);
        }

        // === Сепаратор заморозки ===
        if (freeze_cols > 0 && freeze_cols < col_count) {
            attron(COLOR_PAIR(6));
            mvaddch(row_y, current_x - 1, ACS_VLINE);
            attroff(COLOR_PAIR(6));
        }

        // === Скроллируемые столбцы, начиная с left_col ===
        int sc_col = left_col;
        int drawn_sc = 0;
        while (sc_col < col_count) {
            if (col_hidden[sc_col]) { sc_col++; continue; }
            if (drawn_sc >= visible_cols) break;

            const char *raw = (sc_col < field_count) ? fields[sc_col] : "";

            char display_cell[MAX_LINE_LEN];
            strncpy(display_cell, raw, sizeof(display_cell) - 1);
            display_cell[sizeof(display_cell) - 1] = '\0';
            if (strlen(display_cell) > 200) strcpy(display_cell, "(очень длинный текст)");

            char *display_val = format_cell_value(display_cell, sc_col);

            int is_current_cell = (display_pos == cur_display_row && sc_col == cur_col);
            int is_current_row  = (display_pos == cur_display_row);
            int is_current_col  = (sc_col == cur_col);

            if (is_current_cell)          attron(COLOR_PAIR(2));
            else if (is_current_row || is_current_col) attron(COLOR_PAIR(1));
            else                          attron(COLOR_PAIR(8));

            const char *fmt = (col_types[sc_col] == COL_NUM) ? "%*s" : "%-*s";
            char *disp = truncate_for_display(display_val, col_widths[sc_col] - 2);
            mvprintw(row_y, current_x, fmt, col_widths[sc_col] - 2, disp);

            current_x += col_widths[sc_col] + 2;
            attroff(COLOR_PAIR(2) | COLOR_PAIR(8) | COLOR_PAIR(1));
            free(display_val);
            sc_col++;
            drawn_sc++;
        }

        // Освобождаем память после парсинга
        for (int k = 0; k < field_count; k++) {
            free(fields[k]);
        }
        free(fields);
    }
}

// ────────────────────────────────────────────────
// Статус-бар внизу экрана
// ────────────────────────────────────────────────
void draw_status_bar(int y, int x, const char *filename, int row_count, const char *size_str)
{
    // Имя файла 
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

    // Число строк в файле и вес
    char row_buf[32];
    format_number_with_spaces(row_count, row_buf, sizeof(row_buf));

    attron(COLOR_PAIR(6));
    mvprintw(y - 1, 2, "[ %s ]", size_str);
    mvprintw(y - 1, 2 + strlen(size_str) + 6, "[ %s ]", row_buf);
    attroff(COLOR_PAIR(6));

    attron(COLOR_PAIR(6));
    mvprintw(y, x, "❯");
    attroff(COLOR_PAIR(6));
}

// ────────────────────────────────────────────────
// Окно со списком сохраненных фильтров
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
    box(win, 0, 0);
    mvwprintw(win, 0, (width - 18) / 2, " Saved Filters ");

    int selected = 0;
    int top = 0;
    int visible = height - 3;
    int ch;

    while (1)
    {
        werase(win);
        box(win, 0, 0);
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
        else if (ch == 10 || ch == KEY_ENTER || ch == 343) // 343 = Shift+Enter на некоторых терминалах
        {
            strncpy(filter_query, saved_filters[selected], sizeof(filter_query) - 1);
            filter_query[sizeof(filter_query) - 1] = '\0';
            in_filter_mode = 1;

            // Закрываем окно списка
            delwin(win);
            touchwin(stdscr);
            refresh();

            // Обновляем статус-бар и применяем фильтр
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

            // Удаляем из памяти
            free(saved_filters[selected]);
            for (int j = selected; j < saved_filter_count - 1; j++)
            {
                saved_filters[j] = saved_filters[j + 1];
            }
            saved_filter_count--;

            if (selected >= saved_filter_count) selected--;
            if (top > saved_filter_count - visible) top = saved_filter_count - visible;
            if (top < 0) top = 0;

            // Перезаписываем .csvf-файл без удалённого фильтра
            char cfg_path[1024];
            snprintf(cfg_path, sizeof(cfg_path), "%s.csvf", csv_filename);

            // Читаем весь файл в память
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

            // Перезаписываем без удалённого
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
// Получает содержимое ячейки по номеру столбца (cur_col) из строки
// Возвращает новую строку (нужно free), всегда корректно обрабатывает ,, и "текст, с запятой"
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

        if (*p == ',' && !in_quotes)
        {
            // Конец текущего поля
            if (col == target_col)
            {
                result[pos] = '\0';
                // убираем ведущие/конечные пробелы, если нужно
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

        // Пишем символ в результат, только если это нужный столбец
        if (col == target_col)
        {
            if (pos < MAX_LINE_LEN - 1)
            {
                result[pos++] = *p;
            }
        }
        p++;
    }

    // Последнее поле (если строка не закончилась запятой)
    if (col == target_col)
    {
        result[pos] = '\0';
        return result;
    }

    // Если дошли сюда — столбец не найден
    free(result);
    return strdup("");
}

// ────────────────────────────────────────────────
// Спиннер прогресса (правый угол статус-бара)
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
