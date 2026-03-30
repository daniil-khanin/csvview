/**
 * table_edit.c
 *
 * Implementation of CSV table structure editing
 * Adding/removing columns, filling with values, rebuilding the header
 */

#include "table_edit.h"
#include "csvview_defs.h"     // global variables and constants
#include "utils.h"            // save_file, col_letter, etc.
#include "column_format.h"    // save_column_settings()
#include "formula.h"          // formula_compile / eval
#include "filtering.h"        // filtered_rows, filtered_count, filter_active
#include "sorting.h"          // sorted_rows, sorted_count, sort_col
#include "file_format.h"      // g_fmt, parse_row, build_row

#include <ncurses.h>          // main ncurses header: mvprintw, refresh, getch, LINES, COLOR_PAIR, etc.
#include <stdio.h>            // fopen, fprintf, rename
#include <stdlib.h>           // malloc, free, atoi
#include <string.h>           // strcpy, strncpy, strlen
#include <math.h>             // floor, fabs

// ────────────────────────────────────────────────
// Add a new column
// ────────────────────────────────────────────────

void add_column_and_save(int insert_pos, const char *new_name, const char *csv_filename)
{
    if (col_count >= MAX_COLS)
    {
        mvprintw(LINES - 1, 0, "Too many columns (max %d)", MAX_COLS);
        refresh();
        getch();
        return;
    }

    // Shift arrays to the right starting from insert_pos
    for (int i = col_count; i > insert_pos; i--)
    {
        column_names[i] = column_names[i - 1];
        col_types[i]    = col_types[i - 1];
        col_widths[i]   = col_widths[i - 1];
        col_formats[i]  = col_formats[i - 1];
    }

    // New column
    column_names[insert_pos] = strdup(new_name && *new_name ? new_name : "untitled");
    col_types[insert_pos]    = COL_STR;
    col_widths[insert_pos]   = 12;
    col_formats[insert_pos].truncate_len   = 0;
    col_formats[insert_pos].decimal_places = -1;
    col_formats[insert_pos].date_format[0] = '\0';

    col_count++;

    // Rewrite the file with the new column
    char temp_name[1024];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", csv_filename);

    FILE *out = fopen(temp_name, "w");
    if (!out)
    {
        mvprintw(LINES - 1, 0, "Cannot create temp file");
        refresh();
        getch();
        return;
    }

    for (int r = 0; r < row_count; r++)
    {
        char buf[MAX_LINE_LEN];
        const char *line = rows[r].line_cache ? rows[r].line_cache : "";
        if (!rows[r].line_cache)
        {
            if (fseek(f, rows[r].offset, SEEK_SET) == 0 &&
                fgets(buf, sizeof(buf), f))
            {
                buf[strcspn(buf, "\r\n")] = '\0';
                line = buf;
            }
        }

        int count = 0;
        char **fields = g_fmt->parse_row(line, &count);

        /* For CSV: parse_row returns old col_count-1 fields — insert the new slot.
           For NDJSON: parse_row already returns col_count fields (new key absent → ""). */
        if (count < col_count)
        {
            fields = realloc(fields, (size_t)col_count * sizeof(char *));
            for (int i = count; i > insert_pos; i--) fields[i] = fields[i - 1];
            const char *val = (r == 0 && use_headers && g_fmt->has_header_row)
                              ? new_name : "";
            fields[insert_pos] = strdup(val);
            count = col_count;
        }

        char *new_line = g_fmt->build_row(fields, count, column_names, col_types);
        free_csv_fields(fields, count);
        fprintf(out, "%s\n", new_line ? new_line : "");
        free(new_line);
    }

    fclose(out);

    // Replace the original file
    if (rename(temp_name, csv_filename) != 0)
    {
        mvprintw(LINES - 1, 0, "Failed to rename temp file");
        refresh();
        getch();
        return;
    }

    // Re-index offsets and cache
    free(rows);
    rows = NULL;
    row_count = 0;

    fclose(f);
    f = fopen(csv_filename, "r");
    if (!f)
    {
        mvprintw(LINES - 1, 0, "Failed to reopen file");
        refresh();
        getch();
        return;
    }

    char buf[MAX_LINE_LEN];
    long offset = 0;
    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    while (fgets(buf, sizeof(buf), f))
    {
        if (row_count >= MAX_ROWS) break;
        rows[row_count].offset = offset;
        rows[row_count].line_cache = NULL;
        offset += strlen(buf);
        row_count++;
    }

    // Save new settings
    save_column_settings(csv_filename);
    refresh();
}

// ────────────────────────────────────────────────
// Fill a column with values
// ────────────────────────────────────────────────

static void fill_progress(const char *msg) {
    mvprintw(LINES-1, 0, "%-*s", COLS-1, "");
    mvprintw(LINES-1, 0, ":cf %s", msg);
    refresh();
}

void fill_column(int col_idx, const char *arg, const char *csv_filename)
{
    if (col_idx < 0 || col_idx >= col_count)
    {
        mvprintw(LINES - 1, 0, "Invalid column");
        refresh();
        getch();
        return;
    }

    char value[256] = {0};
    int start = 0;
    int step = 1;

    // Parse the argument
    if (arg[0] == '"' && arg[strlen(arg)-1] == '"')
    {
        strncpy(value, arg + 1, strlen(arg) - 2);
        value[strlen(arg) - 2] = '\0';
    }
    else if (strncmp(arg, "num(", 4) == 0)
    {
        const char *p = arg + 4;
        const char *close = strchr(p, ')');
        if (close)
        {
            char tmp[64];
            strncpy(tmp, p, close - p);
            tmp[close - p] = '\0';

            char *comma = strchr(tmp, ',');
            if (comma)
            {
                *comma = '\0';
                start = atoi(tmp);
                step = atoi(comma + 1);
                if (step == 0) step = 1;
            }
            else
            {
                start = atoi(tmp);
                step = 1;
            }
        }
        else
        {
            start = 0;
            step = 1;
        }
    }
    else
    {
        /* ── Formula mode ────────────────────────────────────────────────── */
        Formula *fml = formula_compile(arg);
        if (formula_error(fml)) {
            mvprintw(LINES-1, 0, "Formula error: %s", formula_error(fml));
            refresh(); getch();
            formula_free(fml); return;
        }

        /* Build display-index map (needed for rank/pct per-row aggregates) */
        int  start_row = use_headers ? 1 : 0;
        int  all_n     = row_count - start_row;
        int *disp_rows;
        int  disp_count;
        int  disp_owns = 0;

        if (filter_active && filtered_count > 0) {
            disp_rows  = filtered_rows;
            disp_count = filtered_count;
        } else if (sort_col >= 0 && sorted_count > 0) {
            disp_rows  = sorted_rows;
            disp_count = sorted_count;
        } else {
            disp_rows  = malloc((size_t)all_n * sizeof(int));
            disp_count = all_n;
            disp_owns  = 1;
            for (int i = 0; i < all_n; i++) disp_rows[i] = start_row + i;
        }

        /* row → display_idx map (-1 if not in view) */
        int *row_to_disp = malloc((size_t)row_count * sizeof(int));
        if (!row_to_disp) {
            formula_free(fml);
            if (disp_owns) free(disp_rows);
            return;
        }
        for (int i = 0; i < row_count; i++) row_to_disp[i] = -1;
        for (int i = 0; i < disp_count; i++) row_to_disp[disp_rows[i]] = i;

        /* Precompute aggregates */
        mvprintw(LINES-1, 0, ":cf Computing aggregates...");
        refresh();
        if (formula_precompute(fml, disp_rows, disp_count, fill_progress) != 0) {
            mvprintw(LINES-1, 0, "Formula error: %s", formula_error(fml));
            refresh(); getch();
            formula_free(fml);
            if (disp_owns) free(disp_rows);
            free(row_to_disp); return;
        }

        /* Fill loop */
        int row_start = start_row;
        long done = 0, errors = 0;

        for (int r = row_start; r < row_count; r++) {
            /* lazy load — always needed for save even for rows outside the view */
            if (!rows[r].line_cache) {
                fseek(f, rows[r].offset, SEEK_SET);
                char ln_buf[MAX_LINE_LEN];
                if (fgets(ln_buf, sizeof(ln_buf), f)) {
                    ln_buf[strcspn(ln_buf, "\r\n")] = '\0';
                    rows[r].line_cache = strdup(ln_buf);
                } else {
                    rows[r].line_cache = strdup("");
                }
            }

            int di = row_to_disp[r];

            /* rows outside the current view are left unchanged — preserve original content */
            if (di < 0) {
                done++;
                continue;
            }

            char temp_val[64] = "";
            {
                double result;
                if (formula_eval_row(fml, r, di, rows[r].line_cache, &result) == 0) {
                    /* format: integer if no fractional part, otherwise up to 10 significant digits */
                    if (result == floor(result) && fabs(result) < 1e15)
                        snprintf(temp_val, sizeof(temp_val), "%.0f", result);
                    else
                        snprintf(temp_val, sizeof(temp_val), "%.10g", result);
                } else {
                    errors++;
                }
            }

            /* reconstruct the line with temp_val at col_idx */
            {
            int count = 0;
            char **fields = g_fmt->parse_row(rows[r].line_cache, &count);
            if (count <= col_idx) {
                fields = realloc(fields, (size_t)(col_idx + 1) * sizeof(char *));
                while (count <= col_idx) fields[count++] = strdup("");
            }
            free(fields[col_idx]);
            fields[col_idx] = strdup(temp_val);
            char *new_line = g_fmt->build_row(fields, count, column_names, col_types);
            free_csv_fields(fields, count);
            free(rows[r].line_cache);
            rows[r].line_cache = new_line ? new_line : strdup("");
            }

            done++;
            if (done % 500000 == 0) {
                mvprintw(LINES-1, 0, ":cf Filling... %ld rows", done);
                refresh();
            }
        }

        formula_free(fml);
        if (disp_owns) free(disp_rows);
        free(row_to_disp);

        if (errors > 0) {
            mvprintw(LINES-1, 0, "Done. %ld rows filled, %ld errors (div/0 or empty)", done, errors);
            refresh(); getch();
        }

        goto save_and_reindex;
    }

    // Fill the column (text / num modes)
    {
    int row_start = use_headers ? 1 : 0;
    int num = start;

    for (int r = row_start; r < row_count; r++)
    {
        // Lazy-load the row
        if (!rows[r].line_cache)
        {
            fseek(f, rows[r].offset, SEEK_SET);
            char line_buf[MAX_LINE_LEN];
            if (fgets(line_buf, sizeof(line_buf), f))
            {
                line_buf[strcspn(line_buf, "\r\n")] = '\0';
                rows[r].line_cache = strdup(line_buf);
            }
            else
            {
                rows[r].line_cache = strdup("");
            }
        }

        char temp_val[256];
        if (strncmp(arg, "num(", 4) == 0)
        {
            snprintf(temp_val, sizeof(temp_val), "%d", num);
            num += step;
        }
        else
        {
            strncpy(temp_val, value, sizeof(temp_val) - 1);
            temp_val[sizeof(temp_val) - 1] = '\0';
        }

        int count = 0;
        char **fields = g_fmt->parse_row(rows[r].line_cache, &count);

        /* Extend if row has fewer fields than expected */
        if (count <= col_idx)
        {
            fields = realloc(fields, (size_t)(col_idx + 1) * sizeof(char *));
            while (count <= col_idx) fields[count++] = strdup("");
        }

        free(fields[col_idx]);
        fields[col_idx] = strdup(temp_val);

        char *new_line = g_fmt->build_row(fields, count, column_names, col_types);
        free_csv_fields(fields, count);

        free(rows[r].line_cache);
        rows[r].line_cache = new_line ? new_line : strdup("");
    }
    } /* end text/num block */

    save_and_reindex:
    // Save changes to file
    if (save_file(csv_filename, f, rows, row_count) != 0)
    {
        mvprintw(LINES - 1, 0, "Failed to save file");
        refresh();
        getch();
        return;
    }

    // Re-index offsets and cache
    free(rows);
    rows = NULL;
    row_count = 0;

    fclose(f);
    f = fopen(csv_filename, "r");
    if (!f)
    {
        mvprintw(LINES - 1, 0, "Failed to reopen file");
        refresh();
        getch();
        return;
    }

    char buf[MAX_LINE_LEN];
    long offset = 0;
    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    while (fgets(buf, sizeof(buf), f))
    {
        if (row_count >= MAX_ROWS) break;
        rows[row_count].offset = offset;
        rows[row_count].line_cache = NULL;
        offset += strlen(buf);
        row_count++;
    }

    save_column_settings(csv_filename);
    refresh();
}

// ────────────────────────────────────────────────
// Delete a column
// ────────────────────────────────────────────────

void delete_column(int col_idx, const char *arg, const char *csv_filename)
{
    // Check that the cursor is on the expected column (if arg is provided)
    if (arg && *arg)
    {
        const char *current_name = (use_headers && column_names[col_idx]) ? column_names[col_idx] : "";
        if (strcmp(current_name, arg) != 0)
        {
            attron(COLOR_PAIR(1));
            mvprintw(LINES - 1, 0, "Cursor not on column '%s' — cannot delete %d", arg, col_idx);
            attroff(COLOR_PAIR(1));
            refresh();
            getch();
            clrtoeol();
            refresh();
            return;
        }
    }

    // Free the column name
    free(column_names[col_idx]);

    // Shift arrays to the left
    for (int i = col_idx; i < col_count - 1; i++)
    {
        column_names[i] = column_names[i + 1];
        col_types[i]    = col_types[i + 1];
        col_widths[i]   = col_widths[i + 1];
        col_formats[i]  = col_formats[i + 1];
    }

    col_count--;

    // Move cursor to the previous column
    if (cur_col > 0) cur_col--;

    // Rewrite the file without the deleted column
    char temp_name[1024];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", csv_filename);

    FILE *out = fopen(temp_name, "w");
    if (!out)
    {
        mvprintw(LINES - 1, 0, "Cannot create temp file");
        refresh();
        getch();
        return;
    }

    for (int r = 0; r < row_count; r++)
    {
        char buf[MAX_LINE_LEN];
        const char *line = rows[r].line_cache ? rows[r].line_cache : "";
        if (!rows[r].line_cache)
        {
            if (fseek(f, rows[r].offset, SEEK_SET) == 0 &&
                fgets(buf, sizeof(buf), f))
            {
                buf[strcspn(buf, "\r\n")] = '\0';
                line = buf;
            }
        }

        int count = 0;
        char **fields = g_fmt->parse_row(line, &count);

        /* For CSV: parse_row returns old (pre-delete) col_count fields — remove col_idx.
           For NDJSON: parse_row uses updated column_names[] so already excludes deleted key. */
        if (count > col_count)
        {
            free(fields[col_idx]);
            for (int i = col_idx; i < count - 1; i++) fields[i] = fields[i + 1];
            count--;
        }

        char *new_line = g_fmt->build_row(fields, count, column_names, col_types);
        free_csv_fields(fields, count);
        fprintf(out, "%s\n", new_line ? new_line : "");
        free(new_line);
    }

    fclose(out);

    // Replace the original file
    if (rename(temp_name, csv_filename) != 0)
    {
        mvprintw(LINES - 1, 0, "Failed to rename temp file");
        refresh();
        getch();
        return;
    }

    // Re-index offsets
    free(rows);
    rows = NULL;
    row_count = 0;

    fclose(f);
    f = fopen(csv_filename, "r");
    if (!f)
    {
        mvprintw(LINES - 1, 0, "Failed to reopen file");
        refresh();
        getch();
        return;
    }

    char buf[MAX_LINE_LEN];
    long offset = 0;
    rows = malloc((MAX_ROWS + 1) * sizeof(RowIndex));
    while (fgets(buf, sizeof(buf), f))
    {
        if (row_count >= MAX_ROWS) break;
        rows[row_count].offset = offset;
        rows[row_count].line_cache = NULL;
        offset += strlen(buf);
        row_count++;
    }

    save_column_settings(csv_filename);
    clrtoeol();
    refresh();
}

// ────────────────────────────────────────────────
// Rebuild the header row after column renames
// ────────────────────────────────────────────────

void rebuild_header_row(void)
{
    if (row_count == 0) return;
    g_fmt->rebuild_header();
}