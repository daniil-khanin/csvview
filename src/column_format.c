/**
 * column_format.c
 *
 * Реализация форматирования и настройки отображения столбцов
 */

#include "column_format.h"
#include "utils.h"          // trim, col_letter

#include <ncurses.h>
#include <stdio.h>          // snprintf
#include <stdlib.h>         // malloc, free, strtod
#include <string.h>         // strlen, strcpy, strncpy
#include <time.h>           // struct tm, strptime, strftime
#include <math.h>           // INFINITY (если нужно)

// ────────────────────────────────────────────────
// Инициализация форматов столбцов
// ────────────────────────────────────────────────

void init_column_formats(void)
{
    for (int i = 0; i < col_count; i++)
    {
        col_formats[i].truncate_len   = 0;        // не обрезать строки
        col_formats[i].decimal_places = -1;       // авто для чисел
        col_formats[i].date_format[0] = '\0';     // без формата даты
        col_widths[i] = CELL_WIDTH;               // ширина по умолчанию
    }
}

// ────────────────────────────────────────────────
// Обрезка строки с добавлением "..."
// ────────────────────────────────────────────────

char *truncate_string(const char *str, int max_len)
{
    if (!str || max_len <= 0) {
        return strdup(str ? str : "");
    }

    size_t len = strlen(str);
    if (len <= (size_t)max_len) {
        return strdup(str);
    }

    char *result = malloc(max_len + 4); // +3 для "..." и \0
    if (!result) {
        return strdup(str);
    }

    strncpy(result, str, max_len);
    strcpy(result + max_len, "...");

    return result;
}

// ────────────────────────────────────────────────
// Форматирование числа
// ────────────────────────────────────────────────

char *format_number(const char *raw_str, int decimals)
{
    if (!raw_str || !*raw_str) {
        return strdup("0");
    }

    // Normalize decimal separator: replace comma with dot (e.g. "1234,56" → "1234.56")
    char normalized[64];
    strncpy(normalized, raw_str, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    for (char *p = normalized; *p; p++) {
        if (*p == ',') *p = '.';
    }

    char *endptr;
    double num = strtod(normalized, &endptr);
    if (*endptr != '\0' && *endptr != '\n') {
        // Не чистое число — возвращаем как есть
        return strdup(raw_str);
    }

    char fmt[16];
    if (decimals < 0) {
        // Авто-режим: до 6 знаков, без лишних нулей
        snprintf(fmt, sizeof(fmt), "%%.6g");
    } else {
        snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), fmt, num);

    // В авто-режиме убираем лишние нули после точки
    if (decimals < 0)
    {
        char *dot = strchr(buf, '.');
        if (dot)
        {
            char *end = buf + strlen(buf) - 1;
            while (end > dot && *end == '0') {
                *end-- = '\0';
            }
            if (*end == '.') {
                *end = '\0';
            }
        }
    }

    return strdup(buf);
}

// ────────────────────────────────────────────────
// Форматирование даты
// ────────────────────────────────────────────────

char *format_date(const char *date_str, const char *target_format)
{
    if (!date_str || !*date_str) {
        return strdup("");
    }

    if (!target_format || !*target_format) {
        return strdup(date_str);
    }

    struct tm tm = {0};
    const char *input_fmts[] = {
        "%Y-%m-%d",
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%d.%m.%Y",
        "%d.%m.%Y %H:%M",
        "%m/%d/%Y",
        "%Y/%m/%d",
        "%Y%m%d",
        "%Y-%m",
        "%m-%Y",
        NULL
    };

    char *parsed_end = NULL;
    for (int i = 0; input_fmts[i]; i++)
    {
        memset(&tm, 0, sizeof(tm));
        parsed_end = strptime(date_str, input_fmts[i], &tm);
        if (parsed_end && (*parsed_end == '\0' || isspace(*parsed_end)))
        {
            char buf[64];
            strftime(buf, sizeof(buf), target_format, &tm);
            return strdup(buf);
        }
    }

    // Не удалось распознать — возвращаем как есть
    return strdup(date_str);
}

// ────────────────────────────────────────────────
// Главная функция форматирования ячейки
// ────────────────────────────────────────────────

char *format_cell_value(const char *raw_value, int col_idx)
{
    if (!raw_value) {
        return strdup("");
    }

    if (col_idx < 0 || col_idx >= col_count) {
        return strdup(raw_value);
    }

    char *formatted = NULL;

    switch (col_types[col_idx])
    {
        case COL_STR:
        {
            int len = col_formats[col_idx].truncate_len;
            if (len > 0) {
                formatted = truncate_string(raw_value, len);
            } else {
                formatted = strdup(raw_value);
            }
            break;
        }

        case COL_NUM:
        {
            int dec = col_formats[col_idx].decimal_places;
            formatted = format_number(raw_value, dec);
            break;
        }

        case COL_DATE:
        {
            const char *fmt = col_formats[col_idx].date_format;
            if (fmt && *fmt) {
                formatted = format_date(raw_value, fmt);
            } else {
                formatted = strdup(raw_value);
            }
            break;
        }

        default:
            formatted = strdup(raw_value);
    }

    return formatted ? formatted : strdup(raw_value);
}

void save_column_settings(const char *csv_filename)
{
    if (!csv_filename || !*csv_filename) {
        return;
    }

    char csvf_path[1024];
    snprintf(csvf_path, sizeof(csvf_path), "%s.csvf", csv_filename);

    FILE *fp = fopen(csvf_path, "w");
    if (!fp) {
        return;
    }

    // Основные настройки
    fprintf(fp, "use_headers:%d\n", use_headers);
    fprintf(fp, "col_count:%d\n", col_count);
    fprintf(fp, "freeze:%d\n", freeze_cols);

    // Настройки каждого столбца
    for (int i = 0; i < col_count; i++)
    {
        char type_char = 'S';
        if (col_types[i] == COL_NUM)  type_char = 'N';
        if (col_types[i] == COL_DATE) type_char = 'D';

        fprintf(fp, "%d:%c:%d:%d:%s\n",
                i,
                type_char,
                col_formats[i].truncate_len,
                col_formats[i].decimal_places,
                col_formats[i].date_format);
    }

    // Скрытые столбцы
    int has_hidden = 0;
    for (int i = 0; i < col_count; i++) if (col_hidden[i]) { has_hidden = 1; break; }
    if (has_hidden) {
        fprintf(fp, "hidden:");
        int first = 1;
        for (int i = 0; i < col_count; i++) {
            if (col_hidden[i]) {
                if (!first) fprintf(fp, ",");
                fprintf(fp, "%d", i);
                first = 0;
            }
        }
        fprintf(fp, "\n");
    }

    // Ширины столбцов
    fprintf(fp, "widths:");
    for (int i = 0; i < col_count; i++)
    {
        if (i > 0) fprintf(fp, ",");
        fprintf(fp, "%d", col_widths[i]);
    }
    fprintf(fp, "\n");

    // Сохранённые фильтры (в том же файле)
    for (int f = 0; f < saved_filter_count; f++)
    {
        if (saved_filters[f]) {
            fprintf(fp, "filter: %s\n", saved_filters[f]);
        }
    }

    fclose(fp);
}

int load_column_settings(const char *csv_filename)
{
    if (!csv_filename || !*csv_filename) {
        return 0;
    }

    char csvf_path[1024];
    snprintf(csvf_path, sizeof(csvf_path), "%s.csvf", csv_filename);

    FILE *fp = fopen(csvf_path, "r");
    if (!fp) {
        return 0; // файла нет — оставляем дефолт
    }

    // Сбрасываем скрытые столбцы перед загрузкой
    memset(col_hidden, 0, sizeof(col_hidden));

    char line[256];
    int loaded_count = 0;
    int temp_use_headers = 0;

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = '\0';

        // Фильтры загружаем отдельно (как раньше)
        if (strncmp(line, "filter: ", 8) == 0)
        {
            if (saved_filter_count < MAX_SAVED_FILTERS) {
                saved_filters[saved_filter_count++] = strdup(line + 8);
            }
            continue;
        }

        // Ширины столбцов
        if (strncmp(line, "widths:", 7) == 0)
        {
            char *p = line + 7;
            int idx = 0;
            char *tok = strtok(p, ",");
            while (tok && idx < col_count)
            {
                col_widths[idx++] = atoi(tok);
                tok = strtok(NULL, ",");
            }
            continue;
        }

        // Основные параметры
        if (strncmp(line, "use_headers:", 12) == 0)
        {
            temp_use_headers = atoi(line + 12);
        }
        else if (strncmp(line, "freeze:", 7) == 0)
        {
            int n = atoi(line + 7);
            if (n >= 0) freeze_cols = n;
        }
        else if (strncmp(line, "hidden:", 7) == 0)
        {
            char *p = line + 7;
            char *tok = strtok(p, ",");
            while (tok) {
                int idx = atoi(tok);
                if (idx >= 0 && idx < MAX_COLS) col_hidden[idx] = 1;
                tok = strtok(NULL, ",");
            }
        }
        else if (strncmp(line, "col_count:", 10) == 0)
        {
            loaded_count = atoi(line + 10);
            if (loaded_count != col_count)
            {
                fclose(fp);
                return 0; // количество столбцов не совпадает — игнорируем
            }
        }
        else
        {
            // Формат строки столбца: idx:type:truncate:decimals:date_format
            int idx, truncate, decimals;
            char type_char, date_fmt[32] = {0};
            if (sscanf(line, "%d:%c:%d:%d:%31[^\n]",
                       &idx, &type_char, &truncate, &decimals, date_fmt) >= 4)
            {
                if (idx >= 0 && idx < col_count)
                {
                    col_types[idx] = (type_char == 'N') ? COL_NUM :
                                     (type_char == 'D') ? COL_DATE : COL_STR;
                    col_formats[idx].truncate_len   = truncate;
                    col_formats[idx].decimal_places = decimals;
                    strncpy(col_formats[idx].date_format, date_fmt,
                            sizeof(col_formats[idx].date_format) - 1);
                    col_formats[idx].date_format[sizeof(col_formats[idx].date_format) - 1] = '\0';
                }
            }
        }
    }

    fclose(fp);

    // Применяем загруженные настройки только если всё совпало
    if (loaded_count == col_count)
    {
        use_headers = temp_use_headers;
        return 1; // успех
    }

    return 0;
}

/*
int show_column_setup(const char *csv_filename)
{
    int height, width;
    getmaxyx(stdscr, height, width);
    int win_top = 2;
    int win_height = height - 4;
    int win_width = width - 4;
    int vis_lines = win_height - 10; // место для заголовков + подсказок

    int top_item = 0;
    int cur_item = 0;
    int cur_field = 0; // 0 = тип, 1 = формат

    // Превью — массив сырых значений из первой строки данных
    char preview_values[MAX_COLS][MAX_LINE_LEN] = {{0}};
    int preview_valid = 0;

    // Загружаем превью один раз при входе (первая строка данных)
    if (row_count > (use_headers ? 1 : 0))
    {
        int preview_row = use_headers ? 1 : 0; // пропускаем заголовок, если он есть

        if (!rows[preview_row].line_cache)
        {
            fseek(f, rows[preview_row].offset, SEEK_SET);
            char *line = malloc(MAX_LINE_LEN);
            if (fgets(line, MAX_LINE_LEN, f))
            {
                line[strcspn(line, "\r\n")] = '\0';
                rows[preview_row].line_cache = line;
            }
            else
            {
                rows[preview_row].line_cache = strdup("");
            }
        }

        // Парсим строку с помощью новой функции
        int field_count = 0;
        char **fields = parse_csv_line(rows[preview_row].line_cache, &field_count);

        if (fields && field_count > 0)
        {
            for (int c = 0; c < field_count && c < col_count; c++)
            {
                strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                preview_values[c][sizeof(preview_values[c]) - 1] = '\0';
            }
            preview_valid = 1;

            // Освобождаем память
            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
        }
    }

    while (1)
    {
        clear();

        // Рамка окна
        attron(COLOR_PAIR(6));
        mvaddch(win_top, 1, ACS_ULCORNER);
        for (int x = 2; x < win_width; x++) {
            mvaddch(win_top, x, ACS_HLINE);
        }
        mvaddch(win_top, win_width, ACS_URCORNER);
        for (int y = win_top + 1; y < win_top + win_height - 2; y++) {
            mvaddch(y, 1, ACS_VLINE);
            mvaddch(y, win_width, ACS_VLINE);
        }
        mvaddch(win_top + win_height - 2, 1, ACS_LLCORNER);
        for (int x = 2; x < win_width; x++) {
            mvaddch(win_top + win_height - 2, x, ACS_HLINE);
        }
        mvaddch(win_top + win_height - 2, win_width, ACS_LRCORNER);
        attroff(COLOR_PAIR(6));

        // Заголовки таблицы
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(win_top + 1, 3,   "Column");
        mvprintw(win_top + 1, 65,  "Type");
        mvprintw(win_top + 1, 82,  "Format");
        mvprintw(win_top + 1, 110, "Preview (first data row)");
        attroff(COLOR_PAIR(5) | A_BOLD);

        // Вывод списка столбцов + превью
        for (int i = 0; i < vis_lines && top_item + i < col_count; i++)
        {
            int idx = top_item + i;
            char *name = column_names[idx] ? column_names[idx] : "";

            char type_str[32];
            switch (col_types[idx]) {
                case COL_STR:  strcpy(type_str, "String"); break;
                case COL_NUM:  strcpy(type_str, "Number"); break;
                case COL_DATE: strcpy(type_str, "Date");   break;
                default:       strcpy(type_str, "???");    break;
            }

            char fmt_str[64] = "—";
            if (col_types[idx] == COL_STR) {
                if (col_formats[idx].truncate_len > 0) {
                    snprintf(fmt_str, sizeof(fmt_str), "length %d", col_formats[idx].truncate_len);
                }
            } else if (col_types[idx] == COL_NUM) {
                if (col_formats[idx].decimal_places < 0) {
                    strcpy(fmt_str, "auto");
                } else {
                    snprintf(fmt_str, sizeof(fmt_str), "%d decimals", col_formats[idx].decimal_places);
                }
            } else if (col_types[idx] == COL_DATE) {
                if (col_formats[idx].date_format[0]) {
                    strncpy(fmt_str, col_formats[idx].date_format, sizeof(fmt_str) - 1);
                    fmt_str[sizeof(fmt_str) - 1] = '\0';
                } else {
                    strcpy(fmt_str, "auto");
                }
            }

            // Превью — применяем текущий формат
            char preview[128] = "(no data)";
            if (preview_valid && idx < col_count && preview_values[idx][0])
            {
                char *formatted = format_cell_value(preview_values[idx], idx);
                strncpy(preview, formatted, sizeof(preview) - 1);
                preview[sizeof(preview) - 1] = '\0';
                free(formatted);

                // Обрезаем превью
                if (strlen(preview) > 60) {
                    preview[57] = '.';
                    preview[58] = '.';
                    preview[59] = '.';
                    preview[60] = '\0';
                }
            }

            if (idx == cur_item) {
                attron(A_REVERSE);
            }

            mvprintw(win_top + 3 + i, 3,   "%-60s", name);
            mvprintw(win_top + 3 + i, 65,  "%-15s", type_str);
            mvprintw(win_top + 3 + i, 82,  "%-25s", fmt_str);
            mvprintw(win_top + 3 + i, 110, "%-50s", preview);

            attroff(A_REVERSE);
        }

        // Подсказки
        int info_y = win_top + win_height - 7;
        attron(COLOR_PAIR(5));
        mvprintw(info_y, 3, "↑↓ — выбрать столбец    ←→/Tab — Тип ↔ Формат");
        mvprintw(info_y + 1, 3, "S/N/D — быстрый выбор типа");
        mvprintw(info_y + 2, 3, "Enter — сохранить и выйти");
        mvprintw(info_y + 3, 3, "H — включить заголовки");
        mvprintw(info_y + 4, 3, "Esc/q — отмена");
        attroff(COLOR_PAIR(5));

        refresh();

        int ch = getch();

        if (ch == KEY_UP)
        {
            if (cur_item > 0) {
                cur_item--;
                if (cur_item < top_item) top_item--;
            }
        }
        else if (ch == KEY_DOWN)
        {
            if (cur_item < col_count - 1) {
                cur_item++;
                if (cur_item >= top_item + vis_lines) top_item++;
            }
        }
        else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t')
        {
            cur_field = !cur_field;
        }
        else if (ch == 's' || ch == 'S') { col_types[cur_item] = COL_STR; }
        else if (ch == 'n' || ch == 'N') { col_types[cur_item] = COL_NUM; }
        else if (ch == 'd' || ch == 'D') { col_types[cur_item] = COL_DATE; }
        else if (ch == '\n' || ch == KEY_ENTER)
        {
            if (cur_field == 1) // редактирование формата
            {
                echo();
                curs_set(1);
                char buf[64] = "";
                const char *prompt = "";
                if (col_types[cur_item] == COL_STR) {
                    prompt = "Обрезать до (0=все): ";
                    snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].truncate_len);
                } else if (col_types[cur_item] == COL_NUM) {
                    prompt = "Знаков после точки (-1=авто): ";
                    snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].decimal_places);
                } else if (col_types[cur_item] == COL_DATE) {
                    prompt = "Формат даты (%%Y-%%m-%%d и т.п.): ";
                    strncpy(buf, col_formats[cur_item].date_format, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                }

                mvprintw(win_top + 4 + (cur_item - top_item), 70, "%s", prompt);
                clrtoeol();
                mvprintw(win_top + 4 + (cur_item - top_item), 70 + strlen(prompt), "%s", buf);
                move(win_top + 4 + (cur_item - top_item), 70 + strlen(prompt) + strlen(buf));
                int pos = strlen(buf);
                int done = 0;

                while (!done)
                {
                    int key = getch();
                    if (key == '\n' || key == KEY_ENTER)
                    {
                        if (col_types[cur_item] == COL_STR) {
                            col_formats[cur_item].truncate_len = atoi(buf);
                        } else if (col_types[cur_item] == COL_NUM) {
                            col_formats[cur_item].decimal_places = atoi(buf);
                        } else if (col_types[cur_item] == COL_DATE) {
                            strncpy(col_formats[cur_item].date_format, buf,
                                    sizeof(col_formats[cur_item].date_format) - 1);
                            col_formats[cur_item].date_format[sizeof(col_formats[cur_item].date_format) - 1] = '\0';
                        }
                        done = 1;
                    }
                    else if (key == 27) { done = 1; }
                    else if (key == KEY_BACKSPACE || key == 127)
                    {
                        if (pos > 0) {
                            pos--;
                            buf[pos] = '\0';
                        }
                    }
                    else if (key >= 32 && key <= 126 && pos < 63)
                    {
                        buf[pos++] = (char)key;
                        buf[pos] = '\0';
                    }

                    mvprintw(win_top + 4 + (cur_item - top_item), 70, "%s", prompt);
                    clrtoeol();
                    printw("%s", buf);
                    move(win_top + 4 + (cur_item - top_item), 70 + strlen(prompt) + pos);
                    refresh();
                }
                noecho();
                curs_set(0);
            }
            else
            {
                save_column_settings(csv_filename);
                return 0;
            }
        }
        else if (ch == '1' || ch == '2')
        {
            use_headers = (ch == '1');
            save_column_settings(csv_filename);
            return 0;
        }
        else if (ch == 'h' || ch == 'H')
        {
            use_headers = 1;
            save_column_settings(csv_filename);
            return 0;
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q')
        {
            return 1;
        }
    }

    save_column_settings(csv_filename);
    return 0;
}
*/

int show_column_setup(const char *csv_filename)
{
    int height, width;
    getmaxyx(stdscr, height, width);
    int win_top = 2;
    int win_height = height - 4;
    int win_width = width - 4;
    int vis_lines = win_height - 10;

    int top_item = 0;
    int cur_item = 0;

    // Превью — сырые значения из первой строки данных (уже распарсенные)
    char preview_values[MAX_COLS][MAX_LINE_LEN] = {{0}};
    int preview_valid = 0;

    // Загрузка превью (один раз)
    if (row_count > (use_headers ? 1 : 0))
    {
        int preview_row = use_headers ? 1 : 0;
        if (!rows[preview_row].line_cache)
        {
            fseek(f, rows[preview_row].offset, SEEK_SET);
            char *line = malloc(MAX_LINE_LEN);
            if (fgets(line, MAX_LINE_LEN, f))
            {
                line[strcspn(line, "\r\n")] = '\0';
                rows[preview_row].line_cache = line;
            }
            else
            {
                rows[preview_row].line_cache = strdup("");
            }
        }

        int field_count = 0;
        char **fields = parse_csv_line(rows[preview_row].line_cache, &field_count);
        if (fields)
        {
            for (int c = 0; c < field_count && c < col_count; c++)
            {
                strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                preview_values[c][sizeof(preview_values[c]) - 1] = '\0';
            }
            preview_valid = 1;

            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
        }
    }

    while (1)
    {
        clear();

        // Рамка
        attron(COLOR_PAIR(6));
        mvaddch(win_top, 1, ACS_ULCORNER);
        for (int x = 2; x < win_width; x++) mvaddch(win_top, x, ACS_HLINE);
        mvaddch(win_top, win_width, ACS_URCORNER);
        for (int y = win_top + 1; y < win_top + win_height - 2; y++) {
            mvaddch(y, 1, ACS_VLINE);
            mvaddch(y, win_width, ACS_VLINE);
        }
        mvaddch(win_top + win_height - 2, 1, ACS_LLCORNER);
        for (int x = 2; x < win_width; x++) mvaddch(win_top + win_height - 2, x, ACS_HLINE);
        mvaddch(win_top + win_height - 2, win_width, ACS_LRCORNER);
        attroff(COLOR_PAIR(6));

        // Статус заголовков
        attron(COLOR_PAIR(use_headers ? 3 : 2) | A_BOLD);
        mvprintw(win_top + 1, 3, "Headers: %s", use_headers ? "ON  [H to toggle]" : "OFF [H to toggle]");
        attroff(COLOR_PAIR(use_headers ? 3 : 2) | A_BOLD);

        // Заголовки столбцов
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(win_top + 2, 3,   "Column");
        mvprintw(win_top + 2, 65,  "Type");
        mvprintw(win_top + 2, 82,  "Format");
        mvprintw(win_top + 2, 107, "Preview (first data row)");
        attroff(COLOR_PAIR(5) | A_BOLD);

        // Список столбцов
        for (int i = 0; i < vis_lines && top_item + i < col_count; i++)
        {
            int idx = top_item + i;
            char *name = column_names[idx] ? column_names[idx] : "";

            char type_str[32];
            switch (col_types[idx]) {
                case COL_STR:  strcpy(type_str, "String"); break;
                case COL_NUM:  strcpy(type_str, "Number"); break;
                case COL_DATE: strcpy(type_str, "Date");   break;
                default:       strcpy(type_str, "???");    break;
            }

            char fmt_str[64] = "—";
            if (col_types[idx] == COL_STR) {
                if (col_formats[idx].truncate_len > 0) {
                    snprintf(fmt_str, sizeof(fmt_str), "length %d", col_formats[idx].truncate_len);
                }
            } else if (col_types[idx] == COL_NUM) {
                if (col_formats[idx].decimal_places < 0) strcpy(fmt_str, "auto");
                else snprintf(fmt_str, sizeof(fmt_str), "%d decimals", col_formats[idx].decimal_places);
            } else if (col_types[idx] == COL_DATE) {
                if (col_formats[idx].date_format[0]) {
                    strncpy(fmt_str, col_formats[idx].date_format, sizeof(fmt_str) - 1);
                    fmt_str[sizeof(fmt_str) - 1] = '\0';
                } else strcpy(fmt_str, "auto");
            }

            char preview[128] = "(no data)";
            if (preview_valid && idx < col_count && preview_values[idx][0])
            {
                char *formatted = format_cell_value(preview_values[idx], idx);
                strncpy(preview, formatted, sizeof(preview) - 1);
                preview[sizeof(preview) - 1] = '\0';
                free(formatted);
                if (strlen(preview) > 60) strcpy(preview + 57, "...");
            }

            if (idx == cur_item) attron(A_REVERSE);

            // Имя столбца с маркером скрытия
            if (col_hidden[idx]) {
                attron(COLOR_PAIR(2));
                mvprintw(win_top + 4 + i, 3, "[H] %-56s", name);
                attroff(COLOR_PAIR(2));
            } else {
                mvprintw(win_top + 4 + i, 3, "    %-56s", name);
            }
            mvprintw(win_top + 4 + i, 65,  "%-15s", type_str);
            mvprintw(win_top + 4 + i, 82,  "%-25s", fmt_str);
            mvprintw(win_top + 4 + i, 107, "%-50s", preview);

            attroff(A_REVERSE);
        }

        // Подсказки (обновлённые)
        char hint[128];
        snprintf(hint, sizeof(hint),
                 "[ ↑↓/jk move • S/N/D type • X hide/show • Enter edit • H headers • q/Esc save&exit ]");

        int hint_len = strlen(hint);
        int hint_x = (win_width - hint_len) / 2;

        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(win_top + win_height - 2, hint_x, "%s", hint);
        attroff(COLOR_PAIR(1) | A_BOLD);

        refresh();

        int ch = getch();

        if (ch == KEY_UP || ch == 'k' || ch == 'K')
        {
            if (cur_item > 0) {
                cur_item--;
                if (cur_item < top_item) top_item--;
            }
        }
        else if (ch == KEY_DOWN || ch == 'j' || ch == 'J')
        {
            if (cur_item < col_count - 1) {
                cur_item++;
                if (cur_item >= top_item + vis_lines) top_item++;
            }
        }
        else if (ch == 's' || ch == 'S')
        {
            col_types[cur_item] = COL_STR;
        }
        else if (ch == 'n' || ch == 'N')
        {
            col_types[cur_item] = COL_NUM;
        }
        else if (ch == 'd' || ch == 'D')
        {
            col_types[cur_item] = COL_DATE;
        }
        else if (ch == '\n' || ch == KEY_ENTER)
        {
            // Вход в редактирование формата текущего столбца
            echo();
            curs_set(1);
            char buf[64] = "";
            const char *prompt = "";
            int max_input_len = 63;

            if (col_types[cur_item] == COL_STR) {
                prompt = "Length (0=all): ";
                snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].truncate_len);
            } else if (col_types[cur_item] == COL_NUM) {
                prompt = "Precisions (-1=auto): ";
                snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].decimal_places);
            } else if (col_types[cur_item] == COL_DATE) {
                prompt = "Date format (ex: %Y-%m-%d): ";
                strncpy(buf, col_formats[cur_item].date_format, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
            } else {
                prompt = "Wrong type";
            }

            mvprintw(win_top + 4 + (cur_item - top_item), 82, "%s", prompt);
            clrtoeol();
            mvprintw(win_top + 4 + (cur_item - top_item), 82 + strlen(prompt), "%s", buf);
            move(win_top + 4 + (cur_item - top_item), 82 + strlen(prompt) + strlen(buf));
            int pos = strlen(buf);
            int done = 0;

            while (!done)
            {
                int key = getch();
                if (key == '\n' || key == KEY_ENTER)
                {
                    // Применяем
                    if (col_types[cur_item] == COL_STR) {
                        col_formats[cur_item].truncate_len = atoi(buf);
                    } else if (col_types[cur_item] == COL_NUM) {
                        col_formats[cur_item].decimal_places = atoi(buf);
                    } else if (col_types[cur_item] == COL_DATE) {
                        strncpy(col_formats[cur_item].date_format, buf,
                                sizeof(col_formats[cur_item].date_format) - 1);
                        col_formats[cur_item].date_format[sizeof(col_formats[cur_item].date_format) - 1] = '\0';
                    }
                    done = 1;
                }
                else if (key == 27) { // Esc — отмена редактирования
                    done = 1;
                }
                else if (key == KEY_BACKSPACE || key == 127)
                {
                    if (pos > 0) {
                        pos--;
                        buf[pos] = '\0';
                    }
                }
                else if (key >= 32 && key <= 126 && pos < max_input_len)
                {
                    buf[pos++] = (char)key;
                    buf[pos] = '\0';
                }

                // Перерисовка поля ввода
                mvprintw(win_top + 4 + (cur_item - top_item), 82, "%s", prompt);
                clrtoeol();
                printw("%s", buf);
                move(win_top + 4 + (cur_item - top_item), 82 + strlen(prompt) + pos);
                refresh();
            }

            noecho();
            curs_set(0);
        }
        else if (ch == 'x' || ch == 'X')
        {
            col_hidden[cur_item] = !col_hidden[cur_item];
            save_column_settings(csv_filename);
        }
        else if (ch == 'h' || ch == 'H')
        {
            use_headers = !use_headers;
            save_column_settings(csv_filename);

            // Перезагружаем превью для новой строки
            memset(preview_values, 0, sizeof(preview_values));
            preview_valid = 0;
            if (row_count > (use_headers ? 1 : 0))
            {
                int preview_row = use_headers ? 1 : 0;
                if (!rows[preview_row].line_cache)
                {
                    fseek(f, rows[preview_row].offset, SEEK_SET);
                    char *pline = malloc(MAX_LINE_LEN);
                    if (fgets(pline, MAX_LINE_LEN, f))
                    {
                        pline[strcspn(pline, "\r\n")] = '\0';
                        rows[preview_row].line_cache = pline;
                    }
                    else
                    {
                        rows[preview_row].line_cache = strdup("");
                        free(pline);
                    }
                }
                int field_count = 0;
                char **fields = parse_csv_line(rows[preview_row].line_cache, &field_count);
                if (fields)
                {
                    for (int c = 0; c < field_count && c < col_count; c++)
                    {
                        strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                        preview_values[c][sizeof(preview_values[c]) - 1] = '\0';
                    }
                    preview_valid = 1;
                    for (int k = 0; k < field_count; k++) free(fields[k]);
                    free(fields);
                }
            }
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q')
        {
            save_column_settings(csv_filename);
            return 0;
        }
    }

    save_column_settings(csv_filename);
    return 0;
}
