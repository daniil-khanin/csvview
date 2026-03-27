/**
 * graph_svg.c — SVG export of the current graph view.
 *
 * Exports exactly what the user sees: current zoom, hidden series,
 * grid settings, dual-Y axis, scatter vs line/bar/dot mode.
 * White background, print-ready palette.
 */

#define _XOPEN_SOURCE 700
#include "graph_svg.h"
#include "graph.h"
#include "csvview_defs.h"
#include "csv_mmap.h"
#include "sorting.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Colour palette (optimised for white background) ──────────────────── */
static const char *SVG_COLORS[7] = {
    "#0077bb", "#cc3311", "#009944", "#ee7733",
    "#aa3377", "#33bbee", "#888888"
};

/* ── Layout helpers ───────────────────────────────────────────────────── */
typedef struct {
    int w, h;           /* total SVG size */
    int ml, mr, mt, mb; /* margins */
    int pw, ph;         /* plot width / height */
} SvgLayout;

static SvgLayout make_layout(int w, int h, int dual_right)
{
    SvgLayout L;
    L.w  = w; L.h = h;
    L.ml = 72;
    L.mr = dual_right ? 75 : 24;
    L.mt = 40;
    L.mb = 58;
    L.pw = w - L.ml - L.mr;
    L.ph = h - L.mt - L.mb;
    return L;
}

static int svg_py_val(double val, double ymin, double ymax, int log_scale,
                      const SvgLayout *L)
{
    double norm;
    if (log_scale && ymin > 0 && ymax > 0) {
        double lmn = log10(ymin), lmx = log10(ymax);
        norm = (lmx == lmn) ? 0.5
             : (log10(val > 1e-300 ? val : 1e-300) - lmn) / (lmx - lmn);
    } else {
        norm = (ymax == ymin) ? 0.5 : (val - ymin) / (ymax - ymin);
    }
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    return L->mt + L->ph - (int)round(norm * L->ph);
}

/* ── Inline field extractor (mirrors get_field_graph in graph.c) ──────── */
static void svg_get_field(const char *line, int idx, char *buf, int bsz)
{
    buf[0] = '\0';
    if (!line || idx < 0) return;
    int field = 0;
    const char *p = line;
    int in_q = 0;
    while (*p && field < idx) {
        if (*p == '"') { in_q = !in_q; p++; continue; }
        if (!in_q && *p == csv_delimiter) field++;
        p++;
    }
    if (!*p || field != idx) return;
    char *out = buf, *end = buf + bsz - 1;
    in_q = (*p == '"');
    if (in_q) p++;
    while (*p && out < end) {
        if (in_q) {
            if (*p == '"') { if (*(p+1)=='"'){*out++='"';p+=2;continue;} break; }
        } else {
            if (*p == csv_delimiter || *p == '\n' || *p == '\r') break;
        }
        *out++ = *p++;
    }
    *out = '\0';
}

static double svg_parse_num(const char *s)
{
    if (!s || !*s) return 0;
    char tmp[64]; int i = 0;
    for (const char *p = s; *p && i < 63; p++, i++) {
        tmp[i] = (*p == ',') ? '.' : *p;  /* European decimal */
    }
    tmp[i] = '\0';
    return atof(tmp);
}

/* ── SVG primitives ───────────────────────────────────────────────────── */
static void write_header(FILE *out, int w, int h)
{
    fprintf(out,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n"
        "<rect width=\"%d\" height=\"%d\" fill=\"white\"/>\n",
        w, h, w, h, w, h);
}

static void write_footer(FILE *out)
{
    fprintf(out, "</svg>\n");
}

static void write_grid_and_border(FILE *out, const SvgLayout *L)
{
    if (graph_grid & 1) {
        for (int yi = 1; yi <= 2; yi++) {
            int gy = L->mt + (int)round((double)yi / 3.0 * L->ph);
            fprintf(out,
                "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                "stroke=\"#ccc\" stroke-width=\"1\"/>\n",
                L->ml, gy, L->ml + L->pw, gy);
        }
    }
    if (graph_grid & 2) {
        for (int xi = 1; xi <= 3; xi++) {
            int gx = L->ml + (int)round((double)xi / 4.0 * L->pw);
            fprintf(out,
                "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                "stroke=\"#ccc\" stroke-width=\"1\"/>\n",
                gx, L->mt, gx, L->mt + L->ph);
        }
    }
    /* Axis border */
    fprintf(out,
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
        "stroke=\"#999\" stroke-width=\"1\"/>\n"   /* Y axis */
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
        "stroke=\"#999\" stroke-width=\"1\"/>\n",  /* X axis */
        L->ml, L->mt, L->ml, L->mt + L->ph,
        L->ml, L->mt + L->ph, L->ml + L->pw, L->mt + L->ph);
}

static void write_y_labels(FILE *out, const SvgLayout *L,
                            double ymin, double ymax, int log_scale,
                            const char *color, int right_side)
{
    char buf[32];
    for (int yi = 0; yi < 4; yi++) {
        double frac = (double)yi / 3.0;
        int    gy   = L->mt + (int)round(frac * L->ph);
        double val;
        if (log_scale && ymin > 0 && ymax > 0) {
            double lmx = log10(ymax), lmn = log10(ymin);
            val = pow(10.0, lmx - frac * (lmx - lmn));
            snprintf(buf, sizeof(buf), "%.2e", val);
        } else {
            val = ymax - frac * (ymax - ymin);
            snprintf(buf, sizeof(buf), "%.3g", val);
        }
        if (!right_side) {
            fprintf(out,
                "<text x=\"%d\" y=\"%d\" fill=\"%s\" "
                "text-anchor=\"end\" font-family=\"monospace\" font-size=\"11\">%s</text>\n",
                L->ml - 5, gy + 4, color, buf);
        } else {
            fprintf(out,
                "<text x=\"%d\" y=\"%d\" fill=\"%s\" "
                "text-anchor=\"start\" font-family=\"monospace\" font-size=\"11\">%s</text>\n",
                L->ml + L->pw + 5, gy + 4, color, buf);
        }
    }
}

/* X labels for regular graph (row indices or 5 evenly spaced) */
static void write_x_labels_regular(FILE *out, const SvgLayout *L,
                                    int n_points)
{
    char buf[32];
    int n_labels = 6;
    for (int xi = 0; xi <= n_labels; xi++) {
        double frac = (double)xi / n_labels;
        int    gx   = L->ml + (int)round(frac * L->pw);
        int    idx  = (int)round(frac * (n_points - 1));
        snprintf(buf, sizeof(buf), "%d", idx + 1);
        fprintf(out,
            "<text x=\"%d\" y=\"%d\" fill=\"#666\" "
            "text-anchor=\"middle\" font-family=\"monospace\" font-size=\"11\">%s</text>\n",
            gx, L->mt + L->ph + 18, buf);
    }
}

/* X labels for scatter (actual X values) */
static void write_x_labels_scatter(FILE *out, const SvgLayout *L,
                                    double xmin, double xmax)
{
    char buf[32];
    for (int xi = 0; xi <= 4; xi++) {
        double frac = (double)xi / 4.0;
        double val  = xmin + frac * (xmax - xmin);
        int    gx   = L->ml + (int)round(frac * L->pw);
        snprintf(buf, sizeof(buf), "%g", val);
        fprintf(out,
            "<text x=\"%d\" y=\"%d\" fill=\"#666\" "
            "text-anchor=\"middle\" font-family=\"monospace\" font-size=\"11\">%s</text>\n",
            gx, L->mt + L->ph + 18, buf);
    }
}

static void write_legend(FILE *out, const SvgLayout *L)
{
    int lx = L->ml + 8, ly = L->mt + L->ph + 38;
    for (int s = 0; s < graph_col_count; s++) {
        if (graph_series_hidden[s]) continue;
        const char *color = SVG_COLORS[s % 7];
        char cn[24] = "";
        if (use_headers && column_names[graph_col_list[s]])
            snprintf(cn, sizeof(cn), "%.20s", column_names[graph_col_list[s]]);
        else
            col_letter(graph_col_list[s], cn);
        fprintf(out,
            "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"3\" fill=\"%s\"/>\n"
            "<text x=\"%d\" y=\"%d\" fill=\"#333\" "
            "font-family=\"monospace\" font-size=\"11\">%s</text>\n",
            lx, ly - 1, color,
            lx + 18, ly + 3, cn);
        lx += (int)strlen(cn) * 7 + 32;
        if (lx > L->ml + L->pw - 60) { lx = L->ml + 8; ly += 18; }
    }
}

/* ── Regular graph export ─────────────────────────────────────────────── */
static void export_regular_svg(FILE *out, const SvgLayout *L,
                                RowIndex *rows_arr, FILE *fp, int total_rows)
{
    /* Pre-scan: compute shared Y scale (mirrors csvview.c multi-series block) */
    double gmin = INFINITY, gmax = -INFINITY;
    double rmin = INFINITY, rmax = -INFINITY;
    int vis_idx = 0;

    /* Store per-series data for drawing */
    double *series_vals[10] = {0};
    int     series_n[10]    = {0};

    for (int s = 0; s < graph_col_count; s++) {
        if (graph_series_hidden[s]) continue;
        int pc = 0; bool agg = false; char dfmt[32];
        double *vals = extract_plot_values(graph_col_list[s], rows_arr, fp,
                                           total_rows, &pc, &agg, dfmt);
        if (!vals) { vis_idx++; continue; }

        /* Apply zoom */
        int zs = (graph_zoom_start > 0) ? graph_zoom_start : 0;
        int ze = (graph_zoom_end > 0 && graph_zoom_end <= pc) ? graph_zoom_end : pc;
        if (zs >= ze) { zs = 0; ze = pc; }
        double *vz = vals + zs;
        int     nz = ze - zs;

        int is_right = (graph_dual_yaxis && vis_idx > 0) ? 1 : 0;
        for (int i = 0; i < nz; i++) {
            if (is_right) {
                if (vz[i] < rmin) rmin = vz[i];
                if (vz[i] > rmax) rmax = vz[i];
            } else {
                if (vz[i] < gmin) gmin = vz[i];
                if (vz[i] > gmax) gmax = vz[i];
            }
        }
        series_vals[s] = vals;   /* full array, offset by zs when drawing */
        series_n[s]    = nz;
        vis_idx++;
    }

    if (isinf(gmin)) { gmin = 0; gmax = 1; }
    if (gmin == gmax) { gmax += 1; gmin -= 1; }

    /* For log scale: gmin must be > 0; find smallest positive value */
    if (graph_scale == SCALE_LOG && gmin <= 0) {
        double pos_min = INFINITY;
        for (int s = 0; s < graph_col_count; s++) {
            if (!series_vals[s]) continue;
            int zs2 = (graph_zoom_start > 0) ? graph_zoom_start : 0;
            double *vz2 = series_vals[s] + zs2;
            for (int i = 0; i < series_n[s]; i++) {
                if (vz2[i] > 0 && vz2[i] < pos_min) pos_min = vz2[i];
            }
        }
        gmin = isinf(pos_min) ? 1.0 : pos_min;
        if (gmax <= gmin) gmax = gmin * 10.0;
    }

    if (graph_dual_yaxis && !isinf(rmin)) {
        if (rmin == rmax) { rmax += 1; rmin -= 1; }
        if (graph_scale == SCALE_LOG && rmin <= 0) {
            double pos_rmin = INFINITY;
            for (int s = 0; s < graph_col_count; s++) {
                if (!series_vals[s]) continue;
                int zs2 = (graph_zoom_start > 0) ? graph_zoom_start : 0;
                double *vz2 = series_vals[s] + zs2;
                for (int i = 0; i < series_n[s]; i++) {
                    if (vz2[i] > 0 && vz2[i] < pos_rmin) pos_rmin = vz2[i];
                }
            }
            rmin = isinf(pos_rmin) ? 1.0 : pos_rmin;
            if (rmax <= rmin) rmax = rmin * 10.0;
        }
    } else {
        rmin = gmin; rmax = gmax;
    }

    write_grid_and_border(out, L);

    /* Y-axis labels */
    write_y_labels(out, L, gmin, gmax, graph_scale == SCALE_LOG, "#333", 0);
    if (graph_dual_yaxis && !isinf(rmin)) {
        /* right axis in color of first right-axis series */
        int rs = -1;
        for (int s = 0; s < graph_col_count; s++) {
            if (!graph_series_hidden[s]) { if (rs < 0) rs = -1; else { rs = s; break; } }
        }
        /* find second visible series */
        int cnt = 0;
        for (int s = 0; s < graph_col_count && rs < 0; s++) {
            if (!graph_series_hidden[s]) {
                if (++cnt == 2) rs = s;
            }
        }
        const char *rc = (rs >= 0) ? SVG_COLORS[rs % 7] : "#888";
        write_y_labels(out, L, rmin, rmax, graph_scale == SCALE_LOG, rc, 1);
    }

    /* Find n_points for X labels (first visible series) */
    int n_pts = 0;
    for (int s = 0; s < graph_col_count; s++) {
        if (!graph_series_hidden[s] && series_n[s] > 0) { n_pts = series_n[s]; break; }
    }
    write_x_labels_regular(out, L, n_pts > 0 ? n_pts : 1);

    /* Draw series */
    vis_idx = 0;
    for (int s = 0; s < graph_col_count; s++) {
        if (graph_series_hidden[s]) continue;
        double *vz   = series_vals[s];
        int     nz   = series_n[s];
        if (!vz || nz == 0) { vis_idx++; continue; }

        /* Apply zoom offset */
        int zs = (graph_zoom_start > 0) ? graph_zoom_start : 0;
        int ze_orig;
        {
            int pc_tmp = nz; /* already sliced */ (void)pc_tmp;
        }
        vz += zs; /* shift to zoom start — already done above, re-check */
        /* Actually series_vals[s] points to the full malloc'd array,
           series_n[s] is the zoomed count. Re-apply offset: */
        {
            int zs2 = (graph_zoom_start > 0) ? graph_zoom_start : 0;
            vz = series_vals[s] + zs2;
            ze_orig = nz;
            (void)ze_orig;
        }

        double ymin_s = (graph_dual_yaxis && vis_idx > 0) ? rmin : gmin;
        double ymax_s = (graph_dual_yaxis && vis_idx > 0) ? rmax : gmax;
        const char *color = SVG_COLORS[s % 7];

        /* Decimate to at most pw points */
        int step = nz / L->pw;
        if (step < 1) step = 1;
        int np = (nz + step - 1) / step;

        if (graph_type == GRAPH_LINE) {
            fprintf(out, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"1.5\" "
                    "stroke-linejoin=\"round\" points=\"", color);
            for (int i = 0; i < np; i++) {
                int di = i * step;
                if (di >= nz) di = nz - 1;
                int gx = L->ml + (int)round((double)i / (np - 1) * L->pw);
                int gy = svg_py_val(vz[di], ymin_s, ymax_s,
                                    graph_scale == SCALE_LOG, L);
                fprintf(out, "%d,%d ", gx, gy);
            }
            fprintf(out, "\"/>\n");

        } else if (graph_type == GRAPH_DOT) {
            fprintf(out, "<path fill=\"none\" stroke=\"%s\" stroke-width=\"2.5\" "
                    "stroke-linecap=\"round\" d=\"", color);
            for (int i = 0; i < np; i++) {
                int di = i * step;
                if (di >= nz) di = nz - 1;
                int gx = L->ml + (int)round((double)i / (np - 1) * L->pw);
                int gy = svg_py_val(vz[di], ymin_s, ymax_s,
                                    graph_scale == SCALE_LOG, L);
                fprintf(out, "M%d,%d h0 ", gx, gy);
            }
            fprintf(out, "\"/>\n");

        } else { /* GRAPH_BAR */
            int bar_w = L->pw / np;
            if (bar_w < 1) bar_w = 1;
            int half  = bar_w / 2;
            int base  = L->mt + L->ph;
            fprintf(out, "<g fill=\"%s\" opacity=\"0.8\">\n", color);
            for (int i = 0; i < np; i++) {
                int di = i * step;
                if (di >= nz) di = nz - 1;
                int gx = L->ml + (int)round((double)i / (np - 1) * L->pw);
                int gy = svg_py_val(vz[di], ymin_s, ymax_s,
                                    graph_scale == SCALE_LOG, L);
                int bh = base - gy;
                if (bh < 1) bh = 1;
                fprintf(out, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n",
                        gx - half, gy, bar_w > 1 ? bar_w - 1 : 1, bh);
            }
            fprintf(out, "</g>\n");
        }
        vis_idx++;
    }

    /* Cursor vertical line */
    if (show_graph_cursor && graph_cursor_pos >= 0 && n_pts > 0) {
        int cp  = graph_cursor_pos;
        if (cp >= n_pts) cp = n_pts - 1;
        int step2 = n_pts / L->pw; if (step2 < 1) step2 = 1;
        int np2   = (n_pts + step2 - 1) / step2;
        int gx    = L->ml + (int)round((double)cp / (np2 > 1 ? np2 - 1 : 1) * L->pw);
        fprintf(out,
            "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
            "stroke=\"#999\" stroke-width=\"1\" stroke-dasharray=\"4,3\"/>\n",
            gx, L->mt, gx, L->mt + L->ph);
    }

    /* Legend (multi-series) */
    if (graph_col_count > 1)
        write_legend(out, L);

    for (int s = 0; s < graph_col_count; s++)
        free(series_vals[s]);
}

/* ── Scatter export ───────────────────────────────────────────────────── */
static void export_scatter_svg(FILE *out, const SvgLayout *L,
                                RowIndex *rows_arr, FILE *fp, int total_rows)
{
    int x_col = graph_scatter_x_col;
    int start_row = use_headers ? 1 : 0;
    int display_count = filter_active ? filtered_count
                                       : (total_rows - start_row);
    if (display_count <= 0) return;

    /* Collect all (x, y) pairs for each Y series to find shared X range */
    double xmin = INFINITY, xmax = -INFINITY;

    typedef struct { double *xs; double *ys; int n; } Series;
    Series sv[10] = {{0}};
    int n_series = (graph_col_count > 0) ? graph_col_count : 1;

    for (int s = 0; s < n_series; s++) {
        if (graph_series_hidden[s]) continue;
        int y_col = graph_col_list[s];

        double *xs = malloc(display_count * sizeof(double));
        double *ys = malloc(display_count * sizeof(double));
        if (!xs || !ys) { free(xs); free(ys); continue; }

        int n = 0;
        char xb[64], yb[64], lb[MAX_LINE_LEN];
        for (int di = 0; di < display_count; di++) {
            int r = get_real_row(di + start_row);
            if (r < 0 || r >= total_rows) continue;
            const char *lp = csv_mmap_get_line(rows_arr[r].offset, lb, sizeof(lb));
            if (!lp) {
                /* fallback: fseek */
                if (fp) {
                    fseek(fp, rows_arr[r].offset, SEEK_SET);
                    if (!fgets(lb, sizeof(lb), fp)) continue;
                    lp = lb;
                } else continue;
            }
            svg_get_field(lp, x_col, xb, sizeof(xb));
            svg_get_field(lp, y_col, yb, sizeof(yb));
            if (!xb[0] || !yb[0]) continue;
            double xv = svg_parse_num(xb), yv = svg_parse_num(yb);
            xs[n] = xv; ys[n] = yv; n++;
            if (xv < xmin) xmin = xv;
            if (xv > xmax) xmax = xv;
        }
        sv[s].xs = xs; sv[s].ys = ys; sv[s].n = n;
    }

    if (isinf(xmin)) { xmin = 0; xmax = 1; }
    if (xmin == xmax) { xmax += 1; xmin -= 1; }

    /* Y range per-series or shared */
    double ymin = INFINITY, ymax = -INFINITY;
    for (int s = 0; s < n_series; s++) {
        if (!sv[s].xs) continue;
        for (int i = 0; i < sv[s].n; i++) {
            if (sv[s].ys[i] < ymin) ymin = sv[s].ys[i];
            if (sv[s].ys[i] > ymax) ymax = sv[s].ys[i];
        }
    }
    if (isinf(ymin)) { ymin = 0; ymax = 1; }
    if (ymin == ymax) { ymax += 1; ymin -= 1; }

    write_grid_and_border(out, L);

    /* Y labels */
    write_y_labels(out, L, ymin, ymax, 0, "#333", 0);

    /* X labels */
    write_x_labels_scatter(out, L, xmin, xmax);

    /* Axis column name labels */
    char xname[32] = "", yname[32] = "";
    if (use_headers && column_names[x_col])
        snprintf(xname, sizeof(xname), "%.28s", column_names[x_col]);
    else
        col_letter(x_col, xname);
    if (use_headers && column_names[graph_col_list[0]])
        snprintf(yname, sizeof(yname), "%.28s", column_names[graph_col_list[0]]);
    else
        col_letter(graph_col_list[0], yname);

    fprintf(out,
        "<text x=\"%d\" y=\"%d\" fill=\"#333\" font-weight=\"bold\" "
        "text-anchor=\"middle\" font-family=\"monospace\" font-size=\"12\">"
        "X: %s</text>\n",
        L->ml + L->pw / 2, L->mt + L->ph + 36, xname);
    fprintf(out,
        "<text x=\"%d\" y=\"%d\" fill=\"%s\" font-weight=\"bold\" "
        "font-family=\"monospace\" font-size=\"12\">Y: %s</text>\n",
        L->ml + 4, L->mt + 14, SVG_COLORS[0], yname);

    /* Draw points for each series */
    for (int s = 0; s < n_series; s++) {
        if (!sv[s].xs || sv[s].n == 0) continue;
        const char *color = SVG_COLORS[s % 7];

        /* Compute Pearson r */
        double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
        int n = sv[s].n;
        for (int i = 0; i < n; i++) {
            sx += sv[s].xs[i]; sy += sv[s].ys[i];
            sxy += sv[s].xs[i] * sv[s].ys[i];
            sx2 += sv[s].xs[i] * sv[s].xs[i];
            sy2 += sv[s].ys[i] * sv[s].ys[i];
        }
        double denom = sqrt(((double)n*sx2 - sx*sx) * ((double)n*sy2 - sy*sy));
        double r_corr = (denom > 0) ? ((double)n*sxy - sx*sy) / denom : 0;

        /* Pearson r label */
        char rbuf[32];
        snprintf(rbuf, sizeof(rbuf), "r=%.3f", r_corr);
        int rx = L->ml + L->pw - (int)strlen(rbuf) * 7 - 10;
        int ry = L->mt + 14 + s * 16;
        fprintf(out,
            "<text x=\"%d\" y=\"%d\" fill=\"%s\" font-weight=\"bold\" "
            "font-family=\"monospace\" font-size=\"12\">%s</text>\n",
            rx, ry, color, rbuf);

        /* Points as compact path: M x,y h0 */
        fprintf(out,
            "<path fill=\"none\" stroke=\"%s\" stroke-width=\"2\" "
            "stroke-linecap=\"round\" opacity=\"0.75\" d=\"", color);
        for (int i = 0; i < n; i++) {
            double nx = (sv[s].xs[i] - xmin) / (xmax - xmin);
            double ny = (sv[s].ys[i] - ymin) / (ymax - ymin);
            if (nx < 0 || nx > 1 || ny < 0 || ny > 1) continue;
            int gx = L->ml + (int)round(nx * L->pw);
            int gy = L->mt + L->ph - (int)round(ny * L->ph);
            fprintf(out, "M%d,%d h0 ", gx, gy);
        }
        fprintf(out, "\"/>\n");
    }

    /* Cursor vertical line */
    if (show_graph_cursor && graph_cursor_pos >= 0) {
        int cp = graph_cursor_pos;
        if (cp >= L->pw) cp = L->pw - 1;
        int gx = L->ml + cp;
        fprintf(out,
            "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
            "stroke=\"#aaa\" stroke-width=\"1\" stroke-dasharray=\"4,3\"/>\n",
            gx, L->mt, gx, L->mt + L->ph);
    }

    /* Legend for multi-series scatter */
    if (n_series > 1)
        write_legend(out, L);

    for (int s = 0; s < n_series; s++) {
        free(sv[s].xs);
        free(sv[s].ys);
    }
}

/* ── Public entry point ───────────────────────────────────────────────── */
int export_graph_svg(const char *filename, int svg_w, int svg_h,
                     RowIndex *rows_arr, FILE *fp, int total_rows)
{
    FILE *out = fopen(filename, "w");
    if (!out) return -1;

    int dual = graph_dual_yaxis && !graph_scatter_mode && graph_col_count > 1;
    SvgLayout L = make_layout(svg_w, svg_h, dual);

    write_header(out, svg_w, svg_h);

    /* Title: filename + graph type */
    const char *mode_str = graph_scatter_mode ? "scatter"
                         : (graph_type == GRAPH_BAR) ? "bar"
                         : (graph_type == GRAPH_DOT) ? "dot" : "line";
    fprintf(out,
        "<text x=\"%d\" y=\"22\" fill=\"#333\" font-weight=\"bold\" "
        "font-family=\"monospace\" font-size=\"13\">%s [%s]</text>\n",
        L.ml, filename, mode_str);

    if (graph_scatter_mode && graph_scatter_x_col >= 0) {
        export_scatter_svg(out, &L, rows_arr, fp, total_rows);
    } else {
        export_regular_svg(out, &L, rows_arr, fp, total_rows);
    }

    write_footer(out);
    fclose(out);
    return 0;
}
