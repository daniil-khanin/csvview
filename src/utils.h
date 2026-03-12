#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <strings.h>    
#include <ctype.h>  

#include "csvview_defs.h"    

/**
 * Преобразует номер столбца (0-based) в буквенное обозначение Excel-стиля (A, AA, ZZ...)
 */
void col_letter(int col, char *buf);

/**
 * Преобразует буквенное обозначение столбца (A, AA, ZZ...) в номер (0-based)
 */
int col_to_num(const char *label);

/**
 * Находит номер столбца по его текстовому имени из заголовков
 */
int col_name_to_num(const char *name);

/**
 * Сохраняет все строки в файл (создаёт временный файл + rename)
 */
int save_file(const char *filename, FILE *orig_f, RowIndex *rows, int row_count);

/**
 * Регистронезависимый поиск подстроки (аналог strcasestr)
 */
char *strcasestr_custom(const char *haystack, const char *needle);

/**
 * Удаляет начальные и конечные пробельные символы (in-place)
 */
char *trim(char *str);

/**
 * Парсит строку фильтра в структурированное выражение FilterExpr
 * (выделяет память — нужно вызвать free_filter_expr после использования!)
 */
int parse_filter_expression(const char *query, FilterExpr *expr);

/**
 * Извлекает значение ячейки по имени столбца (возвращает новую строку — нужно free)
 */
char *get_column_value(const char *line, const char *col_name, int use_headers);

/**
 * Проверяет, выполняется ли одно условие для заданного значения ячейки
 */
int evaluate_condition(const char *cell, const Condition *cond);

/**
 * Проверяет, проходит ли строка под весь фильтр (с учётом AND/OR/!)
 */
int row_matches_filter(const char *line, const FilterExpr *expr);

/**
 * Освобождает всю динамически выделенную память внутри FilterExpr
 */
void free_filter_expr(FilterExpr *expr);

/**
 * Простое сравнение двух чисел с плавающей точкой.
 * Возвращает отрицательное значение, если da < db,
 * положительное если da > db, и ноль если равны.
 */

int compare_double(const void *a, const void *b);

void format_number_with_spaces(long long num, char *buf, size_t bufsize);

char *truncate_for_display(const char *str, int max_width);

char *clean_column_name(const char *raw);

/**
 * Парсит одну строку CSV по правилам RFC 4180 (простая реализация).
 * Возвращает массив выделенных полей (нужно free каждое поле и сам массив).
 *
 * @param line      Входная строка (из файла или кэша)
 * @param out_count [out] Сколько полей получилось
 * @return          Массив char* (NULL-terminated), каждый элемент — malloc-строка
 *                  При ошибке возвращает NULL, out_count = 0
 */
char **parse_csv_line(const char *line, int *out_count);

#endif