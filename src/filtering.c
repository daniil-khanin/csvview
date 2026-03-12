/**
 * filtering.c
 *
 * Модуль фильтрации строк в таблице CSV.
 * Содержит:
 *   - apply_filter() — основная функция применения фильтра
 *   - load_saved_filters() — загрузка сохранённых фильтров из файла
 *   - save_filter() — сохранение текущего фильтра в файл и в память
 */

#include "csvview_defs.h"       // все константы, типы, extern глобальные
#include "utils.h"              // parse_filter_expression, free_filter_expr и т.д.
#include "filtering.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Применяет текущий фильтр к таблице и заполняет массив отфильтрованных строк
 *
 * Обрабатывает как новый синтаксис (Age > 18 AND City = "Moscow"),
 * так и старый (R:поиск, C[Имя]:поиск).
 * Загружает строки по необходимости (если line_cache пустой).
 * Результат: заполняет filtered_rows[] и filtered_count.
 *
 * @param rows          Массив индексов строк (с offset и line_cache)
 * @param f             Открытый FILE* исходного CSV
 * @param row_count     Общее количество строк в файле
 */
void apply_filter(RowIndex *rows, FILE *f, int row_count)
{
    filtered_count = 0;
    filter_active = 0;

    // Если фильтр пустой — ничего не делаем
    if (strlen(filter_query) == 0) {
        return;
    }

    FilterExpr expr = {0};
    int new_syntax_ok = 0;

    // Пытаемся распарсить как новый расширенный фильтр
    if (strpbrk(filter_query, "><=!(") ||
        strstr(filter_query, "AND") ||
        strstr(filter_query, "OR") ||
        strstr(filter_query, "NOT") ||
        filter_query[0] == '!')
    {
        if (parse_filter_expression(filter_query, &expr) == 0) {
            new_syntax_ok = 1;
        }
    }

    // Пропускаем заголовок, если он есть
    int start_row = use_headers ? 1 : 0;

    for (int r = start_row; r < row_count && filtered_count < MAX_ROWS; r++)
    {
        // Загружаем строку, если кэш пустой
        if (!rows[r].line_cache)
        {
            fseek(f, rows[r].offset, SEEK_SET);
            char *line = malloc(MAX_LINE_LEN);
            if (fgets(line, MAX_LINE_LEN, f))
            {
                line[strcspn(line, "\n")] = '\0';
                rows[r].line_cache = line;
            }
            else
            {
                rows[r].line_cache = strdup("");
            }
        }

        const char *line = rows[r].line_cache;
        if (!line || !*line) {
            continue;
        }

        int match = 0;

        if (new_syntax_ok)
        {
            // Новый синтаксис — используем row_matches_filter
            match = row_matches_filter(line, &expr);
        }
        else
        {
            // Старый синтаксис: R: или C[Имя]:
            int is_column_filter = 0;
            char target_col[256] = "";
            char search_str_buf[256] = "";
            char *search_str = search_str_buf;
            int is_exact = 0;

            // Парсим запрос
            if (strncmp(filter_query, "C[", 2) == 0)
            {
                is_column_filter = 1;
                char *end = strchr(filter_query + 2, ']');
                if (end)
                {
                    int len = end - (filter_query + 2);
                    strncpy(target_col, filter_query + 2, len);
                    target_col[len] = '\0';
                    search_str = end + 1;
                    if (*search_str == ':') search_str++;
                }
            }
            else if (strncmp(filter_query, "R:", 2) == 0)
            {
                search_str = filter_query + 2;
            }
            else
            {
                continue; // неверный формат — пропускаем
            }

            // Обработка кавычек для точного поиска
            if (*search_str == '"' || *search_str == '\'')
            {
                char quote = *search_str;
                search_str++;
                char *end_quote = strchr(search_str, quote);
                if (end_quote)
                {
                    int len = end_quote - search_str;
                    strncpy(search_str_buf, search_str, len);
                    search_str_buf[len] = '\0';
                    search_str = search_str_buf;
                    is_exact = 1;
                }
            }

            int target_col_num = -1;
            if (is_column_filter)
            {
                if (use_headers) {
                    target_col_num = col_name_to_num(target_col);
                }
                if (target_col_num < 0) {
                    target_col_num = col_to_num(target_col);
                }
            }

            int field_count = 0;
            char **fields = parse_csv_line(line, &field_count);
            if (fields)
            {
                for (int c = 0; c < field_count && c < col_count && !match; c++)
                {
                    const char *token = fields[c];

                    if (is_column_filter)
                    {
                        if (c == target_col_num)
                        {
                            if (is_exact)
                            {
                                if (strcmp(token, search_str) == 0) match = 1;
                            }
                            else
                            {
                                if (strcasestr_custom(token, search_str)) match = 1;
                            }
                        }
                    }
                    else // R: — поиск по любой ячейке
                    {
                        if (is_exact)
                        {
                            if (strcmp(token, search_str) == 0) match = 1;
                        }
                        else
                        {
                            if (strcasestr_custom(token, search_str)) match = 1;
                        }
                    }
                }
                free_csv_fields(fields, field_count);
            }
        }

        if (match)
        {
            filtered_rows[filtered_count++] = r;
        }
    }

    // Освобождаем память, если использовали новый парсер
    if (new_syntax_ok)
    {
        free_filter_expr(&expr);
    }

    filter_active = (filtered_count > 0);
}

/**
 * @brief Загружает сохранённые фильтры из файла <csv_filename>
 *
 * Файл ищется как <csv_filename> (без расширения .csvf).
 * Формат строки: "filter: запрос"
 *
 * @param csv_filename  Имя исходного CSV-файла
 */
void load_saved_filters(const char *csv_filename)
{
    char cfg_path[1024];
    snprintf(cfg_path, sizeof(cfg_path), "%s", csv_filename);

    FILE *fp = fopen(cfg_path, "r");
    if (!fp) {
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "filter: ", 8) == 0)
        {
            char *query = line + 8;

            // Проверяем дубликат
            int dup = 0;
            for (int j = 0; j < saved_filter_count; j++)
            {
                if (strcmp(saved_filters[j], query) == 0)
                {
                    dup = 1;
                    break;
                }
            }

            if (!dup && saved_filter_count < MAX_SAVED_FILTERS)
            {
                saved_filters[saved_filter_count++] = strdup(query);
            }
        }
    }

    fclose(fp);
}

/**
 * @brief Сохраняет текущий фильтр в файл <csv_filename>.csvf и в память
 *
 * @param csv_filename  Имя исходного CSV-файла
 * @param query         Строка фильтра для сохранения
 */
void save_filter(const char *csv_filename, const char *query)
{
    if (!query || !*query) {
        return;
    }

    char cfg_path[1024];
    snprintf(cfg_path, sizeof(cfg_path), "%s.csvf", csv_filename);

    FILE *fp = fopen(cfg_path, "a");
    if (!fp) {
        return;
    }

    fprintf(fp, "filter: %s\n", query);
    fclose(fp);

    // Добавляем в память (для быстрого доступа в UI)
    if (saved_filter_count < MAX_SAVED_FILTERS)
    {
        saved_filters[saved_filter_count++] = strdup(query);
    }
}