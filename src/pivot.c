/**
 * pivot.c
 *
 * Реализация сводных таблиц (pivot tables) для csvview
 */

#include "pivot.h"
#include "utils.h"          // get_column_value, col_name_to_num, col_to_num
#include "csvview_defs.h"   // globals, RowIndex, ColType
#include "ui_draw.h"
#include "sorting.h"
#include "filtering.h"

#include <ncurses.h>        // отрисовка
#include <stdlib.h>         // malloc, free, qsort, calloc
#include <string.h>         // strcpy, strcmp, strlen
#include <stdio.h>          // fopen, fprintf, sscanf
#include <math.h>           // INFINITY
#include <time.h>           // struct tm, strptime


unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

HashMap *hash_map_create(int size) {
    HashMap *map = malloc(sizeof(HashMap));
    map->size = size;
    map->buckets = calloc(size, sizeof(Entry*));
    return map;
}

int compare_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void hash_map_put(HashMap *map, const char *key, void *value) {
    unsigned long h = hash_string(key) % map->size;
    Entry *e = map->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            e->value = value;
            return;
        }
        e = e->next;
    }
    e = malloc(sizeof(Entry));
    e->key = strdup(key);
    e->value = value;
    e->next = map->buckets[h];
    map->buckets[h] = e;
}

void *hash_map_get(HashMap *map, const char *key) {
    unsigned long h = hash_string(key) % map->size;
    Entry *e = map->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) return e->value;
        e = e->next;
    }
    return NULL;
}

char **hash_map_keys(HashMap *map, int *count) {
    *count = 0;
    for (int i = 0; i < map->size; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            (*count)++;
            e = e->next;
        }
    }
    char **keys = malloc(*count * sizeof(char*));
    int idx = 0;
    for (int i = 0; i < map->size; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            keys[idx++] = strdup(e->key);
            e = e->next;
        }
    }
    return keys;
}

void hash_map_destroy(HashMap *map) {
    for (int i = 0; i < map->size; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            Entry *next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(map->buckets);
    free(map);
}

// Pivot functions
int compare_date_keys(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;

    int ya = 0, ma = 0, qa = 0;
    int yb = 0, mb = 0, qb = 0;

    // Год-квартал
    if (strstr(sa, "-Q") && strstr(sb, "-Q")) {
        sscanf(sa, "%d-Q%d", &ya, &qa);
        sscanf(sb, "%d-Q%d", &yb, &qb);
        if (ya != yb) return ya < yb ? -1 : 1;
        return qa < qb ? -1 : (qa > qb ? 1 : 0);
    }
    // Год-месяц
    if (strchr(sa, '-') && strchr(sb, '-')) {
        sscanf(sa, "%d-%d", &ya, &ma);
        sscanf(sb, "%d-%d", &yb, &mb);
        if (ya != yb) return ya < yb ? -1 : 1;
        return ma < mb ? -1 : (ma > mb ? 1 : 0);
    }
    // Просто год или век
    ya = atoi(sa);
    yb = atoi(sb);
    return ya < yb ? -1 : (ya > yb ? 1 : 0);
}

// ────────────────────────────────────────────────
// Хелперы для сортировки order-массивов по значению агрегации
// ────────────────────────────────────────────────

static Agg          *g_sort_agg_arr = NULL;
static const char   *g_sort_agg_str = NULL;
static ColType       g_sort_vtype   = COL_NUM;
static int           g_sort_dir     = 1;  // 1=ASC, -1=DESC

static double agg_sort_value(const Agg *a, const char *agg_str) {
    if (strcmp(agg_str, "COUNT") == 0 || strcmp(agg_str, "UNIQUE COUNT") == 0)
        return (double)a->count;
    if (strcmp(agg_str, "SUM") == 0)  return a->sum;
    if (strcmp(agg_str, "AVG") == 0)  return a->count > 0 ? a->sum / (double)a->count : 0.0;
    if (strcmp(agg_str, "MIN") == 0)  return a->min;
    if (strcmp(agg_str, "MAX") == 0)  return a->max;
    return 0.0;
}

static int compare_order_by_agg(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    double va = agg_sort_value(&g_sort_agg_arr[ia], g_sort_agg_str);
    double vb = agg_sort_value(&g_sort_agg_arr[ib], g_sort_agg_str);
    if (va < vb) return -g_sort_dir;
    if (va > vb) return  g_sort_dir;
    return 0;
}

int load_pivot_settings(const char *csv_filename, PivotSettings *settings) {
    char pivot_path[1024];
    snprintf(pivot_path, sizeof(pivot_path), "%s.pivot", csv_filename);
    FILE *fp = fopen(pivot_path, "r");
    if (!fp) return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], val[192];
        if (sscanf(line, "%63[^:]: %191[^\n]", key, val) != 2) continue;
        trim(val);
        if (strcmp(key, "row_group") == 0) {
            settings->row_group_col = strcmp(val, "None") == 0 ? NULL : strdup(val);
        } else if (strcmp(key, "col_group") == 0) {
            settings->col_group_col = strcmp(val, "None") == 0 ? NULL : strdup(val);
        } else if (strcmp(key, "value_col") == 0) {
            settings->value_col = strdup(val);
        } else if (strcmp(key, "aggregation") == 0) {
            settings->aggregation = strdup(val);
        } else if (strcmp(key, "date_grouping") == 0) {
            settings->date_grouping = strdup(val);
        } else if (strcmp(key, "show_row_totals") == 0) {
            settings->show_row_totals = strcmp(val, "Yes") == 0 ? 1 : 0;
        } else if (strcmp(key, "show_col_totals") == 0) {
            settings->show_col_totals = strcmp(val, "Yes") == 0 ? 1 : 0;
        } else if (strcmp(key, "show_grand_total") == 0) {
            settings->show_grand_total = strcmp(val, "Yes") == 0 ? 1 : 0;
        } else if (strcmp(key, "row_sort") == 0) {
            settings->row_sort = strdup(val);
        } else if (strcmp(key, "col_sort") == 0) {
            settings->col_sort = strdup(val);
        }
    }
    fclose(fp);

    // Validate columns
    int row_group_idx = settings->row_group_col ? (use_headers ? col_name_to_num(settings->row_group_col) : col_to_num(settings->row_group_col)) : -1;
    int col_group_idx = settings->col_group_col ? (use_headers ? col_name_to_num(settings->col_group_col) : col_to_num(settings->col_group_col)) : -1;
    int value_idx = settings->value_col ? (use_headers ? col_name_to_num(settings->value_col) : col_to_num(settings->value_col)) : -1;

    if (row_group_idx < -1 || row_group_idx >= col_count || col_group_idx < -1 || col_group_idx >= col_count || value_idx < 0 || value_idx >= col_count) {
        // Invalid, free
        free(settings->row_group_col);
        free(settings->col_group_col);
        free(settings->value_col);
        free(settings->aggregation);
        free(settings->date_grouping);
        free(settings->row_sort);
        free(settings->col_sort);
        memset(settings, 0, sizeof(PivotSettings));
        return 0;
    }
    // Дефолты для полей, которых нет в старых .pivot файлах
    if (!settings->row_sort) settings->row_sort = strdup("KEY_ASC");
    if (!settings->col_sort) settings->col_sort = strdup("KEY_ASC");
    return 1;
}

void save_pivot_settings(const char *csv_filename, const PivotSettings *settings) {
    char pivot_path[1024];
    snprintf(pivot_path, sizeof(pivot_path), "%s.pivot", csv_filename);
    FILE *fp = fopen(pivot_path, "w");
    if (!fp) return;

    fprintf(fp, "row_group: %s\n", settings->row_group_col ? settings->row_group_col : "None");
    fprintf(fp, "col_group: %s\n", settings->col_group_col ? settings->col_group_col : "None");
    fprintf(fp, "value_col: %s\n", settings->value_col ? settings->value_col : "None");
    fprintf(fp, "aggregation: %s\n", settings->aggregation ? settings->aggregation : "SUM");
    fprintf(fp, "date_grouping: %s\n", settings->date_grouping ? settings->date_grouping : "Auto");
    fprintf(fp, "show_row_totals: %s\n", settings->show_row_totals ? "Yes" : "No");
    fprintf(fp, "show_col_totals: %s\n", settings->show_col_totals ? "Yes" : "No");
    fprintf(fp, "show_grand_total: %s\n", settings->show_grand_total ? "Yes" : "No");
    fprintf(fp, "row_sort: %s\n", settings->row_sort ? settings->row_sort : "KEY_ASC");
    fprintf(fp, "col_sort: %s\n", settings->col_sort ? settings->col_sort : "KEY_ASC");

    fclose(fp);
}

char *get_group_key(const char *val, ColType type, const char *grouping, int col_idx) {
    if (!val || !*val) return strdup("");
    if (type != COL_DATE || !grouping || strcmp(grouping, "Auto") == 0) {
        return strdup(val);
    }

    struct tm tm = {0};
    const char *fmt = col_formats[col_idx].date_format;

    if (!fmt || !*fmt) {
        fmt = "%Y-%m-%d";  // fallback
    }

    // Парсим дату по формату
    if (strptime(val, fmt, &tm) == NULL) {
        // Если не удалось — пробуем fallback
        if (strptime(val, "%Y-%m-%d", &tm) == NULL) {
            return strdup(val);  // ошибка парсинга — возвращаем как есть
        }
    }

    // Теперь применяем гранулярность из grouping
    char *key = malloc(32);
    int year = tm.tm_year + 1900;
    int month = tm.tm_mon + 1;

    if (strcmp(grouping, "Month") == 0) {
        snprintf(key, 32, "%04d-%02d", year, month);
    } else if (strcmp(grouping, "Quarter") == 0) {
        int q = (month - 1) / 3 + 1;
        snprintf(key, 32, "%04d-Q%d", year, q);
    } else if (strcmp(grouping, "Year") == 0) {
        snprintf(key, 32, "%04d", year);
    } else if (strcmp(grouping, "Century") == 0) {
        int cent = year / 100;
        snprintf(key, 32, "%02dxx", cent);
    } else {
        free(key);
        return strdup(val);
    }

    return key;
}

void update_agg(Agg *agg, const char *val, ColType value_type) {
    agg->count++;

    if (!val || !*val) {
        return;
    }

    if (value_type == COL_NUM) {
        // Очень агрессивная очистка: оставляем только цифры, точку, минус и запятую
        char clean[128] = {0};
        int j = 0;
        int has_dot_or_comma = 0;

        for (int i = 0; val[i] && j < (int)sizeof(clean)-1; i++) {
            char c = val[i];
            if (isdigit(c) || c == '-' || c == '+' || c == '.') {
                clean[j++] = c;
            } else if (c == ',') {
                // запятая → точка, но только одна
                if (!has_dot_or_comma) {
                    clean[j++] = '.';
                    has_dot_or_comma = 1;
                }
            }
            // всё остальное ($, €, пробелы, буквы) игнорируем
        }
        clean[j] = '\0';

        // Если строка пустая после очистки — выходим
        if (!*clean) return;

        char *endptr;
        double num = strtod(clean, &endptr);

        // Если удалось распарсить почти всю строку
        if (endptr != clean && (*endptr == '\0' || isspace(*endptr))) {
            agg->sum += num;
            if (agg->count == 1 || num < agg->min) agg->min = num;
            if (agg->count == 1 || num > agg->max) agg->max = num;
        }
        // else — не число, просто пропускаем (count уже увеличен)
    }
    else {
        // Для нечисловых — min/max строки
        if (!agg->min_str || strcmp(val, agg->min_str) < 0) {
            free(agg->min_str);
            agg->min_str = strdup(val);
        }
        if (!agg->max_str || strcmp(val, agg->max_str) > 0) {
            free(agg->max_str);
            agg->max_str = strdup(val);
        }
    }
}

char *get_agg_display(const Agg *agg, const char *aggregation, ColType value_type) {
    static char buf[64];
    buf[0] = '\0';

    if (strcmp(aggregation, "COUNT") == 0) {
        snprintf(buf, sizeof(buf), "%d", agg->count);
    }
    else if (strcmp(aggregation, "UNIQUE COUNT") == 0) {
        snprintf(buf, sizeof(buf), "%d", agg->unique_count);
    }
    else if (strcmp(aggregation, "SUM") == 0) {
        snprintf(buf, sizeof(buf), "%.2f", agg->sum);
    }
    else if (strcmp(aggregation, "AVG") == 0) {
        double avg = agg->count > 0 ? agg->sum / agg->count : 0.0;
        snprintf(buf, sizeof(buf), "%.2f", avg);
    }
    else if (strcmp(aggregation, "MIN") == 0) {
        if (value_type == COL_NUM && agg->count > 0) {
            snprintf(buf, sizeof(buf), "%.2f", agg->min);
        } else if (agg->min_str) {
            strncpy(buf, agg->min_str, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
        }
    }
    else if (strcmp(aggregation, "MAX") == 0) {
        if (value_type == COL_NUM && agg->count > 0) {
            snprintf(buf, sizeof(buf), "%.2f", agg->max);
        } else if (agg->max_str) {
            strncpy(buf, agg->max_str, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
        }
    }

    if (buf[0] == '\0') strcpy(buf, "—");
    return buf;
}

void draw_table_frame(int y, int x, int height, int width) {
    attron(COLOR_PAIR(6));
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + width - 1, ACS_URCORNER);
    mvaddch(y + height - 1, x, ACS_LLCORNER);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);

    for (int i = 1; i < width - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + 2, x + i, ACS_HLINE);
        mvaddch(y + height - 1, x + i, ACS_HLINE);
    }

    for (int i = 1; i < height - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + width - 1, ACS_VLINE);
    }

    mvaddch(y + 2, x, ACS_LTEE);
    mvaddch(y + 2, x + width - 1, ACS_RTEE);

    attroff(COLOR_PAIR(6));

    if (in_filter_mode) mvprintw(2, 2, "F:%s", filter_query);
}

void show_pivot_settings_window(PivotSettings *settings, const char *csv_filename, int height, int width) {
    int win_h = 16, win_w = 90;
    WINDOW *win = newwin(win_h, win_w, (height - win_h) / 2, (width - win_w) / 2);
    box(win, 0, 0);
    keypad(win, TRUE);

    // Значения по умолчанию
    if (!settings->aggregation)  settings->aggregation  = strdup("SUM");
    if (!settings->date_grouping) settings->date_grouping = strdup("Auto");
    if (!settings->row_sort)     settings->row_sort      = strdup("KEY_ASC");
    if (!settings->col_sort)     settings->col_sort      = strdup("KEY_ASC");

    int current_field = 0;

    const char *fields[] = {
        "Rows group by",
        "Columns group by",
        "Values column",
        "Aggregation",
        "Date grouping",
        "Show row totals",
        "Show column totals",
        "Grand total",
        "Row sort",
        "Column sort"
    };
    int num_fields = 10;

    char **col_options = malloc((col_count + 1) * sizeof(char*));
    col_options[0] = strdup("None");
    for (int i = 0; i < col_count; i++) {
        if (use_headers && column_names[i]) {
            col_options[i + 1] = strdup(column_names[i]);
        } else {
            char buf[16];
            col_letter(i, buf);
            col_options[i + 1] = strdup(buf);
        }
    }
    int num_col_options = col_count + 1;

    const char *agg_options[] = {"SUM", "AVG", "COUNT", "MIN", "MAX", "UNIQUE COUNT"};
    int num_agg_options = 6;

    const char *date_options[] = {"Auto", "Month", "Quarter", "Year", "Century"};
    int num_date_options = 5;

    const char *yn_options[] = {"Yes", "No"};
    int num_yn = 2;

    const char *sort_display[] = {"Key ↑", "Key ↓", "Val ↑", "Val ↓"};
    const char *sort_values[]  = {"KEY_ASC", "KEY_DESC", "VAL_ASC", "VAL_DESC"};
    int num_sort_opts = 4;

    // Текущие индексы
    int row_group_idx = 0, col_group_idx = 0, value_idx = 0, agg_idx = 0, date_idx = 0;
    int row_tot_idx = settings->show_row_totals ? 0 : 1;
    int col_tot_idx = settings->show_col_totals ? 0 : 1;
    int grand_idx = settings->show_grand_total ? 0 : 1;
    int row_sort_idx = 0, col_sort_idx = 0;

    // Инициализация из настроек (если загружены из файла)
    if (settings->row_group_col) {
        for (int i = 1; i < num_col_options; i++) {
            if (strcmp(col_options[i], settings->row_group_col) == 0) {
                row_group_idx = i;
                break;
            }
        }
    }
    if (settings->col_group_col) {
        for (int i = 1; i < num_col_options; i++) {
            if (strcmp(col_options[i], settings->col_group_col) == 0) {
                col_group_idx = i;
                break;
            }
        }
    }
    if (settings->value_col) {
        for (int i = 1; i < num_col_options; i++) {
            if (strcmp(col_options[i], settings->value_col) == 0) {
                value_idx = i;
                break;
            }
        }
    }
    for (int i = 0; i < num_agg_options; i++) {
        if (strcmp(agg_options[i], settings->aggregation) == 0) {
            agg_idx = i;
            break;
        }
    }
    for (int i = 0; i < num_date_options; i++) {
        if (strcmp(date_options[i], settings->date_grouping) == 0) {
            date_idx = i;
            break;
        }
    }
    for (int i = 0; i < num_sort_opts; i++) {
        if (strcmp(sort_values[i], settings->row_sort) == 0) { row_sort_idx = i; break; }
    }
    for (int i = 0; i < num_sort_opts; i++) {
        if (strcmp(sort_values[i], settings->col_sort) == 0) { col_sort_idx = i; break; }
    }

    while (1) {
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 1, 2, "Pivot Table Settings");

        // Проверяем, есть ли хотя бы один столбец даты в группировке
        int has_date_group = 0;
        int r_idx = row_group_idx > 0 ? (use_headers ? col_name_to_num(col_options[row_group_idx]) : col_to_num(col_options[row_group_idx])) : -1;
        int c_idx = col_group_idx > 0 ? (use_headers ? col_name_to_num(col_options[col_group_idx]) : col_to_num(col_options[col_group_idx])) : -1;
        if (r_idx >= 0 && col_types[r_idx] == COL_DATE) has_date_group = 1;
        if (c_idx >= 0 && col_types[c_idx] == COL_DATE) has_date_group = 1;

        for (int f = 0; f < num_fields; f++) {
            char *val = NULL;

            if (f == 0) val = col_options[row_group_idx];
            else if (f == 1) val = col_options[col_group_idx];
            else if (f == 2) val = col_options[value_idx];
            else if (f == 3) val = (char*)agg_options[agg_idx];
            else if (f == 4) {
                // Date grouping — всегда показываем, но блокируем, если нет дат
                val = (char*)date_options[date_idx];
                if (!has_date_group) val = "Auto (disabled)";
            }
            else if (f == 5) val = (char*)yn_options[row_tot_idx];
            else if (f == 6) val = (char*)yn_options[col_tot_idx];
            else if (f == 7) val = (char*)yn_options[grand_idx];
            else if (f == 8) val = (char*)sort_display[row_sort_idx];
            else if (f == 9) val = (char*)sort_display[col_sort_idx];

            int y_pos = f + 3;
            if (f == current_field) wattron(win, A_REVERSE);

            if (f == 4 && !has_date_group) {
                // Серый цвет для заблокированного поля
                wattron(win, COLOR_PAIR(6));  // COLOR_PAIR(6) — обычно серый/тёмный
            }

            mvwprintw(win, y_pos, 2, "%s:", fields[f]);
            mvwprintw(win, y_pos, 24, "%s", val ? val : "None");

            wattroff(win, COLOR_PAIR(6));
            if (f == current_field) wattroff(win, A_REVERSE);
        }

        mvwprintw(win, win_h - 1, 2, "↑↓ j/k - select | ←→ h/l - change | Enter - Build | Esc/q - Cancel");
        wrefresh(win);

        int ch = wgetch(win);

        if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            current_field = (current_field + 1) % num_fields;
        }
        else if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            current_field = (current_field - 1 + num_fields) % num_fields;
        }
        else if (ch == KEY_RIGHT || ch == 'l' || ch == 'L') {
            int real_f = current_field;  // теперь не используем field_map, показываем все
            if (real_f == 0) row_group_idx = (row_group_idx + 1) % num_col_options;
            else if (real_f == 1) col_group_idx = (col_group_idx + 1) % num_col_options;
            else if (real_f == 2) value_idx = (value_idx + 1) % num_col_options;
            else if (real_f == 3) {
                int vnum = value_idx > 0 ? (use_headers ? col_name_to_num(col_options[value_idx]) : col_to_num(col_options[value_idx])) : -1;
                ColType vt = (vnum >= 0) ? col_types[vnum] : COL_STR;
                int max_a = (vt == COL_NUM) ? 6 : (vt == COL_DATE ? 5 : 2);
                agg_idx = (agg_idx + 1) % max_a;
            }
            else if (real_f == 4) {
                // Меняем только если активно (есть дата)
                if (has_date_group) {
                    date_idx = (date_idx + 1) % num_date_options;
                }
            }
            else if (real_f == 5) row_tot_idx = (row_tot_idx + 1) % num_yn;
            else if (real_f == 6) col_tot_idx = (col_tot_idx + 1) % num_yn;
            else if (real_f == 7) grand_idx = (grand_idx + 1) % num_yn;
            else if (real_f == 8) row_sort_idx = (row_sort_idx + 1) % num_sort_opts;
            else if (real_f == 9) col_sort_idx = (col_sort_idx + 1) % num_sort_opts;
        }
        else if (ch == KEY_LEFT || ch == 'h' || ch == 'H') {
            int real_f = current_field;
            if (real_f == 0) row_group_idx = (row_group_idx - 1 + num_col_options) % num_col_options;
            else if (real_f == 1) col_group_idx = (col_group_idx - 1 + num_col_options) % num_col_options;
            else if (real_f == 2) value_idx = (value_idx - 1 + num_col_options) % num_col_options;
            else if (real_f == 3) {
                int vnum = value_idx > 0 ? (use_headers ? col_name_to_num(col_options[value_idx]) : col_to_num(col_options[value_idx])) : -1;
                ColType vt = (vnum >= 0) ? col_types[vnum] : COL_STR;
                int max_a = (vt == COL_NUM) ? 6 : (vt == COL_DATE ? 5 : 2);
                agg_idx = (agg_idx - 1 + max_a) % max_a;
            }
            else if (real_f == 4) {
                if (has_date_group) {
                    date_idx = (date_idx - 1 + num_date_options) % num_date_options;
                }
            }
            else if (real_f == 5) row_tot_idx = (row_tot_idx - 1 + num_yn) % num_yn;
            else if (real_f == 6) col_tot_idx = (col_tot_idx - 1 + num_yn) % num_yn;
            else if (real_f == 7) grand_idx = (grand_idx - 1 + num_yn) % num_yn;
            else if (real_f == 8) row_sort_idx = (row_sort_idx - 1 + num_sort_opts) % num_sort_opts;
            else if (real_f == 9) col_sort_idx = (col_sort_idx - 1 + num_sort_opts) % num_sort_opts;
        }
        else if (ch == 10 || ch == KEY_ENTER) {
            free(settings->row_group_col);
            free(settings->col_group_col);
            free(settings->value_col);
            free(settings->aggregation);
            free(settings->date_grouping);
            free(settings->row_sort);
            free(settings->col_sort);

            settings->row_group_col = row_group_idx > 0 ? strdup(col_options[row_group_idx]) : NULL;
            settings->col_group_col = col_group_idx > 0 ? strdup(col_options[col_group_idx]) : NULL;
            settings->value_col = value_idx > 0 ? strdup(col_options[value_idx]) : NULL;
            settings->aggregation = strdup(agg_options[agg_idx]);

            int has_date = 0;
            if (settings->row_group_col) {
                int idx = use_headers ? col_name_to_num(settings->row_group_col) : col_to_num(settings->row_group_col);
                if (idx >= 0 && col_types[idx] == COL_DATE) has_date = 1;
            }
            if (settings->col_group_col) {
                int idx = use_headers ? col_name_to_num(settings->col_group_col) : col_to_num(settings->col_group_col);
                if (idx >= 0 && col_types[idx] == COL_DATE) has_date = 1;
            }
            settings->date_grouping = has_date ? strdup(date_options[date_idx]) : strdup("Auto");

            settings->show_row_totals  = (row_tot_idx == 0);
            settings->show_col_totals  = (col_tot_idx == 0);
            settings->show_grand_total = (grand_idx == 0);
            settings->row_sort = strdup(sort_values[row_sort_idx]);
            settings->col_sort = strdup(sort_values[col_sort_idx]);

            if (settings->value_col) {
                save_pivot_settings(csv_filename, settings);
                delwin(win);
                for (int i = 0; i < num_col_options; i++) free(col_options[i]);
                free(col_options);
                build_and_show_pivot(settings, csv_filename, height, width);
                return;
            }
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q') {
            delwin(win);
            for (int i = 0; i < num_col_options; i++) free(col_options[i]);
            free(col_options);
            return;
        }
    }
}

void build_and_show_pivot(PivotSettings *settings, const char *csv_filename, int height, int width) {
    // Get indices
    int row_group_idx = settings->row_group_col ? (use_headers ? col_name_to_num(settings->row_group_col) : col_to_num(settings->row_group_col)) : -1;
    int col_group_idx = settings->col_group_col ? (use_headers ? col_name_to_num(settings->col_group_col) : col_to_num(settings->col_group_col)) : -1;
    int value_idx = (use_headers ? col_name_to_num(settings->value_col) : col_to_num(settings->value_col));
    if (value_idx < 0) return;

    ColType value_type = col_types[value_idx];
    ColType row_type = (row_group_idx >= 0) ? col_types[row_group_idx] : COL_STR;
    ColType col_type = (col_group_idx >= 0) ? col_types[col_group_idx] : COL_STR;

    int display_count = filter_active ? filtered_count : (sort_col >= 0 ? sorted_count : row_count);

    // Определяем типы группировки — ТОЛЬКО ЗДЕСЬ, ОДИН РАЗ
    int row_is_date = (row_group_idx >= 0) && (col_types[row_group_idx] == COL_DATE);
    int col_is_date = (col_group_idx >= 0) && (col_types[col_group_idx] == COL_DATE);

    // Pass 1: collect unique row and col keys
    HashMap *row_map = hash_map_create(1024);
    HashMap *col_map = hash_map_create(1024);

    draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
    attron(COLOR_PAIR(3));
    printw(" | Collecting groups... 0%%                   ");
    attroff(COLOR_PAIR(3));
    refresh();

    int start_row = use_headers ? 1 : 0;
    for (int d = 0; d < display_count; d++) {
        int real_row = get_real_row(d);
        if (real_row < start_row) continue;

        if (!rows[real_row].line_cache) {
            fseek(f, rows[real_row].offset, SEEK_SET);
            char buf[MAX_LINE_LEN];
            if (fgets(buf, sizeof(buf), f)) {
                buf[strcspn(buf, "\n")] = '\0';
                rows[real_row].line_cache = strdup(buf);
            } else {
                rows[real_row].line_cache = strdup("");
            }
        }
        const char *line = rows[real_row].line_cache;

        if (row_group_idx >= 0) {
            char *rval = get_column_value(line, settings->row_group_col, use_headers);
            char *rkey = get_group_key(rval, row_type, settings->date_grouping, row_group_idx);
            if (rkey && *rkey) {
                if (!hash_map_get(row_map, rkey)) hash_map_put(row_map, rkey, (void*)1);
            }
            free(rkey);
            free(rval);
        } else {
            hash_map_put(row_map, "Total", (void*)1);
        }

        if (col_group_idx >= 0) {
            char *cval = get_column_value(line, settings->col_group_col, use_headers);
            char *ckey = get_group_key(cval, col_type, settings->date_grouping, col_group_idx);
            if (ckey && *ckey) {
                if (!hash_map_get(col_map, ckey)) hash_map_put(col_map, ckey, (void*)1);
            }
            free(ckey);
            free(cval);
        } else {
            hash_map_put(col_map, "Total", (void*)1);
        }

        if (d % 50000 == 0) {
            draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Collecting groups... %3d%%                 ", (int)(100.0 * d / display_count));
            attroff(COLOR_PAIR(3));
            refresh();
        }
    }

    int unique_rows;
    char **row_keys = hash_map_keys(row_map, &unique_rows);

    int unique_cols;
    char **col_keys = hash_map_keys(col_map, &unique_cols);

    // Сортируем ключи
    if (row_is_date) {
        qsort(row_keys, unique_rows, sizeof(char*), compare_date_keys);
    } else {
        qsort(row_keys, unique_rows, sizeof(char*), compare_str);
    }

    if (col_is_date) {
        qsort(col_keys, unique_cols, sizeof(char*), compare_date_keys);
    } else {
        qsort(col_keys, unique_cols, sizeof(char*), compare_str);
    }

    int total_rows = unique_rows + (settings->show_col_totals ? 1 : 0);
    int total_cols = unique_cols + (settings->show_row_totals ? 1 : 0);

    // Вычисляем ширину левого столбца
    int max_row_key_len = strlen("Row \\ Col");
    for (int i = 0; i < unique_rows; i++) {
        int len = strlen(row_keys[i]);
        if (len > max_row_key_len) max_row_key_len = len;
    }
    if (settings->show_col_totals) {
        int len = strlen("Total");
        if (len > max_row_key_len) max_row_key_len = len;
    }

    int pivot_row_index_width = max_row_key_len + 4; // запас по бокам

    const int MIN_PIVOT_ROW_WIDTH = 12;
    const int MAX_PIVOT_ROW_WIDTH = 40;
    if (pivot_row_index_width < MIN_PIVOT_ROW_WIDTH) pivot_row_index_width = MIN_PIVOT_ROW_WIDTH;
    if (pivot_row_index_width > MAX_PIVOT_ROW_WIDTH) pivot_row_index_width = MAX_PIVOT_ROW_WIDTH;

    // Allocate matrix
    Agg **matrix = malloc(unique_rows * sizeof(Agg*));
    for (int i = 0; i < unique_rows; i++) {
        matrix[i] = calloc(unique_cols, sizeof(Agg));
        for (int j = 0; j < unique_cols; j++) {
            matrix[i][j].min = INFINITY;
            matrix[i][j].max = -INFINITY;
        }
    }

    Agg *row_totals = calloc(unique_rows, sizeof(Agg));
    for (int i = 0; i < unique_rows; i++) {
        row_totals[i].min = INFINITY;
        row_totals[i].max = -INFINITY;
    }

    Agg *col_totals = calloc(unique_cols, sizeof(Agg));
    for (int i = 0; i < unique_cols; i++) {
        col_totals[i].min = INFINITY;
        col_totals[i].max = -INFINITY;
    }

    Agg grand = {0, 0, INFINITY, -INFINITY, 0, NULL, NULL};

    HashMap **cell_uniques = calloc(unique_rows * unique_cols, sizeof(HashMap*));
    HashMap **row_unique_maps = calloc(unique_rows, sizeof(HashMap*));
    HashMap **col_unique_maps = calloc(unique_cols, sizeof(HashMap*));
    HashMap *grand_unique_map = hash_map_create(4096);

    // Второй проход: агрегация
    draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
    attron(COLOR_PAIR(3));
    printw(" | Aggregating... 0%%                         ");
    attroff(COLOR_PAIR(3));
    refresh();

    for (int d = 0; d < display_count; d++) {
        int real_row = get_real_row(d);
        if (real_row < start_row) continue;

        if (!rows[real_row].line_cache) {
            fseek(f, rows[real_row].offset, SEEK_SET);
            char buf[MAX_LINE_LEN];
            if (fgets(buf, sizeof(buf), f)) {
                buf[strcspn(buf, "\n")] = '\0';
                rows[real_row].line_cache = strdup(buf);
            } else {
                rows[real_row].line_cache = strdup("");
            }
        }
        const char *line = rows[real_row].line_cache;

        // Получаем значения
        char *rval = (row_group_idx >= 0) ? get_column_value(line, settings->row_group_col, use_headers) : strdup("Total");
        char *cval = (col_group_idx >= 0) ? get_column_value(line, settings->col_group_col, use_headers) : strdup("Total");
        char *vval = get_column_value(line, settings->value_col, use_headers);

        // Генерируем ключи
        char *rkey = get_group_key(rval, row_type, settings->date_grouping, row_group_idx);
        char *ckey = get_group_key(cval, col_type, settings->date_grouping, col_group_idx);

        // Поиск индексов — с правильным компаратором
        int ridx = -1;
        if (rkey && *rkey) {
            char *rkey_ptr = rkey;
            if (row_is_date) {
                char **rfind = (char**)bsearch(&rkey_ptr, row_keys, unique_rows, sizeof(char*), compare_date_keys);
                ridx = rfind ? (rfind - row_keys) : -1;
            } else {
                char **rfind = (char**)bsearch(&rkey_ptr, row_keys, unique_rows, sizeof(char*), compare_str);
                ridx = rfind ? (rfind - row_keys) : -1;
            }
        }

        int cidx = -1;
        if (ckey && *ckey) {
            char *ckey_ptr = ckey;
            if (col_is_date) {
                char **cfind = (char**)bsearch(&ckey_ptr, col_keys, unique_cols, sizeof(char*), compare_date_keys);
                cidx = cfind ? (cfind - col_keys) : -1;
            } else {
                char **cfind = (char**)bsearch(&ckey_ptr, col_keys, unique_cols, sizeof(char*), compare_str);
                cidx = cfind ? (cfind - col_keys) : -1;
            }
        }

        if (ridx >= 0 && cidx >= 0) {
            Agg *agg = &matrix[ridx][cidx];
            update_agg(agg, vval, value_type);

            // Unique count для ячейки
            int cell_idx = ridx * unique_cols + cidx;
            if (!cell_uniques[cell_idx]) cell_uniques[cell_idx] = hash_map_create(64);
            const char *v_for_unique = vval ? vval : "";
            if (!hash_map_get(cell_uniques[cell_idx], v_for_unique)) {
                hash_map_put(cell_uniques[cell_idx], v_for_unique, (void*)1);
                agg->unique_count++;
            }

            // Row total
            update_agg(&row_totals[ridx], vval, value_type);
            if (!row_unique_maps[ridx]) row_unique_maps[ridx] = hash_map_create(4096);
            if (!hash_map_get(row_unique_maps[ridx], v_for_unique)) {
                hash_map_put(row_unique_maps[ridx], v_for_unique, (void*)1);
                row_totals[ridx].unique_count++;
            }

            // Col total
            update_agg(&col_totals[cidx], vval, value_type);
            if (!col_unique_maps[cidx]) col_unique_maps[cidx] = hash_map_create(4096);
            if (!hash_map_get(col_unique_maps[cidx], v_for_unique)) {
                hash_map_put(col_unique_maps[cidx], v_for_unique, (void*)1);
                col_totals[cidx].unique_count++;
            }

            // Grand total
            update_agg(&grand, vval, value_type);
            if (!hash_map_get(grand_unique_map, v_for_unique)) {
                hash_map_put(grand_unique_map, v_for_unique, (void*)1);
                grand.unique_count++;
            }
        }

        // Освобождаем память
        free(rval);
        free(cval);
        free(vval);
        free(rkey);
        free(ckey);

        if (d % 50000 == 0) {
            draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Aggregating... %3d%%                       ", (int)(100.0 * d / display_count));
            attroff(COLOR_PAIR(3));
            refresh();
        }
    }

    // Free uniques
    for (int i = 0; i < unique_rows * unique_cols; i++) if (cell_uniques[i]) hash_map_destroy(cell_uniques[i]);
    free(cell_uniques);
    for (int i = 0; i < unique_rows; i++) if (row_unique_maps[i]) hash_map_destroy(row_unique_maps[i]);
    free(row_unique_maps);
    for (int i = 0; i < unique_cols; i++) if (col_unique_maps[i]) hash_map_destroy(col_unique_maps[i]);
    free(col_unique_maps);
    hash_map_destroy(grand_unique_map);
    hash_map_destroy(row_map);
    hash_map_destroy(col_map);

    // ── Строим display-порядок строк и столбцов согласно настройкам сортировки ──
    const char *rsort = settings->row_sort ? settings->row_sort : "KEY_ASC";
    const char *csort = settings->col_sort ? settings->col_sort : "KEY_ASC";

    int *row_order = malloc(unique_rows * sizeof(int));
    int *col_order = malloc(unique_cols * sizeof(int));
    for (int i = 0; i < unique_rows; i++) row_order[i] = i;
    for (int i = 0; i < unique_cols; i++) col_order[i] = i;

    if (strcmp(rsort, "KEY_DESC") == 0) {
        for (int i = 0; i < unique_rows / 2; i++) {
            int tmp = row_order[i];
            row_order[i] = row_order[unique_rows - 1 - i];
            row_order[unique_rows - 1 - i] = tmp;
        }
    } else if (strcmp(rsort, "VAL_ASC") == 0 || strcmp(rsort, "VAL_DESC") == 0) {
        g_sort_agg_arr = row_totals;
        g_sort_agg_str = settings->aggregation ? settings->aggregation : "SUM";
        g_sort_vtype   = value_type;
        g_sort_dir     = strcmp(rsort, "VAL_ASC") == 0 ? 1 : -1;
        qsort(row_order, unique_rows, sizeof(int), compare_order_by_agg);
    }

    if (strcmp(csort, "KEY_DESC") == 0) {
        for (int i = 0; i < unique_cols / 2; i++) {
            int tmp = col_order[i];
            col_order[i] = col_order[unique_cols - 1 - i];
            col_order[unique_cols - 1 - i] = tmp;
        }
    } else if (strcmp(csort, "VAL_ASC") == 0 || strcmp(csort, "VAL_DESC") == 0) {
        g_sort_agg_arr = col_totals;
        g_sort_agg_str = settings->aggregation ? settings->aggregation : "SUM";
        g_sort_vtype   = value_type;
        g_sort_dir     = strcmp(csort, "VAL_ASC") == 0 ? 1 : -1;
        qsort(col_order, unique_cols, sizeof(int), compare_order_by_agg);
    }

    // Строки отображения sort для заголовка
    const char *rsort_disp = strcmp(rsort,"KEY_ASC")==0  ? "Key\xe2\x86\x91" :
                             strcmp(rsort,"KEY_DESC")==0  ? "Key\xe2\x86\x93" :
                             strcmp(rsort,"VAL_ASC")==0   ? "Val\xe2\x86\x91" : "Val\xe2\x86\x93";
    const char *csort_disp = strcmp(csort,"KEY_ASC")==0  ? "Key\xe2\x86\x91" :
                             strcmp(csort,"KEY_DESC")==0  ? "Key\xe2\x86\x93" :
                             strcmp(csort,"VAL_ASC")==0   ? "Val\xe2\x86\x91" : "Val\xe2\x86\x93";

    // Now display loop
    int top_row = 0, cur_row_p = 0, left_col_p = 0, cur_col_p = 0;
    int vis_rows = height - 7;
    int vis_cols = (width - pivot_row_index_width - 2) / CELL_WIDTH;

    while (1) {
        clear();
        draw_menu(0, 1, width, 2);
        draw_table_frame(1, 0, height - 2, width);
        draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);

        // Временный буфер для буквенных обозначений столбцов (A, B, AA...)
        char buf[16];

        // Сбрасываем позицию курсора и начинаем вывод с y=3, x=2
        move(3, 2);

        attron(COLOR_PAIR(6));  // основной цвет для скобок и меток

        printw("[");

        // Rows
        printw("Rows:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));  // яркий для имени столбца
        if (row_group_idx >= 0) {
            const char *name = use_headers && column_names[row_group_idx] ? column_names[row_group_idx] : (col_letter(row_group_idx, buf), buf);
            printw("%s", name);
        } else {
            printw("None");
        }
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Columns
        printw("[Cols:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        if (col_group_idx >= 0) {
            const char *name = use_headers && column_names[col_group_idx] ? column_names[col_group_idx] : (col_letter(col_group_idx, buf), buf);
            printw("%s", name);
        } else {
            printw("None");
        }
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Values
        printw("[Val:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        if (value_idx >= 0) {
            const char *name = use_headers && column_names[value_idx] ? column_names[value_idx] : (col_letter(value_idx, buf), buf);
            printw("%s", name);
        } else {
            printw("None");
        }
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Agg
        printw("[Agg:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", settings->aggregation ? settings->aggregation : "SUM");
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Date
        printw("[Date:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", settings->date_grouping ? settings->date_grouping : "Auto");
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Sort
        printw("[RS:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", rsort_disp);
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw(" CS:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", csort_disp);
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        attroff(COLOR_PAIR(6));
        refresh();

        // Заголовок столбцов (верхняя строка)
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(4, 2, "%-*s", pivot_row_index_width - 2, "Row \\ Col");
        for (int c = 0; c < vis_cols; c++) {
            int cid = left_col_p + c;
            if (cid >= total_cols) break;
            char *key = (cid < unique_cols) ? col_keys[col_order[cid]] : "Total";
            int is_current_col = (cid == cur_col_p);
            if (is_current_col) {
                attron(COLOR_PAIR(3)); // подсветка текущего столбца
            } else {
                attron(COLOR_PAIR(6));
            }
            mvprintw(4, pivot_row_index_width + c * CELL_WIDTH, "%*s",
                     CELL_WIDTH - 2, key);
            if (is_current_col) attroff(COLOR_PAIR(3));
            else attroff(COLOR_PAIR(6));
        }
        attroff(COLOR_PAIR(6) | A_BOLD);

        // Тело таблицы
        for (int i = 0; i < vis_rows; i++) {
            int rid = top_row + i;
            if (rid >= total_rows) break;
            int is_current_row = (rid == cur_row_p);
            // Название строки (слева)
            char *rkey = (rid < unique_rows) ? row_keys[row_order[rid]] : "Total";
            if (is_current_row) {
                attron(COLOR_PAIR(3)); // подсветка текущей строки
            } else {
                attron(COLOR_PAIR(6));
            }
            mvprintw(5 + i, 2, "%-*s", pivot_row_index_width - 2, rkey);
            if (is_current_row) attroff(COLOR_PAIR(3));
            else attroff(COLOR_PAIR(6));

            // Ячейки строки
            for (int c = 0; c < vis_cols; c++) {
                int cid = left_col_p + c;
                if (cid >= total_cols) break;
                const Agg *agg;
                int arid = (rid < unique_rows) ? row_order[rid] : unique_rows;
                int acid = (cid < unique_cols) ? col_order[cid] : unique_cols;
                if (rid < unique_rows && cid < unique_cols) agg = &matrix[arid][acid];
                else if (rid < unique_rows && cid == unique_cols) agg = &row_totals[arid];
                else if (rid == unique_rows && cid < unique_cols) agg = &col_totals[acid];
                else agg = &grand;
                char *disp = get_agg_display(agg, settings->aggregation, value_type);
                int is_current_cell = (rid == cur_row_p && cid == cur_col_p);
                int is_current_row_cell = (rid == cur_row_p);
                int is_current_col_cell = (cid == cur_col_p);
                if (is_current_cell) {
                    attron(COLOR_PAIR(2)); // яркая текущая ячейка
                } else if (is_current_row_cell || is_current_col_cell) {
                    attron(COLOR_PAIR(1)); // подсветка строки или столбца
                } else {
                    attron(COLOR_PAIR(8)); // обычный цвет
                }
                mvprintw(5 + i, pivot_row_index_width + c * CELL_WIDTH,
                         "%*s", CELL_WIDTH - 2, disp);
                attroff(COLOR_PAIR(2) | COLOR_PAIR(1) | COLOR_PAIR(8));
            }
        }
        refresh();

        int ch = getch();
        if (ch == ':') {
            char cmd_buf[128] = {0};
            draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | :");
            attroff(COLOR_PAIR(3));
            refresh();
            echo();
            wgetnstr(stdscr, cmd_buf, sizeof(cmd_buf) - 1);
            noecho();

            char *cmd = cmd_buf;
            char *arg = strchr(cmd_buf, ' ');
            if (arg) {
                *arg = '\0';
                arg++;
                while (*arg == ' ') arg++;
            }

            if (strcmp(cmd, "e") == 0) {
                char filename[256] = "pivot.csv";
                if (arg && *arg) strncpy(filename, arg, sizeof(filename) - 1);
                if (strstr(filename, ".csv") == NULL) strcat(filename, ".csv");

                FILE *out = fopen(filename, "w");
                if (out) {
                    // Header
                    fprintf(out, "Row");
                    for (int c = 0; c < total_cols; c++) {
                        char *key = (c < unique_cols) ? col_keys[col_order[c]] : "Total";
                        fprintf(out, ",%s", key);
                    }
                    fprintf(out, "\n");

                    // Body
                    for (int r = 0; r < total_rows; r++) {
                        int ar = (r < unique_rows) ? row_order[r] : unique_rows;
                        char *rkey = (r < unique_rows) ? row_keys[ar] : "Total";
                        fprintf(out, "%s", rkey);
                        for (int c = 0; c < total_cols; c++) {
                            int ac = (c < unique_cols) ? col_order[c] : unique_cols;
                            const Agg *agg;
                            if (r < unique_rows && c < unique_cols) agg = &matrix[ar][ac];
                            else if (r < unique_rows && c == unique_cols) agg = &row_totals[ar];
                            else if (r == unique_rows && c < unique_cols) agg = &col_totals[ac];
                            else agg = &grand;
                            char *disp = get_agg_display(agg, settings->aggregation, value_type);
                            fprintf(out, ",%s", disp);
                        }
                        fprintf(out, "\n");
                    }
                    fclose(out);
                    mvprintw(height - 1, 0, "Exported to %s", filename);
                    refresh();
                    getch();
                }
            } else if (strcmp(cmd, "q") == 0) {
                break;
            } else if (strcmp(cmd, "o") == 0) {
                show_pivot_settings_window(settings, csv_filename, height, width);
                break;
            }
            clrtoeol();
            refresh();
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (cur_row_p < total_rows - 1) cur_row_p++;
            if (cur_row_p >= top_row + vis_rows) top_row++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (cur_row_p > 0) cur_row_p--;
            if (cur_row_p < top_row) top_row = cur_row_p;
        } else if (ch == KEY_RIGHT || ch == 'l') {
            if (cur_col_p < total_cols - 1) cur_col_p++;
            if (cur_col_p >= left_col_p + vis_cols) left_col_p++;
        } else if (ch == KEY_LEFT || ch == 'h') {
            if (cur_col_p > 0) cur_col_p--;
            if (cur_col_p < left_col_p) left_col_p = cur_col_p;
        } 

        // H (большая) — на самый первый столбец
        else if (ch == 'H') {
            cur_col_p = 0;
            left_col_p = 0;
        }
        // L (большая) — на самый последний столбец
        else if (ch == 'L') {
            cur_col_p = total_cols - 1;
            // подстраиваем left_col_p так, чтобы последний столбец был виден
            left_col_p = cur_col_p - vis_cols + 1;
            if (left_col_p < 0) left_col_p = 0;
        }
        // Home → первая строка таблицы (не только видимая)
        else if (ch == KEY_HOME || ch == 'K') {
            cur_row_p = 0;
            top_row = 0;
        }
        // End → последняя строка таблицы
        else if (ch == KEY_END || ch == 'J') {
            cur_row_p = total_rows - 1;
            // подстраиваем top_row так, чтобы последняя строка была видна
            top_row = cur_row_p - vis_rows + 1;
            if (top_row < 0) top_row = 0;
        }
        // PageUp — страница вверх
        else if (ch == KEY_PPAGE) {
            int step = vis_rows - 1;
            if (step < 1) step = 1;
            cur_row_p -= step;
            if (cur_row_p < 0) cur_row_p = 0;
            top_row -= step;
            if (top_row < 0) top_row = 0;
        }
        // PageDown — страница вниз
        else if (ch == KEY_NPAGE) {
            int step = vis_rows - 1;
            if (step < 1) step = 1;
            cur_row_p += step;
            if (cur_row_p >= total_rows) cur_row_p = total_rows - 1;
            top_row += step;
            if (top_row > total_rows - vis_rows) top_row = total_rows - vis_rows;
            if (top_row < 0) top_row = 0;
        }
        else if (ch == 'q' || ch == 27) {
            break;
        }
    }

    // Free memory
    for (int i = 0; i < unique_rows; i++) {
        for (int j = 0; j < unique_cols; j++) {
            free(matrix[i][j].min_str);
            free(matrix[i][j].max_str);
        }
        free(matrix[i]);
    }
    free(matrix);

    for (int i = 0; i < unique_rows; i++) {
        free(row_totals[i].min_str);
        free(row_totals[i].max_str);
    }
    free(row_totals);

    for (int i = 0; i < unique_cols; i++) {
        free(col_totals[i].min_str);
        free(col_totals[i].max_str);
    }
    free(col_totals);

    free(grand.min_str);
    free(grand.max_str);

    for (int i = 0; i < unique_rows; i++) free(row_keys[i]);
    free(row_keys);

    for (int i = 0; i < unique_cols; i++) free(col_keys[i]);
    free(col_keys);

    free(row_order);
    free(col_order);
}