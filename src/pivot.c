/**
 * pivot.c
 *
 * Реализация сводных таблиц (pivot tables) для csvview
 */

#define _XOPEN_SOURCE 700  /* strptime on Linux */

#include "pivot.h"
#include "utils.h"          // get_column_value, col_name_to_num, col_to_num
#include "csvview_defs.h"   // globals, RowIndex, ColType
#include "ui_draw.h"
#include "sorting.h"
#include "filtering.h"
#include "graph.h"          // draw_bresenham
#include "csv_mmap.h"

#include <ncurses.h>        // отрисовка
#include <stdlib.h>         // malloc, free, qsort, calloc
#include <string.h>         // strcpy, strcmp, strlen
#include <stdio.h>          // fopen, fprintf, sscanf
#include <math.h>           // INFINITY
#include <time.h>           // struct tm, strptime
#include <wchar.h>          // wcrtomb для Braille
#include <pthread.h>


/* ── Inline single-field extractor (no malloc) ── */
static void get_field_csv(const char *line, int idx, char *buf, int buf_size)
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

/* ── Pass-2 parallel worker ── */
typedef struct {
    int start_d, count;
    int start_row;
    int row_group_idx, col_group_idx, value_idx;
    int unique_rows, unique_cols;
    char **row_keys, **col_keys;
    int row_is_date, col_is_date;
    const char *date_grouping;
    ColType row_type, col_type, value_type;
    /* outputs — allocated inside thread */
    Agg *matrix;       /* unique_rows × unique_cols flat */
    Agg *row_totals;   /* unique_rows */
    Agg *col_totals;   /* unique_cols */
    Agg  grand;
} PivotPass2Worker;

static void *pivot_pass2_thread(void *arg)
{
    PivotPass2Worker *w = arg;
    int nr = w->unique_rows, nc = w->unique_cols;

    w->matrix    = calloc((size_t)nr * nc, sizeof(Agg));
    w->row_totals = calloc(nr, sizeof(Agg));
    w->col_totals = calloc(nc, sizeof(Agg));
    w->grand = (Agg){0, 0, INFINITY, -INFINITY, 0, NULL, NULL};
    for (int i = 0; i < nr * nc; i++) { w->matrix[i].min = INFINITY; w->matrix[i].max = -INFINITY; }
    for (int i = 0; i < nr; i++) { w->row_totals[i].min = INFINITY; w->row_totals[i].max = -INFINITY; }
    for (int i = 0; i < nc; i++) { w->col_totals[i].min = INFINITY; w->col_totals[i].max = -INFINITY; }

    char rval_buf[512], cval_buf[512], vval_buf[512];

    for (int d = w->start_d; d < w->start_d + w->count; d++) {
        int real_row = get_real_row(d);
        if (real_row < w->start_row) continue;
        const char *line = rows[real_row].line_cache ? rows[real_row].line_cache : "";
        if (!*line) continue;

        if (w->row_group_idx >= 0) get_field_csv(line, w->row_group_idx, rval_buf, sizeof(rval_buf));
        else strcpy(rval_buf, "Total");
        if (w->col_group_idx >= 0) get_field_csv(line, w->col_group_idx, cval_buf, sizeof(cval_buf));
        else strcpy(cval_buf, "Total");
        get_field_csv(line, w->value_idx, vval_buf, sizeof(vval_buf));

        char *rkey = get_group_key(rval_buf, w->row_type, w->date_grouping, w->row_group_idx);
        char *ckey = get_group_key(cval_buf, w->col_type, w->date_grouping, w->col_group_idx);

        int ridx = -1;
        if (rkey && *rkey) {
            char *p = rkey;
            char **r = w->row_is_date
                ? (char**)bsearch(&p, w->row_keys, nr, sizeof(char*), compare_date_keys)
                : (char**)bsearch(&p, w->row_keys, nr, sizeof(char*), compare_str);
            ridx = r ? (int)(r - w->row_keys) : -1;
        }
        int cidx = -1;
        if (ckey && *ckey) {
            char *p = ckey;
            char **c = w->col_is_date
                ? (char**)bsearch(&p, w->col_keys, nc, sizeof(char*), compare_date_keys)
                : (char**)bsearch(&p, w->col_keys, nc, sizeof(char*), compare_str);
            cidx = c ? (int)(c - w->col_keys) : -1;
        }
        free(rkey); free(ckey);

        if (ridx >= 0 && cidx >= 0) {
            update_agg(&w->matrix[ridx * nc + cidx], vval_buf, w->value_type);
            update_agg(&w->row_totals[ridx], vval_buf, w->value_type);
            update_agg(&w->col_totals[cidx], vval_buf, w->value_type);
            update_agg(&w->grand, vval_buf, w->value_type);
        }
    }
    return NULL;
}

unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

HashMap *hash_map_create(int size) {
    HashMap *map = malloc(sizeof(HashMap));
    map->size = size;
    map->buckets = calloc(size, sizeof(Entry*));
    return map;
}

int compare_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void hash_map_put(HashMap *map, const char *key, void *value) {
    unsigned long h = hash_string(key) % map->size;
    Entry *e = map->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            e->value = value;
            return;
        }
        e = e->next;
    }
    e = malloc(sizeof(Entry));
    e->key = strdup(key);
    e->value = value;
    e->next = map->buckets[h];
    map->buckets[h] = e;
}

void *hash_map_get(HashMap *map, const char *key) {
    unsigned long h = hash_string(key) % map->size;
    Entry *e = map->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) return e->value;
        e = e->next;
    }
    return NULL;
}

char **hash_map_keys(HashMap *map, int *count) {
    *count = 0;
    for (int i = 0; i < map->size; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            (*count)++;
            e = e->next;
        }
    }
    char **keys = malloc(*count * sizeof(char*));
    int idx = 0;
    for (int i = 0; i < map->size; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            keys[idx++] = strdup(e->key);
            e = e->next;
        }
    }
    return keys;
}

void hash_map_destroy(HashMap *map) {
    for (int i = 0; i < map->size; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            Entry *next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(map->buckets);
    free(map);
}

// Pivot functions
int compare_date_keys(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;

    int ya = 0, ma = 0, qa = 0;
    int yb = 0, mb = 0, qb = 0;

    // Год-квартал
    if (strstr(sa, "-Q") && strstr(sb, "-Q")) {
        sscanf(sa, "%d-Q%d", &ya, &qa);
        sscanf(sb, "%d-Q%d", &yb, &qb);
        if (ya != yb) return ya < yb ? -1 : 1;
        return qa < qb ? -1 : (qa > qb ? 1 : 0);
    }
    // Год-месяц
    if (strchr(sa, '-') && strchr(sb, '-')) {
        sscanf(sa, "%d-%d", &ya, &ma);
        sscanf(sb, "%d-%d", &yb, &mb);
        if (ya != yb) return ya < yb ? -1 : 1;
        return ma < mb ? -1 : (ma > mb ? 1 : 0);
    }
    // Просто год или век
    ya = atoi(sa);
    yb = atoi(sb);
    return ya < yb ? -1 : (ya > yb ? 1 : 0);
}

// ────────────────────────────────────────────────
// Хелперы для сортировки order-массивов по значению агрегации
// ────────────────────────────────────────────────

static Agg          *g_sort_agg_arr = NULL;
static const char   *g_sort_agg_str = NULL;
static ColType       g_sort_vtype   = COL_NUM;
static int           g_sort_dir     = 1;  // 1=ASC, -1=DESC

static double agg_sort_value(const Agg *a, const char *agg_str) {
    if (strcmp(agg_str, "COUNT") == 0 || strcmp(agg_str, "UNIQUE COUNT") == 0)
        return (double)a->count;
    if (strcmp(agg_str, "SUM") == 0)  return a->sum;
    if (strcmp(agg_str, "AVG") == 0)  return a->count > 0 ? a->sum / (double)a->count : 0.0;
    if (strcmp(agg_str, "MIN") == 0)  return a->min;
    if (strcmp(agg_str, "MAX") == 0)  return a->max;
    return 0.0;
}

static int compare_order_by_agg(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    double va = agg_sort_value(&g_sort_agg_arr[ia], g_sort_agg_str);
    double vb = agg_sort_value(&g_sort_agg_arr[ib], g_sort_agg_str);
    if (va < vb) return -g_sort_dir;
    if (va > vb) return  g_sort_dir;
    return 0;
}

// ────────────────────────────────────────────────
// Нормализация Y для pivot-графика
// ────────────────────────────────────────────────
static double pivot_norm_y(double val, double min_y, double max_y, GraphScale scale) {
    if (scale == SCALE_LOG) {
        if (val <= 0.0) return 0.0;
        return log10(val) / log10(max_y > 0.0 ? max_y : 1.0);
    }
    if (max_y == min_y) return 0.5;
    return (val - min_y) / (max_y - min_y);
}

static void format_y_label(char *buf, int bufsz, double val) {
    double av = fabs(val);
    if (av >= 1e9)       snprintf(buf, bufsz, "%.2fG", val / 1e9);
    else if (av >= 1e6)  snprintf(buf, bufsz, "%.2fM", val / 1e6);
    else if (av >= 1e3)  snprintf(buf, bufsz, "%.1fK", val / 1e3);
    else if (av >= 100)  snprintf(buf, bufsz, "%.0f",  val);
    else if (av >= 10)   snprintf(buf, bufsz, "%.1f",  val);
    else                 snprintf(buf, bufsz, "%.2f",  val);
}

// ────────────────────────────────────────────────
// График для сводной таблицы (Braille, multi-series)
// ────────────────────────────────────────────────
static void draw_pivot_graph(
    int gx, int height, int gwidth,
    Agg **matrix, Agg *row_totals, Agg *col_totals, const Agg *grand,
    char **row_keys, int unique_rows,
    char **col_keys, int unique_cols,
    int *row_order, int *col_order,
    bool *series_pinned,
    int cur_row_p, int cur_col_p,
    int graph_axis,
    GraphType gtype, GraphScale gscale,
    const char *aggregation,
    int total_rows, int total_cols
) {
    // ── Геометрия области ──────────────────────────────────────────────────────
    int plot_start_y = 5;
    int plot_start_x = gx + 10;
    int legend_y     = height - 3;
    int xlabel_y     = height - 4;
    int plot_height  = xlabel_y - plot_start_y - 1;
    int plot_width   = gx + gwidth - plot_start_x - 2;

    if (plot_height < 4 || plot_width < 8) return;

    // ── Активные серии (cursor first, then pinned, max 6) ─────────────────────
    int cursor_series  = (graph_axis == 0) ? cur_col_p : cur_row_p;
    int max_series_idx = (graph_axis == 0) ? total_cols : total_rows;

    int active_series[6];
    int n_active = 0;

    if (cursor_series >= 0 && cursor_series < max_series_idx)
        active_series[n_active++] = cursor_series;

    for (int i = 0; i < max_series_idx && n_active < 6; i++) {
        if (i == cursor_series) continue;
        if (series_pinned[i]) active_series[n_active++] = i;
    }
    if (n_active == 0) return;

    // ── Число точек по оси X ──────────────────────────────────────────────────
    int n_points = (graph_axis == 0) ? total_rows : total_cols;
    if (n_points == 0) return;

    // ── Данные для всех серий + глобальный min/max ─────────────────────────────
    double *all_values[6] = {NULL};
    double global_min = INFINITY, global_max = -INFINITY;

    for (int s = 0; s < n_active; s++) {
        int sidx = active_series[s];
        double *vals = malloc(n_points * sizeof(double));
        if (!vals) continue;

        for (int i = 0; i < n_points; i++) {
            const Agg *agg;
            if (graph_axis == 0) {
                // X = строки, серия = столбец sidx
                int arid = (i    < unique_rows) ? row_order[i]    : -1;
                int acid = (sidx < unique_cols) ? col_order[sidx] : -1;
                if      (i < unique_rows && sidx < unique_cols)  agg = &matrix[arid][acid];
                else if (i < unique_rows && sidx == unique_cols) agg = &row_totals[arid];
                else if (i == unique_rows && sidx < unique_cols) agg = &col_totals[acid];
                else                                              agg = grand;
            } else {
                // X = столбцы, серия = строка sidx
                int arid = (sidx < unique_rows) ? row_order[sidx] : -1;
                int acid = (i    < unique_cols) ? col_order[i]    : -1;
                if      (sidx < unique_rows && i < unique_cols)  agg = &matrix[arid][acid];
                else if (sidx < unique_rows && i == unique_cols) agg = &row_totals[arid];
                else if (sidx == unique_rows && i < unique_cols) agg = &col_totals[acid];
                else                                              agg = grand;
            }
            vals[i] = agg_sort_value(agg, aggregation);
            if (vals[i] < global_min) global_min = vals[i];
            if (vals[i] > global_max) global_max = vals[i];
        }
        all_values[s] = vals;
    }

    if (isinf(global_min) || isinf(global_max)) goto pivot_graph_cleanup;
    if (global_min == global_max) { global_max += 1.0; }

    // ── Y-метки (max / mid / min) ─────────────────────────────────────────────
    {
        char ybuf[16];
        attron(COLOR_PAIR(6));
        format_y_label(ybuf, sizeof(ybuf), global_max);
        mvprintw(plot_start_y, gx + 1, "%8s", ybuf);
        double mid = (global_max + global_min) / 2.0;
        format_y_label(ybuf, sizeof(ybuf), mid);
        mvprintw(plot_start_y + plot_height / 2, gx + 1, "%8s", ybuf);
        format_y_label(ybuf, sizeof(ybuf), global_min);
        mvprintw(plot_start_y + plot_height - 1, gx + 1, "%8s", ybuf);
        attroff(COLOR_PAIR(6));
    }

    // ── X-метки (до 6) ────────────────────────────────────────────────────────
    {
        int n_xlabels = (n_points < 6) ? n_points : 6;
        attron(COLOR_PAIR(6));
        for (int i = 0; i < n_xlabels; i++) {
            int pt = (n_xlabels > 1) ? i * (n_points - 1) / (n_xlabels - 1) : 0;
            const char *label = "?";
            if (graph_axis == 0)
                label = (pt < unique_rows) ? row_keys[row_order[pt]] : "Total";
            else
                label = (pt < unique_cols) ? col_keys[col_order[pt]] : "Total";

            char lbuf[12];
            snprintf(lbuf, sizeof(lbuf), "%.10s", label);
            int xpos = plot_start_x + (n_points > 1 ? pt * (plot_width - 1) / (n_points - 1) : 0);
            int llen = (int)strlen(lbuf);
            if (xpos + llen > gx + gwidth - 1) xpos = gx + gwidth - 1 - llen;
            mvprintw(xlabel_y, xpos, "%s", lbuf);
        }
        attroff(COLOR_PAIR(6));
    }

    // ── Рендеринг серий через Braille ─────────────────────────────────────────
    {
        int pixel_h = plot_height * 4;
        int pixel_w = plot_width  * 2;

        // Grouped bars: размеры слотов
        int group_w  = (n_points > 0) ? pixel_w / n_points : pixel_w;
        if (group_w < 1) group_w = 1;
        int bar_slot = (n_active > 0) ? group_w / n_active : group_w;
        if (bar_slot < 1) bar_slot = 1;
        int bar_hw = bar_slot / 2;
        if (bar_hw < 1) bar_hw = 1;

        for (int s = 0; s < n_active; s++) {
            if (!all_values[s]) continue;
            double *vals = all_values[s];

            bool *dots = calloc(pixel_h * pixel_w, sizeof(bool));
            if (!dots) continue;

            // Оси
            draw_bresenham(dots, pixel_w, pixel_h, 0, pixel_h-1, pixel_w-1, pixel_h-1);
            draw_bresenham(dots, pixel_w, pixel_h, 0, pixel_h-1, 0, 0);

            if (gtype == GRAPH_BAR) {
                for (int i = 0; i < n_points; i++) {
                    double norm   = pivot_norm_y(vals[i], global_min, global_max, gscale);
                    int bar_h_px  = (int)round(norm * (pixel_h - 1));
                    int bar_center = i * group_w + s * bar_slot + bar_slot / 2;
                    for (int h = 0; h <= bar_h_px; h++) {
                        for (int bw = -(bar_hw - 1); bw <= (bar_hw - 1); bw++) {
                            int px = bar_center + bw;
                            if (px >= 0 && px < pixel_w)
                                dots[(pixel_h - 1 - h) * pixel_w + px] = true;
                        }
                    }
                }
            } else if (gtype == GRAPH_LINE) {
                for (int i = 0; i < n_points - 1; i++) {
                    double y0n = pivot_norm_y(vals[i],   global_min, global_max, gscale);
                    double y1n = pivot_norm_y(vals[i+1], global_min, global_max, gscale);
                    int py0 = (int)round((1.0 - y0n) * (pixel_h - 1));
                    int py1 = (int)round((1.0 - y1n) * (pixel_h - 1));
                    int px0 = (n_points > 1) ? i       * (pixel_w - 1) / (n_points - 1) : 0;
                    int px1 = (n_points > 1) ? (i + 1) * (pixel_w - 1) / (n_points - 1) : 0;
                    draw_bresenham(dots, pixel_w, pixel_h, px0, py0, px1, py1);
                }
            } else { // GRAPH_DOT
                for (int i = 0; i < n_points; i++) {
                    double norm = pivot_norm_y(vals[i], global_min, global_max, gscale);
                    int py = (int)round((1.0 - norm) * (pixel_h - 1));
                    int px = (n_points > 1) ? i * (pixel_w - 1) / (n_points - 1) : 0;
                    if (px >= 0 && px < pixel_w && py >= 0 && py < pixel_h) {
                        dots[py * pixel_w + px] = true;
                        if (px > 0)          dots[py * pixel_w + (px-1)] = true;
                        if (px < pixel_w-1)  dots[py * pixel_w + (px+1)] = true;
                        if (py > 0)          dots[(py-1) * pixel_w + px] = true;
                        if (py < pixel_h-1)  dots[(py+1) * pixel_w + px] = true;
                    }
                }
            }

            // Braille-рендеринг с цветом серии
            int color = GRAPH_COLOR_BASE + s;
            int is_cursor_s = (active_series[s] == cursor_series);
            attr_t attrs = (attr_t)COLOR_PAIR(color) | (is_cursor_s ? (attr_t)A_BOLD : 0);
            attron(attrs);

            for (int cy = 0; cy < plot_height; cy++) {
                for (int cx = 0; cx < plot_width; cx++) {
                    int code = 0;
                    for (int by = 0; by < 4; by++) {
                        for (int bx = 0; bx < 2; bx++) {
                            int py2 = cy * 4 + by;
                            int px2 = cx * 2 + bx;
                            if (px2 < pixel_w && py2 < pixel_h && dots[py2 * pixel_w + px2]) {
                                int bit = (bx == 0) ? by : by + 3;
                                if (by == 3) bit = (bx == 0) ? 6 : 7;
                                code |= (1 << bit);
                            }
                        }
                    }
                    if (code != 0) {
                        char braille_utf8[5] = {0};
                        wcrtomb(braille_utf8, 0x2800 + code, NULL);
                        mvaddstr(plot_start_y + cy, plot_start_x + cx, braille_utf8);
                    }
                }
            }
            attroff(attrs);
            free(dots);
        }
    }

    // ── Легенда (предпоследняя строка внутри рамки) ────────────────────────────
    {
        int lx = gx + 2;
        int max_name = (n_active > 0) ? (gwidth - 4) / n_active - 4 : 12;
        if (max_name < 3) max_name = 3;

        for (int s = 0; s < n_active && lx < gx + gwidth - 2; s++) {
            int sidx = active_series[s];
            const char *name;
            if (graph_axis == 0)
                name = (sidx < unique_cols) ? col_keys[col_order[sidx]] : "Total";
            else
                name = (sidx < unique_rows) ? row_keys[row_order[sidx]] : "Total";

            int is_cursor_s = (sidx == cursor_series);
            int is_pinned   = series_pinned[sidx];
            int color       = GRAPH_COLOR_BASE + s;

            attron(COLOR_PAIR(color) | A_BOLD);
            // ■ курсорная серия, ▪ закреплённая, ─ остальные (не бывает, но на всякий)
            const char *marker = is_cursor_s  ? "\xe2\x96\xa0" :
                                 is_pinned     ? "\xe2\x96\xaa" : "\xe2\x96\xa1";
            mvaddstr(legend_y, lx, marker);
            attroff(COLOR_PAIR(color) | A_BOLD);
            lx += 2;

            attron(COLOR_PAIR(6));
            char nbuf[32];
            snprintf(nbuf, sizeof(nbuf), "%.*s", max_name, name);
            mvprintw(legend_y, lx, "%s", nbuf);
            attroff(COLOR_PAIR(6));
            lx += (int)strlen(nbuf) + 2;
        }
    }

pivot_graph_cleanup:
    for (int s = 0; s < n_active; s++) free(all_values[s]);
}

int load_pivot_settings(const char *csv_filename, PivotSettings *settings) {
    char pivot_path[1024];
    snprintf(pivot_path, sizeof(pivot_path), "%s.pivot", csv_filename);
    FILE *fp = fopen(pivot_path, "r");
    if (!fp) return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], val[192];
        if (sscanf(line, "%63[^:]: %191[^\n]", key, val) != 2) continue;
        trim(val);
        if (strcmp(key, "row_group") == 0) {
            settings->row_group_col = strcmp(val, "None") == 0 ? NULL : strdup(val);
        } else if (strcmp(key, "col_group") == 0) {
            settings->col_group_col = strcmp(val, "None") == 0 ? NULL : strdup(val);
        } else if (strcmp(key, "value_col") == 0) {
            settings->value_col = strdup(val);
        } else if (strcmp(key, "aggregation") == 0) {
            settings->aggregation = strdup(val);
        } else if (strcmp(key, "date_grouping") == 0) {
            settings->date_grouping = strdup(val);
        } else if (strcmp(key, "show_row_totals") == 0) {
            settings->show_row_totals = strcmp(val, "Yes") == 0 ? 1 : 0;
        } else if (strcmp(key, "show_col_totals") == 0) {
            settings->show_col_totals = strcmp(val, "Yes") == 0 ? 1 : 0;
        } else if (strcmp(key, "show_grand_total") == 0) {
            settings->show_grand_total = strcmp(val, "Yes") == 0 ? 1 : 0;
        } else if (strcmp(key, "row_sort") == 0) {
            settings->row_sort = strdup(val);
        } else if (strcmp(key, "col_sort") == 0) {
            settings->col_sort = strdup(val);
        }
    }
    fclose(fp);

    // Validate columns
    int row_group_idx = settings->row_group_col ? (use_headers ? col_name_to_num(settings->row_group_col) : col_to_num(settings->row_group_col)) : -1;
    int col_group_idx = settings->col_group_col ? (use_headers ? col_name_to_num(settings->col_group_col) : col_to_num(settings->col_group_col)) : -1;
    int value_idx = settings->value_col ? (use_headers ? col_name_to_num(settings->value_col) : col_to_num(settings->value_col)) : -1;

    if (row_group_idx < -1 || row_group_idx >= col_count || col_group_idx < -1 || col_group_idx >= col_count || value_idx < 0 || value_idx >= col_count) {
        // Invalid, free
        free(settings->row_group_col);
        free(settings->col_group_col);
        free(settings->value_col);
        free(settings->aggregation);
        free(settings->date_grouping);
        free(settings->row_sort);
        free(settings->col_sort);
        memset(settings, 0, sizeof(PivotSettings));
        return 0;
    }
    // Дефолты для полей, которых нет в старых .pivot файлах
    if (!settings->row_sort) settings->row_sort = strdup("KEY_ASC");
    if (!settings->col_sort) settings->col_sort = strdup("KEY_ASC");
    return 1;
}

void save_pivot_settings(const char *csv_filename, const PivotSettings *settings) {
    char pivot_path[1024];
    snprintf(pivot_path, sizeof(pivot_path), "%s.pivot", csv_filename);
    FILE *fp = fopen(pivot_path, "w");
    if (!fp) return;

    fprintf(fp, "row_group: %s\n", settings->row_group_col ? settings->row_group_col : "None");
    fprintf(fp, "col_group: %s\n", settings->col_group_col ? settings->col_group_col : "None");
    fprintf(fp, "value_col: %s\n", settings->value_col ? settings->value_col : "None");
    fprintf(fp, "aggregation: %s\n", settings->aggregation ? settings->aggregation : "SUM");
    fprintf(fp, "date_grouping: %s\n", settings->date_grouping ? settings->date_grouping : "Auto");
    fprintf(fp, "show_row_totals: %s\n", settings->show_row_totals ? "Yes" : "No");
    fprintf(fp, "show_col_totals: %s\n", settings->show_col_totals ? "Yes" : "No");
    fprintf(fp, "show_grand_total: %s\n", settings->show_grand_total ? "Yes" : "No");
    fprintf(fp, "row_sort: %s\n", settings->row_sort ? settings->row_sort : "KEY_ASC");
    fprintf(fp, "col_sort: %s\n", settings->col_sort ? settings->col_sort : "KEY_ASC");

    fclose(fp);
}

char *get_group_key(const char *val, ColType type, const char *grouping, int col_idx) {
    if (!val || !*val) return strdup("");
    if (type != COL_DATE || !grouping || strcmp(grouping, "Auto") == 0) {
        return strdup(val);
    }

    struct tm tm = {0};
    const char *fmt = col_formats[col_idx].date_format;

    if (!fmt || !*fmt) {
        fmt = "%Y-%m-%d";  // fallback
    }

    // Парсим дату по формату
    if (strptime(val, fmt, &tm) == NULL) {
        // Если не удалось — пробуем fallback
        if (strptime(val, "%Y-%m-%d", &tm) == NULL) {
            return strdup(val);  // ошибка парсинга — возвращаем как есть
        }
    }

    // Теперь применяем гранулярность из grouping
    char *key = malloc(32);
    int year = tm.tm_year + 1900;
    int month = tm.tm_mon + 1;

    if (strcmp(grouping, "Month") == 0) {
        snprintf(key, 32, "%04d-%02d", year, month);
    } else if (strcmp(grouping, "Quarter") == 0) {
        int q = (month - 1) / 3 + 1;
        snprintf(key, 32, "%04d-Q%d", year, q);
    } else if (strcmp(grouping, "Year") == 0) {
        snprintf(key, 32, "%04d", year);
    } else if (strcmp(grouping, "Century") == 0) {
        int cent = year / 100;
        snprintf(key, 32, "%02dxx", cent);
    } else {
        free(key);
        return strdup(val);
    }

    return key;
}

void update_agg(Agg *agg, const char *val, ColType value_type) {
    agg->count++;

    if (!val || !*val) {
        return;
    }

    if (value_type == COL_NUM) {
        // Очень агрессивная очистка: оставляем только цифры, точку, минус и запятую
        char clean[128] = {0};
        int j = 0;
        int has_dot_or_comma = 0;

        for (int i = 0; val[i] && j < (int)sizeof(clean)-1; i++) {
            char c = val[i];
            if (isdigit(c) || c == '-' || c == '+' || c == '.') {
                clean[j++] = c;
            } else if (c == ',') {
                // запятая → точка, но только одна
                if (!has_dot_or_comma) {
                    clean[j++] = '.';
                    has_dot_or_comma = 1;
                }
            }
            // всё остальное ($, €, пробелы, буквы) игнорируем
        }
        clean[j] = '\0';

        // Если строка пустая после очистки — выходим
        if (!*clean) return;

        char *endptr;
        double num = parse_double(clean, &endptr);

        // Если удалось распарсить почти всю строку
        if (endptr != clean && (*endptr == '\0' || isspace(*endptr))) {
            agg->sum += num;
            if (agg->count == 1 || num < agg->min) agg->min = num;
            if (agg->count == 1 || num > agg->max) agg->max = num;
        }
        // else — не число, просто пропускаем (count уже увеличен)
    }
    else {
        // Для нечисловых — min/max строки
        if (!agg->min_str || strcmp(val, agg->min_str) < 0) {
            free(agg->min_str);
            agg->min_str = strdup(val);
        }
        if (!agg->max_str || strcmp(val, agg->max_str) > 0) {
            free(agg->max_str);
            agg->max_str = strdup(val);
        }
    }
}

char *get_agg_display(const Agg *agg, const char *aggregation, ColType value_type) {
    static char buf[64];
    buf[0] = '\0';

    if (strcmp(aggregation, "COUNT") == 0) {
        snprintf(buf, sizeof(buf), "%d", agg->count);
    }
    else if (strcmp(aggregation, "UNIQUE COUNT") == 0) {
        snprintf(buf, sizeof(buf), "%d", agg->unique_count);
    }
    else if (strcmp(aggregation, "SUM") == 0) {
        snprintf(buf, sizeof(buf), "%.2f", agg->sum);
    }
    else if (strcmp(aggregation, "AVG") == 0) {
        double avg = agg->count > 0 ? agg->sum / agg->count : 0.0;
        snprintf(buf, sizeof(buf), "%.2f", avg);
    }
    else if (strcmp(aggregation, "MIN") == 0) {
        if (value_type == COL_NUM && agg->count > 0) {
            snprintf(buf, sizeof(buf), "%.2f", agg->min);
        } else if (agg->min_str) {
            strncpy(buf, agg->min_str, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
        }
    }
    else if (strcmp(aggregation, "MAX") == 0) {
        if (value_type == COL_NUM && agg->count > 0) {
            snprintf(buf, sizeof(buf), "%.2f", agg->max);
        } else if (agg->max_str) {
            strncpy(buf, agg->max_str, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
        }
    }

    if (buf[0] == '\0') strcpy(buf, "—");
    return buf;
}

void draw_table_frame(int y, int x, int height, int width) {
    attron(COLOR_PAIR(6));
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + width - 1, ACS_URCORNER);
    mvaddch(y + height - 1, x, ACS_LLCORNER);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);

    for (int i = 1; i < width - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + 2, x + i, ACS_HLINE);
        mvaddch(y + height - 1, x + i, ACS_HLINE);
    }

    for (int i = 1; i < height - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + width - 1, ACS_VLINE);
    }

    mvaddch(y + 2, x, ACS_LTEE);
    mvaddch(y + 2, x + width - 1, ACS_RTEE);

    attroff(COLOR_PAIR(6));

    if (in_filter_mode) mvprintw(2, 2, "F:%s", filter_query);
}

void show_pivot_settings_window(PivotSettings *settings, const char *csv_filename, int height, int width) {
    int win_h = 16, win_w = 90;
    WINDOW *win = newwin(win_h, win_w, (height - win_h) / 2, (width - win_w) / 2);
    box(win, 0, 0);
    keypad(win, TRUE);

    // Значения по умолчанию
    if (!settings->aggregation)  settings->aggregation  = strdup("SUM");
    if (!settings->date_grouping) settings->date_grouping = strdup("Auto");
    if (!settings->row_sort)     settings->row_sort      = strdup("KEY_ASC");
    if (!settings->col_sort)     settings->col_sort      = strdup("KEY_ASC");

    int current_field = 0;

    const char *fields[] = {
        "Rows group by",
        "Columns group by",
        "Values column",
        "Aggregation",
        "Date grouping",
        "Show row totals",
        "Show column totals",
        "Grand total",
        "Row sort",
        "Column sort"
    };
    int num_fields = 10;

    char **col_options = malloc((col_count + 1) * sizeof(char*));
    col_options[0] = strdup("None");
    for (int i = 0; i < col_count; i++) {
        if (use_headers && column_names[i]) {
            col_options[i + 1] = strdup(column_names[i]);
        } else {
            char buf[16];
            col_letter(i, buf);
            col_options[i + 1] = strdup(buf);
        }
    }
    int num_col_options = col_count + 1;

    const char *agg_options[] = {
        "SUM", "AVG", "COUNT", "MIN", "MAX", "UNIQUE COUNT",
        "SUM+COUNT", "SUM+AVG", "MIN+MAX", "SUM+COUNT+AVG", "COUNT+UNIQUE COUNT"
    };
    int num_agg_options = 11;

    const char *date_options[] = {"Auto", "Month", "Quarter", "Year", "Century"};
    int num_date_options = 5;

    const char *yn_options[] = {"Yes", "No"};
    int num_yn = 2;

    const char *sort_display[] = {"Key ↑", "Key ↓", "Val ↑", "Val ↓"};
    const char *sort_values[]  = {"KEY_ASC", "KEY_DESC", "VAL_ASC", "VAL_DESC"};
    int num_sort_opts = 4;

    // Текущие индексы
    int row_group_idx = 0, col_group_idx = 0, value_idx = 0, agg_idx = 0, date_idx = 0;
    int row_tot_idx = settings->show_row_totals ? 0 : 1;
    int col_tot_idx = settings->show_col_totals ? 0 : 1;
    int grand_idx = settings->show_grand_total ? 0 : 1;
    int row_sort_idx = 0, col_sort_idx = 0;

    // Инициализация из настроек (если загружены из файла)
    if (settings->row_group_col) {
        for (int i = 1; i < num_col_options; i++) {
            if (strcmp(col_options[i], settings->row_group_col) == 0) {
                row_group_idx = i;
                break;
            }
        }
    }
    if (settings->col_group_col) {
        for (int i = 1; i < num_col_options; i++) {
            if (strcmp(col_options[i], settings->col_group_col) == 0) {
                col_group_idx = i;
                break;
            }
        }
    }
    if (settings->value_col) {
        for (int i = 1; i < num_col_options; i++) {
            if (strcmp(col_options[i], settings->value_col) == 0) {
                value_idx = i;
                break;
            }
        }
    }
    for (int i = 0; i < num_agg_options; i++) {
        if (strcmp(agg_options[i], settings->aggregation) == 0) {
            agg_idx = i;
            break;
        }
    }
    for (int i = 0; i < num_date_options; i++) {
        if (strcmp(date_options[i], settings->date_grouping) == 0) {
            date_idx = i;
            break;
        }
    }
    for (int i = 0; i < num_sort_opts; i++) {
        if (strcmp(sort_values[i], settings->row_sort) == 0) { row_sort_idx = i; break; }
    }
    for (int i = 0; i < num_sort_opts; i++) {
        if (strcmp(sort_values[i], settings->col_sort) == 0) { col_sort_idx = i; break; }
    }

    while (1) {
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 1, 2, "Pivot Table Settings");

        // Проверяем, есть ли хотя бы один столбец даты в группировке
        int has_date_group = 0;
        int r_idx = row_group_idx > 0 ? (use_headers ? col_name_to_num(col_options[row_group_idx]) : col_to_num(col_options[row_group_idx])) : -1;
        int c_idx = col_group_idx > 0 ? (use_headers ? col_name_to_num(col_options[col_group_idx]) : col_to_num(col_options[col_group_idx])) : -1;
        if (r_idx >= 0 && col_types[r_idx] == COL_DATE) has_date_group = 1;
        if (c_idx >= 0 && col_types[c_idx] == COL_DATE) has_date_group = 1;

        for (int f = 0; f < num_fields; f++) {
            char *val = NULL;

            if (f == 0) val = col_options[row_group_idx];
            else if (f == 1) val = col_options[col_group_idx];
            else if (f == 2) val = col_options[value_idx];
            else if (f == 3) val = (char*)agg_options[agg_idx];
            else if (f == 4) {
                // Date grouping — всегда показываем, но блокируем, если нет дат
                val = (char*)date_options[date_idx];
                if (!has_date_group) val = "Auto (disabled)";
            }
            else if (f == 5) val = (char*)yn_options[row_tot_idx];
            else if (f == 6) val = (char*)yn_options[col_tot_idx];
            else if (f == 7) val = (char*)yn_options[grand_idx];
            else if (f == 8) val = (char*)sort_display[row_sort_idx];
            else if (f == 9) val = (char*)sort_display[col_sort_idx];

            int y_pos = f + 3;
            if (f == current_field) wattron(win, A_REVERSE);

            if (f == 4 && !has_date_group) {
                // Серый цвет для заблокированного поля
                wattron(win, COLOR_PAIR(6));  // COLOR_PAIR(6) — обычно серый/тёмный
            }

            mvwprintw(win, y_pos, 2, "%s:", fields[f]);
            mvwprintw(win, y_pos, 24, "%s", val ? val : "None");

            wattroff(win, COLOR_PAIR(6));
            if (f == current_field) wattroff(win, A_REVERSE);
        }

        mvwprintw(win, win_h - 1, 2, "↑↓ j/k - select | ←→ h/l - change | Enter - Build | Esc/q - Cancel");
        wrefresh(win);

        int ch = wgetch(win);

        if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            current_field = (current_field + 1) % num_fields;
        }
        else if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            current_field = (current_field - 1 + num_fields) % num_fields;
        }
        else if (ch == KEY_RIGHT || ch == 'l' || ch == 'L') {
            int real_f = current_field;  // теперь не используем field_map, показываем все
            if (real_f == 0) row_group_idx = (row_group_idx + 1) % num_col_options;
            else if (real_f == 1) col_group_idx = (col_group_idx + 1) % num_col_options;
            else if (real_f == 2) value_idx = (value_idx + 1) % num_col_options;
            else if (real_f == 3) {
                int vnum = value_idx > 0 ? (use_headers ? col_name_to_num(col_options[value_idx]) : col_to_num(col_options[value_idx])) : -1;
                ColType vt = (vnum >= 0) ? col_types[vnum] : COL_STR;
                int max_a = (vt == COL_NUM) ? num_agg_options : (vt == COL_DATE ? 5 : 2);
                agg_idx = (agg_idx + 1) % max_a;
            }
            else if (real_f == 4) {
                // Меняем только если активно (есть дата)
                if (has_date_group) {
                    date_idx = (date_idx + 1) % num_date_options;
                }
            }
            else if (real_f == 5) row_tot_idx = (row_tot_idx + 1) % num_yn;
            else if (real_f == 6) col_tot_idx = (col_tot_idx + 1) % num_yn;
            else if (real_f == 7) grand_idx = (grand_idx + 1) % num_yn;
            else if (real_f == 8) row_sort_idx = (row_sort_idx + 1) % num_sort_opts;
            else if (real_f == 9) col_sort_idx = (col_sort_idx + 1) % num_sort_opts;
        }
        else if (ch == KEY_LEFT || ch == 'h' || ch == 'H') {
            int real_f = current_field;
            if (real_f == 0) row_group_idx = (row_group_idx - 1 + num_col_options) % num_col_options;
            else if (real_f == 1) col_group_idx = (col_group_idx - 1 + num_col_options) % num_col_options;
            else if (real_f == 2) value_idx = (value_idx - 1 + num_col_options) % num_col_options;
            else if (real_f == 3) {
                int vnum = value_idx > 0 ? (use_headers ? col_name_to_num(col_options[value_idx]) : col_to_num(col_options[value_idx])) : -1;
                ColType vt = (vnum >= 0) ? col_types[vnum] : COL_STR;
                int max_a = (vt == COL_NUM) ? num_agg_options : (vt == COL_DATE ? 5 : 2);
                agg_idx = (agg_idx - 1 + max_a) % max_a;
            }
            else if (real_f == 4) {
                if (has_date_group) {
                    date_idx = (date_idx - 1 + num_date_options) % num_date_options;
                }
            }
            else if (real_f == 5) row_tot_idx = (row_tot_idx - 1 + num_yn) % num_yn;
            else if (real_f == 6) col_tot_idx = (col_tot_idx - 1 + num_yn) % num_yn;
            else if (real_f == 7) grand_idx = (grand_idx - 1 + num_yn) % num_yn;
            else if (real_f == 8) row_sort_idx = (row_sort_idx - 1 + num_sort_opts) % num_sort_opts;
            else if (real_f == 9) col_sort_idx = (col_sort_idx - 1 + num_sort_opts) % num_sort_opts;
        }
        else if (ch == 10 || ch == KEY_ENTER) {
            free(settings->row_group_col);
            free(settings->col_group_col);
            free(settings->value_col);
            free(settings->aggregation);
            free(settings->date_grouping);
            free(settings->row_sort);
            free(settings->col_sort);

            settings->row_group_col = row_group_idx > 0 ? strdup(col_options[row_group_idx]) : NULL;
            settings->col_group_col = col_group_idx > 0 ? strdup(col_options[col_group_idx]) : NULL;
            settings->value_col = value_idx > 0 ? strdup(col_options[value_idx]) : NULL;
            settings->aggregation = strdup(agg_options[agg_idx]);

            int has_date = 0;
            if (settings->row_group_col) {
                int idx = use_headers ? col_name_to_num(settings->row_group_col) : col_to_num(settings->row_group_col);
                if (idx >= 0 && col_types[idx] == COL_DATE) has_date = 1;
            }
            if (settings->col_group_col) {
                int idx = use_headers ? col_name_to_num(settings->col_group_col) : col_to_num(settings->col_group_col);
                if (idx >= 0 && col_types[idx] == COL_DATE) has_date = 1;
            }
            settings->date_grouping = has_date ? strdup(date_options[date_idx]) : strdup("Auto");

            settings->show_row_totals  = (row_tot_idx == 0);
            settings->show_col_totals  = (col_tot_idx == 0);
            settings->show_grand_total = (grand_idx == 0);
            settings->row_sort = strdup(sort_values[row_sort_idx]);
            settings->col_sort = strdup(sort_values[col_sort_idx]);

            if (settings->value_col) {
                save_pivot_settings(csv_filename, settings);
                delwin(win);
                for (int i = 0; i < num_col_options; i++) free(col_options[i]);
                free(col_options);
                build_and_show_pivot(settings, csv_filename, height, width);
                return;
            }
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q') {
            delwin(win);
            for (int i = 0; i < num_col_options; i++) free(col_options[i]);
            free(col_options);
            return;
        }
    }
}

void build_and_show_pivot(PivotSettings *settings, const char *csv_filename, int height, int width) {
    // Get indices
    int row_group_idx = settings->row_group_col ? (use_headers ? col_name_to_num(settings->row_group_col) : col_to_num(settings->row_group_col)) : -1;
    int col_group_idx = settings->col_group_col ? (use_headers ? col_name_to_num(settings->col_group_col) : col_to_num(settings->col_group_col)) : -1;
    int value_idx = (use_headers ? col_name_to_num(settings->value_col) : col_to_num(settings->value_col));
    if (value_idx < 0) return;

    ColType value_type = col_types[value_idx];
    ColType row_type = (row_group_idx >= 0) ? col_types[row_group_idx] : COL_STR;
    ColType col_type = (col_group_idx >= 0) ? col_types[col_group_idx] : COL_STR;

    // Parse aggregation list: "SUM+COUNT" → agg_list[]={"SUM","COUNT"}, agg_count=2
    char agg_list[6][32];
    int agg_count = 0;
    {
        char agg_buf[64];
        strncpy(agg_buf, settings->aggregation ? settings->aggregation : "SUM", sizeof(agg_buf) - 1);
        agg_buf[sizeof(agg_buf) - 1] = '\0';
        char *tok = strtok(agg_buf, "+");
        while (tok && agg_count < 6) {
            strncpy(agg_list[agg_count], tok, 31);
            agg_list[agg_count][31] = '\0';
            agg_count++;
            tok = strtok(NULL, "+");
        }
        if (agg_count == 0) { strcpy(agg_list[0], "SUM"); agg_count = 1; }
    }

    int display_count = filter_active ? filtered_count : (sort_col >= 0 ? sorted_count : row_count);

    // Определяем типы группировки — ТОЛЬКО ЗДЕСЬ, ОДИН РАЗ
    int row_is_date = (row_group_idx >= 0) && (col_types[row_group_idx] == COL_DATE);
    int col_is_date = (col_group_idx >= 0) && (col_types[col_group_idx] == COL_DATE);

    // Pass 1: collect unique row and col keys
    HashMap *row_map = hash_map_create(1024);
    HashMap *col_map = hash_map_create(1024);

    draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
    attron(COLOR_PAIR(3));
    printw(" | Collecting groups... 0%%                   ");
    attroff(COLOR_PAIR(3));
    refresh();

    int start_row = use_headers ? 1 : 0;

    // При отсутствии фильтра/сортировки читаем файл последовательно — без fseek на каждую строку
    int use_seq = (!filter_active && sort_col < 0);
    if (use_seq && !g_mmap_base) rewind(f);
    char seq_buf[MAX_LINE_LEN];

    for (int d = 0; d < display_count; d++) {
        int real_row = get_real_row(d);
        const char *line;

        if (use_seq) {
            if (g_mmap_base) {
                /* mmap: direct offset access, no sequential file pointer needed */
                char *ml = csv_mmap_get_line((long)rows[d].offset, seq_buf, sizeof(seq_buf));
                if (!ml) break;
                if (real_row < start_row) continue;
                line = seq_buf;
            } else {
                if (!fgets(seq_buf, sizeof(seq_buf), f)) break;
                seq_buf[strcspn(seq_buf, "\r\n")] = '\0';
                if (real_row < start_row) continue;
                line = seq_buf;
            }
            if (!rows[real_row].line_cache)
                rows[real_row].line_cache = strdup(seq_buf);
        } else {
            if (real_row < start_row) continue;
            if (!rows[real_row].line_cache) {
                if (g_mmap_base) {
                    char mbuf[MAX_LINE_LEN];
                    char *ml = csv_mmap_get_line(rows[real_row].offset, mbuf, sizeof(mbuf));
                    rows[real_row].line_cache = strdup(ml ? ml : "");
                } else {
                    fseek(f, rows[real_row].offset, SEEK_SET);
                    char buf[MAX_LINE_LEN];
                    if (fgets(buf, sizeof(buf), f)) {
                        buf[strcspn(buf, "\n")] = '\0';
                        rows[real_row].line_cache = strdup(buf);
                    } else {
                        rows[real_row].line_cache = strdup("");
                    }
                }
            }
            line = rows[real_row].line_cache;
        }

        /* Extract only the fields we need — no malloc for field array */
        char p1_rval[512], p1_cval[512];

        if (row_group_idx >= 0) {
            get_field_csv(line, row_group_idx, p1_rval, sizeof(p1_rval));
            char *rkey = get_group_key(p1_rval, row_type, settings->date_grouping, row_group_idx);
            if (rkey && *rkey) {
                if (!hash_map_get(row_map, rkey)) hash_map_put(row_map, rkey, (void*)1);
            }
            free(rkey);
        } else {
            hash_map_put(row_map, "Total", (void*)1);
        }

        if (col_group_idx >= 0) {
            get_field_csv(line, col_group_idx, p1_cval, sizeof(p1_cval));
            char *ckey = get_group_key(p1_cval, col_type, settings->date_grouping, col_group_idx);
            if (ckey && *ckey) {
                if (!hash_map_get(col_map, ckey)) hash_map_put(col_map, ckey, (void*)1);
            }
            free(ckey);
        } else {
            hash_map_put(col_map, "Total", (void*)1);
        }

        if (d % 50000 == 0) {
            draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | Collecting groups... %3d%%                 ", (int)(100.0 * d / display_count));
            attroff(COLOR_PAIR(3));
            refresh();
        }
    }

    int unique_rows;
    char **row_keys = hash_map_keys(row_map, &unique_rows);

    int unique_cols;
    char **col_keys = hash_map_keys(col_map, &unique_cols);

    // Сортируем ключи
    if (row_is_date) {
        qsort(row_keys, unique_rows, sizeof(char*), compare_date_keys);
    } else {
        qsort(row_keys, unique_rows, sizeof(char*), compare_str);
    }

    if (col_is_date) {
        qsort(col_keys, unique_cols, sizeof(char*), compare_date_keys);
    } else {
        qsort(col_keys, unique_cols, sizeof(char*), compare_str);
    }

    int total_rows = unique_rows + (settings->show_col_totals ? 1 : 0);
    int total_cols = (unique_cols + (settings->show_row_totals ? 1 : 0)) * agg_count;
    int max_logical_cols = unique_cols + (settings->show_row_totals ? 1 : 0);

    // Вычисляем ширину левого столбца
    int max_row_key_len = strlen("Row \\ Col");
    for (int i = 0; i < unique_rows; i++) {
        int len = strlen(row_keys[i]);
        if (len > max_row_key_len) max_row_key_len = len;
    }
    if (settings->show_col_totals) {
        int len = strlen("Total");
        if (len > max_row_key_len) max_row_key_len = len;
    }

    int pivot_row_index_width = max_row_key_len + 4; // запас по бокам

    const int MIN_PIVOT_ROW_WIDTH = 12;
    const int MAX_PIVOT_ROW_WIDTH = 40;
    if (pivot_row_index_width < MIN_PIVOT_ROW_WIDTH) pivot_row_index_width = MIN_PIVOT_ROW_WIDTH;
    if (pivot_row_index_width > MAX_PIVOT_ROW_WIDTH) pivot_row_index_width = MAX_PIVOT_ROW_WIDTH;

    // Allocate matrix
    Agg **matrix = malloc(unique_rows * sizeof(Agg*));
    for (int i = 0; i < unique_rows; i++) {
        matrix[i] = calloc(unique_cols, sizeof(Agg));
        for (int j = 0; j < unique_cols; j++) {
            matrix[i][j].min = INFINITY;
            matrix[i][j].max = -INFINITY;
        }
    }

    Agg *row_totals = calloc(unique_rows, sizeof(Agg));
    for (int i = 0; i < unique_rows; i++) {
        row_totals[i].min = INFINITY;
        row_totals[i].max = -INFINITY;
    }

    Agg *col_totals = calloc(unique_cols, sizeof(Agg));
    for (int i = 0; i < unique_cols; i++) {
        col_totals[i].min = INFINITY;
        col_totals[i].max = -INFINITY;
    }

    Agg grand = {0, 0, INFINITY, -INFINITY, 0, NULL, NULL};

    // Вычисляем хорошие начальные размеры хеш-таблиц (следующая степень 2)
    // чтобы избежать коллизий при большом числе уникальных значений
    int hm_grand  = 16; { int n = display_count / 2; while (hm_grand < n && hm_grand < 65536) hm_grand <<= 1; }
    int hm_perrow = 16; { int n = (display_count / (unique_rows > 0 ? unique_rows : 1)) * 2; while (hm_perrow < n && hm_perrow < 65536) hm_perrow <<= 1; }
    int hm_percol = 16; { int n = (display_count / (unique_cols > 0 ? unique_cols : 1)) * 2; while (hm_percol < n && hm_percol < 65536) hm_percol <<= 1; }

    HashMap **cell_uniques    = calloc(unique_rows * unique_cols, sizeof(HashMap*));
    HashMap **row_unique_maps = calloc(unique_rows, sizeof(HashMap*));
    HashMap **col_unique_maps = calloc(unique_cols, sizeof(HashMap*));
    HashMap  *grand_unique_map = hash_map_create(hm_grand);

    // Второй проход: агрегация (строки уже закэшированы в Pass 1 — I/O не нужен)
    draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
    attron(COLOR_PAIR(3));
    printw(" | Aggregating... 0%%                         ");
    attroff(COLOR_PAIR(3));
    refresh();

    /* ── Parallel aggregation: SUM/COUNT/MIN/MAX across N threads ── */
    {
        int nthreads = csv_num_threads();
        int chunk2   = (display_count + nthreads - 1) / nthreads;
        PivotPass2Worker *p2w = calloc(nthreads, sizeof(PivotPass2Worker));
        pthread_t        *p2t = malloc(nthreads * sizeof(pthread_t));

        for (int t = 0; t < nthreads; t++) {
            p2w[t].start_d      = t * chunk2;
            p2w[t].count        = chunk2;
            if (p2w[t].start_d + p2w[t].count > display_count)
                p2w[t].count = display_count - p2w[t].start_d;
            if (p2w[t].count < 0) p2w[t].count = 0;
            p2w[t].start_row    = start_row;
            p2w[t].row_group_idx = row_group_idx;
            p2w[t].col_group_idx = col_group_idx;
            p2w[t].value_idx    = value_idx;
            p2w[t].unique_rows  = unique_rows;
            p2w[t].unique_cols  = unique_cols;
            p2w[t].row_keys     = row_keys;
            p2w[t].col_keys     = col_keys;
            p2w[t].row_is_date  = row_is_date;
            p2w[t].col_is_date  = col_is_date;
            p2w[t].date_grouping = settings->date_grouping;
            p2w[t].row_type     = row_type;
            p2w[t].col_type     = col_type;
            p2w[t].value_type   = value_type;
            pthread_create(&p2t[t], NULL, pivot_pass2_thread, &p2w[t]);
        }
        for (int t = 0; t < nthreads; t++) pthread_join(p2t[t], NULL);

        /* Merge partial results into main matrix */
        for (int t = 0; t < nthreads; t++) {
            for (int i = 0; i < unique_rows; i++) {
                for (int j = 0; j < unique_cols; j++) {
                    Agg *src = &p2w[t].matrix[i * unique_cols + j];
                    Agg *dst = &matrix[i][j];
                    dst->sum   += src->sum;
                    dst->count += src->count;
                    if (src->min < dst->min) dst->min = src->min;
                    if (src->max > dst->max) dst->max = src->max;
                }
                Agg *rs = &p2w[t].row_totals[i];
                row_totals[i].sum   += rs->sum;
                row_totals[i].count += rs->count;
                if (rs->min < row_totals[i].min) row_totals[i].min = rs->min;
                if (rs->max > row_totals[i].max) row_totals[i].max = rs->max;
            }
            for (int j = 0; j < unique_cols; j++) {
                Agg *cs = &p2w[t].col_totals[j];
                col_totals[j].sum   += cs->sum;
                col_totals[j].count += cs->count;
                if (cs->min < col_totals[j].min) col_totals[j].min = cs->min;
                if (cs->max > col_totals[j].max) col_totals[j].max = cs->max;
            }
            grand.sum   += p2w[t].grand.sum;
            grand.count += p2w[t].grand.count;
            if (p2w[t].grand.min < grand.min) grand.min = p2w[t].grand.min;
            if (p2w[t].grand.max > grand.max) grand.max = p2w[t].grand.max;
            free(p2w[t].matrix);
            free(p2w[t].row_totals);
            free(p2w[t].col_totals);
        }
        free(p2w);
        free(p2t);
    }

    /* ── Sequential pass: unique counts (hash maps not thread-safe) ── */
    /* Only executed when UNIQUE aggregation is requested */
    int need_unique = 0;
    for (int a = 0; a < agg_count; a++)
        if (strcmp(agg_list[a], "UNIQUE") == 0) { need_unique = 1; break; }

    if (need_unique) {
        char rval_buf[512], cval_buf[512], vval_buf[512];
        for (int d = 0; d < display_count; d++) {
            int real_row = get_real_row(d);
            if (real_row < start_row) continue;
            const char *line = rows[real_row].line_cache ? rows[real_row].line_cache : "";
            if (!*line) continue;

            if (row_group_idx >= 0) get_field_csv(line, row_group_idx, rval_buf, sizeof(rval_buf));
            else strcpy(rval_buf, "Total");
            if (col_group_idx >= 0) get_field_csv(line, col_group_idx, cval_buf, sizeof(cval_buf));
            else strcpy(cval_buf, "Total");
            get_field_csv(line, value_idx, vval_buf, sizeof(vval_buf));

            char *rkey = get_group_key(rval_buf, row_type, settings->date_grouping, row_group_idx);
            char *ckey = get_group_key(cval_buf, col_type, settings->date_grouping, col_group_idx);

            int ridx = -1;
            if (rkey && *rkey) {
                char *p = rkey;
                char **r = row_is_date
                    ? (char**)bsearch(&p, row_keys, unique_rows, sizeof(char*), compare_date_keys)
                    : (char**)bsearch(&p, row_keys, unique_rows, sizeof(char*), compare_str);
                ridx = r ? (int)(r - row_keys) : -1;
            }
            int cidx = -1;
            if (ckey && *ckey) {
                char *p = ckey;
                char **c = col_is_date
                    ? (char**)bsearch(&p, col_keys, unique_cols, sizeof(char*), compare_date_keys)
                    : (char**)bsearch(&p, col_keys, unique_cols, sizeof(char*), compare_str);
                cidx = c ? (int)(c - col_keys) : -1;
            }
            free(rkey); free(ckey);

            if (ridx >= 0 && cidx >= 0) {
                const char *v = vval_buf;
                int cell_idx = ridx * unique_cols + cidx;
                if (!cell_uniques[cell_idx]) cell_uniques[cell_idx] = hash_map_create(256);
                if (!hash_map_get(cell_uniques[cell_idx], v)) {
                    hash_map_put(cell_uniques[cell_idx], v, (void*)1);
                    matrix[ridx][cidx].unique_count++;
                }
                if (!row_unique_maps[ridx]) row_unique_maps[ridx] = hash_map_create(hm_perrow);
                if (!hash_map_get(row_unique_maps[ridx], v)) {
                    hash_map_put(row_unique_maps[ridx], v, (void*)1);
                    row_totals[ridx].unique_count++;
                }
                if (!col_unique_maps[cidx]) col_unique_maps[cidx] = hash_map_create(hm_percol);
                if (!hash_map_get(col_unique_maps[cidx], v)) {
                    hash_map_put(col_unique_maps[cidx], v, (void*)1);
                    col_totals[cidx].unique_count++;
                }
                if (!hash_map_get(grand_unique_map, v)) {
                    hash_map_put(grand_unique_map, v, (void*)1);
                    grand.unique_count++;
                }
            }
            if (d % 50000 == 0) {
                draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
                attron(COLOR_PAIR(3));
                printw(" | Unique count... %3d%%                     ", (int)(100.0 * d / display_count));
                attroff(COLOR_PAIR(3));
                refresh();
            }
        }
    }

    // Free uniques
    for (int i = 0; i < unique_rows * unique_cols; i++) if (cell_uniques[i]) hash_map_destroy(cell_uniques[i]);
    free(cell_uniques);
    for (int i = 0; i < unique_rows; i++) if (row_unique_maps[i]) hash_map_destroy(row_unique_maps[i]);
    free(row_unique_maps);
    for (int i = 0; i < unique_cols; i++) if (col_unique_maps[i]) hash_map_destroy(col_unique_maps[i]);
    free(col_unique_maps);
    hash_map_destroy(grand_unique_map);
    hash_map_destroy(row_map);
    hash_map_destroy(col_map);

    // ── Строим display-порядок строк и столбцов согласно настройкам сортировки ──
    const char *rsort = settings->row_sort ? settings->row_sort : "KEY_ASC";
    const char *csort = settings->col_sort ? settings->col_sort : "KEY_ASC";

    int *row_order = malloc(unique_rows * sizeof(int));
    int *col_order = malloc(unique_cols * sizeof(int));
    for (int i = 0; i < unique_rows; i++) row_order[i] = i;
    for (int i = 0; i < unique_cols; i++) col_order[i] = i;

    if (strcmp(rsort, "KEY_DESC") == 0) {
        for (int i = 0; i < unique_rows / 2; i++) {
            int tmp = row_order[i];
            row_order[i] = row_order[unique_rows - 1 - i];
            row_order[unique_rows - 1 - i] = tmp;
        }
    } else if (strcmp(rsort, "VAL_ASC") == 0 || strcmp(rsort, "VAL_DESC") == 0) {
        g_sort_agg_arr = row_totals;
        g_sort_agg_str = agg_list[0];
        g_sort_vtype   = value_type;
        g_sort_dir     = strcmp(rsort, "VAL_ASC") == 0 ? 1 : -1;
        qsort(row_order, unique_rows, sizeof(int), compare_order_by_agg);
    }

    if (strcmp(csort, "KEY_DESC") == 0) {
        for (int i = 0; i < unique_cols / 2; i++) {
            int tmp = col_order[i];
            col_order[i] = col_order[unique_cols - 1 - i];
            col_order[unique_cols - 1 - i] = tmp;
        }
    } else if (strcmp(csort, "VAL_ASC") == 0 || strcmp(csort, "VAL_DESC") == 0) {
        g_sort_agg_arr = col_totals;
        g_sort_agg_str = agg_list[0];
        g_sort_vtype   = value_type;
        g_sort_dir     = strcmp(csort, "VAL_ASC") == 0 ? 1 : -1;
        qsort(col_order, unique_cols, sizeof(int), compare_order_by_agg);
    }

    // Строки отображения sort для заголовка
    const char *rsort_disp = strcmp(rsort,"KEY_ASC")==0  ? "Key\xe2\x86\x91" :
                             strcmp(rsort,"KEY_DESC")==0  ? "Key\xe2\x86\x93" :
                             strcmp(rsort,"VAL_ASC")==0   ? "Val\xe2\x86\x91" : "Val\xe2\x86\x93";
    const char *csort_disp = strcmp(csort,"KEY_ASC")==0  ? "Key\xe2\x86\x91" :
                             strcmp(csort,"KEY_DESC")==0  ? "Key\xe2\x86\x93" :
                             strcmp(csort,"VAL_ASC")==0   ? "Val\xe2\x86\x91" : "Val\xe2\x86\x93";

    // Now display loop
    int top_row = 0, cur_row_p = 0, left_col_p = 0, cur_col_p = 0;
    int vis_rows = height - 7 - (agg_count > 1 ? 1 : 0);

    // Graph state
    int graph_split  = 0;
    int graph_axis   = 0;  // 0 = X-axis is rows, series = cols; 1 = X-axis is cols, series = rows
    GraphType  pivot_gtype  = GRAPH_BAR;
    GraphScale pivot_gscale = SCALE_LINEAR;
    bool series_pinned[512];
    memset(series_pinned, 0, sizeof(series_pinned));

    while (1) {
        // ── Geometry (recalculated every frame) ───────────────────────────────
        int table_w, graph_x, graph_w, vis_cols;
        if (graph_split) {
            table_w = (int)(width * 0.35);
            if (table_w < pivot_row_index_width + CELL_WIDTH + 4) table_w = pivot_row_index_width + CELL_WIDTH + 4;
            graph_x = table_w + 1;
            graph_w = width - graph_x;
            vis_cols = (table_w - pivot_row_index_width - 2) / CELL_WIDTH;
        } else {
            table_w = width;
            graph_x = 0; graph_w = 0;
            vis_cols = (width - pivot_row_index_width - 2) / CELL_WIDTH;
        }
        if (vis_cols < 1) vis_cols = 1;
        clear();
        draw_menu(0, 1, width, 2);
        draw_table_frame(1, 0, height - 2, width);
        draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);

        // Временный буфер для буквенных обозначений столбцов (A, B, AA...)
        char buf[16];

        // Сбрасываем позицию курсора и начинаем вывод с y=3, x=2
        move(3, 2);

        attron(COLOR_PAIR(6));  // основной цвет для скобок и меток

        printw("[");

        // Rows
        printw("Rows:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));  // яркий для имени столбца
        if (row_group_idx >= 0) {
            const char *name = use_headers && column_names[row_group_idx] ? column_names[row_group_idx] : (col_letter(row_group_idx, buf), buf);
            printw("%s", name);
        } else {
            printw("None");
        }
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Columns
        printw("[Cols:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        if (col_group_idx >= 0) {
            const char *name = use_headers && column_names[col_group_idx] ? column_names[col_group_idx] : (col_letter(col_group_idx, buf), buf);
            printw("%s", name);
        } else {
            printw("None");
        }
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Values
        printw("[Val:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        if (value_idx >= 0) {
            const char *name = use_headers && column_names[value_idx] ? column_names[value_idx] : (col_letter(value_idx, buf), buf);
            printw("%s", name);
        } else {
            printw("None");
        }
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Agg
        printw("[Agg:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", settings->aggregation ? settings->aggregation : "SUM");
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        // Линия между блоками
        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Date
        printw("[Date:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", settings->date_grouping ? settings->date_grouping : "Auto");
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        addch(ACS_HLINE);
        addch(ACS_HLINE);

        // Sort
        printw("[RS:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", rsort_disp);
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw(" CS:");
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(3));
        printw("%s", csort_disp);
        attroff(COLOR_PAIR(3));
        attron(COLOR_PAIR(6));
        printw("]");

        attroff(COLOR_PAIR(6));

        refresh();

        // Заголовок столбцов
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(4, 2, "%-*s", pivot_row_index_width - 2, "Row \\ Col");
        attroff(COLOR_PAIR(6) | A_BOLD);

        // Row 1: col group keys (only at first sub-col of each key)
        for (int c = 0; c < vis_cols; c++) {
            int cid = left_col_p + c;
            if (cid >= total_cols) break;
            int logical_col = cid / agg_count;
            int agg_sub     = cid % agg_count;
            if (agg_sub == 0) {
                char *key = (logical_col < unique_cols) ? col_keys[col_order[logical_col]] : "Total";
                int is_current_col = (logical_col == cur_col_p / agg_count);
                char col_key_buf[CELL_WIDTH * 2 + 1];
                snprintf(col_key_buf, CELL_WIDTH - 1, "%s", key);
                attron(COLOR_PAIR(is_current_col ? 3 : 6) | A_BOLD);
                mvprintw(4, pivot_row_index_width + c * CELL_WIDTH, "%*s",
                         CELL_WIDTH - 2, col_key_buf);
                attroff(COLOR_PAIR(3) | COLOR_PAIR(6) | A_BOLD);
            }
        }

        // Row 2 (only for multi-agg): agg sub-headers
        if (agg_count > 1) {
            for (int c = 0; c < vis_cols; c++) {
                int cid = left_col_p + c;
                if (cid >= total_cols) break;
                int agg_sub = cid % agg_count;
                int is_cur  = (cid == cur_col_p);
                attron(COLOR_PAIR(is_cur ? 3 : 6));
                mvprintw(5, pivot_row_index_width + c * CELL_WIDTH, "%*s",
                         CELL_WIDTH - 2, agg_list[agg_sub]);
                attroff(COLOR_PAIR(3) | COLOR_PAIR(6));
            }
        }

        // Тело таблицы
        int data_row_y = 5 + (agg_count > 1 ? 1 : 0);
        for (int i = 0; i < vis_rows; i++) {
            int rid = top_row + i;
            if (rid >= total_rows) break;
            int is_current_row = (rid == cur_row_p);
            // Название строки (слева)
            char *rkey = (rid < unique_rows) ? row_keys[row_order[rid]] : "Total";
            if (is_current_row) {
                attron(COLOR_PAIR(3)); // подсветка текущей строки
            } else {
                attron(COLOR_PAIR(6));
            }
            char rkey_buf[41]; // MAX_PIVOT_ROW_WIDTH=40 + null
            snprintf(rkey_buf, pivot_row_index_width - 1, "%s", rkey);
            mvprintw(data_row_y + i, 2, "%-*s", pivot_row_index_width - 2, rkey_buf);
            if (is_current_row) attroff(COLOR_PAIR(3));
            else attroff(COLOR_PAIR(6));

            // Ячейки строки
            for (int c = 0; c < vis_cols; c++) {
                int cid = left_col_p + c;
                if (cid >= total_cols) break;
                int logical_col = cid / agg_count;
                int agg_sub     = cid % agg_count;
                const Agg *agg;
                int arid = (rid < unique_rows) ? row_order[rid] : unique_rows;
                int acid = (logical_col < unique_cols) ? col_order[logical_col] : unique_cols;
                if (rid < unique_rows && logical_col < unique_cols) agg = &matrix[arid][acid];
                else if (rid < unique_rows && logical_col == unique_cols) agg = &row_totals[arid];
                else if (rid == unique_rows && logical_col < unique_cols) agg = &col_totals[acid];
                else agg = &grand;
                char *disp = get_agg_display(agg, agg_list[agg_sub], value_type);
                int is_current_cell = (rid == cur_row_p && cid == cur_col_p);
                int is_current_row_cell = (rid == cur_row_p);
                int is_current_col_cell = (logical_col == cur_col_p / agg_count);
                if (is_current_cell) {
                    attron(COLOR_PAIR(2)); // яркая текущая ячейка
                } else if (is_current_row_cell || is_current_col_cell) {
                    attron(COLOR_PAIR(1)); // подсветка строки или столбца
                } else {
                    attron(COLOR_PAIR(8)); // обычный цвет
                }
                mvprintw(data_row_y + i, pivot_row_index_width + c * CELL_WIDTH,
                         "%*s", CELL_WIDTH - 2, disp);
                attroff(COLOR_PAIR(2) | COLOR_PAIR(1) | COLOR_PAIR(8));
            }
        }

        // ── Vertical divider + graph panel ────────────────────────────────────
        if (graph_split) {
            // Vertical divider: plain top at y=5, vlines 5..height-3, T-bottom at height-2
            attron(COLOR_PAIR(6));
            for (int y = 5; y <= height - 3; y++) mvaddch(y, table_w, ACS_VLINE);
            mvaddch(height - 2, table_w, ACS_BTEE);   // ┴ connects to bottom frame

            // Graph info on bottom frame, right of divider: [G:bar lin col→x]
            // '[' and 'G:' gray, values yellow, ']' gray
            const char *gtype_s  = (pivot_gtype  == GRAPH_BAR)  ? "bar"  :
                                   (pivot_gtype  == GRAPH_LINE) ? "line" : "dot";
            const char *gscale_s = (pivot_gscale == SCALE_LOG)  ? "log"  : "lin";
            const char *gaxis_s  = (graph_axis   == 0)          ? "row\xe2\x86\x92x" : "col\xe2\x86\x92x";
            int gix = table_w + 2;
            attron(COLOR_PAIR(6));
            mvprintw(height - 2, gix, "[G:");
            gix += 3;
            attroff(COLOR_PAIR(6));
            attron(COLOR_PAIR(3));
            mvprintw(height - 2, gix, "%s %s %s", gtype_s, gscale_s, gaxis_s);
            gix += (int)strlen(gtype_s) + 1 + (int)strlen(gscale_s) + 1 + (int)strlen(gaxis_s) + 2; // +2 for arrow bytes width diff
            attroff(COLOR_PAIR(3));
            attron(COLOR_PAIR(6));
            // find column after the printed text
            {
                int after_x = table_w + 2 + 3 + (int)strlen(gtype_s) + 1 + (int)strlen(gscale_s) + 1 + (int)strlen(gaxis_s);
                // gaxis_s contains UTF-8 arrow (→ = 3 bytes but 1 char wide)
                // compensate: printed width = strlen - 2 extra bytes
                after_x -= 2;
                mvaddch(height - 2, after_x, ']');
            }
            attroff(COLOR_PAIR(6));

            // Draw graph
            draw_pivot_graph(
                graph_x, height, graph_w,
                matrix, row_totals, col_totals, &grand,
                row_keys, unique_rows,
                col_keys, unique_cols,
                row_order, col_order,
                series_pinned,
                cur_row_p, cur_col_p / agg_count,
                graph_axis,
                pivot_gtype, pivot_gscale,
                agg_list[0],
                total_rows, max_logical_cols
            );
        }
        refresh();

        int ch = getch();
        if (ch == ':') {
            char cmd_buf[128] = {0};
            draw_status_bar(height - 1, 1, csv_filename, row_count, file_size_str);
            attron(COLOR_PAIR(3));
            printw(" | :");
            attroff(COLOR_PAIR(3));
            refresh();
            echo();
            wgetnstr(stdscr, cmd_buf, sizeof(cmd_buf) - 1);
            noecho();

            char *cmd = cmd_buf;
            char *arg = strchr(cmd_buf, ' ');
            if (arg) {
                *arg = '\0';
                arg++;
                while (*arg == ' ') arg++;
            }

            if (strcmp(cmd, "e") == 0) {
                char filename[256] = "pivot.csv";
                if (arg && *arg) strncpy(filename, arg, sizeof(filename) - 1);
                if (strstr(filename, ".csv") == NULL) strcat(filename, ".csv");

                FILE *out = fopen(filename, "w");
                if (out) {
                    // Header
                    fprintf(out, "Row");
                    for (int c = 0; c < total_cols; c++) {
                        int lc = c / agg_count;
                        int as = c % agg_count;
                        char *key = (lc < unique_cols) ? col_keys[col_order[lc]] : "Total";
                        if (agg_count > 1)
                            fprintf(out, ",%s_%s", key, agg_list[as]);
                        else
                            fprintf(out, ",%s", key);
                    }
                    fprintf(out, "\n");

                    // Body
                    for (int r = 0; r < total_rows; r++) {
                        int ar = (r < unique_rows) ? row_order[r] : unique_rows;
                        char *rkey = (r < unique_rows) ? row_keys[ar] : "Total";
                        fprintf(out, "%s", rkey);
                        for (int c = 0; c < total_cols; c++) {
                            int lc = c / agg_count;
                            int as = c % agg_count;
                            int ac = (lc < unique_cols) ? col_order[lc] : unique_cols;
                            const Agg *agg;
                            if (r < unique_rows && lc < unique_cols) agg = &matrix[ar][ac];
                            else if (r < unique_rows && lc == unique_cols) agg = &row_totals[ar];
                            else if (r == unique_rows && lc < unique_cols) agg = &col_totals[ac];
                            else agg = &grand;
                            char *disp = get_agg_display(agg, agg_list[as], value_type);
                            fprintf(out, ",%s", disp);
                        }
                        fprintf(out, "\n");
                    }
                    fclose(out);
                    mvprintw(height - 1, 0, "Exported to %s", filename);
                    refresh();
                    getch();
                }
            } else if (strcmp(cmd, "q") == 0) {
                break;
            } else if (strcmp(cmd, "o") == 0) {
                show_pivot_settings_window(settings, csv_filename, height, width);
                break;
            } else if (strcmp(cmd, "gt") == 0 && arg) {
                if (strcmp(arg, "bar")  == 0) pivot_gtype = GRAPH_BAR;
                else if (strcmp(arg, "line") == 0) pivot_gtype = GRAPH_LINE;
                else if (strcmp(arg, "dot")  == 0) pivot_gtype = GRAPH_DOT;
            } else if (strcmp(cmd, "gs") == 0 && arg) {
                if (strcmp(arg, "log") == 0) pivot_gscale = SCALE_LOG;
                else if (strcmp(arg, "lin") == 0) pivot_gscale = SCALE_LINEAR;
            }
            clrtoeol();
            refresh();
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (cur_row_p < total_rows - 1) cur_row_p++;
            if (cur_row_p >= top_row + vis_rows) top_row++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (cur_row_p > 0) cur_row_p--;
            if (cur_row_p < top_row) top_row = cur_row_p;
        } else if (ch == KEY_RIGHT || ch == 'l') {
            if (cur_col_p < total_cols - 1) cur_col_p++;
            if (cur_col_p >= left_col_p + vis_cols) left_col_p++;
        } else if (ch == KEY_LEFT || ch == 'h') {
            if (cur_col_p > 0) cur_col_p--;
            if (cur_col_p < left_col_p) left_col_p = cur_col_p;
        } 

        // H (большая) — на самый первый столбец
        else if (ch == 'H') {
            cur_col_p = 0;
            left_col_p = 0;
        }
        // L (большая) — на самый последний столбец
        else if (ch == 'L') {
            cur_col_p = total_cols - 1;
            // подстраиваем left_col_p так, чтобы последний столбец был виден
            left_col_p = cur_col_p - vis_cols + 1;
            if (left_col_p < 0) left_col_p = 0;
        }
        // Home → первая строка таблицы (не только видимая)
        else if (ch == KEY_HOME || ch == 'K') {
            cur_row_p = 0;
            top_row = 0;
        }
        // End → последняя строка таблицы
        else if (ch == KEY_END || ch == 'J') {
            cur_row_p = total_rows - 1;
            // подстраиваем top_row так, чтобы последняя строка была видна
            top_row = cur_row_p - vis_rows + 1;
            if (top_row < 0) top_row = 0;
        }
        // PageUp — страница вверх
        else if (ch == KEY_PPAGE) {
            int step = vis_rows - 1;
            if (step < 1) step = 1;
            cur_row_p -= step;
            if (cur_row_p < 0) cur_row_p = 0;
            top_row -= step;
            if (top_row < 0) top_row = 0;
        }
        // PageDown — страница вниз
        else if (ch == KEY_NPAGE) {
            int step = vis_rows - 1;
            if (step < 1) step = 1;
            cur_row_p += step;
            if (cur_row_p >= total_rows) cur_row_p = total_rows - 1;
            top_row += step;
            if (top_row > total_rows - vis_rows) top_row = total_rows - vis_rows;
            if (top_row < 0) top_row = 0;
        }
        // G — toggle graph split
        else if (ch == 'G') {
            graph_split = !graph_split;
            if (graph_split) {
                memset(series_pinned, 0, sizeof(series_pinned));
                left_col_p = 0;
            }
        }
        // Space — pin/unpin current series
        else if (ch == ' ') {
            if (graph_split) {
                int sidx = (graph_axis == 0) ? (cur_col_p / agg_count) : cur_row_p;
                int max_sidx = (graph_axis == 0) ? max_logical_cols : total_rows;
                if (sidx >= 0 && sidx < max_sidx && sidx < 512)
                    series_pinned[sidx] = !series_pinned[sidx];
            }
        }
        // a — toggle graph axis
        else if (ch == 'a') {
            if (graph_split) {
                graph_axis = !graph_axis;
                memset(series_pinned, 0, sizeof(series_pinned));
            }
        }
        // s — toggle lin/log scale
        else if (ch == 's') {
            if (graph_split)
                pivot_gscale = (pivot_gscale == SCALE_LINEAR) ? SCALE_LOG : SCALE_LINEAR;
        }
        // Enter — drill-down: возврат в основную таблицу с фильтром по текущей ячейке
        else if (ch == '\n' || ch == KEY_ENTER) {
            char flt[512] = "";

            // Фильтр по строке (row group)
            if (settings->row_group_col && *settings->row_group_col && cur_row_p < unique_rows) {
                char *rkey = row_keys[row_order[cur_row_p]];
                snprintf(flt, sizeof(flt), "%s = \"%s\"", settings->row_group_col, rkey);
            }

            // Фильтр по столбцу (col group)
            int logical_cur_col = cur_col_p / agg_count;
            if (settings->col_group_col && *settings->col_group_col && logical_cur_col < unique_cols) {
                char *ckey = col_keys[col_order[logical_cur_col]];
                if (flt[0]) {
                    char tmp[512];
                    snprintf(tmp, sizeof(tmp), "%s AND %s = \"%s\"",
                             flt, settings->col_group_col, ckey);
                    strncpy(flt, tmp, sizeof(flt) - 1);
                } else {
                    snprintf(flt, sizeof(flt), "%s = \"%s\"",
                             settings->col_group_col, ckey);
                }
            }

            if (flt[0]) {
                strncpy(pivot_drilldown_filter, flt, sizeof(pivot_drilldown_filter) - 1);
                pivot_drilldown_filter[sizeof(pivot_drilldown_filter) - 1] = '\0';
                break;
            }
            // Enter на Total-строке/столбце — игнорируем
        }
        else if (ch == 'q' || ch == 27) {
            break;
        }
    }

    // Free memory
    for (int i = 0; i < unique_rows; i++) {
        for (int j = 0; j < unique_cols; j++) {
            free(matrix[i][j].min_str);
            free(matrix[i][j].max_str);
        }
        free(matrix[i]);
    }
    free(matrix);

    for (int i = 0; i < unique_rows; i++) {
        free(row_totals[i].min_str);
        free(row_totals[i].max_str);
    }
    free(row_totals);

    for (int i = 0; i < unique_cols; i++) {
        free(col_totals[i].min_str);
        free(col_totals[i].max_str);
    }
    free(col_totals);

    free(grand.min_str);
    free(grand.max_str);

    for (int i = 0; i < unique_rows; i++) free(row_keys[i]);
    free(row_keys);

    for (int i = 0; i < unique_cols; i++) free(col_keys[i]);
    free(col_keys);

    free(row_order);
    free(col_order);
}