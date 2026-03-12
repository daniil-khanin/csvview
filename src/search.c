/**
 * search.c
 *
 * Реализация поиска по содержимому ячеек таблицы CSV
 */

#include "search.h"
#include "utils.h"          // strcasestr_custom, get_column_value и т.д.

#include <stdio.h>          // fseek, fgets
#include <stdlib.h>         // malloc, strdup
#include <string.h>         // strlen, strcpy, strtok

// ────────────────────────────────────────────────
// Выполнение поиска по всей таблице
// ────────────────────────────────────────────────

void perform_search(RowIndex *rows, FILE *f, int row_count)
{
    // Сброс результатов поиска
    search_count = 0;
    search_index = -1;

    // Если запрос пустой — ничего не ищем
    if (strlen(search_query) == 0) {
        return;
    }

    // Пропускаем заголовок, если он есть
    int start_row = use_headers ? 1 : 0;

    for (int r = start_row; r < row_count && search_count < MAX_SEARCH_RESULTS; r++)
    {
        // Ленивая загрузка строки в кэш
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

        int field_count = 0;
        char **fields = parse_csv_line(rows[r].line_cache, &field_count);
        if (!fields) continue;

        for (int c = 0; c < field_count && c < col_count && search_count < MAX_SEARCH_RESULTS; c++)
        {
            if (strcasestr_custom(fields[c], search_query))
            {
                search_results[search_count].row = r;
                search_results[search_count].col = c;
                search_count++;
            }
        }

        free_csv_fields(fields, field_count);
    }
}

// ────────────────────────────────────────────────
// Переход к результату поиска
// ────────────────────────────────────────────────

void goto_search_result(int index,
                        int *cur_display_row,
                        int *top_display_row,
                        int *cur_col,
                        int *left_col,
                        int visible_rows,
                        int visible_cols,
                        int row_count)
{
    // Проверка корректности индекса
    if (index < 0 || index >= search_count) {
        return;
    }

    search_index = index;

    // Реальный индекс строки в файле
    int target_real_row = search_results[index].row;
    int target_col = search_results[index].col;

    // Находим видимую позицию строки (если фильтр активен)
    int target_display_row = -1;
    if (filter_active)
    {
        for (int i = 0; i < filtered_count; i++)
        {
            if (filtered_rows[i] == target_real_row)
            {
                target_display_row = i;
                break;
            }
        }

        // Если строка не прошла фильтр — не прыгаем
        if (target_display_row == -1) {
            return;
        }
    }
    else
    {
        // Без фильтра — просто смещение на заголовок
        target_display_row = target_real_row - (use_headers ? 1 : 0);
    }

    // Устанавливаем курсор на найденную строку и столбец
    *cur_display_row = target_display_row;
    cur_real_row = target_real_row;
    *cur_col = target_col;

    // Вертикальная прокрутка — центрируем строку
    *top_display_row = *cur_display_row - (visible_rows / 2);
    if (*top_display_row < 0) {
        *top_display_row = 0;
    }

    int display_count = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));
    if (*top_display_row > display_count - visible_rows) {
        *top_display_row = display_count - visible_rows;
    }

    // Горизонтальная прокрутка — центрируем столбец
    *left_col = *cur_col - (visible_cols / 2);
    if (*left_col < 0) {
        *left_col = 0;
    }
    if (*left_col > col_count - visible_cols) {
        *left_col = col_count - visible_cols;
    }
}