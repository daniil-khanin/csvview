/**
 * graph.h
 *
 * Интерфейс модуля графиков для csvview
 * Отрисовка линейных, столбчатых и точечных графиков по столбцу
 */

#ifndef GRAPH_H
#define GRAPH_H

#include "csvview_defs.h"   // globals (graph_type, graph_scale, rows, f и т.д.)
#include "utils.h"          // format_cell_value, col_letter и т.д.

// ────────────────────────────────────────────────
// Публичные функции модуля
// ────────────────────────────────────────────────

/**
 * @brief Рисует график по выбранному столбцу (линия, столбцы, точки)
 *
 * Поддерживает:
 *   - линейный график (GRAPH_LINE)
 *   - столбчатый график (GRAPH_BAR)
 *   - точечный график (GRAPH_DOT)
 *   - логарифмическую шкалу (SCALE_LOG)
 *   - агрегацию по датам (если using_date_x и date_col задан)
 *   - выделение аномалий (если show_anomalies)
 *   - курсор с отображением значения (если show_graph_cursor)
 *
 * @param col           Индекс столбца для значений Y
 * @param height        Высота доступной области экрана
 * @param width         Ширина доступной области экрана
 * @param rows          Массив индексов строк
 * @param f             FILE* исходного файла
 * @param row_count     Общее количество строк
 * @param csv_filename  Имя файла (для статус-бара)
 * @param cursor_pos    Позиция курсора (-1 = не показывать)
 *
 * @note
 *   - Использует глобальные: graph_type, graph_scale, using_date_x, date_col,
 *     show_anomalies, show_graph_cursor, graph_anomaly_count, graph_anomalies
 *   - Лениво загружает строки в кэш
 *   - Автоматически определяет min/max по всем данным
 *   - Подсвечивает аномалии (z-score > ANOMALY_THRESHOLD)
 *   - Центрирует курсор и значение под ним
 *
 * @warning
 *   - Зависит от ncurses (mvaddch, attron и т.д.)
 *   - При большом количестве точек прореживает данные
 *   - Если столбец некорректен или нет данных — просто возвращается
 *
 * @see
 *   - draw_table_body() — место вызова (по клавише 'g')
 *   - format_cell_value() — форматирование значений
 *   - get_real_row() — преобразование индекса
 */
void draw_graph(int col, int height, int width, RowIndex *rows, FILE *f, int row_count, int cursor_pos, int min_max_show);

/**
 * Scatter plot: one dot per row at (x_col value, y_col value).
 * Draws braille dots, X/Y axis labels, and Pearson r in the corner.
 * Supports multi-series overlay via graph_overlay_mode / graph_global_min/max.
 */
void draw_scatter(int x_col, int y_col, int height, int width,
                  RowIndex *rows, FILE *f, int row_count, int cursor_pos);

/**
 * Рисует линию между двумя пикселями в массиве dots (алгоритм Брезенхема).
 * Экспортируется для использования в draw_pivot_graph.
 */
void draw_bresenham(bool *dots, int w, int h, int x0, int y0, int x1, int y1);

// Добавляем в конец graph.h
int find_min_index(double *values, int count);
int find_max_index(double *values, int count);

/**
 * Извлекает числовые значения из выбранного столбца для построения графика.
 * Учитывает фильтр, сортировку, агрегацию по месяцам (если включена).
 *
 * @param col                   индекс столбца (0-based)
 * @param rows                  массив индексов строк
 * @param f                     открытый файл csv
 * @param row_count             общее количество строк в файле
 * @param out_point_count       [out] сюда записывается количество полученных значений
 * @param out_aggregate         [out] true, если применялась агрегация по месяцам
 * @param target_date_fmt_out   [out] сюда копируется итоговый формат даты (буфер должен быть ≥32 байт)
 *
 * @return                      malloc-массив double (нужно free после использования)
 *                              или NULL при ошибке / нет данных
 */
double *extract_plot_values(int col, RowIndex *rows, FILE *f, int row_count, int *out_point_count, bool *out_aggregate, char *target_date_fmt_out );

/* Multi-series overlay — set before calling draw_graph in a loop */
extern double graph_global_min;
extern double graph_global_max;
extern int    graph_overlay_mode;
extern int    graph_draw_cursor_overlay; /* 1 = show cursor even in overlay passes */
extern int    graph_grid;               /* 0=off, 1=y-lines, 2=x-lines, 3=both */
extern double graph_last_cursor_y;      /* Y value at cursor after last draw_graph */
extern char   graph_last_cursor_x[64]; /* X label at cursor after last draw_graph */
extern int    graph_zoom_start;        /* first visible data point (0 = from start) */
extern int    graph_zoom_end;          /* last visible data point (-1 = all) */
extern int    graph_total_points;      /* total data points before zoom */
/* Dual Y axis — set by caller per series */
extern int    graph_use_right_axis;   /* 1 = current series uses right Y axis */
extern double graph_right_min;        /* right axis min */
extern double graph_right_max;        /* right axis max */
extern int    graph_right_axis_drawn; /* reset to 0 before loop; set by draw_graph */

#endif /* GRAPH_H */