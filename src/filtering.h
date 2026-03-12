/**
 * filtering.h
 *
 * Интерфейс модуля фильтрации для csvview
 * Объявления функций работы с фильтрами
 */

#ifndef FILTERING_H
#define FILTERING_H

#include "csvview_defs.h"   // для RowIndex, FilterExpr и глобальных переменных

/**
 * Применяет текущий фильтр (filter_query) к таблице
 * Заполняет filtered_rows[] и filtered_count
 */
void apply_filter(RowIndex *rows, FILE *f, int row_count);

/**
 * Загружает сохранённые фильтры из файла <csv_filename>
 * (ожидает строки вида "filter: запрос")
 */
void load_saved_filters(const char *csv_filename);

/**
 * Сохраняет фильтр в файл <csv_filename>.csvf и в память
 */
void save_filter(const char *csv_filename, const char *query);

#endif /* FILTERING_H */