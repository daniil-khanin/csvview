/**
 * table_edit.c
 *
 * Реализация редактирования структуры таблицы CSV
 * Добавление/удаление столбцов, заполнение значениями, пересборка заголовка
 */

#include "table_edit.h"       
#include "csvview_defs.h"     // глобальные переменные и константы
#include "utils.h"            // save_file, col_letter и т.д.
#include "column_format.h"    // save_column_settings()

#include <ncurses.h>          // ← это главное! mvprintw, refresh, getch, LINES, COLOR_PAIR и т.д.
#include <stdio.h>            // fopen, fprintf, rename
#include <stdlib.h>           // malloc, free, atoi
#include <string.h>           // strcpy, strncpy, strlen

// ────────────────────────────────────────────────
// Добавление нового столбца
// ────────────────────────────────────────────────

void add_column_and_save(int insert_pos, const char *new_name, const char *csv_filename)
{
    if (col_count >= MAX_COLS)
    {
        mvprintw(LINES - 1, 0, "Too many columns (max %d)", MAX_COLS);
        refresh();
        getch();
        return;
    }

    // Сдвигаем массивы вправо начиная с insert_pos
    for (int i = col_count; i > insert_pos; i--)
    {
        column_names[i] = column_names[i - 1];
        col_types[i]    = col_types[i - 1];
        col_widths[i]   = col_widths[i - 1];
        col_formats[i]  = col_formats[i - 1];
    }

    // Новый столбец
    column_names[insert_pos] = strdup(new_name && *new_name ? new_name : "untitled");
    col_types[insert_pos]    = COL_STR;
    col_widths[insert_pos]   = 12;
    col_formats[insert_pos].truncate_len   = 0;
    col_formats[insert_pos].decimal_places = -1;
    col_formats[insert_pos].date_format[0] = '\0';

    col_count++;

    // Перезаписываем файл с новым столбцом
    char temp_name[1024];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", csv_filename);

    FILE *out = fopen(temp_name, "w");
    if (!out)
    {
        mvprintw(LINES - 1, 0, "Cannot create temp file");
        refresh();
        getch();
        return;
    }

    for (int r = 0; r < row_count; r++)
    {
        const char *line = rows[r].line_cache ? rows[r].line_cache : "";
        if (!rows[r].line_cache)
        {
            if (fseek(f, rows[r].offset, SEEK_SET) == 0)
            {
                char buf[MAX_LINE_LEN];
                if (fgets(buf, sizeof(buf), f))
                {
                    size_t len = strlen(buf);
                    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
                    line = buf;
                }
            }
        }

        // Вставляем новый столбец (запятая + значение)
        char new_line[MAX_LINE_LEN * 2] = {0};
        int pos = 0;
        int col = 0;
        const char *p = line;
        int in_quotes = 0;

        while (*p)
        {
            if (col == insert_pos)
            {
                // Вставляем запятую и значение
                new_line[pos++] = ',';
                const char *val = (r == 0 && use_headers) ? new_name : "";
                strcpy(new_line + pos, val);
                pos += strlen(val);
            }

            // Копируем текущее поле
            while (*p)
            {
                if (*p == '"' && !in_quotes) in_quotes = 1;
                else if (*p == '"' && in_quotes) in_quotes = 0;
                else if (*p == ',' && !in_quotes) break;
                new_line[pos++] = *p++;
            }

            if (*p == ',') {
                new_line[pos++] = *p++;
            }

            col++;
        }

        // Если новый столбец в конце строки
        if (insert_pos >= col)
        {
            while (col < insert_pos)
            {
                new_line[pos++] = ',';
                col++;
            }
            new_line[pos++] = ','; // запятая перед новым значением
            const char *val = (r == 0 && use_headers) ? new_name : "";
            strcpy(new_line + pos, val);
            pos += strlen(val);
        }

        new_line[pos] = '\0';
        fprintf(out, "%s\n", new_line);
    }

    fclose(out);

    // Заменяем оригинальный файл
    if (rename(temp_name, csv_filename) != 0)
    {
        mvprintw(LINES - 1, 0, "Failed to rename temp file");
        refresh();
        getch();
        return;
    }

    // Переиндексация offsets и кэша
    free(rows);
    rows = NULL;
    row_count = 0;

    fclose(f);
    f = fopen(csv_filename, "r");
    if (!f)
    {
        mvprintw(LINES - 1, 0, "Failed to reopen file");
        refresh();
        getch();
        return;
    }

    char buf[MAX_LINE_LEN];
    long offset = 0;
    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    while (fgets(buf, sizeof(buf), f))
    {
        if (row_count >= MAX_ROWS) break;
        rows[row_count].offset = offset;
        rows[row_count].line_cache = NULL;
        offset += strlen(buf);
        row_count++;
    }

    // Сохраняем новые настройки
    save_column_settings(csv_filename);
    refresh();
}

// ────────────────────────────────────────────────
// Заполнение столбца значениями
// ────────────────────────────────────────────────

void fill_column(int col_idx, const char *arg, const char *csv_filename)
{
    if (col_idx < 0 || col_idx >= col_count)
    {
        mvprintw(LINES - 1, 0, "Invalid column");
        refresh();
        getch();
        return;
    }

    char value[256] = {0};
    int start = 0;
    int step = 1;

    // Парсим аргумент
    if (arg[0] == '"' && arg[strlen(arg)-1] == '"')
    {
        strncpy(value, arg + 1, strlen(arg) - 2);
        value[strlen(arg) - 2] = '\0';
    }
    else if (strncmp(arg, "num(", 4) == 0)
    {
        const char *p = arg + 4;
        const char *close = strchr(p, ')');
        if (close)
        {
            char tmp[64];
            strncpy(tmp, p, close - p);
            tmp[close - p] = '\0';

            char *comma = strchr(tmp, ',');
            if (comma)
            {
                *comma = '\0';
                start = atoi(tmp);
                step = atoi(comma + 1);
                if (step == 0) step = 1;
            }
            else
            {
                start = atoi(tmp);
                step = 1;
            }
        }
        else
        {
            start = 0;
            step = 1;
        }
    }
    else
    {
        mvprintw(LINES - 1, 0, "Invalid argument: \"string\" or num() / num(N) / num(N,M)");
        refresh();
        getch();
        return;
    }

    // Заполняем столбец
    int row_start = use_headers ? 1 : 0;
    int num = start;

    for (int r = row_start; r < row_count; r++)
    {
        // Ленивая загрузка строки
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

        // Формируем новую строку с заменённым значением
        char new_line[MAX_LINE_LEN * 2] = {0};
        int pos = 0;
        const char *p = rows[r].line_cache;
        int current_col = 0;
        int in_quotes = 0;

        while (*p)
        {
            if (current_col == col_idx)
            {
                // Пропускаем старое значение (до запятой или конца)
                while (*p)
                {
                    if (*p == '"' && !in_quotes) in_quotes = 1;
                    else if (*p == '"' && in_quotes) in_quotes = 0;
                    else if (*p == ',' && !in_quotes) {
                        p++;
                        break;
                    }
                    p++;
                }

                // Вставляем новое значение
                char temp_val[256];
                if (strncmp(arg, "num(", 4) == 0)
                {
                    snprintf(temp_val, sizeof(temp_val), "%d", num);
                    num += step;
                }
                else
                {
                    strncpy(temp_val, value, sizeof(temp_val) - 1);
                    temp_val[sizeof(temp_val) - 1] = '\0';
                }

                strcpy(new_line + pos, temp_val);
                pos += strlen(temp_val);

                // Запятая после значения (если не последний столбец)
                if (current_col < col_count - 1) {
                    new_line[pos++] = ',';
                }
            }
            else
            {
                // Копируем поле как есть
                while (*p)
                {
                    if (*p == '"' && !in_quotes) in_quotes = 1;
                    else if (*p == '"' && in_quotes) in_quotes = 0;
                    else if (*p == ',' && !in_quotes) break;
                    new_line[pos++] = *p++;
                }
                if (*p == ',') {
                    new_line[pos++] = *p++;
                }
            }

            current_col++;
        }

        // Если столбец в конце строки
        if (current_col <= col_idx)
        {
            while (current_col < col_idx)
            {
                new_line[pos++] = ',';
                current_col++;
            }

            char temp_val[256];
            if (strncmp(arg, "num(", 4) == 0)
            {
                snprintf(temp_val, sizeof(temp_val), "%d", num);
                num += step;
            }
            else
            {
                strncpy(temp_val, value, sizeof(temp_val) - 1);
                temp_val[sizeof(temp_val) - 1] = '\0';
            }

            strcpy(new_line + pos, temp_val);
            pos += strlen(temp_val);

            if (col_idx < col_count - 1) {
                new_line[pos++] = ',';
            }
        }

        new_line[pos] = '\0';

        // Заменяем кэш строки
        free(rows[r].line_cache);
        rows[r].line_cache = strdup(new_line);
    }

    // Сохраняем изменения в файл
    if (save_file(csv_filename, f, rows, row_count) != 0)
    {
        mvprintw(LINES - 1, 0, "Failed to save file");
        refresh();
        getch();
        return;
    }

    // Переиндексация offsets и кэша
    free(rows);
    rows = NULL;
    row_count = 0;

    fclose(f);
    f = fopen(csv_filename, "r");
    if (!f)
    {
        mvprintw(LINES - 1, 0, "Failed to reopen file");
        refresh();
        getch();
        return;
    }

    char buf[MAX_LINE_LEN];
    long offset = 0;
    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    while (fgets(buf, sizeof(buf), f))
    {
        if (row_count >= MAX_ROWS) break;
        rows[row_count].offset = offset;
        rows[row_count].line_cache = NULL;
        offset += strlen(buf);
        row_count++;
    }

    save_column_settings(csv_filename);
    refresh();
}

// ────────────────────────────────────────────────
// Удаление столбца
// ────────────────────────────────────────────────

void delete_column(int col_idx, const char *arg, const char *csv_filename)
{
    // Проверка, что курсор на нужном столбце (если arg передан)
    if (arg && *arg)
    {
        const char *current_name = (use_headers && column_names[col_idx]) ? column_names[col_idx] : "";
        if (strcmp(current_name, arg) != 0)
        {
            attron(COLOR_PAIR(1));
            mvprintw(LINES - 1, 0, "Cursor not on column '%s' — cannot delete %d", arg, col_idx);
            attroff(COLOR_PAIR(1));
            refresh();
            getch();
            clrtoeol();
            refresh();
            return;
        }
    }

    // Освобождаем имя столбца
    free(column_names[col_idx]);

    // Сдвигаем массивы влево
    for (int i = col_idx; i < col_count - 1; i++)
    {
        column_names[i] = column_names[i + 1];
        col_types[i]    = col_types[i + 1];
        col_widths[i]   = col_widths[i + 1];
        col_formats[i]  = col_formats[i + 1];
    }

    col_count--;

    // Перемещаем курсор на предыдущий столбец
    if (cur_col > 0) cur_col--;

    // Перезаписываем файл без удалённого столбца
    char temp_name[1024];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", csv_filename);

    FILE *out = fopen(temp_name, "w");
    if (!out)
    {
        mvprintw(LINES - 1, 0, "Cannot create temp file");
        refresh();
        getch();
        return;
    }

    for (int r = 0; r < row_count; r++)
    {
        const char *line = rows[r].line_cache ? rows[r].line_cache : "";
        if (!rows[r].line_cache)
        {
            if (fseek(f, rows[r].offset, SEEK_SET) == 0)
            {
                char buf[MAX_LINE_LEN];
                if (fgets(buf, sizeof(buf), f))
                {
                    size_t len = strlen(buf);
                    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
                    line = buf;
                }
            }
        }

        // Формируем новую строку без col_idx
        char new_line[MAX_LINE_LEN * 2] = {0};
        int pos = 0;
        const char *p = line;
        int current_col = 0;
        int in_quotes = 0;

        while (*p)
        {
            if (current_col == col_idx)
            {
                // Пропускаем удаляемый столбец
                while (*p)
                {
                    if (*p == '"' && !in_quotes) in_quotes = 1;
                    else if (*p == '"' && in_quotes) in_quotes = 0;
                    else if (*p == ',' && !in_quotes) {
                        p++;
                        break;
                    }
                    p++;
                }
            }
            else
            {
                // Копируем поле
                while (*p)
                {
                    if (*p == '"' && !in_quotes) in_quotes = 1;
                    else if (*p == '"' && in_quotes) in_quotes = 0;
                    else if (*p == ',' && !in_quotes) break;
                    new_line[pos++] = *p++;
                }
                if (*p == ',') {
                    new_line[pos++] = *p++;
                }
            }

            current_col++;
        }

        new_line[pos] = '\0';
        fprintf(out, "%s\n", new_line);
    }

    fclose(out);

    // Заменяем оригинальный файл
    if (rename(temp_name, csv_filename) != 0)
    {
        mvprintw(LINES - 1, 0, "Failed to rename temp file");
        refresh();
        getch();
        return;
    }

    // Переиндексация offsets
    free(rows);
    rows = NULL;
    row_count = 0;

    fclose(f);
    f = fopen(csv_filename, "r");
    if (!f)
    {
        mvprintw(LINES - 1, 0, "Failed to reopen file");
        refresh();
        getch();
        return;
    }

    char buf[MAX_LINE_LEN];
    long offset = 0;
    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    while (fgets(buf, sizeof(buf), f))
    {
        if (row_count >= MAX_ROWS) break;
        rows[row_count].offset = offset;
        rows[row_count].line_cache = NULL;
        offset += strlen(buf);
        row_count++;
    }

    save_column_settings(csv_filename);
    clrtoeol();
    refresh();
}

// ────────────────────────────────────────────────
// Пересборка заголовка после изменения имён столбцов
// ────────────────────────────────────────────────

void rebuild_header_row(void)
{
    if (row_count == 0 || !use_headers) {
        return;
    }

    char new_header[MAX_LINE_LEN * 2] = {0};
    int pos = 0;

    for (int c = 0; c < col_count; c++)
    {
        if (c > 0) new_header[pos++] = ',';

        const char *name = column_names[c] ? column_names[c] : "";

        if (strchr(name, ',') || strchr(name, '"'))
        {
            // Экранирование кавычек
            new_header[pos++] = '"';
            for (const char *p = name; *p; p++)
            {
                if (*p == '"') new_header[pos++] = '"';
                new_header[pos++] = *p;
            }
            new_header[pos++] = '"';
        }
        else
        {
            strcpy(new_header + pos, name);
            pos += strlen(name);
        }
    }

    new_header[pos] = '\0';

    // Заменяем кэш первой строки
    free(rows[0].line_cache);
    rows[0].line_cache = strdup(new_header);
}