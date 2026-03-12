/**
 * sorting.c
 *
 * Реализация сортировки строк таблицы по выбранному столбцу
 */

#include "sorting.h"
#include "utils.h"          // get_column_value, col_name_to_num и т.д.
#include "ui_draw.h"        // spinner_tick / spinner_clear

#include <stdlib.h>         // qsort
#include <string.h>         // strcasecmp, strlen
#include <stdio.h>          // fseek, fgets (если нужно)

/**
 * @brief Функция сравнения двух строк для qsort (callback)
 *
 * Сравнивает значения двух строк таблицы в столбце sort_col.
 * Используется исключительно как аргумент для qsort().
 *
 * Логика сравнения:
 *   1. Заголовок (индекс 0) всегда считается "меньше" всех данных (если use_headers == 1)
 *   2. Лениво загружает строки в line_cache, если их ещё нет
 *   3. Получает значения ячеек по имени столбца (через get_column_value)
 *   4. Пытается сравнить как числа (strtod), если оба значения — валидные числа
 *   5. Иначе — лексикографическое регистронезависимое сравнение (strcasecmp)
 *   6. Умножает результат на sort_order (1 = asc, -1 = desc)
 *
 * @param pa  Указатель на индекс первой строки (int*)
 * @param pb  Указатель на индекс второй строки (int*)
 * @return
 *   -1   — первая строка должна идти раньше второй
 *    0   — строки равны по значению в sort_col
 *    1   — первая строка должна идти позже второй
 *    (результат уже умножен на sort_order)
 *
 * @note
 *   - Функция **лениво загружает** строки в rows[].line_cache (strdup внутри)
 *   - Если кэш не удалось загрузить — строка считается пустой ("")
 *   - Если sort_col некорректен или имя столбца не найдено — сравнивает пустые строки
 *   - Выделяет и освобождает память для val_a / val_b (через get_column_value и strdup)
 *
 * @example
 *   // Пример вызова через qsort (в build_sorted_index)
 *   qsort(sorted_rows, sorted_count, sizeof(int), compare_rows_by_column);
 *
 *   // Если sort_col = 2, sort_order = 1, столбец "Price"
 *   // "10.5" < "100" → числовое сравнение → -1
 *   // "apple" < "banana" → строковое → -1
 *   // "Apple" < "banana" → -1 (регистронезависимо)
 *
 * @warning
 *   - Зависит от глобальных переменных: sort_col, sort_order, rows, f, column_names,
 *     use_headers, col_count, MAX_LINE_LEN
 *   - Может выделять много памяти при первом вызове (если кэш пустой)
 *   - НЕ вызывать напрямую — только через qsort()
 *   - Если f == NULL или offsets некорректны → возможны ошибки чтения файла
 *   - При очень больших файлах ленивая загрузка может замедлить первую сортировку
 *
 * @see
 *   - build_sorted_index() — единственное место, где эта функция используется
 *   - get_column_value() — получение значения ячейки
 *   - strcasecmp() — регистронезависимое сравнение строк
 */

int compare_rows_by_column(const void *pa, const void *pb)
{
    // Извлекаем индексы строк из аргументов qsort
    int ra = *(const int *)pa;
    int rb = *(const int *)pb;

    // Заголовок (если есть) всегда считается "меньше" всех данных
    // Это гарантирует, что строка 0 остаётся первой
    if (use_headers)
    {
        if (ra == 0) return -1;
        if (rb == 0) return 1;
    }

    // Ленивая загрузка строки A, если кэш пустой
    char *line_a = rows[ra].line_cache;
    if (!line_a)
    {
        fseek(f, rows[ra].offset, SEEK_SET);
        char buf[MAX_LINE_LEN];
        if (fgets(buf, sizeof(buf), f))
        {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            rows[ra].line_cache = strdup(buf);
            line_a = rows[ra].line_cache;
        }
        else
        {
            line_a = "";  // ошибка чтения → пустая строка
        }
    }

    // Ленивая загрузка строки B
    char *line_b = rows[rb].line_cache;
    if (!line_b)
    {
        fseek(f, rows[rb].offset, SEEK_SET);
        char buf[MAX_LINE_LEN];
        if (fgets(buf, sizeof(buf), f))
        {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            rows[rb].line_cache = strdup(buf);
            line_b = rows[rb].line_cache;
        }
        else
        {
            line_b = "";
        }
    }

    // Определяем имя столбца для сравнения
    const char *col_name = (use_headers && sort_col < col_count && column_names[sort_col])
                           ? column_names[sort_col]
                           : "";

    // Получаем значения ячеек (всегда новая память)
    char *val_a = get_column_value(line_a ? line_a : "", col_name, use_headers);
    char *val_b = get_column_value(line_b ? line_b : "", col_name, use_headers);

    // Защита от NULL (маловероятно, но на всякий случай)
    if (!val_a) val_a = strdup("");
    if (!val_b) val_b = strdup("");

    // Пытаемся интерпретировать как числа
    char *end_a, *end_b;
    double num_a = strtod(val_a, &end_a);
    double num_b = strtod(val_b, &end_b);

    int is_num_a = (end_a != val_a && *end_a == '\0');
    int is_num_b = (end_b != val_b && *end_b == '\0');

    int result;
    if (is_num_a && is_num_b)
    {
        // Числовое сравнение
        if (num_a < num_b)      result = -1;
        else if (num_a > num_b) result = 1;
        else                    result = 0;
    }
    else
    {
        // Строковое регистронезависимое сравнение
        result = strcasecmp(val_a, val_b);
    }

    // Освобождаем временно выделенную память
    free(val_a);
    free(val_b);

    // Учитываем направление сортировки (asc или desc)
    return sort_order * result;
}

/**
 * @brief Строит массив sorted_rows[] — индексы строк в отсортированном порядке
 *
 * Заполняет глобальный массив sorted_rows[] индексами строк данных (пропуская заголовок),
 * отсортированными по текущему столбцу sort_col.
 *
 * Логика работы:
 *   1. Проверяет корректность sort_col
 *   2. Определяет диапазон строк данных (исключая заголовок при use_headers)
 *   3. Заполняет массив последовательными индексами строк данных
 *   4. Выполняет qsort() с функцией compare_rows_by_column
 *
 * После вызова:
 *   - sorted_count = количество отсортированных строк (данных, без заголовка)
 *   - sorted_rows[0..sorted_count-1] = индексы в массиве rows[] в нужном порядке
 *
 * @note
 *   - Если sort_col некорректен (<0 или >= col_count) → сортировка сбрасывается (sorted_count = 0)
 *   - Заголовок (индекс 0) никогда не попадает в sorted_rows[]
 *   - Вызывает compare_rows_by_column, которая может лениво загружать строки в кэш
 *   - Эффективность: O(n log n) по количеству строк данных
 *
 * @example
 *   // Перед вызовом
 *   sort_col = 3;          // столбец D
 *   sort_order = -1;       // по убыванию
 *   use_headers = 1;
 *   row_count = 1001;      // 1 заголовок + 1000 данных
 *
 *   build_sorted_index();
 *   // После:
 *   // sorted_count == 1000
 *   // sorted_rows[0] — индекс самой большой строки по столбцу D
 *   // sorted_rows[999] — индекс самой маленькой
 *
 * @warning
 *   - Зависит от глобальных переменных: sort_col, sort_order, col_count,
 *     use_headers, row_count, sorted_rows, sorted_count
 *   - Может вызвать значительное потребление памяти и времени при первой сортировке
 *     (из-за ленивой загрузки строк в compare_rows_by_column)
 *   - НЕ сохраняет порядок равных элементов (нестабильная сортировка)
 *   - Если row_count очень большой — может потребоваться много времени
 *
 * @see
 *   - compare_rows_by_column() — функция сравнения, вызываемая qsort()
 *   - get_real_row() — использует результат этой функции
 *   - qsort() — стандартная функция сортировки из <stdlib.h>
 */
void build_sorted_index(void)
{
    // Проверка: если столбец для сортировки не задан или некорректен
    if (sort_col < 0 || sort_col >= col_count)
    {
        sorted_count = 0;
        return;
    }

    // Определяем начало данных (пропускаем заголовок, если он есть)
    int start = use_headers ? 1 : 0;

    if (filter_active && filtered_count > 0)
    {
        // Если фильтр активен — сортируем только отфильтрованные строки
        sorted_count = filtered_count;
        for (int i = 0; i < sorted_count; i++)
        {
            sorted_rows[i] = filtered_rows[i];
        }
    }
    else
    {
        // Иначе — сортируем все строки данных
        sorted_count = row_count - start;
        for (int i = 0; i < sorted_count; i++)
        {
            sorted_rows[i] = start + i;
        }
    }

    // Если строк больше одной — выполняем сортировку
    if (sorted_count > 1)
    {
        spinner_tick();
        qsort(sorted_rows, sorted_count, sizeof(int), compare_rows_by_column);
        spinner_clear();
    }
}

/**
 * @brief Преобразует видимый (экранный) индекс строки в реальный индекс в массиве rows[]
 *
 * Возвращает настоящий номер строки в файле (индекс в rows[]),
 * учитывая текущее состояние:
 *   - активную сортировку (sorted_rows[])
 *   - активный фильтр (filtered_rows[])
 *   - наличие заголовка (use_headers)
 *
 * Логика приоритетов:
 *   1. Если включена сортировка и количество отсортированных строк совпадает с видимыми —
 *      используем sorted_rows[display_idx]
 *   2. Если включён фильтр — используем filtered_rows[display_idx]
 *   3. В противном случае — обычный порядок + смещение на заголовок
 *
 * @param display_idx   Видимый индекс строки на экране (0 = первая видимая строка данных)
 *                      Может быть отрицательным или больше видимого количества
 *
 * @return
 *   Реальный индекс строки в массиве rows[] (0-based, включая заголовок если есть)
 *   Если display_idx < 0 → возвращает 0 (первая строка)
 *   Если display_idx слишком большой → возвращает индекс последней видимой строки
 *
 * @note
 *   - Функция используется везде, где нужно знать "что именно отображается под курсором"
 *     (draw_table_body, обработка клавиш, статистика, экспорт и т.д.)
 *   - Если одновременно активны и сортировка, и фильтр — приоритет у сортировки
 *     (но в текущей логике программы это редкий случай — фильтр обычно применяется раньше)
 *   - Корректно обрабатывает случай, когда заголовок есть (use_headers == 1)
 *
 * @example
 *   // Ситуация 1: фильтр активен, 50 строк видно
 *   filter_active = 1;
 *   filtered_count = 50;
 *   get_real_row(0)   → filtered_rows[0]
 *   get_real_row(49)  → filtered_rows[49]
 *   get_real_row(100) → filtered_rows[49] (последняя)
 *
 *   // Ситуация 2: сортировка активна, 1000 строк данных
 *   sort_col = 2;
 *   sorted_count = 1000;
 *   get_real_row(5)   → sorted_rows[5]
 *
 *   // Ситуация 3: ничего не активно, есть заголовок
 *   use_headers = 1;
 *   row_count = 1001;
 *   get_real_row(0)   → 1  (первая строка данных)
 *   get_real_row(999) → 1000
 *
 * @warning
 *   - Зависит от глобальных переменных: filter_active, filtered_count, filtered_rows,
 *     sort_col, sorted_count, sorted_rows, use_headers, row_count
 *   - НЕ проверяет границы массивов filtered_rows / sorted_rows — предполагается,
 *     что sorted_count и filtered_count всегда корректны
 *   - Если sorted_count != количеству видимых строк при сортировке — возможны ошибки
 *     (например, если фильтр применился после сортировки)
 *
 * @see
 *   - apply_filter() — заполняет filtered_rows и filtered_count
 *   - build_sorted_index() — заполняет sorted_rows и sorted_count
 *   - draw_table_body() — основной потребитель этой функции
 */

int get_real_row(int display_idx)
{
    // Защита от отрицательного индекса — всегда возвращаем первую строку
    if (display_idx < 0) {
        return 0;
    }

    // Вычисляем общее количество видимых строк (без заголовка)
    int total_visible = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));

    // Если запрашиваемый индекс за пределами видимых строк — возвращаем последнюю
    if (display_idx >= total_visible)
    {
        return total_visible - 1 + (use_headers ? 1 : 0);
    }

    // Приоритет 1: если активна сортировка и количество совпадает с видимыми
    // (т.е. сортировка применяется к текущему набору видимых строк)
    if (sort_col >= 0 && sorted_count == total_visible)
    {
        return sorted_rows[display_idx];
    }

    // Приоритет 2: если активен фильтр — берём из отфильтрованного массива
    if (filter_active)
    {
        return filtered_rows[display_idx];
    }

    // Обычный порядок: просто смещение на заголовок (если он есть)
    return display_idx + (use_headers ? 1 : 0);
}