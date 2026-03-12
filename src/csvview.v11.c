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
#include "help.h"
#include "concat_files.h"
#include "split_file.h"

// ────────────────────────────────────────────────
// Глобальные переменные — определения (инициализация)
// ────────────────────────────────────────────────

// 1. Фильтры и сохранённые запросы
char *saved_filters[MAX_SAVED_FILTERS] = {NULL};
int   saved_filter_count = 0;

// 2. Поиск
SearchResult search_results[MAX_SEARCH_RESULTS] = {{0}};
int          search_count    = 0;
int          search_index    = -1;
char         search_query[256] = "";
int          in_search_mode  = 0;

// 3. Фильтрация
int  filtered_rows[MAX_ROWS] = {0};
int  filtered_count          = 0;
char filter_query[256]       = "";
int  in_filter_mode          = 0;
int  filter_active           = 0;

// 4. Навигация и текущее состояние таблицы
int  cur_display_row  = 0;     // позиция курсора в видимом списке
int  top_display_row  = 0;     // первая видимая строка
int  cur_real_row     = 0;     // реальный номер строки в файле
int  cur_col          = 0;
int  left_col         = 0;
char file_size_str[32] = "неизв.";

// 5. Столбцы и метаданные
char       *column_names[MAX_COLS] = {NULL};
ColType     col_types[MAX_COLS]    = {0};
ColumnFormat col_formats[MAX_COLS]  = {{0}};
int         col_count              = 0;
int         use_headers            = 0;
int         col_widths[MAX_COLS]   = {0};

// 6. Сортировка
int sorted_rows[MAX_ROWS] = {0};
int sorted_count          = 0;
int sort_col              = -1;     // -1 = без сортировки
int sort_order            = 0;      // 1 = asc, -1 = desc, 0 = none

// 7. Данные файла
int       row_count = 0;
RowIndex *rows      = NULL;
FILE     *f         = NULL;

// 8. Графики и визуализация
int       in_graph_mode           = 0;
int       graph_col_list[10]      = {0};
int       graph_col_count         = 0;
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

// 9. Сохранение состояния (для undo/временного хранения)
int   save_sort_col       = -1;

int   save_sort_order     = 0;
int   save_sorted_count   = 0;
int  *save_sorted_rows    = NULL;
int   save_filtered_count = 0;
int  *save_filtered_rows  = NULL;

// 10. Разделитель полей (по умолчанию ',' — CSV)
char csv_delimiter = ',';

// 11. Заморозка столбцов (первые N всегда видны)
int freeze_cols = 0;

// 12. Drill-down из pivot: фильтр для возврата в основную таблицу
char pivot_drilldown_filter[512] = "";

// ────────────────────────────────────────────────
// main
// ────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "/h") == 0)) {
        show_help(0);  // консольный вывод
        return 0;
    }

    int concat_mode = 0;
    int split_mode  = 0;
    int split_drop  = 0;
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
        } else if (strcmp(argv[i], "--drop-col") == 0) {
            split_drop = 1;
        } else if (strncmp(argv[i], "--by=", 5) == 0) {
            split_by = argv[i] + 5;
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
        if (argc < 2) {
            fprintf(stderr, "Usage: %s <file.csv>\n", argv[0]);
            free(input_files);
            return 1;
        }
        file_to_open = strdup(argv[1]);
        free(input_files);
    }

    // Автодетект разделителя по расширению файла (если не задан --sep)
    if (csv_delimiter == ',') {
        const char *ext = strrchr(file_to_open, '.');
        if (ext) {
            if (strcasecmp(ext, ".tsv") == 0)      csv_delimiter = '\t';
            else if (strcasecmp(ext, ".psv") == 0) csv_delimiter = '|';
        }
    }

    f = fopen(file_to_open, "r");
    if (!f) { perror("fopen"); return 1; }

    struct stat st;
    if (fstat(fileno(f), &st) == 0) {
        long long size = st.st_size;
        if (size < 1024LL * 1024) snprintf(file_size_str, sizeof(file_size_str), "%.0f KB", size / 1024.0);
        else if (size < 1024LL * 1024 * 1024) snprintf(file_size_str, sizeof(file_size_str), "%.1f MB", size / (1024.0 * 1024.0));
        else snprintf(file_size_str, sizeof(file_size_str), "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }

    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    if (!rows) { perror("malloc"); return 1; }

    rows[0].offset = 0; rows[0].line_cache = NULL;
    long pos = 0;
    row_count = 1;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        pos += strlen(buf);
        if (row_count < MAX_ROWS) {
            rows[row_count].offset = pos;
            rows[row_count].line_cache = NULL;
            row_count++;
        }
    }
    row_count--;
    rewind(f);

    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();

        init_color(COLOR_WHITE + 9, 1000 * 119 / 255, 1000 * 131 / 255, 1000 * 133 / 255); // выделенная ячейка
        init_color(COLOR_WHITE + 8, 1000 * 141 / 255, 1000 * 141 / 255, 1000 * 141 / 255); // основной цвет для текста

        init_pair(1, 250, COLOR_BLACK);
        init_pair(2, COLOR_BLACK, COLOR_WHITE + 9);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(5, COLOR_CYAN, COLOR_BLACK);
        init_pair(6, 244, COLOR_BLACK);
        init_pair(7, COLOR_BLACK, COLOR_YELLOW);

        if (can_change_color()) {
            init_pair(8, COLOR_WHITE + 8, COLOR_BLACK);
        } else {
            init_pair(8, COLOR_WHITE, COLOR_BLACK);  // или COLOR_WHITE с A_DIM
        }

        init_pair(GRAPH_COLOR_BASE + 0, COLOR_RED,     COLOR_BLACK);
        init_pair(GRAPH_COLOR_BASE + 1, COLOR_GREEN,   COLOR_BLACK);
        init_pair(GRAPH_COLOR_BASE + 2, COLOR_BLUE,    COLOR_BLACK);
        init_pair(GRAPH_COLOR_BASE + 3, COLOR_YELLOW,  COLOR_BLACK);
        init_pair(GRAPH_COLOR_BASE + 4, COLOR_CYAN,    COLOR_BLACK);
        init_pair(GRAPH_COLOR_BASE + 5, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(GRAPH_COLOR_BASE + 6, COLOR_WHITE,   COLOR_BLACK); // default        
    }

    if (col_count == 0) {  // защита от повторного парсинга
        if (!rows[0].line_cache) {
            fseek(f, rows[0].offset, SEEK_SET);
            char *line = malloc(MAX_LINE_LEN);
            if (!line) {
                // обработка ошибки памяти, например:
                endwin();
                fprintf(stderr, "Не хватает памяти для заголовка\n");
                exit(1);
            }
            if (fgets(line, MAX_LINE_LEN, f)) {
                // Убираем ВСЕ возможные окончания строки и мусор
                line[strcspn(line, "\r\n")] = '\0';
                // Дополнительно убираем trailing пробелы (включая неразрывный пробел)
                char *end = line + strlen(line) - 1;
                while (end >= line && 
                       (*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0)) {
                    *end-- = '\0';
                }
                rows[0].line_cache = line;
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
                // Убираем ведущие пробелы/табы/неразрывные пробелы
                while (*t == ' ' || *t == '\t' || (unsigned char)*t == 0xA0) t++;
                // Убираем trailing пробелы/табы/неразрывные пробелы
                char *end = t + strlen(t) - 1;
                while (end >= t &&
                       (*end == ' ' || *end == '\t' || (unsigned char)*end == 0xA0))
                    *end-- = '\0';

                column_names[col_count] = strdup(t);
                if (!column_names[col_count])
                    column_names[col_count] = strdup("(ошибка памяти)");
                col_types[col_count] = COL_STR;
                col_count++;
            }
            free_csv_fields(hdr_fields, hdr_count);
        }

        // Инициализируем форматы после определения col_count
        init_column_formats();
    }

    int settings_loaded = load_column_settings(file_to_open);

    if (!settings_loaded) {
        if (show_column_setup(file_to_open)) {
            endwin();
            return 0;
        }
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
    int left_col = 0;

    char current_cell_content[256] = "(пусто)";
    char col_name[32];

    while (1) {
        clear();

        int table_top = 3;
        int table_height = height - table_top - 1;
        int table_width = width - 4;
        int visible_rows = table_height - 3;
        // Вычисляем ширину замороженных столбцов
        int frozen_px = 0;
        for (int fc = 0; fc < freeze_cols && fc < col_count; fc++)
            frozen_px += col_widths[fc] + 2;
        if (freeze_cols > 0 && freeze_cols < col_count) frozen_px += 1; // сепаратор

        // visible_cols = число скроллируемых (не замороженных) столбцов
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
            int col = graph_col_list[current_graph];
            draw_graph(col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
        } else {
            draw_table_body(table_top, ROW_DATA_OFFSET, visible_rows, top_display_row, cur_display_row, cur_col, left_col, visible_cols, rows, f, row_count);
        }

        current_cell_content[0] = '\0';
        if (cur_real_row < row_count) {
            if (!rows[cur_real_row].line_cache) {
                fseek(f, rows[cur_real_row].offset, SEEK_SET);
                char *line = malloc(MAX_LINE_LEN);
                if (fgets(line, MAX_LINE_LEN, f)) {
                    line[strcspn(line, "\r\n")] = '\0';  // лучше убирать и \r и \n
                    rows[cur_real_row].line_cache = line;
                } else {
                    rows[cur_real_row].line_cache = strdup("");
                }
            }

            // Вот здесь вызываем новую функцию
            char *cell_content = get_cell_content(rows[cur_real_row].line_cache, cur_col);

            // Защита от слишком длинного
            if (strlen(cell_content) > 200) {
                strcpy(current_cell_content, "(очень длинный текст)");
            } else {
                strncpy(current_cell_content, cell_content, sizeof(current_cell_content)-1);
                current_cell_content[sizeof(current_cell_content)-1] = '\0';
            }

            free(cell_content);
        }
        if (use_headers && column_names[cur_col]) {
            strncpy(col_name, column_names[cur_col], 31);
            col_name[31] = '\0';
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

        refresh();

        int ch = getch();

        if (in_graph_mode) {
            if (ch == 'q' || ch == 27) {  // Esc
                in_graph_mode = 0;
                if (using_date_x) {
                    sort_col = save_sort_col;
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
                if (in_filter_mode) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(3));
                    printw(" | Filtering...               ");
                    attroff(COLOR_PAIR(3));
                    refresh();
                    
                    apply_filter(rows, f, row_count);
                }

                continue;
            } else if (ch == KEY_LEFT || ch == 'j' || ch == 'h') {
                if (graph_cursor_pos > 0) graph_cursor_pos--;
                min_max_show = 0;
            } else if (ch == KEY_RIGHT || ch == 'k' || ch == 'l') {
                if (graph_cursor_pos < graph_visible_points - 1) graph_cursor_pos++;
                min_max_show = 0;
            } else if (ch == 'r') {
                // Redraw (no-op, loop will redraw)
            } else if (ch == ':') {  // Вход в режим команд
                char cmd_buf[128] = {0};

                attron(COLOR_PAIR(3));
                printw(" | :");
                attroff(COLOR_PAIR(3));
                refresh();
                echo();
                wgetnstr(stdscr, cmd_buf, sizeof(cmd_buf) - 1);
                noecho();

                // Разделяем команду и аргумент (если есть)
                char *cmd = cmd_buf;
                char *arg = strchr(cmd_buf, ' ');
                if (arg) {
                    *arg = '\0';
                    arg++;
                    // убираем лишние пробелы в начале аргумента
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

                    // Убираем кавычки, если они есть
                    if (arg_copy[0] == '"' && arg_copy[strlen(arg_copy)-1] == '"') {
                        memmove(arg_copy, arg_copy + 1, strlen(arg_copy) - 2);
                        arg_copy[strlen(arg_copy) - 2] = '\0';
                    }

                    // Проверяем на "off"
                    if (strcasecmp(arg_copy, "off") == 0) {
                        date_x_col = -1;
                        using_date_x = 0;

                        // Возвращаем старую сортировку, если она была сохранена
                        if (save_sort_col != -999) {  // -999 как маркер "не сохранено"
                            sort_col = save_sort_col;
                            sort_order = save_sort_order;

                            if (filter_active) {
                                // Восстанавливаем отфильтрованные индексы (если нужно)
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

                        // Перерисовываем график, если мы в режиме
                        if (in_graph_mode) {
                            clear();
                            int col = graph_col_list[current_graph];
                            draw_graph(col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                            refresh();
                        }
                        continue;
                    }

                    // Обычный выбор столбца
                    int selected_col = -1;

                    // По букве (A, B, AA...)
                    if (strlen(arg_copy) <= 3 && isupper(arg_copy[0])) {
                        selected_col = col_to_num(arg_copy);
                    }
                    // По имени столбца
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

                    // Сохраняем текущую сортировку (только если ещё не сохранено)
                    if (save_sort_col == -999) {  // маркер "не сохранено"
                        save_sort_col = sort_col;
                        save_sort_order = sort_order;
                        if (filter_active) {
                            save_filtered_count = filtered_count;
                            save_filtered_rows = malloc(sizeof(int) * filtered_count);
                            if (save_filtered_rows) {
                                memcpy(save_filtered_rows, filtered_rows, sizeof(int) * filtered_count);
                            }
                        } else {
                            save_sorted_count = sorted_count;
                            save_sorted_rows = malloc(sizeof(int) * MAX_ROWS);
                            if (save_sorted_rows) {
                                memcpy(save_sorted_rows, sorted_rows, sizeof(int) * sorted_count);
                            }
                        }
                    }

                    // Сортируем таблицу по выбранному столбцу дат (по возрастанию)
                    sort_col = selected_col;
                    sort_order = 1;

                    if (filter_active) {
                        qsort(filtered_rows, filtered_count, sizeof(int), compare_rows_by_column);
                    } else {
                        build_sorted_index();
                        sorted_count = row_count - (use_headers ? 1 : 0);
                    }

                    // Восстанавливаем старый sort_col / sort_order (но индексы уже отсортированы по дате!)
                    sort_col = save_sort_col;
                    sort_order = save_sort_order;

                    // Устанавливаем новую ось X
                    date_x_col = selected_col;
                    using_date_x = 1;

                    // Если уже в режиме графика — сразу перерисовываем
                    if (in_graph_mode) {
                        clear();
                        int col = graph_col_list[current_graph];
                        draw_graph(col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);
                        refresh();
                    }

                    // Сообщение об успехе
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

                    // Перерисовываем график сразу (без getch, просто обновляем экран)
                    clear();
                    draw_graph(cur_col, height, width, rows, f, row_count, graph_cursor_pos, min_max_show);  // или текущую колонку
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

                    // Перерисовываем график сразу
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

                    // Перерисовываем график сразу
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
                }                

                clrtoeol();
                refresh();
            } else if (ch == 'm' || ch == 'M') {  // m — минимум, M — максимум 
                if (ch == 'm') {  // ищем минимум
                    min_max_show = 1;
                } else {           // ищем максимум
                    min_max_show = 2;
                }

                // Перерисовываем график с новым курсором и позицией
                draw_graph(current_graph, LINES - 2, COLS, rows, f, row_count, graph_cursor_pos, min_max_show);
            }


            continue;
        }

        if (ch == 'g' || ch == 'G') {
            if (ch == 'g') {
                if (col_types[cur_col] != COL_NUM) {
                    draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                    attron(COLOR_PAIR(1));
                    printw(" | Not a numeric column");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    continue;
                }
                graph_col_list[0] = cur_col;
                graph_col_count = 1;
            } 
            current_graph = 0;
            graph_start = 0;
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
                    save_sort_col = sort_col;
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
                        save_sorted_rows = malloc(sizeof(int) * MAX_ROWS);
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

        if (ch == 's' || ch == 'S') {
            int save_ok = save_file(file_to_open, f, rows, row_count);

            // Сначала рисуем обычный статус (чтобы не было пустоты)
            move(height - 1, 1);
            clrtoeol();
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);

            // Добавляем сообщение справа
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
            napms(1500);  // 1.5 секунды — показываем сообщение

            // Возвращаем обычный статус (стираем только правую часть)
            move(height - 1, width - 35);
            clrtoeol();
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            refresh();
        }        

        if (ch == '\n' || ch == KEY_ENTER) {
            if (cur_real_row >= row_count) continue;

            // Защита от NULL
            if (!rows[cur_real_row].line_cache) {
                rows[cur_real_row].line_cache = strdup("");
            }

            char edit_buffer[INPUT_BUF_SIZE] = {0};
            strncpy(edit_buffer, current_cell_content, INPUT_BUF_SIZE - 1);
            int buffer_len = strlen(edit_buffer);
            int pos = buffer_len;

            echo();
            curs_set(1);

            // Вывод строки редактирования с правильным порядком аргументов
            mvprintw(2, 2, "%s%d: ", col_name, cur_display_row + 1);
            clrtoeol();
            printw("%s", edit_buffer);
            refresh();

            // Позиция курсора — строка 2
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

            // Восстанавливаем старое значение в поле ячейки
            mvprintw(2, 2, "%s%d: ", col_name, cur_display_row + 1);
            clrtoeol();
            printw("%s", current_cell_content);
            refresh();

            if (!canceled && strcmp(edit_buffer, current_cell_content) != 0) {
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

            echo();
            curs_set(1);

            int pos = 0;
            while (1) {
                int key = getch();
                if (key == '\n' || key == KEY_ENTER) break;
                if (key == 27) { in_filter_mode = 0; break; }
                if ((key == KEY_BACKSPACE || key == 127) && pos > 0) {
                    pos--;
                    filter_query[pos] = '\0';
                } else if (key >= 32 && key <= 126 && pos < 255) {
                    filter_query[pos++] = (char)key;
                    filter_query[pos] = '\0';
                }

                mvprintw(2, 2, "F:%s", filter_query);
                clrtoeol();
                move(2, 4 + pos);
                refresh();
            }

            noecho();
            curs_set(0);

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

            // Drill-down: пользователь нажал Enter на ячейке pivot
            if (pivot_drilldown_filter[0]) {
                strncpy(filter_query, pivot_drilldown_filter, sizeof(filter_query) - 1);
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

        // Навигация по видимым строкам
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
            if (cur_col > 0) {
                cur_col--;
                // Прокрутка нужна только для скроллируемой области
                if (cur_col >= freeze_cols && cur_col < left_col) {
                    left_col = cur_col;
                    if (left_col < freeze_cols) left_col = freeze_cols;
                }
            }
        }
        else if (ch == KEY_RIGHT || ch == 'l') {
            if (cur_col < col_count - 1) {
                cur_col++;
                // Прокрутка нужна только для скроллируемой области
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
            left_col = freeze_cols;
            if (left_col >= col_count) left_col = col_count > 0 ? col_count - 1 : 0;
        }
        else if (ch == 'L') {
            cur_col = col_count - 1;
            left_col = cur_col - visible_cols + 1;
            if (left_col < freeze_cols) left_col = freeze_cols;
            if (left_col < 0) left_col = 0;
        }
        else if (ch == 'z' || ch == 'Z') {
            // z — заморозить/разморозить на текущем столбце
            if (freeze_cols == cur_col + 1) {
                freeze_cols = 0;  // снять заморозку
            } else {
                freeze_cols = cur_col + 1;
            }
            if (freeze_cols > col_count) freeze_cols = col_count;
            if (left_col < freeze_cols) left_col = freeze_cols;
            save_column_settings(file_to_open);
        }
        else if (ch == 't' || ch == 'T') {           // F2
            clear();
            show_column_setup(file_to_open);  // false = не начальная настройка
            // после возврата — таблица перерисуется автоматически в следующем цикле
        }
        else if (ch == 'w') {
            if (cur_col < col_count) {
                col_widths[cur_col] += 1;  // увеличить на 5
                if (col_widths[cur_col] > 100) col_widths[cur_col] = 100;  // лимит
            }
            save_column_settings(file_to_open);
        } else if (ch == 'W') {  // Shift+w
            if (cur_col < col_count) {
                col_widths[cur_col] -= 1;
                if (col_widths[cur_col] < 5) col_widths[cur_col] = 5;  // минимум
            }
            save_column_settings(file_to_open);
        } else if (ch == 'a' || ch == 'A') {
            // Авто-подгонка ширины текущего столбца
            if (cur_col < col_count) {
                int max_len = 8;  // минимум для имени столбца
                int sample_rows = filtered_count > 0 ? filtered_count : row_count;
                if (sample_rows > 100) sample_rows = 100;  // сэмплируем максимум 100 строк

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

                // +2 для отступа, +5 для комфорта
                col_widths[cur_col] = max_len + 7;
                if (col_widths[cur_col] > 80) col_widths[cur_col] = 80;
            }
            save_column_settings(file_to_open);
        }
        else if (ch == '[') {
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Sorting...                       ");
            attroff(COLOR_PAIR(3));
            refresh();
            // Сортировка по возрастанию по текущему столбцу
            if (cur_col >= 0 && cur_col < col_count) {
                sort_col = cur_col;
                sort_order = 1;
                build_sorted_index();
                // Сбрасываем позицию на начало, чтобы пользователь видел результат
                cur_display_row = 0;
                top_display_row = 0;
            }
        }
        else if (ch == ']') {
            draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Sorting...                  ");
            attroff(COLOR_PAIR(3));
            refresh();
            // Сортировка по убыванию
            if (cur_col >= 0 && cur_col < col_count) {
                sort_col = cur_col;
                sort_order = -1;
                build_sorted_index();
                cur_display_row = 0;
                top_display_row = 0;
            }
        }
        else if (ch == 'r' || ch == 'R') {
            // Отмена сортировки
            sort_col = -1;
            sort_order = 0;
            sorted_count = 0;
            cur_display_row = 0;
            top_display_row = 0;
        }
        else if (ch == ('R' & 0x1f)) {  // Ctrl+R — перечитать файл
            // 1. Освобождаем все кэши строк
            for (int i = 0; i < row_count; i++) {
                free(rows[i].line_cache);
                rows[i].line_cache = NULL;
            }

            // 2. Переоткрываем файл
            fclose(f);
            f = fopen(file_to_open, "r");
            if (!f) {
                mvprintw(height - 1, 0, "Reload failed: cannot open %s", file_to_open);
                refresh();
                continue;
            }

            // 3. Обновляем размер файла
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

            // 4. Переиндексируем строки
            rows[0].offset = 0;
            rows[0].line_cache = NULL;
            long rpos = 0;
            row_count = 1;
            char rbuf[1024];
            while (fgets(rbuf, sizeof(rbuf), f)) {
                rpos += strlen(rbuf);
                if (row_count < MAX_ROWS) {
                    rows[row_count].offset = rpos;
                    rows[row_count].line_cache = NULL;
                    row_count++;
                }
            }
            row_count--;
            rewind(f);

            // 5. Переприменяем фильтр если был активен
            if (strlen(filter_query) > 0) {
                apply_filter(rows, f, row_count);
            }

            // 6. Переприменяем сортировку если была активна
            if (sort_col >= 0) {
                if (filter_active) {
                    qsort(filtered_rows, filtered_count, sizeof(int), compare_rows_by_column);
                } else {
                    build_sorted_index();
                    sorted_count = row_count - (use_headers ? 1 : 0);
                }
            }

            // 7. Корректируем позицию курсора (файл мог стать короче)
            int new_display_count = filter_active
                ? filtered_count
                : (row_count - (use_headers ? 1 : 0));
            if (cur_display_row >= new_display_count)
                cur_display_row = new_display_count > 0 ? new_display_count - 1 : 0;
            if (top_display_row > cur_display_row)
                top_display_row = cur_display_row;
        }
        else if (ch == 'D' || ch == 'd') {  
            // статистика столбца
            show_column_stats(cur_col);
        }

        // После ввода фильтра (Shift+F или f) — когда ты уже в режиме ввода
        // Но чтобы :fs/:fl работали глобально — лучше обрабатывать в основном цикле
        else if (ch == ':') {  // Вход в режим команд
            char cmd_buf[128] = {0};

            attron(COLOR_PAIR(3));
            printw(" | :");
            attroff(COLOR_PAIR(3));
            refresh();
            echo();
            wgetnstr(stdscr, cmd_buf, sizeof(cmd_buf) - 1);
            noecho();

            // Разделяем команду и аргумент (если есть)
            char *cmd = cmd_buf;
            char *arg = strchr(cmd_buf, ' ');
            if (arg) {
                *arg = '\0';
                arg++;
                // убираем лишние пробелы в начале аргумента
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
            } else if (strcmp(cmd, "fs") == 0 && filter_active) {
                // Сохраняем текущий filter_query
                //char cfg_path[1024];
                save_filter(file_to_open, filter_query);

                attron(COLOR_PAIR(3));
                printw(" | Filter saved to %s.csvf", file_to_open);
                attroff(COLOR_PAIR(3));
                refresh();
            } else if (strcmp(cmd, "fl") == 0) {
                // Показываем список
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
            } else if (strcmp(cmd, "fz") == 0) {
                // :fz N — заморозить первые N столбцов (:fz 0 — снять заморозку)
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
            } else if (strcmp(cmd, "fqu") == 0) {
                // :fqu — быстрый фильтр ТОЛЬКО по текущей ячейке (сбрасывает старый фильтр)

                // Получаем имя текущего столбца
                char col_name[64] = {0};
                if (use_headers && column_names[cur_col]) {
                    strncpy(col_name, column_names[cur_col], sizeof(col_name) - 1);
                } else {
                    col_letter(cur_col, col_name);
                }

                // Получаем значение текущей ячейки
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

                // Формируем новый фильтр (только этот)
                char new_filter[256] = {0};
                snprintf(new_filter, sizeof(new_filter), "%s = \"%s\"", col_name, cell);
                free(cell);

                // Сбрасываем старый фильтр и вставляем новый
                strncpy(filter_query, new_filter, sizeof(filter_query) - 1);
                filter_query[sizeof(filter_query) - 1] = '\0';

                // Применяем
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
                // Быстрый фильтр по текущей ячейке с модификаторами n/o/no

                char op_eq = ' ';   // по умолчанию =
                char *logic = "AND"; // по умолчанию AND

                // Проверяем суффикс после fq
                char suffix[4] = {0};
                if (strlen(cmd) > 2) strncpy(suffix, cmd + 2, 3);

                if (strstr(suffix, "n")) op_eq = '!'; // !=
                if (strstr(suffix, "o")) logic = "OR";

                // Получаем имя столбца
                char col_name[64] = {0};
                if (use_headers && column_names[cur_col]) {
                    strncpy(col_name, column_names[cur_col], sizeof(col_name) - 1);
                } else {
                    col_letter(cur_col, col_name);
                }

                // Получаем значение ячейки
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

                // Формируем условие
                char new_cond[256] = {0};
                snprintf(new_cond, sizeof(new_cond), "%s %c= \"%s\"", col_name, op_eq, cell);
                free(cell);

                // Добавляем к существующему фильтру
                if (filter_query[0] != '\0') {
                    char temp[512];
                    snprintf(temp, sizeof(temp), " %s %s", logic, new_cond);
                    strncat(filter_query, temp, sizeof(filter_query) - strlen(filter_query) - 1);
                } else {
                    strncpy(filter_query, new_cond, sizeof(filter_query) - 1);
                    filter_query[sizeof(filter_query) - 1] = '\0';
                }

                // Применяем
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

                // Реальный индекс строки в файле
                int display_pos = row_num - 1;
                int real_row = get_real_row(display_pos);

                if (real_row < 0 || real_row >= row_count) {
                    mvprintw(LINES - 1, 0, "Error: invalid real row");
                    refresh();
                    getch();
                    continue;
                }

                // Удаляем строку из кэша (освобождаем память)
                free(rows[real_row].line_cache);
                rows[real_row].line_cache = NULL;

                // Сдвигаем массив rows влево
                for (int i = real_row; i < row_count - 1; i++) {
                    rows[i] = rows[i + 1];
                }
                row_count--;

                // Если строка была в filtered — обновляем filtered_rows
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

                // Перезаписываем файл без удалённой строки
                if (save_file(file_to_open, f, rows, row_count) != 0) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Failed to save file after row deletion");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    continue;
                }

                // Переиндексация offsets (файл изменился)
                free(rows);
                rows = NULL;
                row_count = 0;

                fclose(f);
                f = fopen(file_to_open, "r");
                if (!f) {
                    mvprintw(LINES - 1, 0, "Failed to reopen file");
                    refresh();
                    getch();
                    continue;
                }

                char buf[MAX_LINE_LEN];
                long offset = 0;
                rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
                while (fgets(buf, sizeof(buf), f)) {
                    if (row_count >= MAX_ROWS) break;
                    rows[row_count].offset = offset;
                    rows[row_count].line_cache = NULL;
                    offset += strlen(buf);
                    row_count++;
                }

                // Обновляем и сохраняем настройки столбцов
                save_column_settings(file_to_open);

                // Корректируем позицию курсора (если удаляли строку ниже курсора — ничего не меняем)
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

                // Убираем кавычки, если они есть
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

                // Старое имя для сообщения
                char old_name[64] = {0};
                if (use_headers && column_names[cur_col]) {
                    strncpy(old_name, column_names[cur_col], sizeof(old_name) - 1);
                } else {
                    col_letter(cur_col, old_name);
                }

                // Освобождаем старое имя (если было)
                if (column_names[cur_col]) {
                    free(column_names[cur_col]);
                }

                // Устанавливаем новое
                column_names[cur_col] = strdup(new_name);

                // Перезаписываем файл (меняется заголовок)
                if (save_file(file_to_open, f, rows, row_count) != 0) {
                    attron(COLOR_PAIR(1));
                    mvprintw(LINES - 1, 0, "Failed to save file after rename");
                    attroff(COLOR_PAIR(1));
                    refresh();
                    getch();
                    continue;
                }

                // Переиндексация offsets после сохранения
                free(rows);
                rows = NULL;
                row_count = 0;

                fclose(f);
                f = fopen(file_to_open, "r");
                if (!f) {
                    mvprintw(LINES - 1, 0, "Failed to reopen file");
                    refresh();
                    getch();
                    continue;
                }

                char buf[MAX_LINE_LEN];
                long offset = 0;
                rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
                while (fgets(buf, sizeof(buf), f)) {
                    if (row_count >= MAX_ROWS) break;
                    rows[row_count].offset = offset;
                    rows[row_count].line_cache = NULL;
                    offset += strlen(buf);
                    row_count++;
                }

                rebuild_header_row();
                save_file(file_to_open, f, rows, row_count);    

                draw_status_bar(height - 1, 1, file_to_open, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Column renamed: %s → %s", old_name, new_name);
                attroff(COLOR_PAIR(3));

                refresh();
            } else if (strcmp(cmd, "cs") == 0) {
                // :cs [filename] — сохранить текущий столбец в CSV (одна колонка)

                char filename[256] = {0};

                if (arg && *arg) {
                    // Имя задано явно
                    strncpy(filename, arg, sizeof(filename) - 5);
                    filename[sizeof(filename) - 5] = '\0';
                } else {
                    // Имя из столбца
                    if (use_headers && column_names[cur_col]) {
                        strncpy(filename, column_names[cur_col], sizeof(filename) - 5);
                    } else {
                        col_letter(cur_col, filename);
                    }
                }

                // Добавляем .csv если нет
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

                // Пишем заголовок (если есть)
                if (use_headers && column_names[cur_col]) {
                    fprintf(out, "\"%s\"\n", column_names[cur_col]);
                } else {
                    char letter_buf[16];
                    col_letter(cur_col, letter_buf);
                    fprintf(out, "\"%s\"\n", letter_buf);
                }

                // Пишем значения столбца
                int row_start = use_headers ? 1 : 0;
                for (int r = row_start; r < row_count; r++) {
                    char *cell = get_column_value(rows[r].line_cache,
                                                  column_names[cur_col] ? column_names[cur_col] : "",
                                                  use_headers);

                    // Экранируем значение
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

                // Если имя передано сразу после пробела
                if (arg && *arg) {
                    strncpy(filename, arg, sizeof(filename) - 5);
                    filename[sizeof(filename) - 5] = '\0';
                } else {
                    // Дефолтное имя
                    if (filter_active) {
                        strcpy(filename, "filtered.csv");
                    } else if (sort_col >= 0) {
                        strcpy(filename, "sorted.csv");
                    } else {
                        strcpy(filename, "table.csv");
                    }

                    // Запрос имени файла
                    mvprintw(LINES - 1, 0, "Export to: [%s] ", filename);
                    refresh();
                    echo();
                    wgetnstr(stdscr, filename, sizeof(filename) - 1);
                    noecho();
                    clrtoeol();
                }

                // Добавляем .csv если нет
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

                // Пишем заголовок (если есть)
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

                // Пишем строки (отфильтрованные или все)
                int count_exported = 0;
                int display_count = filter_active ? filtered_count : row_count;
                int row_start = use_headers ? 1 : 0;

                for (int i = 0; i < display_count; i++) {
                    int display_pos = i;
                    int real_row = get_real_row(display_pos);
                    if (real_row < row_start) continue;

                    if (!rows[real_row].line_cache) {
                        fseek(f, rows[real_row].offset, SEEK_SET);
                        char *line = malloc(MAX_LINE_LEN);
                        if (fgets(line, MAX_LINE_LEN, f)) {
                            line[strcspn(line, "\n")] = '\0';
                            rows[real_row].line_cache = line;
                        } else {
                            rows[real_row].line_cache = strdup("");
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
            } 

            clrtoeol();
            refresh();
        }        
        // Обновляем реальный номер строки
        cur_real_row = get_real_row(cur_display_row);
    }

    for (int i = 0; i < col_count; i++) {
        free(column_names[i]);
    }
    free(rows);
    fclose(f);
    endwin();
    return 0;
}