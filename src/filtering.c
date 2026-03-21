/**
 * filtering.c
 *
 * Модуль фильтрации строк в таблице CSV.
 */

#include "csvview_defs.h"
#include "utils.h"
#include "filtering.h"
#include "ui_draw.h"
#include "csv_mmap.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

/* ── Parallel filter ───────────────────────────────────────────── */

#define FILTER_PAR_THRESHOLD 50000

/* Precomputed state for old-syntax filter (R: / C[name]:) */
typedef struct {
    int  is_column_filter;
    int  target_col_num;
    char search_str[256];
    int  is_exact;
    int  valid;          /* 1 = successfully parsed */
} OldSyntaxState;

/* Per-thread work unit */
typedef struct {
    int            start_row;
    int            end_row;
    /* new syntax */
    int            use_new_syntax;
    FilterExpr    *expr;           /* shared read-only */
    /* old syntax */
    OldSyntaxState old;
    /* output */
    int           *matches;
    int            count;
} FilterWorker;

/* Parse old-syntax filter query into OldSyntaxState (called once before threads) */
static OldSyntaxState parse_old_syntax(const char *fq)
{
    OldSyntaxState s = {0};
    s.target_col_num = -1;

    if (strncmp(fq, "C[", 2) == 0) {
        s.is_column_filter = 1;
        char target_col[256] = "";
        const char *end = strchr(fq + 2, ']');
        if (!end) return s;
        int len = (int)(end - (fq + 2));
        if (len >= 256) len = 255;
        strncpy(target_col, fq + 2, len);
        target_col[len] = '\0';
        const char *sr = end + 1;
        if (*sr == ':') sr++;

        /* Copy search string */
        if (*sr == '"' || *sr == '\'') {
            char quote = *sr++;
            const char *eq = strchr(sr, quote);
            if (eq) {
                int slen = (int)(eq - sr);
                if (slen >= 256) slen = 255;
                strncpy(s.search_str, sr, slen);
                s.search_str[slen] = '\0';
                s.is_exact = 1;
            } else {
                strncpy(s.search_str, sr, 255); s.search_str[255] = '\0';
            }
        } else {
            strncpy(s.search_str, sr, 255); s.search_str[255] = '\0';
        }

        if (use_headers) s.target_col_num = col_name_to_num(target_col);
        if (s.target_col_num < 0) s.target_col_num = col_to_num(target_col);
        s.valid = 1;

    } else if (strncmp(fq, "R:", 2) == 0) {
        const char *sr = fq + 2;
        if (*sr == '"' || *sr == '\'') {
            char quote = *sr++;
            const char *eq = strchr(sr, quote);
            if (eq) {
                int slen = (int)(eq - sr);
                if (slen >= 256) slen = 255;
                strncpy(s.search_str, sr, slen);
                s.search_str[slen] = '\0';
                s.is_exact = 1;
            } else {
                strncpy(s.search_str, sr, 255); s.search_str[255] = '\0';
            }
        } else {
            strncpy(s.search_str, sr, 255); s.search_str[255] = '\0';
        }
        s.valid = 1;
    }
    return s;
}

/* Thread-safe old-syntax match using precomputed OldSyntaxState */
static int old_syntax_match(const char *line, const OldSyntaxState *s)
{
    if (!s->valid) return 0;

    int field_count = 0;
    char **fields = parse_csv_line(line, &field_count);
    if (!fields) return 0;

    int match = 0;
    for (int c = 0; c < field_count && c < col_count && !match; c++) {
        const char *token = fields[c];
        if (s->is_column_filter) {
            if (c == s->target_col_num) {
                match = s->is_exact
                    ? (strcmp(token, s->search_str) == 0)
                    : (strcasestr_custom(token, s->search_str) != NULL);
            }
        } else {
            match = s->is_exact
                ? (strcmp(token, s->search_str) == 0)
                : (strcasestr_custom(token, s->search_str) != NULL);
        }
    }
    free_csv_fields(fields, field_count);
    return match;
}

static void *filter_worker_thread(void *arg)
{
    FilterWorker *w = arg;
    char buf[MAX_LINE_LEN];

    for (int r = w->start_row; r < w->end_row; r++) {
        const char *line;

        if (rows[r].line_cache) {
            line = rows[r].line_cache;
        } else {
            line = csv_mmap_get_line(rows[r].offset, buf, sizeof(buf));
            if (!line) continue;
        }
        if (!*line) continue;

        int match;
        if (w->use_new_syntax) {
            match = row_matches_filter(line, w->expr);
        } else {
            match = old_syntax_match(line, &w->old);
        }

        if (match) w->matches[w->count++] = r;
    }
    return NULL;
}

/* ── apply_filter ─────────────────────────────────────────────── */

void apply_filter(RowIndex *rows_arg, FILE *f_arg, int row_count_arg)
{
    (void)rows_arg;  /* rows global is used directly; parameter kept for API compatibility */
    filtered_count = 0;
    filter_active  = 0;

    if (strlen(filter_query) == 0) return;

    FilterExpr expr       = {0};
    int        new_syntax = 0;

    if (strpbrk(filter_query, "><=!(") ||
        strstr(filter_query, "AND") ||
        strstr(filter_query, "OR")  ||
        strstr(filter_query, "NOT") ||
        filter_query[0] == '!')
    {
        if (parse_filter_expression(filter_query, &expr) == 0)
            new_syntax = 1;
    }

    int start_row = use_headers ? 1 : 0;
    int total     = row_count_arg - start_row;

    /* ── Parallel path (requires mmap) ── */
    if (g_mmap_base && total > FILTER_PAR_THRESHOLD) {

        OldSyntaxState old_state = {0};
        if (!new_syntax) old_state = parse_old_syntax(filter_query);

        int nthreads = csv_num_threads();
        int chunk    = (total + nthreads - 1) / nthreads;

        FilterWorker *workers = calloc(nthreads, sizeof(FilterWorker));
        pthread_t    *tids    = malloc(nthreads * sizeof(pthread_t));

        for (int t = 0; t < nthreads; t++) {
            workers[t].start_row     = start_row + t * chunk;
            workers[t].end_row       = start_row + (t + 1) * chunk;
            if (workers[t].end_row > row_count_arg) workers[t].end_row = row_count_arg;
            workers[t].use_new_syntax = new_syntax;
            workers[t].expr           = &expr;
            workers[t].old            = old_state;

            int cap = workers[t].end_row - workers[t].start_row + 1;
            workers[t].matches = malloc(cap * sizeof(int));
            workers[t].count   = 0;

            pthread_create(&tids[t], NULL, filter_worker_thread, &workers[t]);
        }

        /* Spinner while threads run */
        spinner_tick();

        for (int t = 0; t < nthreads; t++)
            pthread_join(tids[t], NULL);

        spinner_clear();

        /* Merge results in row order (workers cover non-overlapping ranges in order) */
        char tmp[MAX_LINE_LEN];
        for (int t = 0; t < nthreads; t++) {
            for (int i = 0; i < workers[t].count && filtered_count < MAX_ROWS; i++) {
                int r = workers[t].matches[i];
                filtered_rows[filtered_count++] = r;
                /* Cache the matched line for display */
                if (!rows[r].line_cache) {
                    char *l = csv_mmap_get_line(rows[r].offset, tmp, sizeof(tmp));
                    if (l) rows[r].line_cache = strdup(l);
                }
            }
            free(workers[t].matches);
        }
        free(workers);
        free(tids);

    } else {
        /* ── Sequential fallback (no mmap or small file) ── */
        rewind(f_arg);
        char seq_buf[MAX_LINE_LEN];

        for (int r = 0; r < row_count_arg && filtered_count < MAX_ROWS; r++) {
            if (!fgets(seq_buf, sizeof(seq_buf), f_arg)) break;
            if (r < start_row) continue;
            if ((r - start_row) % 5000 == 0 && r > start_row) spinner_tick();

            seq_buf[strcspn(seq_buf, "\r\n")] = '\0';

            const char *line = rows[r].line_cache ? rows[r].line_cache : seq_buf;
            if (!*line) continue;

            int match = 0;
            if (new_syntax) {
                match = row_matches_filter(line, &expr);
            } else {
                /* Old syntax inline (original logic) */
                int is_column_filter = 0;
                char target_col[256] = "";
                char search_str_buf[256] = "";
                char *search_str = search_str_buf;
                int is_exact = 0;

                if (strncmp(filter_query, "C[", 2) == 0) {
                    is_column_filter = 1;
                    char *end = strchr(filter_query + 2, ']');
                    if (end) {
                        int len = (int)(end - (filter_query + 2));
                        strncpy(target_col, filter_query + 2, len);
                        target_col[len] = '\0';
                        search_str = end + 1;
                        if (*search_str == ':') search_str++;
                    }
                } else if (strncmp(filter_query, "R:", 2) == 0) {
                    search_str = filter_query + 2;
                } else {
                    continue;
                }

                if (*search_str == '"' || *search_str == '\'') {
                    char quote = *search_str;
                    search_str++;
                    char *end_quote = strchr(search_str, quote);
                    if (end_quote) {
                        int len = (int)(end_quote - search_str);
                        strncpy(search_str_buf, search_str, len);
                        search_str_buf[len] = '\0';
                        search_str = search_str_buf;
                        is_exact = 1;
                    }
                }

                int target_col_num = -1;
                if (is_column_filter) {
                    if (use_headers) target_col_num = col_name_to_num(target_col);
                    if (target_col_num < 0) target_col_num = col_to_num(target_col);
                }

                int field_count = 0;
                char **fields = parse_csv_line(line, &field_count);
                if (fields) {
                    for (int c = 0; c < field_count && c < col_count && !match; c++) {
                        const char *token = fields[c];
                        if (is_column_filter) {
                            if (c == target_col_num) {
                                if (is_exact) { if (strcmp(token, search_str) == 0) match = 1; }
                                else          { if (strcasestr_custom(token, search_str)) match = 1; }
                            }
                        } else {
                            if (is_exact) { if (strcmp(token, search_str) == 0) match = 1; }
                            else          { if (strcasestr_custom(token, search_str)) match = 1; }
                        }
                    }
                    free_csv_fields(fields, field_count);
                }
            }

            if (match) {
                filtered_rows[filtered_count++] = r;
                if (!rows[r].line_cache) rows[r].line_cache = strdup(seq_buf);
            }
        }
        spinner_clear();
    }

    if (new_syntax) free_filter_expr(&expr);
    filter_active = (filtered_count > 0);
}

/**
 * @brief Загружает сохранённые фильтры из файла <csv_filename>
 *
 * Файл ищется как <csv_filename> (без расширения .csvf).
 * Формат строки: "filter: запрос"
 *
 * @param csv_filename  Имя исходного CSV-файла
 */
void load_saved_filters(const char *csv_filename)
{
    char cfg_path[1024];
    snprintf(cfg_path, sizeof(cfg_path), "%s", csv_filename);

    FILE *fp = fopen(cfg_path, "r");
    if (!fp) {
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "filter: ", 8) == 0)
        {
            char *query = line + 8;

            // Проверяем дубликат
            int dup = 0;
            for (int j = 0; j < saved_filter_count; j++)
            {
                if (strcmp(saved_filters[j], query) == 0)
                {
                    dup = 1;
                    break;
                }
            }

            if (!dup && saved_filter_count < MAX_SAVED_FILTERS)
            {
                saved_filters[saved_filter_count++] = strdup(query);
            }
        }
    }

    fclose(fp);
}

/**
 * @brief Сохраняет текущий фильтр в файл <csv_filename>.csvf и в память
 *
 * @param csv_filename  Имя исходного CSV-файла
 * @param query         Строка фильтра для сохранения
 */
void save_filter(const char *csv_filename, const char *query)
{
    if (!query || !*query) {
        return;
    }

    char cfg_path[1024];
    snprintf(cfg_path, sizeof(cfg_path), "%s.csvf", csv_filename);

    FILE *fp = fopen(cfg_path, "a");
    if (!fp) {
        return;
    }

    fprintf(fp, "filter: %s\n", query);
    fclose(fp);

    // Добавляем в память (для быстрого доступа в UI)
    if (saved_filter_count < MAX_SAVED_FILTERS)
    {
        saved_filters[saved_filter_count++] = strdup(query);
    }
}
