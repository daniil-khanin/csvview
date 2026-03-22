/**
 * column_format.h
 *
 * Интерфейс модуля форматирования и настройки отображения столбцов
 * Содержит функции инициализации форматов, обрезки строк, форматирования чисел/дат и главную функцию форматирования ячейки
 */

#ifndef COLUMN_FORMAT_H
#define COLUMN_FORMAT_H

#include "csvview_defs.h"   // ColType, ColumnFormat, col_formats, col_types и т.д.
#include "utils.h"          // trim, col_letter и т.д.

// ────────────────────────────────────────────────
// Публичные функции модуля
// ────────────────────────────────────────────────

/**
 * @brief Инициализирует форматы всех столбцов значениями по умолчанию
 *
 * Вызывается при загрузке нового файла или сбросе настроек.
 * Устанавливает:
 *   - truncate_len = 0
 *   - decimal_places = -1 (авто)
 *   - date_format = пустая строка
 *   - col_widths[i] = CELL_WIDTH
 */
void init_column_formats(void);

/**
 * @brief Обрезает строку до заданной длины с добавлением "..."
 *
 * @param str       Исходная строка (может быть NULL)
 * @param max_len   Максимальная длина (без учёта "...")
 * @return          Новая строка (malloc) — либо копия, либо обрезанная с "..."
 *                  Всегда нужно free() результат
 */
char *truncate_string(const char *str, int max_len);

/**
 * @brief Форматирует числовую строку с заданным количеством знаков после точки
 *
 * Поддерживает авто-режим (decimals < 0) — сохраняет до 6 знаков, убирает лишние нули
 *
 * @param raw_str   Исходная строка (может быть NULL или нечислом)
 * @param decimals  Количество знаков после точки (-1 = авто)
 * @return          Новая строка (malloc) с отформатированным числом
 *                  Всегда нужно free() результат
 */
char *format_number(const char *raw_str, int decimals);

/**
 * @brief Форматирует строку даты по заданному формату
 *
 * Пытается распознать несколько популярных входных форматов дат.
 * Если формат не распознан — возвращает исходную строку.
 *
 * @param date_str      Исходная строка даты
 * @param target_format Целевой формат (например "%d.%m.%Y")
 * @return              Новая строка (malloc) в целевом формате или исходная
 *                      Всегда нужно free() результат
 */
char *format_date(const char *date_str, const char *target_format);

/**
 * @brief Главная функция форматирования значения ячейки перед отрисовкой
 *
 * Применяет настройки формата столбца (truncate, decimals, date_format)
 * в зависимости от типа столбца (COL_STR, COL_NUM, COL_DATE)
 *
 * @param raw_value     Исходное значение ячейки (может быть NULL)
 * @param col_idx       Индекс столбца (0-based)
 * @return              Отформатированная строка (malloc)
 *                      Всегда нужно free() результат
 */
char *format_cell_value(const char *raw_value, int col_idx);

/**
 * @brief Сохраняет текущие настройки столбцов и фильтры в файл <csv_filename>.csvf
 *
 * Формат файла:
 *   use_headers:N
 *   col_count:N
 *   idx:type:truncate:decimals:date_format
 *   widths:w1,w2,...
 *   filter: запрос1
 *   filter: запрос2
 *
 * @param csv_filename  Имя исходного CSV-файла (без расширения .csvf)
 */
/**
 * @brief Автоматически определяет типы столбцов по сэмплу первых 200 строк данных.
 *
 * Для каждого столбца проверяет ≥90% непустых значений:
 *   - если парсятся как число → COL_NUM
 *   - если совпадают с датой → COL_DATE (приоритет над числами)
 *   - иначе → COL_STR
 * Поддерживаемые форматы дат: YYYY-MM-DD, DD.MM.YYYY, DD/MM/YYYY, YYYY/MM/DD, YYYY-MM.
 * Вызывать только когда .csvf не найден (до show_column_setup).
 */
void auto_detect_column_types(void);

void save_column_settings(const char *csv_filename);

/**
 * @brief Загружает настройки столбцов и фильтры из файла <csv_filename>.csvf
 *
 * Если файл не существует или формат не совпадает — возвращает 0, настройки остаются дефолтными.
 * Успешно загружает только если col_count совпадает с текущим.
 *
 * @param csv_filename  Имя исходного CSV-файла
 * @return 1 — полный успех, 2 — частичный (col_count изменился, глобальные настройки применены), 0 — файл не найден
 */
int  preload_delimiter(const char *csv_filename);  /* returns 1 if skip_comments was set explicitly */
int load_column_settings(const char *csv_filename);

/**
 * @brief Показывает интерактивное окно настройки типов и форматов столбцов
 *
 * Позволяет пользователю:
 *   - менять тип столбца (S/N/D)
 *   - задавать формат (обрезка строк, знаки после точки, формат даты)
 *   - включать/выключать заголовки
 *
 * Поддерживает два режима:
 *   - initial_setup = 1 — первый запуск (можно сразу выбрать с/без заголовков)
 *   - обычный режим — редактирование существующих настроек
 *
 * После изменений вызывает save_column_settings() для сохранения в .csvf
 *
 * @param rows          Массив индексов строк (нужен только для совместимости с сигнатурой)
 * @param f             FILE* исходного файла (не используется напрямую)
 * @param row_count_ptr Указатель на row_count (не используется)
 * @param initial_setup 1 = первый запуск (с выбором заголовков), 0 = обычный режим
 * @param csv_filename  Имя файла для сохранения настроек
 * @return
 *   0 — изменения применены и сохранены
 *   1 — пользователь отменил (Esc/q)
 *
 * @note
 *   - Окно занимает почти весь экран
 *   - Поддерживает навигацию ↑↓ ←→ Tab, быстрый выбор типа (S/N/D), ввод формата Enter
 *   - При первом запуске можно сразу выйти с выбором заголовков (H/1/2)
 *
 * @warning
 *   - Зависит от глобальных: col_count, column_names, col_types, col_formats,
 *     col_widths, use_headers, saved_filters и т.д.
 *   - Вызывает save_column_settings() при выходе с применением
 *   - Если col_count == 0 — просто возвращается
 */
int show_column_setup(const char *csv_filename);

#endif /* COLUMN_FORMAT_H */