/**
 * csvview_defs.h
 * 
 * Центральный заголовочный файл проекта csvview.v11
 * Содержит:
 *   - все #define константы
 *   - все typedef enum и typedef struct
 *   - объявления (extern) глобальных переменных
 *   - минимально необходимые типы и макросы
 */

#ifndef CSVVIEW_DEFS_H
#define CSVVIEW_DEFS_H

#pragma once

#include <stdbool.h>        
#include <stddef.h>         
#include <stdint.h>
#include <stdio.h>         

// ────────────────────────────────────────────────
// Константы проекта
// ────────────────────────────────────────────────

#define CSVVIEW_VERSION         15

#define MAX_ROWS           		50000000
#define MAX_COLS                702
#define CELL_WIDTH              20
#define MAX_LINE_LEN           	8192
#define INPUT_BUF_SIZE         	1024
#define ROW_NUMBER_WIDTH        8
#define ROW_DATA_OFFSET         11
#define MAX_SEARCH_RESULTS 		100000

// Окно статистики
#define STATS_W                 150
#define STATS_H                 36
#define LEFT_W          		55
#define RIGHT_W         		(STATS_W - LEFT_W - 5)

// Гистограмма и статистика
#define MAX_BINS        		30
#define TOP_FREQ_COUNT  		10
#define BAR_MAX_WIDTH   		35

#define MAX_AGG 				100000
#define MAX_SAVED_FILTERS 		50

// Графики
#define GRAPH_COLOR_BASE 		10
#define ANOMALY_THRESHOLD 		3.0

// ────────────────────────────────────────────────
// Перечисления (enum)
// ────────────────────────────────────────────────

typedef enum {
    COL_STR,
    COL_NUM,
    COL_DATE
} ColType;

typedef enum {
    OP_EQ, OP_NE, OP_GT, OP_GE, OP_LT, OP_LE
} CompareOp;

typedef enum {
    LOGIC_AND,
    LOGIC_OR
} LogicOperator;

typedef enum {
    GRAPH_LINE,
    GRAPH_BAR,
    GRAPH_DOT
} GraphType;

typedef enum {
    SCALE_LINEAR,
    SCALE_LOG
} GraphScale;

// ────────────────────────────────────────────────
// Структуры
// ────────────────────────────────────────────────

typedef struct {
    int truncate_len;       // 0 = не обрезать, >0 = макс. кол-во символов
    int decimal_places;     // -1 = авто, 0..8 = фиксированное
    char date_format[32];   // "YYYY-MM-DD", "DD.MM.YYYY", etc.
} ColumnFormat;

typedef struct {
    long offset;
    char *line_cache;
} RowIndex;

typedef struct {
    int row;
    int col;
} SearchResult;

typedef struct {
    char *column;
    CompareOp op;
    char *value;
    int value_is_num;
    double value_num;
} Condition;

typedef struct {
    Condition *conditions;
    LogicOperator *logic_ops;
    int cond_count;
    int negated;
} FilterExpr;

typedef struct {
    double sum;
    int count;
    double min;
    double max;
    int unique_count;
    char *min_str;
    char *max_str;
} Agg;

typedef struct Entry {
    char *key;
    void *value;
    struct Entry *next;
} Entry;

typedef struct {
    Entry **buckets;
    int size;
} HashMap;

typedef struct {
    char *row_group_col;
    char *col_group_col;
    char *value_col;
    char *aggregation;
    char *date_grouping;
    int show_row_totals;
    int show_col_totals;
    int show_grand_total;
    char *row_sort;     // "KEY_ASC", "KEY_DESC", "VAL_ASC", "VAL_DESC"
    char *col_sort;     // "KEY_ASC", "KEY_DESC", "VAL_ASC", "VAL_DESC"
} PivotSettings;

// ────────────────────────────────────────────────
// Глобальные переменные (только объявления — extern)
// ────────────────────────────────────────────────

// Фильтры и поиск
extern char *saved_filters[MAX_SAVED_FILTERS];
extern int saved_filter_count;

extern SearchResult search_results[MAX_SEARCH_RESULTS];
extern int search_count;
extern int search_index;
extern char search_query[256];
extern int in_search_mode;

extern int *filtered_rows;
extern int filtered_count;
extern char filter_query[256];
extern int in_filter_mode;
extern int filter_active;

// Навигация и отображение
extern int cur_display_row;
extern int top_display_row;
extern int cur_real_row;
extern int cur_col;
extern int left_col;

extern char file_size_str[32];

// Столбцы и заголовки
extern char *column_names[MAX_COLS];
extern ColType col_types[MAX_COLS];
extern ColumnFormat col_formats[MAX_COLS];
extern int col_count;
extern int use_headers;
extern int col_widths[MAX_COLS];

// Сортировка
extern int sort_col;
extern int sort_order;

/* Multi-column sort levels */
#define MAX_SORT_LEVELS 8
typedef struct { int col; int order; /* 1=asc, -1=desc */ } SortLevel;
extern SortLevel sort_levels[MAX_SORT_LEVELS];
extern int       sort_level_count;
extern int *sorted_rows;
extern int sorted_count;

// Общие данные файла
extern int row_count;
extern RowIndex *rows;
extern FILE *f;

// Графики
extern int in_graph_mode;
extern int graph_col_list[10];
extern int graph_col_count;
extern int current_graph;
extern int graph_start;
extern int graph_scroll_step;
extern int using_date_x;
extern int date_col;
extern int date_x_col;
extern int current_graph_color_pair;
extern GraphType graph_type;
extern GraphScale graph_scale;
extern int graph_cursor_pos;
extern int graph_visible_points;
extern int graph_anomaly_count;
extern double *graph_anomalies;
extern bool show_anomalies;
extern bool show_graph_cursor;

// Временное сохранение состояния сортировки/фильтра
extern int save_sort_col;
extern int save_sort_order;
extern int save_sort_level_count;
extern int save_sorted_count;
extern int *save_sorted_rows;
extern int save_filtered_count;
extern int *save_filtered_rows;

// Разделитель полей: ',' — CSV (default), '\t' — TSV, '|' — PSV и т.д.
extern char csv_delimiter;

// Заморозка столбцов: первые N столбцов всегда видны (не прокручиваются)
extern int freeze_cols;

// Скрытие столбцов: 1 = столбец скрыт (не отображается)
extern int col_hidden[MAX_COLS];

// Относительная нумерация строк: 0 = абсолютная, 1 = относительная
extern int relative_line_numbers;

// Закладки (vim-style): индекс 0–25 = буквы a–z, значение = real row index (-1 = не задана)
extern int bookmarks[26];

// Drill-down из pivot: после Enter на ячейке здесь хранится фильтр для основной таблицы
// Пустая строка = нет drill-down, заполненная = надо применить фильтр
extern char pivot_drilldown_filter[512];

#endif