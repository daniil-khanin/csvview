/**
 * search.h
 *
 * Интерфейс модуля поиска по содержимому таблицы CSV
 * Объявления функций выполнения поиска и перехода к результату
 */

#ifndef SEARCH_H
#define SEARCH_H

#include "csvview_defs.h"   // RowIndex, SearchResult, search_results и глобальные переменные
#include "utils.h"          // strcasestr_custom и т.д.

// ────────────────────────────────────────────────
// Публичные функции модуля
// ────────────────────────────────────────────────

/**
 * @brief Выполняет поиск подстроки search_query по всем ячейкам таблицы
 *
 * Ищет регистронезависимо (strcasestr_custom) по всем ячейкам всех строк данных.
 * Результат сохраняется в глобальный массив search_results[] и search_count.
 * Каждый найденный результат — это пара (row, col).
 *
 * @param rows          Массив индексов строк (с offset и line_cache)
 * @param f             Открытый FILE* исходного CSV
 * @param row_count     Общее количество строк в файле (включая заголовок)
 *
 * @note
 *   - Пропускает строку-заголовок (если use_headers == 1)
 *   - Лениво загружает строки в line_cache, если их ещё нет
 *   - Ограничивает количество результатов до MAX_SEARCH_RESULTS
 *   - После вызова search_count и search_results готовы к использованию
 *   - search_index сбрасывается в -1 (нет текущего результата)
 *
 * @warning
 *   - Зависит от глобальных: search_query, search_results, search_count, search_index,
 *     use_headers, col_count, MAX_SEARCH_RESULTS
 *   - Может загрузить много строк в кэш при большом файле и коротком запросе
 *   - Не учитывает активный фильтр — ищет по всему файлу
 *
 * @see
 *   - goto_search_result() — переход к найденному результату
 *   - strcasestr_custom() — регистронезависимый поиск подстроки
 */
void perform_search(RowIndex *rows, FILE *f, int row_count);

/**
 * @brief Переходит к указанному результату поиска и прокручивает таблицу
 *
 * Устанавливает курсор на найденную ячейку (search_results[index]),
 * корректирует видимую область (top_display_row, left_col),
 * чтобы целевая строка была примерно посередине экрана.
 *
 * Учитывает:
 *   - активный фильтр (ищет позицию в filtered_rows[])
 *   - наличие заголовка (use_headers)
 *
 * @param index             Индекс результата в search_results[] (0..search_count-1)
 * @param cur_display_row   [out] Указатель на текущую видимую позицию курсора
 * @param top_display_row   [out] Указатель на первую видимую строку
 * @param cur_col           [out] Указатель на текущий столбец
 * @param left_col          [out] Указатель на левый видимый столбец
 * @param visible_rows      Количество видимых строк на экране
 * @param visible_cols      Количество видимых столбцов на экране
 * @param row_count         Общее количество строк в файле
 *
 * @note
 *   - Если index некорректен — функция просто возвращается
 *   - Если строка не прошла текущий фильтр — не прыгает
 *   - Прокрутка центрирует строку по вертикали и горизонтали
 *
 * @warning
 *   - Зависит от глобальных: search_results, search_index, filter_active,
 *     filtered_rows, filtered_count, use_headers, col_count
 *   - НЕ проверяет, что visible_rows/visible_cols корректны (предполагается, что да)
 *
 * @see
 *   - perform_search() — заполняет массив результатов
 *   - draw_table_body() — использует обновлённые cur_display_row и top_display_row
 */
void goto_search_result(int index,
                        int *cur_display_row,
                        int *top_display_row,
                        int *cur_col,
                        int *left_col,
                        int visible_rows,
                        int visible_cols,
                        int row_count);

#endif /* SEARCH_H */