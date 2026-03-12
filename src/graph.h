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

#endif /* GRAPH_H */