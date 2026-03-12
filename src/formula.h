#ifndef FORMULA_H
#define FORMULA_H

/**
 * formula.h
 *
 * Вычисление формул для команды :cf
 *
 * Синтаксис:
 *   :cf price * qty
 *   :cf round((revenue - cost) / revenue * 100, 2)
 *   :cf if(qty > 0, revenue / qty, 0)
 *   :cf col_sum(amount) - col_sum_all(amount)
 *
 * Операторы:   + - * /  ( )  унарный -
 * Сравнения:   = != < <= > >=   (используются в if())
 * Функции:     round(x,n)  abs(x)  floor(x)  ceil(x)  mod(x,y)  pow(x,y)
 *              if(cond, val_true, val_false)  empty(col)
 * Агрегаты (по фильтру):
 *   col_sum col_avg col_min col_max col_count
 *   col_median  col_percentile(col,p)  col_stddev  col_var
 *   col_rank(col)  col_pct(col)
 * Агрегаты (по всему файлу, суффикс _all):
 *   col_sum_all ... col_rank_all  col_pct_all
 */

typedef struct Formula Formula;

/* Прогресс-колбэк: вызывается с текстом сообщения */
typedef void (*FormulaProgressFn)(const char *msg);

/* Компилировать формулу. Вернуть NULL при OOM.
   При синтаксической ошибке возвращает объект, formula_error() != NULL */
Formula *formula_compile(const char *expr);

/* Текст последней ошибки, или NULL если всё в порядке */
const char *formula_error(const Formula *f);

/* Предвычислить все агрегаты.
   disp_rows[0..disp_count-1] — реальные индексы строк текущего вида (filtered/sorted).
   Возвращает 0 при успехе. */
int formula_precompute(Formula *f,
                       int *disp_rows, int disp_count,
                       FormulaProgressFn cb);

/* Вычислить формулу для одной строки.
   real_row — индекс в rows[]; disp_idx — позиция в disp_rows (-1 если вне вида).
   line — CSV-строка этой строки (уже прочитана).
   out — результат.
   Возвращает 0 при успехе, 1 если результат неопределён (деление на 0, пустое поле). */
int formula_eval_row(const Formula *f,
                     int real_row, int disp_idx,
                     const char *line,
                     double *out);

/* Освободить память */
void formula_free(Formula *f);

#endif /* FORMULA_H */
