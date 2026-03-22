/**
 * graph.c
 *
 * Реализация отрисовки графиков по столбцу
 * Линейные, столбчатые, точечные графики с логарифмической шкалой и аномалиями
 */

#include "graph.h"
#include "csvview_defs.h"
#include "utils.h"
#include "ui_draw.h"
#include "sorting.h"
#include "filtering.h"
#include "column_format.h"

#include "csv_mmap.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <wchar.h>           // wcrtomb для Braille
#include <pthread.h>

/* Multi-series overlay globals — set by caller before looping draw_graph */
double graph_global_min = NAN;
double graph_global_max = NAN;
int    graph_overlay_mode = 0;  /* 0=single, 1=first pass (draw axes), 2=subsequent pass */
int    graph_draw_cursor_overlay = 0; /* 1 = draw cursor even in overlay mode */
int    graph_grid = 0;          /* 0=off, 1=y-lines, 2=x-lines, 3=both */
double graph_last_cursor_y = NAN;    /* Y value at cursor, set after each draw_graph */
char   graph_last_cursor_x[64];      /* X label at cursor, set after each draw_graph */

/* Inline field extractor — no malloc */
static void get_field_graph(const char *line, int idx, char *buf, int buf_size)
{
    buf[0] = '\0';
    if (!line || idx < 0) return;
    int field = 0;
    const char *p = line;
    int in_quote = 0;
    while (*p && field < idx) {
        if (*p == '"') { in_quote = !in_quote; p++; continue; }
        if (!in_quote && *p == csv_delimiter) field++;
        p++;
    }
    if (!*p || field != idx) return;
    char *out = buf, *out_end = buf + buf_size - 1;
    in_quote = (*p == '"');
    if (in_quote) p++;
    while (*p && out < out_end) {
        if (in_quote) {
            if (*p == '"') {
                if (*(p+1) == '"') { *out++ = '"'; p += 2; continue; }
                break;
            }
        } else {
            if (*p == csv_delimiter || *p == '\n' || *p == '\r') break;
        }
        *out++ = *p++;
    }
    *out = '\0';
}

/* Parallel worker for non-aggregate value extraction */
typedef struct {
    int  start_i;   /* display index start (including start_row offset) */
    int  count;
    int  start_row;
    int  col;
    int  row_count;
    double *values; /* write slice: values[start_i - start_row .. +count] */
} GraphWorker;

static void *graph_worker_thread(void *arg)
{
    GraphWorker *w = arg;
    char line_buf[MAX_LINE_LEN];
    char field_buf[256];
    double *out = w->values + (w->start_i - w->start_row);

    for (int i = 0; i < w->count; i++) {
        int r = get_real_row(w->start_i + i);
        if (r < 0 || r >= w->row_count) { out[i] = 0.0; continue; }

        const char *lp;
        if (g_mmap_base) {
            lp = csv_mmap_get_line(rows[r].offset, line_buf, sizeof(line_buf));
            if (!lp) { out[i] = 0.0; continue; }
        } else if (rows[r].line_cache) {
            lp = rows[r].line_cache;
        } else {
            out[i] = 0.0; continue;
        }
        get_field_graph(lp, w->col, field_buf, sizeof(field_buf));
        out[i] = parse_double(field_buf, NULL);
    }
    return NULL;
}

// ────────────────────────────────────────────────
// Вспомогательные функции
// ────────────────────────────────────────────────

/**
 * Bresenham's line algorithm — рисует линию точками в массиве dots
 */
void draw_bresenham(bool *dots, int w, int h, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int e2;

    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            dots[y0 * w + x0] = true;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/**
 * Нормализация значения по Y-оси
 */
static double norm_y(double val, double min_y, double max_y, GraphScale scale) {
    if (scale == SCALE_LOG) {
        if (val <= 0.0) return 0.0; // защита от log(0) и отрицательных
        return log10(val) / log10(max_y > 0 ? max_y : 1.0);
    } else {
        if (max_y == min_y) return 0.5; // защита от деления на 0
        return (val - min_y) / (max_y - min_y);
    }
}

int find_min_index(double *values, int count) {
    if (count <= 0) return -1;
    int min_idx = 0;
    double min_val = values[0];
    for (int i = 1; i < count; i++) {
        if (values[i] < min_val) {
            min_val = values[i];
            min_idx = i;
        }
    }
    return min_idx;
}

int find_max_index(double *values, int count) {
    if (count <= 0) return -1;
    int max_idx = 0;
    double max_val = values[0];
    for (int i = 1; i < count; i++) {
        if (values[i] > max_val) {
            max_val = values[i];
            max_idx = i;
        }
    }
    return max_idx;
}

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
double *extract_plot_values(
    int col,
    RowIndex *rows,
    FILE *f,
    int row_count,
    int *out_point_count,
    bool *out_aggregate,
    char *target_date_fmt_out
) {
    *out_point_count = 0;
    *out_aggregate = false;
    if (target_date_fmt_out) target_date_fmt_out[0] = '\0';

    if (col < 0 || col >= col_count) return NULL;

    int display_count = filter_active ? filtered_count :
                        (sort_col >= 0 ? sorted_count : row_count);
    int start_row = use_headers ? 1 : 0;

    if (display_count <= start_row) return NULL;
    display_count -= start_row;

    bool aggregate = false;
    char target_date_fmt[32] = "%Y-%m-%d";
    if (using_date_x && date_col >= 0 && col_formats[date_col].date_format[0]) {
        strncpy(target_date_fmt, col_formats[date_col].date_format, sizeof(target_date_fmt)-1);
        target_date_fmt[sizeof(target_date_fmt)-1] = '\0';
        if (strcmp(target_date_fmt, "YYYY-MM") == 0) {
            aggregate = true;
            strcpy(target_date_fmt, "%Y-%m");
        }
    }
    *out_aggregate = aggregate;
    if (target_date_fmt_out) {
        strncpy(target_date_fmt_out, target_date_fmt, 31);
        target_date_fmt_out[31] = '\0';
    }

    double *values = NULL;
    int point_count = 0;

    if (aggregate) {
        // Агрегация по месяцам — читаем строки заново каждый раз
        typedef struct { char month[8]; double sum; } AggPoint;
        AggPoint *agg = calloc(MAX_AGG, sizeof(AggPoint));
        if (!agg) return NULL;

        char prev[8] = "";
        double cur_sum = 0.0;
        int agg_idx = -1;

        char agg_line_buf[MAX_LINE_LEN];
        char agg_date_buf[64], agg_val_buf[64];
        for (int i = start_row; i < display_count + start_row; i++) {
            int r = get_real_row(i);
            if (r < 0 || r >= row_count) continue;

            const char *lp;
            if (g_mmap_base) {
                lp = csv_mmap_get_line(rows[r].offset, agg_line_buf, sizeof(agg_line_buf));
                if (!lp) continue;
            } else if (rows[r].line_cache) {
                lp = rows[r].line_cache;
            } else {
                fseek(f, rows[r].offset, SEEK_SET);
                if (!fgets(agg_line_buf, sizeof(agg_line_buf), f)) continue;
                agg_line_buf[strcspn(agg_line_buf, "\n")] = '\0';
                lp = agg_line_buf;
            }

            get_field_graph(lp, date_col, agg_date_buf, sizeof(agg_date_buf));
            get_field_graph(lp, col,      agg_val_buf,  sizeof(agg_val_buf));

            char ym[8] = "";
            if (agg_date_buf[0] && strlen(agg_date_buf) >= 7) {
                strncpy(ym, agg_date_buf, 7); ym[7] = '\0';
            }

            double val = parse_double(agg_val_buf, NULL);

            if (!ym[0]) continue;

            if (strcmp(ym, prev) != 0) {
                if (agg_idx >= 0) agg[agg_idx].sum = cur_sum;
                agg_idx++;
                if (agg_idx >= MAX_AGG) break;
                strcpy(agg[agg_idx].month, ym);
                cur_sum = 0.0;
                strcpy(prev, ym);
            }
            cur_sum += val;
        }

        if (agg_idx >= 0) {
            agg[agg_idx].sum = cur_sum;
            point_count = agg_idx + 1;
        }

        values = malloc(point_count * sizeof(double));
        if (values) {
            for (int i = 0; i < point_count; i++) values[i] = agg[i].sum;
        }
        free(agg);
    }
    else {
        values = malloc(display_count * sizeof(double));
        if (!values) return NULL;

        int nthreads = (g_mmap_base) ? csv_num_threads() : 1;
        int chunk = (display_count + nthreads - 1) / nthreads;
        GraphWorker *gw = calloc(nthreads, sizeof(GraphWorker));
        pthread_t   *gt = malloc(nthreads * sizeof(pthread_t));
        for (int t = 0; t < nthreads; t++) {
            gw[t].start_i   = start_row + t * chunk;
            gw[t].count     = chunk;
            if (gw[t].start_i + gw[t].count > start_row + display_count)
                gw[t].count = start_row + display_count - gw[t].start_i;
            if (gw[t].count < 0) gw[t].count = 0;
            gw[t].start_row = start_row;
            gw[t].col       = col;
            gw[t].row_count = row_count;
            gw[t].values    = values;
            pthread_create(&gt[t], NULL, graph_worker_thread, &gw[t]);
        }
        for (int t = 0; t < nthreads; t++) pthread_join(gt[t], NULL);
        free(gw); free(gt);
        point_count = display_count;
    }

    *out_point_count = point_count;
    return values;
}

// ────────────────────────────────────────────────
// Основная функция отрисовки графика
// ────────────────────────────────────────────────
void draw_graph(int col, int height, int width, RowIndex *rows, FILE *f, int row_count, int cursor_pos, int min_max_show)
{
    int plot_start_y = 4;
    int plot_start_x = ROW_DATA_OFFSET + 2; // отступ слева для Y-меток
    int plot_height = height - 8; // запас сверху/снизу
    int plot_width = width - plot_start_x - 4; // запас справа
    if (plot_height < 5 || plot_width < 10) return; // слишком маленький экран
    // ─── Очистка области графика ───────────────────────────────────────────────
    if (graph_overlay_mode != 2) {
        for (int y = plot_start_y; y < plot_start_y + plot_height + 2; y++) {
            for (int x = 1; x < width - 1; x++) {
                if (y >= 0 && y < height && x >= 0 && x < width) {
                    mvaddch(y, x, ' ');
                }
            }
        }
    }
    int display_count = filter_active ? filtered_count : (sort_col >= 0 ? sorted_count : row_count);
    int start_row = use_headers ? 1 : 0;
    if (display_count <= start_row) return;
    display_count -= start_row;
    // ─── Получаем числовые значения ────────────────────────────────────────────
    bool aggregate = false;
    char target_date_fmt[32];
    int point_count = 0;
    double *values = extract_plot_values(col, rows, f, row_count, &point_count, &aggregate, target_date_fmt);
    if (!values || point_count == 0) {
        free(values);
        return;
    }
    // ─── min / max / среднее / stddev / аномалии ───────────────────────────────
    double min_y = INFINITY, max_y = -INFINITY;
    int min_y_x_pos = 0;
    int max_y_x_pos = 0;
    for (int i = 0; i < point_count; i++) {
        double v = values[i];
        if (v < min_y) { min_y = v; min_y_x_pos = i;}
        if (v > max_y) { max_y = v; max_y_x_pos = i;}
    }
    if (min_y == max_y) {
        max_y += 1.0;
        min_y -= 1.0;
    }
    if (isinf(min_y) || isinf(max_y)) goto cleanup;
    // Override with caller-provided global scale in multi-series mode
    if (!isnan(graph_global_min) && !isnan(graph_global_max)) {
        min_y = graph_global_min;
        max_y = graph_global_max;
    }
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < point_count; i++) {
        sum += values[i];
        sum_sq += values[i] * values[i];
    }
    double mean = sum / point_count;
    double variance = (sum_sq / point_count) - (mean * mean);
    double stddev = (variance > 0) ? sqrt(variance) : 1.0;
    graph_anomaly_count = 0;
    if (graph_anomalies) free(graph_anomalies);
    graph_anomalies = malloc(point_count * sizeof(double));
    for (int i = 0; i < point_count; i++) {
        double z_score = fabs(values[i] - mean) / stddev;
        if (z_score > ANOMALY_THRESHOLD) {
            graph_anomalies[graph_anomaly_count++] = values[i];
        }
    }
    // ─── Прореживание для отрисовки (с включением экстремумов) ────────────────
    int max_points = plot_width;
    int step = (point_count > max_points) ? (point_count / max_points) + 1 : 1;
    int base_visible_points = (point_count + step - 1) / step;
    if (base_visible_points > max_points) base_visible_points = max_points;

    // Добавляем экстремумы, если они не попали в базовую выборку
    int visible_points = base_visible_points;
    bool min_included = ((min_y_x_pos % step) == 0);
    bool max_included = ((max_y_x_pos % step) == 0);

    if (!min_included) visible_points++;
    if (!max_included && min_y_x_pos != max_y_x_pos) visible_points++;

    graph_visible_points = visible_points;

    double *plot_values = malloc(visible_points * sizeof(double));
    if (!plot_values) goto cleanup;

    int plot_idx = 0;
    int min_added = 0, max_added = 0;

    for (int i = 0; i < base_visible_points; i++) {
        int orig_idx = i * step;
        if (orig_idx >= point_count) orig_idx = point_count - 1;

        // Вставляем min, если пора (перед текущей точкой)
        if (!min_included && min_y_x_pos < orig_idx && !min_added) {
            plot_values[plot_idx++] = values[min_y_x_pos];
            min_added = 1;
        }

        // Вставляем max, если пора
        if (!max_included && max_y_x_pos < orig_idx && !max_added) {
            plot_values[plot_idx++] = values[max_y_x_pos];
            max_added = 1;
        }

        plot_values[plot_idx++] = values[orig_idx];
    }

    // Добавляем min/max в конец, если не вставили
    if (!min_included && !min_added) plot_values[plot_idx++] = values[min_y_x_pos];
    if (!max_included && !max_added) plot_values[plot_idx++] = values[max_y_x_pos];

    // ─── Y-метки (4 штуки: top, 1/3, 2/3, bottom) ──────────────────────────────
    char buf[32];
    if (graph_overlay_mode != 2) {
        for (int yi = 0; yi < 4; yi++) {
            double frac = (double)yi / 3.0;
            int    lrow = plot_start_y + (int)round(frac * (plot_height - 1));
            double val;
            if (graph_scale == SCALE_LOG) {
                double lmax = log10(max_y > 0 ? max_y : 1);
                double lmin = log10(min_y > 0 ? min_y : 1);
                val = pow(10.0, lmax - frac * (lmax - lmin));
                snprintf(buf, sizeof(buf), "%.2e", val);
            } else {
                val = max_y - frac * (max_y - min_y);
                snprintf(buf, sizeof(buf), "%.3g", val);
            }
            mvprintw(lrow, 1, "%8s", buf);
        }
    }
    // ─── X-метки (5–7 штук) ─────────────────────────────────────────────────────
    if (graph_overlay_mode != 2) {
        int label_step = visible_points / 6;
        if (label_step < 1) label_step = 1;
        for (int i = 0; i < visible_points; i += label_step)
        {
            int orig_idx = i * step;
            if (orig_idx >= point_count) orig_idx = point_count - 1;
            char label[32] = "";
            if (using_date_x) {
                int row_idx = start_row + orig_idx;
                int real_row = get_real_row(row_idx);
                if (real_row >= 0 && real_row < row_count) {
                    char *date_raw = get_column_value(rows[real_row].line_cache,
                                                      column_names[date_col] ? column_names[date_col] : "",
                                                      use_headers);
                    if (date_raw) {
                        char *formatted = format_date(date_raw, target_date_fmt);
                        strncpy(label, formatted, sizeof(label) - 1);
                        free(formatted);
                        free(date_raw);
                    }
                }
            } else {
                snprintf(label, sizeof(label), "%d", orig_idx + 1);
            }
            int x_pos = plot_start_x + (i * plot_width / (visible_points - 1));
            int len = strlen(label);
            if (x_pos + len > width - 2) x_pos = width - 2 - len;
            mvprintw(plot_start_y + plot_height, x_pos, "%s", label);
        }
    }
    // ─── Сетка (рисуем до braille, чтобы данные перекрыли линии) ───────────────
    if (graph_overlay_mode != 2 && graph_grid) {
        attron(A_DIM);
        if (graph_grid & 1) {  // горизонтальные линии по Y-меткам (1/3 и 2/3 — крайние совпадают с осями)
            for (int yi = 1; yi <= 2; yi++) {
                int gy = plot_start_y + (int)round((double)yi / 3.0 * (plot_height - 1));
                mvhline(gy, plot_start_x, ACS_HLINE, plot_width);
            }
        }
        if (graph_grid & 2) {  // вертикальные линии по X-меткам
            int xlsv = visible_points / 6;
            if (xlsv < 1) xlsv = 1;
            for (int i = xlsv; i < visible_points - 1; i += xlsv) {
                int gx = plot_start_x + (i * plot_width / (visible_points > 1 ? visible_points - 1 : 1));
                mvvline(plot_start_y, gx, ACS_VLINE, plot_height);
            }
        }
        attroff(A_DIM);
    }
    // ─── Рисование графика ──────────────────────────────────────────────────────
    int pixel_h = plot_height * 4;
    int pixel_w = plot_width * 2;
    bool *dots = calloc(pixel_h * pixel_w, sizeof(bool));
    if (!dots) goto cleanup_free_plot;
    // Оси
    draw_bresenham(dots, pixel_w, pixel_h, 0, pixel_h - 1, pixel_w - 1, pixel_h - 1);
    draw_bresenham(dots, pixel_w, pixel_h, 0, pixel_h - 1, 0, 0);
    if (has_colors()) {
        attron(COLOR_PAIR(current_graph_color_pair));
    }
    if (graph_type == GRAPH_LINE) {
        for (int i = 0; i < visible_points - 1; i++) {
            double y0 = norm_y(plot_values[i], min_y, max_y, graph_scale);
            double y1 = norm_y(plot_values[i + 1], min_y, max_y, graph_scale);
            int py0 = (int)round((1.0 - y0) * (pixel_h - 1));
            int py1 = (int)round((1.0 - y1) * (pixel_h - 1));
            int px0 = (i * (pixel_w - 1)) / (visible_points - 1);
            int px1 = ((i + 1) * (pixel_w - 1)) / (visible_points - 1);
            draw_bresenham(dots, pixel_w, pixel_h, px0, py0, px1, py1);
        }
    } else if (graph_type == GRAPH_BAR) {
        int bar_width = (pixel_w / visible_points) / 2;
        if (bar_width < 1) bar_width = 1;
        for (int i = 0; i < visible_points; i++) {
            double norm = norm_y(plot_values[i], min_y, max_y, graph_scale);
            int height_px = (int)round(norm * (pixel_h - 1));
            int px_center = (i * (pixel_w - 1)) / (visible_points - 1);
            for (int h = 0; h <= height_px; h++) {
                for (int bw = -bar_width; bw <= bar_width; bw++) {
                    int px = px_center + bw;
                    if (px >= 0 && px < pixel_w) {
                        dots[(pixel_h - 1 - h) * pixel_w + px] = true;
                    }
                }
            }
        }
    } else if (graph_type == GRAPH_DOT) {
        for (int i = 0; i < visible_points; i++) {
            double norm = norm_y(plot_values[i], min_y, max_y, graph_scale);
            int py = (int)round((1.0 - norm) * (pixel_h - 1));
            int px = (i * (pixel_w - 1)) / (visible_points - 1);
            if (px >= 0 && px < pixel_w && py >= 0 && py < pixel_h) {
                dots[py * pixel_w + px] = true;
                if (px > 0) dots[py * pixel_w + (px-1)] = true;
                if (px < pixel_w-1) dots[py * pixel_w + (px+1)] = true;
                if (py > 0) dots[(py-1) * pixel_w + px] = true;
                if (py < pixel_h-1) dots[(py+1) * pixel_w + px] = true;
            }
        }
    }
    // Braille-рендеринг
    for (int cy = 0; cy < plot_height; cy++) {
        for (int cx = 0; cx < plot_width; cx++) {
            int code = 0;
            for (int by = 0; by < 4; by++) {
                for (int bx = 0; bx < 2; bx++) {
                    int py = cy * 4 + by;
                    int px = cx * 2 + bx;
                    if (px < pixel_w && py < pixel_h && dots[py * pixel_w + px]) {
                        int bit = (bx == 0) ? by : by + 3;
                        if (by == 3) bit = (bx == 0) ? 6 : 7;
                        code |= (1 << bit);
                    }
                }
            }
            // Skip empty cells — canvas already cleared with spaces (also preserves grid and earlier series)
            if (code == 0) continue;
            char braille_utf8[5] = {0};
            wcrtomb(braille_utf8, 0x2800 + code, NULL);
            mvaddstr(plot_start_y + cy, plot_start_x + cx, braille_utf8);
        }
    }
    if (has_colors()) {
        attroff(COLOR_PAIR(current_graph_color_pair));
    }
    // ─── Подсветка аномалий ─────────────────────────────────────────────────────
    if (graph_overlay_mode == 0 && show_anomalies && graph_anomaly_count > 0) {
        if (has_colors()) {
            attron(COLOR_PAIR(11) | A_BOLD);
        }
        for (int i = 0; i < visible_points; i++) {
            int orig_idx = i * step;
            if (orig_idx >= point_count) orig_idx = point_count - 1;
            double val = values[orig_idx]; // берём из полного массива
            double z_score = fabs(val - mean) / stddev;
            if (z_score > ANOMALY_THRESHOLD) {
                double norm = norm_y(plot_values[i], min_y, max_y, graph_scale);
                int cell_y = (int)round((1.0 - norm) * (plot_height - 1));
                int cell_x = plot_start_x + (i * plot_width) / (visible_points - 1);
                if (cell_y >= 0 && cell_y < plot_height && cell_x >= 0 && cell_x < width) {
                    mvaddch(plot_start_y + cell_y, cell_x, '*');
                }
            }
        }
        if (has_colors()) {
            attroff(COLOR_PAIR(11) | A_BOLD);
        }
    }

    // ─── Курсор и значение под ним ──────────────────────────────────────────────
    if ((graph_overlay_mode == 0 || graph_draw_cursor_overlay) && show_graph_cursor)
    {
        int full_idx = cursor_pos * step;
        if (min_max_show != 0) {
            full_idx = (min_max_show == 1) ? min_y_x_pos : max_y_x_pos;
        }
        int visible_cursor_pos = full_idx / step;
        if (visible_cursor_pos >= visible_points) visible_cursor_pos = visible_points - 1;
        if (visible_cursor_pos < 0) visible_cursor_pos = 0;

        double real_y = values[full_idx];
        double norm = norm_y(real_y, min_y, max_y, graph_scale);
        int cell_y = (int)round((1.0 - norm) * (plot_height - 1));
        int cell_x = plot_start_x + (visible_cursor_pos * plot_width) / (visible_points - 1);

        // Строим x_str (нужен и для одиночного, и для передачи наружу)
        char x_str[64] = "";
        if (using_date_x) {
            int row_idx = start_row + full_idx;
            int real_row = get_real_row(row_idx);
            if (real_row >= 0 && real_row < row_count) {
                char *date_raw = get_column_value(rows[real_row].line_cache,
                                                  column_names[date_col] ? column_names[date_col] : "",
                                                  use_headers);
                if (date_raw) {
                    char *formatted = format_date(date_raw, target_date_fmt);
                    strncpy(x_str, formatted, sizeof(x_str) - 1);
                    free(formatted);
                    free(date_raw);
                }
            }
        }
        if (x_str[0] == '\0') {
            snprintf(x_str, sizeof(x_str), "%d", full_idx + 1);
        }

        // Сохраняем для caller-а (multi-series tooltip)
        graph_last_cursor_y = real_y;
        strncpy(graph_last_cursor_x, x_str, sizeof(graph_last_cursor_x) - 1);
        graph_last_cursor_x[sizeof(graph_last_cursor_x) - 1] = '\0';

        if (cell_y >= 0 && cell_y < plot_height && cell_x >= 0 && cell_x < width)
        {
            // Маркер @ в цвете серии (в multi-series режиме) или в красном (single)
            int marker_pair = (graph_overlay_mode == 0) ? 11 : current_graph_color_pair;
            if (has_colors()) attron(COLOR_PAIR(marker_pair) | A_BOLD);
            mvaddch(plot_start_y + cell_y, cell_x, '@');
            if (has_colors()) attroff(COLOR_PAIR(marker_pair) | A_BOLD);

            // Текстовый тултип — только в single-series режиме
            if (graph_overlay_mode == 0) {
                char val_str[64];
                snprintf(val_str, sizeof(val_str), "%.4f", real_y);
                char info[128];
                snprintf(info, sizeof(info), "X: %s | Y: %s", x_str, val_str);
                int text_y = plot_start_y + cell_y - 1;
                if (text_y < plot_start_y) text_y = plot_start_y + cell_y + 1;
                attron(COLOR_PAIR(1));
                mvprintw(text_y, cell_x - 10, "%s", info);
                attroff(COLOR_PAIR(1));
            }
        }
    }

    free(dots);
cleanup_free_plot:
    free(plot_values);
cleanup:
    free(values);
} 