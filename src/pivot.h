/**
 * pivot.h
 *
 * Интерфейс модуля сводных таблиц (pivot tables) для csvview
 * Содержит все функции работы с хэш-таблицей, агрегацией, настройками и отрисовкой pivot
 */

#ifndef PIVOT_H
#define PIVOT_H

#include "csvview_defs.h"   // Agg, PivotSettings, ColType, RowIndex и глобальные переменные
#include "utils.h"          // get_column_value, col_name_to_num и т.д.

/**
 * @brief Вычисляет хэш строки (используется для HashMap)
 * @param str   Строка для хэширования
 * @return      64-битный хэш
 */
unsigned long hash_string(const char *str);

/**
 * @brief Создаёт новую хэш-таблицу
 * @param size  Начальный размер (количество бакетов)
 * @return      Указатель на HashMap или NULL при ошибке
 */
HashMap *hash_map_create(int size);

/**
 * @brief Добавляет или обновляет значение по ключу
 * @param map   Хэш-таблица
 * @param key   Ключ (строка)
 * @param value Значение (указатель)
 */
void hash_map_put(HashMap *map, const char *key, void *value);

/**
 * @brief Получает значение по ключу
 * @param map   Хэш-таблица
 * @param key   Ключ
 * @return      Значение или NULL, если ключ не найден
 */
void *hash_map_get(HashMap *map, const char *key);

/**
 * @brief Возвращает массив всех ключей в хэш-таблице
 * @param map       Хэш-таблица
 * @param count     [out] Количество ключей
 * @return          Массив строк (нужно free каждый элемент и сам массив)
 */
char **hash_map_keys(HashMap *map, int *count);

/**
 * @brief Освобождает всю память хэш-таблицы
 * @param map   Хэш-таблица (можно NULL)
 */
void hash_map_destroy(HashMap *map);

// ────────────────────────────────────────────────
// Функции сравнения для qsort
// ────────────────────────────────────────────────

/**
 * @brief Сравнение двух строк для qsort (регистрозависимое)
 */
int compare_str(const void *a, const void *b);

/**
 * @brief Сравнение ключей дат для qsort (поддерживает YYYY-MM, YYYY-Qn, YYYY, век)
 */
int compare_date_keys(const void *a, const void *b);

// ────────────────────────────────────────────────
// Агрегация и ключи группировки
// ────────────────────────────────────────────────

/**
 * @brief Получает ключ группировки для значения ячейки (особенно для дат)
 * @param val       Значение ячейки
 * @param type      Тип столбца
 * @param grouping  Масштаб ("Month", "Quarter", "Year", "Century", "Auto")
 * @param col_idx   Индекс столбца (для date_format)
 * @return          Новая строка-ключ (malloc) — нужно free()
 */
char *get_group_key(const char *val, ColType type, const char *grouping, int col_idx);

/**
 * @brief Обновляет агрегацию одной ячейкой
 * @param agg         Структура агрегации
 * @param val         Значение ячейки
 * @param value_type  Тип значения (COL_NUM или другой)
 */
void update_agg(Agg *agg, const char *val, ColType value_type);

/**
 * @brief Форматирует результат агрегации для вывода
 * @param agg           Агрегация
 * @param aggregation   Тип ("SUM", "AVG", "COUNT" и т.д.)
 * @param value_type    Тип значения
 * @return              Статическая строка-буфер (не free!)
 */
char *get_agg_display(const Agg *agg, const char *aggregation, ColType value_type);

// ────────────────────────────────────────────────
// Настройки pivot
// ────────────────────────────────────────────────

/**
 * @brief Загружает настройки pivot из <csv_filename>.pivot
 * @param csv_filename  Имя файла
 * @param settings      Структура для заполнения
 * @return              1 — успешно, 0 — файл не найден / ошибка
 */
int load_pivot_settings(const char *csv_filename, PivotSettings *settings);

/**
 * @brief Сохраняет настройки pivot в <csv_filename>.pivot
 * @param csv_filename  Имя файла
 * @param settings      Настройки
 */
void save_pivot_settings(const char *csv_filename, const PivotSettings *settings);

// ────────────────────────────────────────────────
// Отрисовка и интерфейс
// ────────────────────────────────────────────────

/**
 * @brief Рисует рамку таблицы (используется внутри pivot)
 * @param y      Вертикальная позиция
 * @param x      Горизонтальная позиция
 * @param height Высота
 * @param width  Ширина
 */
void draw_table_frame(int y, int x, int height, int width);

/**
 * @brief Показывает окно настроек сводной таблицы
 * @param settings      Настройки (модифицируются)
 * @param csv_filename  Имя файла
 * @param height        Высота экрана
 * @param width         Ширина экрана
 */
void show_pivot_settings_window(PivotSettings *settings,
                                const char *csv_filename,
                                int height, int width);

/**
 * @brief Строит и отображает сводную таблицу по настройкам
 * @param settings      Настройки
 * @param csv_filename  Имя файла
 * @param height        Высота экрана
 * @param width         Ширина экрана
 */
void build_and_show_pivot(PivotSettings *settings,
                          const char *csv_filename,
                          int height, int width);

#endif /* PIVOT_H */