/**
 * column_format.c
 *
 * Implementation of column formatting and display settings
 */

#define _XOPEN_SOURCE 700  /* strptime on Linux */

#include "column_format.h"
#include "utils.h"          // trim, col_letter
#include "file_format.h"    // g_fmt

#include <ncurses.h>
#include <stdio.h>          // snprintf
#include <stdlib.h>         // malloc, free, strtod
#include <string.h>         // strlen, strcpy, strncpy
#include <time.h>           // struct tm, strptime, strftime
#include <math.h>           // INFINITY (if needed)

// ────────────────────────────────────────────────
// Column format initialization
// ────────────────────────────────────────────────

void init_column_formats(void)
{
    for (int i = 0; i < col_count; i++)
    {
        col_formats[i].truncate_len   = 0;        // do not truncate strings
        col_formats[i].decimal_places = -1;       // auto for numbers
        col_formats[i].date_format[0] = '\0';     // no date format
        col_widths[i] = CELL_WIDTH;               // default width
    }
}

// ────────────────────────────────────────────────
// String truncation with "..." appended
// ────────────────────────────────────────────────

char *truncate_string(const char *str, int max_len)
{
    if (!str || max_len <= 0) {
        return strdup(str ? str : "");
    }

    size_t len = strlen(str);
    if (len <= (size_t)max_len) {
        return strdup(str);
    }

    char *result = malloc(max_len + 4); // +3 for "..." and \0
    if (!result) {
        return strdup(str);
    }

    strncpy(result, str, max_len);
    strcpy(result + max_len, "...");

    return result;
}

// ────────────────────────────────────────────────
// Number formatting
// ────────────────────────────────────────────────

char *format_number(const char *raw_str, int decimals)
{
    if (!raw_str || !*raw_str) {
        return strdup("0");
    }

    char *endptr;
    double num = parse_double(raw_str, &endptr);
    if (*endptr != '\0' && *endptr != '\n') {
        // Not a clean number — return as-is
        return strdup(raw_str);
    }

    char fmt[16];
    if (decimals < 0) {
        // Auto mode: up to 6 digits, no trailing zeros
        snprintf(fmt, sizeof(fmt), "%%.6g");
    } else {
        snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), fmt, num);

    // In auto mode, strip trailing zeros after the decimal point
    if (decimals < 0)
    {
        char *dot = strchr(buf, '.');
        if (dot)
        {
            char *end = buf + strlen(buf) - 1;
            while (end > dot && *end == '0') {
                *end-- = '\0';
            }
            if (*end == '.') {
                *end = '\0';
            }
        }
    }

    return strdup(buf);
}

// ────────────────────────────────────────────────
// Date formatting
// ────────────────────────────────────────────────

char *format_date(const char *date_str, const char *target_format)
{
    if (!date_str || !*date_str) {
        return strdup("");
    }

    if (!target_format || !*target_format) {
        return strdup(date_str);
    }

    struct tm tm = {0};
    const char *input_fmts[] = {
        "%Y-%m-%d",
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%d.%m.%Y",
        "%d.%m.%Y %H:%M",
        "%m/%d/%Y",
        "%Y/%m/%d",
        "%Y%m%d",
        "%Y-%m",
        "%m-%Y",
        NULL
    };

    /* Normalise ISO 8601 datetime: strip sub-seconds (.NNN) and timezone
       indicator (Z / +HH:MM) so strptime can parse it cleanly.
       Example: "1946-01-04T06:07:01.650Z" → "1946-01-04T06:07:01"         */
    char norm[64];
    strncpy(norm, date_str, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    if (strchr(norm, 'T')) {
        char *dot = strchr(norm, '.');
        if (dot) *dot = '\0';
        int nlen = (int)strlen(norm);
        if (nlen > 0 && (norm[nlen-1] == 'Z' || norm[nlen-1] == 'z'))
            norm[nlen-1] = '\0';
    }

    char *parsed_end = NULL;
    for (int i = 0; input_fmts[i]; i++)
    {
        memset(&tm, 0, sizeof(tm));
        /* Try the normalised string first, fall back to the original */
        parsed_end = strptime(norm, input_fmts[i], &tm);
        if (!parsed_end || !(*parsed_end == '\0' || isspace(*parsed_end)))
        {
            memset(&tm, 0, sizeof(tm));
            parsed_end = strptime(date_str, input_fmts[i], &tm);
        }
        /* Accept end-of-string, whitespace, 'T' (date part of ISO datetime),
           or '.' (sub-seconds after time) as valid termination. */
        if (parsed_end && (*parsed_end == '\0' || isspace(*parsed_end) ||
                           *parsed_end == 'T'  || *parsed_end == '.'))
        {
            char buf[64];
            strftime(buf, sizeof(buf), target_format, &tm);
            return strdup(buf);
        }
    }

    // Could not parse — return as-is
    return strdup(date_str);
}

// ────────────────────────────────────────────────
// Main cell formatting function
// ────────────────────────────────────────────────

char *format_cell_value(const char *raw_value, int col_idx)
{
    if (!raw_value) {
        return strdup("");
    }

    if (col_idx < 0 || col_idx >= col_count) {
        return strdup(raw_value);
    }

    char *formatted = NULL;

    switch (col_types[col_idx])
    {
        case COL_STR:
        {
            int len = col_formats[col_idx].truncate_len;
            if (len > 0) {
                formatted = truncate_string(raw_value, len);
            } else {
                formatted = strdup(raw_value);
            }
            break;
        }

        case COL_NUM:
        {
            int dec = col_formats[col_idx].decimal_places;
            formatted = format_number(raw_value, dec);
            break;
        }

        case COL_DATE:
        {
            const char *fmt = col_formats[col_idx].date_format;
            if (fmt && *fmt) {
                formatted = format_date(raw_value, fmt);
            } else {
                formatted = strdup(raw_value);
            }
            break;
        }

        default:
            formatted = strdup(raw_value);
    }

    return formatted ? formatted : strdup(raw_value);
}

// ────────────────────────────────────────────────
// Auto-detection of column types from a row sample
// ────────────────────────────────────────────────

#define AUTODETECT_SAMPLE_ROWS 200
#define AUTODETECT_THRESHOLD   0.90

void auto_detect_column_types(void)
{
    if (row_count == 0 || col_count == 0) return;

    int data_start = use_headers ? 1 : 0;
    int sample_end = data_start + AUTODETECT_SAMPLE_ROWS;
    if (sample_end > row_count) sample_end = row_count;
    int sample_size = sample_end - data_start;
    if (sample_size <= 0) return;

    // Supported date formats (for strptime and date_format string)
    const char *date_fmts[] = {
        "%Y-%m-%d",
        "%d.%m.%Y",
        "%d/%m/%Y",
        "%Y/%m/%d",
        "%Y-%m",
        NULL
    };
    int n_date_fmts = 5;

    for (int c = 0; c < col_count; c++) {
        int num_ok   = 0;
        int date_ok[5] = {0};
        int total    = 0;

        for (int r = data_start; r < sample_end; r++) {
            // Load line into cache if needed
            if (!rows[r].line_cache) {
                fseek(f, rows[r].offset, SEEK_SET);
                char line_buf[MAX_LINE_LEN];
                if (fgets(line_buf, sizeof(line_buf), f)) {
                    line_buf[strcspn(line_buf, "\r\n")] = '\0';
                    rows[r].line_cache = strdup(line_buf);
                } else {
                    rows[r].line_cache = strdup("");
                }
            }

            int field_count = 0;
            char **fields = g_fmt ? g_fmt->parse_row(rows[r].line_cache ? rows[r].line_cache : "", &field_count) : parse_csv_line(rows[r].line_cache, &field_count);
            if (!fields) continue;

            const char *val = (c < field_count) ? fields[c] : "";

            // Skip empty and null-like values
            int is_empty = (!val || !*val ||
                strcmp(val, "NA") == 0 || strcmp(val, "na") == 0 ||
                strcmp(val, "N/A") == 0 || strcmp(val, "n/a") == 0 ||
                strcmp(val, "null") == 0 || strcmp(val, "NULL") == 0 ||
                strcmp(val, "-") == 0);

            if (!is_empty) {
                total++;

                // Numeric check: parse_double + no leading zeros
                int leading_zero = (val[0] == '0' && val[1] != '\0' &&
                                    val[1] != '.' && val[1] != ',');
                if (!leading_zero) {
                    char *endptr;
                    parse_double(val, &endptr);
                    while (*endptr == ' ' || *endptr == '\t') endptr++;
                    if (*endptr == '\0') num_ok++;
                }

                // Date check (also accept 'T' for ISO 8601 datetime prefix)
                for (int d = 0; d < n_date_fmts; d++) {
                    struct tm tm = {0};
                    char *end = strptime(val, date_fmts[d], &tm);
                    if (end && (*end == '\0' || *end == ' ' || *end == 'T'))
                        date_ok[d]++;
                }
            }

            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
        }

        if (total == 0) continue;

        // Best matching date format
        int best_date_fmt = -1;
        int best_date_count = 0;
        for (int d = 0; d < n_date_fmts; d++) {
            if (date_ok[d] > best_date_count) {
                best_date_count = date_ok[d];
                best_date_fmt = d;
            }
        }

        // Dates take priority over numbers (e.g. "2026-01-15" parses as both)
        if (best_date_fmt >= 0 &&
            (double)best_date_count / total >= AUTODETECT_THRESHOLD) {
            col_types[c] = COL_DATE;
            strncpy(col_formats[c].date_format, date_fmts[best_date_fmt],
                    sizeof(col_formats[c].date_format) - 1);
            col_formats[c].date_format[sizeof(col_formats[c].date_format) - 1] = '\0';
        } else if ((double)num_ok / total >= AUTODETECT_THRESHOLD) {
            col_types[c] = COL_NUM;
        }
        // otherwise stays COL_STR
    }
}

void save_column_settings(const char *csv_filename)
{
    if (!csv_filename || !*csv_filename) {
        return;
    }

    char csvf_path[1024];
    snprintf(csvf_path, sizeof(csvf_path), "%s.csvf", csv_filename);

    FILE *fp = fopen(csvf_path, "w");
    if (!fp) {
        return;
    }

    // Global settings
    fprintf(fp, "use_headers:%d\n", use_headers);
    fprintf(fp, "col_count:%d\n", col_count);
    fprintf(fp, "freeze:%d\n", freeze_cols);
    fprintf(fp, "delimiter:%d\n", (int)(unsigned char)csv_delimiter);
    fprintf(fp, "skip_comments:%d\n", skip_comments);

    // Per-column settings
    for (int i = 0; i < col_count; i++)
    {
        char type_char = 'S';
        if (col_types[i] == COL_NUM)  type_char = 'N';
        if (col_types[i] == COL_DATE) type_char = 'D';

        fprintf(fp, "%d:%c:%d:%d:%s\n",
                i,
                type_char,
                col_formats[i].truncate_len,
                col_formats[i].decimal_places,
                col_formats[i].date_format);
    }

    // Hidden columns
    int has_hidden = 0;
    for (int i = 0; i < col_count; i++) if (col_hidden[i]) { has_hidden = 1; break; }
    if (has_hidden) {
        fprintf(fp, "hidden:");
        int first = 1;
        for (int i = 0; i < col_count; i++) {
            if (col_hidden[i]) {
                if (!first) fprintf(fp, ",");
                fprintf(fp, "%d", i);
                first = 0;
            }
        }
        fprintf(fp, "\n");
    }

    // Column widths
    fprintf(fp, "widths:");
    for (int i = 0; i < col_count; i++)
    {
        if (i > 0) fprintf(fp, ",");
        fprintf(fp, "%d", col_widths[i]);
    }
    fprintf(fp, "\n");

    // Saved filters (stored in the same file)
    for (int f = 0; f < saved_filter_count; f++)
    {
        if (saved_filters[f]) {
            fprintf(fp, "filter: %s\n", saved_filters[f]);
        }
    }

    // Bookmarks
    for (int i = 0; i < 26; i++) {
        if (bookmarks[i] >= 0)
            fprintf(fp, "mark: %c %d\n", 'a' + i, bookmarks[i]);
    }

    fclose(fp);
}

/* returns 1 if skip_comments was explicitly set in .csvf, 0 otherwise */
int preload_delimiter(const char *csv_filename)
{
    if (!csv_filename || !*csv_filename) return 0;
    char csvf_path[1024];
    snprintf(csvf_path, sizeof(csvf_path), "%s.csvf", csv_filename);
    FILE *fp = fopen(csvf_path, "r");
    if (!fp) return 0;
    char line[64];
    int found_delim = 0, found_skip = 0;
    while (fgets(line, sizeof(line), fp) && !(found_delim && found_skip)) {
        if (!found_delim && strncmp(line, "delimiter:", 10) == 0) {
            int d = atoi(line + 10);
            if (d > 0 && d < 256) csv_delimiter = (char)d;
            found_delim = 1;
        } else if (!found_skip && strncmp(line, "skip_comments:", 14) == 0) {
            skip_comments = atoi(line + 14) ? 1 : 0;
            found_skip = 1;
        }
    }
    fclose(fp);
    return found_skip;
}

int load_column_settings(const char *csv_filename)
{
    if (!csv_filename || !*csv_filename) {
        return 0;
    }

    char csvf_path[1024];
    snprintf(csvf_path, sizeof(csvf_path), "%s.csvf", csv_filename);

    FILE *fp = fopen(csvf_path, "r");
    if (!fp) {
        return 0; // file not found — keep defaults
    }

    // Reset hidden columns before loading
    memset(col_hidden, 0, sizeof(col_hidden));

    char line[256];
    int loaded_count = 0;
    int temp_use_headers = 0;

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = '\0';

        // Bookmarks
        if (strncmp(line, "mark: ", 6) == 0) {
            char lc; int rr;
            if (sscanf(line + 6, "%c %d", &lc, &rr) == 2 && lc >= 'a' && lc <= 'z')
                bookmarks[lc - 'a'] = rr;
            continue;
        }

        // Filters are loaded separately (as before)
        if (strncmp(line, "filter: ", 8) == 0)
        {
            if (saved_filter_count < MAX_SAVED_FILTERS) {
                saved_filters[saved_filter_count++] = strdup(line + 8);
            }
            continue;
        }

        // Column widths
        if (strncmp(line, "widths:", 7) == 0)
        {
            char *p = line + 7;
            int idx = 0;
            char *tok = strtok(p, ",");
            while (tok && idx < col_count)
            {
                col_widths[idx++] = atoi(tok);
                tok = strtok(NULL, ",");
            }
            continue;
        }

        // Global parameters
        if (strncmp(line, "use_headers:", 12) == 0)
        {
            temp_use_headers = atoi(line + 12);
        }
        else if (strncmp(line, "delimiter:", 10) == 0)
        {
            int d = atoi(line + 10);
            if (d > 0 && d < 256) csv_delimiter = (char)d;
        }
        else if (strncmp(line, "freeze:", 7) == 0)
        {
            int n = atoi(line + 7);
            if (n >= 0) freeze_cols = n;
        }
        else if (strncmp(line, "skip_comments:", 14) == 0)
        {
            skip_comments = atoi(line + 14) ? 1 : 0;
        }
        else if (strncmp(line, "hidden:", 7) == 0)
        {
            char *p = line + 7;
            char *tok = strtok(p, ",");
            while (tok) {
                int idx = atoi(tok);
                if (idx >= 0 && idx < MAX_COLS) col_hidden[idx] = 1;
                tok = strtok(NULL, ",");
            }
        }
        else if (strncmp(line, "col_count:", 10) == 0)
        {
            loaded_count = atoi(line + 10);
            // Do not break — continue reading global settings
            // (use_headers, freeze, delimiter, skip_comments). A mismatch in
            // col_count only means per-column formats cannot be applied.
        }
        else
        {
            // Column row format: idx:type:truncate:decimals:date_format
            int idx, truncate, decimals;
            char type_char, date_fmt[32] = {0};
            if (sscanf(line, "%d:%c:%d:%d:%31[^\n]",
                       &idx, &type_char, &truncate, &decimals, date_fmt) >= 4)
            {
                if (idx >= 0 && idx < col_count)
                {
                    col_types[idx] = (type_char == 'N') ? COL_NUM :
                                     (type_char == 'D') ? COL_DATE : COL_STR;
                    col_formats[idx].truncate_len   = truncate;
                    col_formats[idx].decimal_places = decimals;
                    strncpy(col_formats[idx].date_format, date_fmt,
                            sizeof(col_formats[idx].date_format) - 1);
                    col_formats[idx].date_format[sizeof(col_formats[idx].date_format) - 1] = '\0';
                }
            }
        }
    }

    fclose(fp);

    // Global settings (use_headers) are always applied — even when col_count does not match
    use_headers = temp_use_headers;

    if (loaded_count == col_count)
        return 1;   // full success: both global and per-column settings applied

    // col_count changed (e.g. due to skip_comments) — per-column formats were already
    // loaded in the loop above but may be only partially applied.
    // Return 2 = "partial load": no popup needed, but auto_detect should be run.
    return loaded_count == 0 ? 0 : 2;
}

/*
int show_column_setup(const char *csv_filename)
{
    int height, width;
    getmaxyx(stdscr, height, width);
    int win_top = 2;
    int win_height = height - 4;
    int win_width = width - 4;
    int vis_lines = win_height - 10; // space for headers + hints

    int top_item = 0;
    int cur_item = 0;
    int cur_field = 0; // 0 = type, 1 = format

    // Preview — array of raw values from the first data row
    char preview_values[MAX_COLS][MAX_LINE_LEN] = {{0}};
    int preview_valid = 0;

    // Load preview once on entry (first data row)
    if (row_count > (use_headers ? 1 : 0))
    {
        int preview_row = use_headers ? 1 : 0; // skip header if present

        if (!rows[preview_row].line_cache)
        {
            fseek(f, rows[preview_row].offset, SEEK_SET);
            char *line = malloc(MAX_LINE_LEN);
            if (fgets(line, MAX_LINE_LEN, f))
            {
                line[strcspn(line, "\r\n")] = '\0';
                rows[preview_row].line_cache = line;
            }
            else
            {
                rows[preview_row].line_cache = strdup("");
            }
        }

        // Parse the line using the new function
        int field_count = 0;
        char **fields = g_fmt ? g_fmt->parse_row(rows[preview_row].line_cache ? rows[preview_row].line_cache : "", &field_count) : parse_csv_line(rows[preview_row].line_cache, &field_count);

        if (fields && field_count > 0)
        {
            for (int c = 0; c < field_count && c < col_count; c++)
            {
                strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                preview_values[c][sizeof(preview_values[c]) - 1] = '\0';
            }
            preview_valid = 1;

            // Free memory
            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
        }
    }

    while (1)
    {
        clear();

        // Window border
        attron(COLOR_PAIR(6));
        draw_rounded_box_stdscr(win_top, 1, win_height - 1, win_width);
        attroff(COLOR_PAIR(6));

        // Table headers
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(win_top + 1, 3,   "Column");
        mvprintw(win_top + 1, 65,  "Type");
        mvprintw(win_top + 1, 82,  "Format");
        mvprintw(win_top + 1, 110, "Preview (first data row)");
        attroff(COLOR_PAIR(5) | A_BOLD);

        // Column list + preview output
        for (int i = 0; i < vis_lines && top_item + i < col_count; i++)
        {
            int idx = top_item + i;
            char *name = column_names[idx] ? column_names[idx] : "";

            char type_str[32];
            switch (col_types[idx]) {
                case COL_STR:  strcpy(type_str, "String"); break;
                case COL_NUM:  strcpy(type_str, "Number"); break;
                case COL_DATE: strcpy(type_str, "Date");   break;
                default:       strcpy(type_str, "???");    break;
            }

            char fmt_str[64] = "—";
            if (col_types[idx] == COL_STR) {
                if (col_formats[idx].truncate_len > 0) {
                    snprintf(fmt_str, sizeof(fmt_str), "length %d", col_formats[idx].truncate_len);
                }
            } else if (col_types[idx] == COL_NUM) {
                if (col_formats[idx].decimal_places < 0) {
                    strcpy(fmt_str, "auto");
                } else {
                    snprintf(fmt_str, sizeof(fmt_str), "%d decimals", col_formats[idx].decimal_places);
                }
            } else if (col_types[idx] == COL_DATE) {
                if (col_formats[idx].date_format[0]) {
                    strncpy(fmt_str, col_formats[idx].date_format, sizeof(fmt_str) - 1);
                    fmt_str[sizeof(fmt_str) - 1] = '\0';
                } else {
                    strcpy(fmt_str, "auto");
                }
            }

            // Preview — apply current format
            char preview[128] = "(no data)";
            if (preview_valid && idx < col_count && preview_values[idx][0])
            {
                char *formatted = format_cell_value(preview_values[idx], idx);
                strncpy(preview, formatted, sizeof(preview) - 1);
                preview[sizeof(preview) - 1] = '\0';
                free(formatted);

                // Truncate preview
                if (strlen(preview) > 60) {
                    preview[57] = '.';
                    preview[58] = '.';
                    preview[59] = '.';
                    preview[60] = '\0';
                }
            }

            if (idx == cur_item) {
                attron(A_REVERSE);
            }

            mvprintw(win_top + 3 + i, 3,   "%-60s", name);
            mvprintw(win_top + 3 + i, 65,  "%-15s", type_str);
            mvprintw(win_top + 3 + i, 82,  "%-25s", fmt_str);
            mvprintw(win_top + 3 + i, 110, "%-50s", preview);

            attroff(A_REVERSE);
        }

        // Hints
        int info_y = win_top + win_height - 7;
        attron(COLOR_PAIR(5));
        mvprintw(info_y, 3, "↑↓ — select column    ←→/Tab — Type ↔ Format");
        mvprintw(info_y + 1, 3, "S/N/D — quick type selection");
        mvprintw(info_y + 2, 3, "Enter — save and exit");
        mvprintw(info_y + 3, 3, "H — enable headers");
        mvprintw(info_y + 4, 3, "Esc/q — cancel");
        attroff(COLOR_PAIR(5));

        refresh();

        int ch = getch();

        if (ch == KEY_UP)
        {
            if (cur_item > 0) {
                cur_item--;
                if (cur_item < top_item) top_item--;
            }
        }
        else if (ch == KEY_DOWN)
        {
            if (cur_item < col_count - 1) {
                cur_item++;
                if (cur_item >= top_item + vis_lines) top_item++;
            }
        }
        else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t')
        {
            cur_field = !cur_field;
        }
        else if (ch == 's' || ch == 'S') { col_types[cur_item] = COL_STR; }
        else if (ch == 'n' || ch == 'N') { col_types[cur_item] = COL_NUM; }
        else if (ch == 'd' || ch == 'D') { col_types[cur_item] = COL_DATE; }
        else if (ch == '\n' || ch == KEY_ENTER)
        {
            if (cur_field == 1) // format editing
            {
                echo();
                curs_set(1);
                char buf[64] = "";
                const char *prompt = "";
                if (col_types[cur_item] == COL_STR) {
                    prompt = "Truncate to (0=all): ";
                    snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].truncate_len);
                } else if (col_types[cur_item] == COL_NUM) {
                    prompt = "Decimal places (-1=auto): ";
                    snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].decimal_places);
                } else if (col_types[cur_item] == COL_DATE) {
                    prompt = "Date format (%%Y-%%m-%%d etc.): ";
                    strncpy(buf, col_formats[cur_item].date_format, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                }

                mvprintw(win_top + 4 + (cur_item - top_item), 70, "%s", prompt);
                clrtoeol();
                mvprintw(win_top + 4 + (cur_item - top_item), 70 + strlen(prompt), "%s", buf);
                move(win_top + 4 + (cur_item - top_item), 70 + strlen(prompt) + strlen(buf));
                int pos = strlen(buf);
                int done = 0;

                while (!done)
                {
                    int key = getch();
                    if (key == '\n' || key == KEY_ENTER)
                    {
                        if (col_types[cur_item] == COL_STR) {
                            col_formats[cur_item].truncate_len = atoi(buf);
                        } else if (col_types[cur_item] == COL_NUM) {
                            col_formats[cur_item].decimal_places = atoi(buf);
                        } else if (col_types[cur_item] == COL_DATE) {
                            strncpy(col_formats[cur_item].date_format, buf,
                                    sizeof(col_formats[cur_item].date_format) - 1);
                            col_formats[cur_item].date_format[sizeof(col_formats[cur_item].date_format) - 1] = '\0';
                        }
                        done = 1;
                    }
                    else if (key == 27) { done = 1; }
                    else if (key == KEY_BACKSPACE || key == 127)
                    {
                        if (pos > 0) {
                            pos--;
                            buf[pos] = '\0';
                        }
                    }
                    else if (key >= 32 && key <= 126 && pos < 63)
                    {
                        buf[pos++] = (char)key;
                        buf[pos] = '\0';
                    }

                    mvprintw(win_top + 4 + (cur_item - top_item), 70, "%s", prompt);
                    clrtoeol();
                    printw("%s", buf);
                    move(win_top + 4 + (cur_item - top_item), 70 + strlen(prompt) + pos);
                    refresh();
                }
                noecho();
                curs_set(0);
            }
            else
            {
                save_column_settings(csv_filename);
                return 0;
            }
        }
        else if (ch == '1' || ch == '2')
        {
            use_headers = (ch == '1');
            save_column_settings(csv_filename);
            return 0;
        }
        else if (ch == 'h' || ch == 'H')
        {
            use_headers = 1;
            save_column_settings(csv_filename);
            return 0;
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q')
        {
            return 1;
        }
    }

    save_column_settings(csv_filename);
    return 0;
}
*/

int show_column_setup(const char *csv_filename)
{
    int height, width;
    getmaxyx(stdscr, height, width);
    int win_top = 2;
    int win_height = height - 4;
    int win_width = width - 4;
    int vis_lines = win_height - 10;

    int top_item = 0;
    int cur_item = 0;

    // Preview — raw values from the first data row (already parsed)
    char preview_values[MAX_COLS][MAX_LINE_LEN] = {{0}};
    int preview_valid = 0;

    // Load preview (once)
    if (row_count > (use_headers ? 1 : 0))
    {
        int preview_row = use_headers ? 1 : 0;
        if (!rows[preview_row].line_cache)
        {
            fseek(f, rows[preview_row].offset, SEEK_SET);
            char *line = malloc(MAX_LINE_LEN);
            if (fgets(line, MAX_LINE_LEN, f))
            {
                line[strcspn(line, "\r\n")] = '\0';
                rows[preview_row].line_cache = line;
            }
            else
            {
                rows[preview_row].line_cache = strdup("");
            }
        }

        int field_count = 0;
        char **fields = g_fmt ? g_fmt->parse_row(rows[preview_row].line_cache ? rows[preview_row].line_cache : "", &field_count) : parse_csv_line(rows[preview_row].line_cache, &field_count);
        if (fields)
        {
            for (int c = 0; c < field_count && c < col_count; c++)
            {
                strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                preview_values[c][sizeof(preview_values[c]) - 1] = '\0';
            }
            preview_valid = 1;

            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
        }
    }

    while (1)
    {
        clear();

        // Border
        attron(COLOR_PAIR(6));
        draw_rounded_box_stdscr(win_top, 1, win_height - 1, win_width);
        attroff(COLOR_PAIR(6));

        // Headers status
        attron(COLOR_PAIR(use_headers ? 3 : 2) | A_BOLD);
        mvprintw(win_top + 1, 3, "Headers: %s", use_headers ? "ON  [H to toggle]" : "OFF [H to toggle]");
        attroff(COLOR_PAIR(use_headers ? 3 : 2) | A_BOLD);

        // Current delimiter
        {
            const char *delim_name;
            switch (csv_delimiter) {
                case ',':  delim_name = "comma  ,"; break;
                case ';':  delim_name = "semicolon ;"; break;
                case '\t': delim_name = "tab \\t"; break;
                case '|':  delim_name = "pipe |"; break;
                default:   delim_name = "custom"; break;
            }
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(win_top + 1, 35, "Separator: %-12s [C to cycle]", delim_name);
            attroff(COLOR_PAIR(3) | A_BOLD);
        }

        // Column headers
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(win_top + 2, 3,   "Column");
        mvprintw(win_top + 2, 65,  "Type");
        mvprintw(win_top + 2, 82,  "Format");
        mvprintw(win_top + 2, 107, "Preview (first data row)");
        attroff(COLOR_PAIR(5) | A_BOLD);

        // Column list
        for (int i = 0; i < vis_lines && top_item + i < col_count; i++)
        {
            int idx = top_item + i;
            char *name = column_names[idx] ? column_names[idx] : "";

            char type_str[32];
            switch (col_types[idx]) {
                case COL_STR:  strcpy(type_str, "String"); break;
                case COL_NUM:  strcpy(type_str, "Number"); break;
                case COL_DATE: strcpy(type_str, "Date");   break;
                default:       strcpy(type_str, "???");    break;
            }

            char fmt_str[64] = "—";
            if (col_types[idx] == COL_STR) {
                if (col_formats[idx].truncate_len > 0) {
                    snprintf(fmt_str, sizeof(fmt_str), "length %d", col_formats[idx].truncate_len);
                }
            } else if (col_types[idx] == COL_NUM) {
                if (col_formats[idx].decimal_places < 0) strcpy(fmt_str, "auto");
                else snprintf(fmt_str, sizeof(fmt_str), "%d decimals", col_formats[idx].decimal_places);
            } else if (col_types[idx] == COL_DATE) {
                if (col_formats[idx].date_format[0]) {
                    strncpy(fmt_str, col_formats[idx].date_format, sizeof(fmt_str) - 1);
                    fmt_str[sizeof(fmt_str) - 1] = '\0';
                } else strcpy(fmt_str, "auto");
            }

            char preview[128] = "(no data)";
            if (preview_valid && idx < col_count && preview_values[idx][0])
            {
                char *formatted = format_cell_value(preview_values[idx], idx);
                strncpy(preview, formatted, sizeof(preview) - 1);
                preview[sizeof(preview) - 1] = '\0';
                free(formatted);
                if (strlen(preview) > 60) strcpy(preview + 57, "...");
            }

            if (idx == cur_item) attron(A_REVERSE);

            // Column name with hidden marker
            if (col_hidden[idx]) {
                attron(COLOR_PAIR(2));
                mvprintw(win_top + 4 + i, 3, "[H] %-56s", name);
                attroff(COLOR_PAIR(2));
            } else {
                mvprintw(win_top + 4 + i, 3, "    %-56s", name);
            }
            mvprintw(win_top + 4 + i, 65,  "%-15s", type_str);
            mvprintw(win_top + 4 + i, 82,  "%-25s", fmt_str);
            mvprintw(win_top + 4 + i, 107, "%-50s", preview);

            attroff(A_REVERSE);
        }

        // Hints (updated)
        char hint[128];
        snprintf(hint, sizeof(hint),
                 "[ ↑↓/jk move • S/N/D type • X hide/show • Enter edit • H headers • C separator • q/Esc save ]");

        int hint_len = strlen(hint);
        int hint_x = (win_width - hint_len) / 2;

        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(win_top + win_height - 2, hint_x, "%s", hint);
        attroff(COLOR_PAIR(1) | A_BOLD);

        refresh();

        int ch = getch();

        if (ch == KEY_UP || ch == 'k' || ch == 'K')
        {
            if (cur_item > 0) {
                cur_item--;
                if (cur_item < top_item) top_item--;
            }
        }
        else if (ch == KEY_DOWN || ch == 'j' || ch == 'J')
        {
            if (cur_item < col_count - 1) {
                cur_item++;
                if (cur_item >= top_item + vis_lines) top_item++;
            }
        }
        else if (ch == 's' || ch == 'S')
        {
            col_types[cur_item] = COL_STR;
        }
        else if (ch == 'n' || ch == 'N')
        {
            col_types[cur_item] = COL_NUM;
        }
        else if (ch == 'd' || ch == 'D')
        {
            col_types[cur_item] = COL_DATE;
        }
        else if (ch == '\n' || ch == KEY_ENTER)
        {
            // Enter format editing for the current column
            echo();
            curs_set(1);
            char buf[64] = "";
            const char *prompt = "";
            int max_input_len = 63;

            if (col_types[cur_item] == COL_STR) {
                prompt = "Length (0=all): ";
                snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].truncate_len);
            } else if (col_types[cur_item] == COL_NUM) {
                prompt = "Precisions (-1=auto): ";
                snprintf(buf, sizeof(buf), "%d", col_formats[cur_item].decimal_places);
            } else if (col_types[cur_item] == COL_DATE) {
                prompt = "Date format (ex: %Y-%m-%d): ";
                strncpy(buf, col_formats[cur_item].date_format, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
            } else {
                prompt = "Wrong type";
            }

            mvprintw(win_top + 4 + (cur_item - top_item), 82, "%s", prompt);
            clrtoeol();
            mvprintw(win_top + 4 + (cur_item - top_item), 82 + strlen(prompt), "%s", buf);
            move(win_top + 4 + (cur_item - top_item), 82 + strlen(prompt) + strlen(buf));
            int pos = strlen(buf);
            int done = 0;

            while (!done)
            {
                int key = getch();
                if (key == '\n' || key == KEY_ENTER)
                {
                    // Apply
                    if (col_types[cur_item] == COL_STR) {
                        col_formats[cur_item].truncate_len = atoi(buf);
                    } else if (col_types[cur_item] == COL_NUM) {
                        col_formats[cur_item].decimal_places = atoi(buf);
                    } else if (col_types[cur_item] == COL_DATE) {
                        strncpy(col_formats[cur_item].date_format, buf,
                                sizeof(col_formats[cur_item].date_format) - 1);
                        col_formats[cur_item].date_format[sizeof(col_formats[cur_item].date_format) - 1] = '\0';
                    }
                    done = 1;
                }
                else if (key == 27) { // Esc — cancel editing
                    done = 1;
                }
                else if (key == KEY_BACKSPACE || key == 127)
                {
                    if (pos > 0) {
                        pos--;
                        buf[pos] = '\0';
                    }
                }
                else if (key >= 32 && key <= 126 && pos < max_input_len)
                {
                    buf[pos++] = (char)key;
                    buf[pos] = '\0';
                }

                // Redraw input field
                mvprintw(win_top + 4 + (cur_item - top_item), 82, "%s", prompt);
                clrtoeol();
                printw("%s", buf);
                move(win_top + 4 + (cur_item - top_item), 82 + strlen(prompt) + pos);
                refresh();
            }

            noecho();
            curs_set(0);
        }
        else if (ch == 'x' || ch == 'X')
        {
            col_hidden[cur_item] = !col_hidden[cur_item];
            save_column_settings(csv_filename);
        }
        else if (ch == 'h' || ch == 'H')
        {
            use_headers = !use_headers;
            save_column_settings(csv_filename);

            // Reload preview for the new row
            memset(preview_values, 0, sizeof(preview_values));
            preview_valid = 0;
            if (row_count > (use_headers ? 1 : 0))
            {
                int preview_row = use_headers ? 1 : 0;
                if (!rows[preview_row].line_cache)
                {
                    fseek(f, rows[preview_row].offset, SEEK_SET);
                    char *pline = malloc(MAX_LINE_LEN);
                    if (fgets(pline, MAX_LINE_LEN, f))
                    {
                        pline[strcspn(pline, "\r\n")] = '\0';
                        rows[preview_row].line_cache = pline;
                    }
                    else
                    {
                        rows[preview_row].line_cache = strdup("");
                        free(pline);
                    }
                }
                int field_count = 0;
                char **fields = g_fmt ? g_fmt->parse_row(rows[preview_row].line_cache ? rows[preview_row].line_cache : "", &field_count) : parse_csv_line(rows[preview_row].line_cache, &field_count);
                if (fields)
                {
                    for (int c = 0; c < field_count && c < col_count; c++)
                    {
                        strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                        preview_values[c][sizeof(preview_values[c]) - 1] = '\0';
                    }
                    preview_valid = 1;
                    for (int k = 0; k < field_count; k++) free(fields[k]);
                    free(fields);
                }
            }
        }
        else if (ch == 'c' || ch == 'C')
        {
            // Delimiter cycle: , → ; → \t → | → ,
            switch (csv_delimiter) {
                case ',':  csv_delimiter = ';';  break;
                case ';':  csv_delimiter = '\t'; break;
                case '\t': csv_delimiter = '|';  break;
                default:   csv_delimiter = ',';  break;
            }
            save_column_settings(csv_filename);

            // Re-parse column names from row 0
            for (int i = 0; i < col_count; i++) {
                free(column_names[i]);
                column_names[i] = NULL;
            }
            col_count = 0;
            memset(col_hidden, 0, sizeof(col_hidden));

            // Clear row 0 cache so it is re-read fresh
            free(rows[0].line_cache);
            rows[0].line_cache = NULL;

            // Re-read row 0
            if (f) {
                fseek(f, rows[0].offset, SEEK_SET);
                char line_buf[MAX_LINE_LEN];
                if (fgets(line_buf, sizeof(line_buf), f)) {
                    line_buf[strcspn(line_buf, "\r\n")] = '\0';
                    rows[0].line_cache = strdup(line_buf);
                } else {
                    rows[0].line_cache = strdup("");
                }
            }

            // Parse headers with the new delimiter (CSV only)
            if (g_fmt && g_fmt->has_header_row && rows[0].line_cache) {
                int hdr_count = 0;
                char **hdr_fields = parse_csv_line(rows[0].line_cache, &hdr_count);
                if (hdr_fields) {
                    while (col_count < hdr_count && col_count < MAX_COLS) {
                        char *t = hdr_fields[col_count];
                        while (*t == ' ' || *t == '\t') t++;
                        column_names[col_count] = strdup(t);
                        col_types[col_count] = COL_STR;
                        col_count++;
                    }
                    free_csv_fields(hdr_fields, hdr_count);
                }
            }
            init_column_formats();

            // Update preview
            memset(preview_values, 0, sizeof(preview_values));
            preview_valid = 0;
            if (row_count > (use_headers ? 1 : 0)) {
                int preview_row = use_headers ? 1 : 0;
                // Clear preview row cache to re-read with the new delimiter
                free(rows[preview_row].line_cache);
                rows[preview_row].line_cache = NULL;
                if (f) {
                    fseek(f, rows[preview_row].offset, SEEK_SET);
                    char *pline = malloc(MAX_LINE_LEN);
                    if (pline && fgets(pline, MAX_LINE_LEN, f)) {
                        pline[strcspn(pline, "\r\n")] = '\0';
                        rows[preview_row].line_cache = pline;
                    } else {
                        free(pline);
                        rows[preview_row].line_cache = strdup("");
                    }
                }
                if (rows[preview_row].line_cache) {
                    int field_count = 0;
                    char **fields = g_fmt ? g_fmt->parse_row(rows[preview_row].line_cache ? rows[preview_row].line_cache : "", &field_count) : parse_csv_line(rows[preview_row].line_cache, &field_count);
                    if (fields) {
                        for (int c = 0; c < field_count && c < col_count; c++) {
                            strncpy(preview_values[c], fields[c], sizeof(preview_values[c]) - 1);
                        }
                        preview_valid = 1;
                        for (int k = 0; k < field_count; k++) free(fields[k]);
                        free(fields);
                    }
                }
            }
            // Signal to caller that the delimiter changed — a full reload is required
            save_column_settings(csv_filename);
            return 1;
        }
        else if (ch == 27 || ch == 'q' || ch == 'Q')
        {
            save_column_settings(csv_filename);
            return 0;
        }
    }

    save_column_settings(csv_filename);
    return 0;
}
