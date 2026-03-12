/**
 * sorting.h
 *
 * Интерфейс модуля сортировки для csvview
 * Объявления функций сравнения строк и построения индекса сортировки
 */

#ifndef SORTING_H
#define SORTING_H

#include "csvview_defs.h"   // RowIndex, globals (sort_col, sorted_rows и т.д.)
#include "utils.h"          // get_column_value, col_name_to_num и т.д.

// ────────────────────────────────────────────────
// Публичные функции модуля
// ────────────────────────────────────────────────

/**
 * @brief Функция сравнения двух строк для qsort
 * Используется для сортировки индексов строк по значению в столбце sort_col
 * Учитывает заголовок (всегда первый), числовое/строковое сравнение,
 * направление сортировки (sort_order)
 *
 * @param pa  Указатель на индекс первой строки (int*)
 * @param pb  Указатель на индекс второй строки (int*)
 * @return    -1, 0, 1 в зависимости от порядка (с учётом sort_order)
 */
int compare_rows_by_column(const void *pa, const void *pb);

/**
 * @brief Строит массив sorted_rows[] — индексы строк в отсортированном порядке
 * Заполняет sorted_count и sorted_rows[] только строками данных (пропуская заголовок)
 * Если sort_col < 0 — сбрасывает сортировку (sorted_count = 0)
 */
void build_sorted_index(void);

/**
 * @brief Возвращает реальный индекс строки в файле по видимому индексу
 *
 * Учитывает:
 *   - активную сортировку (sorted_rows)
 *   - активный фильтр (filtered_rows)
 *   - наличие заголовка (use_headers)
 *
 * @param display_idx   Позиция строки на экране (0 = первая видимая)
 * @return              Реальный индекс строки в массиве rows[] (0-based)
 */
int get_real_row(int display_idx);

#endif /* SORTING_H */