#ifndef FORMULA_H
#define FORMULA_H

/**
 * formula.h
 *
 * Formula evaluation for the :cf command
 *
 * Syntax:
 *   :cf price * qty
 *   :cf round((revenue - cost) / revenue * 100, 2)
 *   :cf if(qty > 0, revenue / qty, 0)
 *   :cf col_sum(amount) - col_sum_all(amount)
 *
 * Operators:   + - * /  ( )  unary -
 * Comparisons: = != < <= > >=   (used inside if())
 * Functions:   round(x,n)  abs(x)  floor(x)  ceil(x)  mod(x,y)  pow(x,y)
 *              if(cond, val_true, val_false)  empty(col)
 * Aggregates (over the active filter):
 *   col_sum col_avg col_min col_max col_count
 *   col_median  col_percentile(col,p)  col_stddev  col_var
 *   col_rank(col)  col_pct(col)
 * Aggregates (over the whole file, suffix _all):
 *   col_sum_all ... col_rank_all  col_pct_all
 */

typedef struct Formula Formula;

/* Progress callback: called with a status message */
typedef void (*FormulaProgressFn)(const char *msg);

/* Compile a formula. Returns NULL on OOM.
   On syntax error, returns an object where formula_error() != NULL */
Formula *formula_compile(const char *expr);

/* Text of the last error, or NULL if everything is fine */
const char *formula_error(const Formula *f);

/* Pre-compute all aggregates.
   disp_rows[0..disp_count-1] — real row indices of the current view (filtered/sorted).
   Returns 0 on success. */
int formula_precompute(Formula *f,
                       int *disp_rows, int disp_count,
                       FormulaProgressFn cb);

/* Evaluate the formula for a single row.
   real_row — index into rows[]; disp_idx — position in disp_rows (-1 if outside view).
   line — CSV line for this row (already read).
   out — result.
   Returns 0 on success, 1 if result is undefined (division by zero, empty field). */
int formula_eval_row(const Formula *f,
                     int real_row, int disp_idx,
                     const char *line,
                     double *out);

/* Free memory */
void formula_free(Formula *f);

#endif /* FORMULA_H */
