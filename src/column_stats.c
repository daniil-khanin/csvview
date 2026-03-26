/**
 * column_stats.c
 *
 * Implementation of the column statistics window with calculation of all metrics
 */

#include "column_stats.h"
#include "utils.h"          // get_column_value, trim, col_letter, etc.
#include "csv_mmap.h"

#include <ncurses.h>        // newwin, mvwprintw, wrefresh, etc.
#include <stdlib.h>         // malloc, realloc, free, qsort
#include <string.h>         // strcpy, strncpy, strcmp, strlen
#include <stdio.h>          // snprintf, sscanf
#include <math.h>           // INFINITY, strtod
#include <limits.h>         // for INT_MAX/INT_MIN if needed

// ────────────────────────────────────────────────
// Helper structures (internal to this module only)
// ────────────────────────────────────────────────

typedef struct {
    char *value;
    long count;
} Freq;

typedef struct {
    char label[16];
    long count;
    int sort_key;           // for sorting: year*100 + month, etc.
} DateGroup;

#define MAX_UNIQUE 100000

/* Hash table for O(1) frequency lookup instead of O(n) linear scan */
#define FREQ_HASH_BITS 18
#define FREQ_HASH_SIZE (1 << FREQ_HASH_BITS)
#define FREQ_HASH_MASK (FREQ_HASH_SIZE - 1)

static unsigned int freq_hash_fn(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

/* Find or insert value in freqs[]. Returns 1 on success, 0 if table full. */
static int freq_lookup_insert(Freq *freqs, long *freq_count, long *ht, const char *val)
{
    unsigned int slot = freq_hash_fn(val) & FREQ_HASH_MASK;
    for (int probe = 0; probe < 512; probe++, slot = (slot + 1) & FREQ_HASH_MASK) {
        long idx = ht[slot];
        if (idx < 0) {
            /* Empty — insert new entry */
            if (*freq_count >= MAX_UNIQUE) return 0;
            idx = *freq_count;
            freqs[idx].value = strdup(val);
            freqs[idx].count = 1;
            ht[slot] = idx;
            (*freq_count)++;
            return 1;
        }
        if (strcmp(freqs[idx].value, val) == 0) {
            freqs[idx].count++;
            return 1;
        }
    }
    /* Fallback linear scan (shouldn't happen with good hash distribution) */
    for (long j = 0; j < *freq_count; j++) {
        if (strcmp(freqs[j].value, val) == 0) { freqs[j].count++; return 1; }
    }
    if (*freq_count < MAX_UNIQUE) {
        freqs[*freq_count].value = strdup(val);
        freqs[*freq_count].count = 1;
        (*freq_count)++;
    }
    return 1;
}

// ────────────────────────────────────────────────
// Main column statistics function
// ────────────────────────────────────────────────

void show_column_stats(int col_idx)
{
    // Validate column index
    if (col_idx < 0 || col_idx >= col_count) {
        return;
    }

    // Create a centered statistics window
    int sy = (LINES - STATS_H) / 2;
    int sx = (COLS - STATS_W) / 2;
    WINDOW *win = newwin(STATS_H, STATS_W, sy, sx);
    if (!win) {
        return;
    }
    wbkgd(win, COLOR_PAIR(1));

    scrollok(win, TRUE);
    wattron(win, COLOR_PAIR(6));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(6));

    // Window title
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 0, (STATS_W - 20) / 2, " Column Statistics ");
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // Column name
    char col_name[64];
    if (use_headers && column_names[col_idx]) {
        strncpy(col_name, column_names[col_idx], sizeof(col_name)-1);
        col_name[sizeof(col_name)-1] = '\0';
    } else {
        col_letter(col_idx, col_name);
    }

    mvwprintw(win, 1, 2, "Column: %s (%s)",
              col_name,
              col_types[col_idx] == COL_NUM ? "Number" :
              col_types[col_idx] == COL_DATE ? "Date" : "String");

    wrefresh(win);

    // Show current filter if one is active
    if (filter_active && filter_query[0] != '\0') {
        char filtered_str[128];
        snprintf(filtered_str, sizeof(filtered_str), "Filtered by: %s", filter_query);
        wattron(win, COLOR_PAIR(5));  // Filter input color (replace with your own)
        mvwprintw(win, 2, 2, "%s", filtered_str);
        wattroff(win, COLOR_PAIR(5));
    }

    // ────────────────────────────────────────────────
    // Calculate statistics — single pass over all visible rows
    // ────────────────────────────────────────────────

    long total_cells = row_count - (use_headers ? 1 : 0);
    if (filter_active) {
        total_cells = filtered_count;
    }

    long valid_count = 0;
    long empty_count = 0;
    double sum_val = 0.0;
    double min_val = INFINITY;
    double max_val = -INFINITY;

    // For numbers — store only valid values (for median and histogram)
    double *numeric_values = NULL;
    long numeric_capacity = 0;
    long numeric_count = 0;

    // For dates — frequency counts by month
    #define MAX_DATE_GROUPS 360
    DateGroup monthly_groups[MAX_DATE_GROUPS];
    int monthly_group_count = 0;
    int min_year = 9999, min_month = 99;
    int max_year = 0, max_month = 0;

    // Frequencies for top-10 and mode (always as strings)
    Freq *freqs = calloc(MAX_UNIQUE, sizeof(Freq));
    long freq_count = 0;
    /* Hash table for O(1) freq lookup (initialized to -1 = empty) */
    long *freq_ht = malloc((size_t)FREQ_HASH_SIZE * sizeof(long));
    if (freq_ht) memset(freq_ht, 0xFF, (size_t)FREQ_HASH_SIZE * sizeof(long));

    long count_processed = 0;
    int display_count = filter_active ? filtered_count : row_count;

    for (int i = 0; i < display_count; i++)
    {
        int real_row = get_real_row(i);
        if (real_row < 0 || real_row >= row_count) {
            continue;
        }

        /* Fast row access via mmap or fseek fallback */
        char mmap_buf[MAX_LINE_LEN];
        if (!rows[real_row].line_cache) {
            if (g_mmap_base) {
                char *ml = csv_mmap_get_line(rows[real_row].offset, mmap_buf, sizeof(mmap_buf));
                rows[real_row].line_cache = strdup(ml ? ml : "");
            } else {
                fseek(f, rows[real_row].offset, SEEK_SET);
                char *line = malloc(MAX_LINE_LEN + 1);
                if (fgets(line, MAX_LINE_LEN + 1, f)) {
                    line[strcspn(line, "\n")] = '\0';
                    rows[real_row].line_cache = line;
                } else {
                    free(line);
                    rows[real_row].line_cache = strdup("");
                }
            }
        }

        // Get cell value
        char *cell = get_column_value(rows[real_row].line_cache,
                                      column_names[col_idx] ? column_names[col_idx] : "",
                                      use_headers);

        // Empty or NULL cell
        if (!cell || !*cell || strcmp(trim(cell), "") == 0)
        {
            empty_count++;
            free(cell);
            count_processed++;
            continue;
        }

        valid_count++;

        // Value frequencies (always as string)
        if (freq_ht) {
            freq_lookup_insert(freqs, &freq_count, freq_ht, cell);
        } else {
            /* Fallback linear scan if ht alloc failed */
            int found = 0;
            for (long j = 0; j < freq_count; j++) {
                if (strcmp(freqs[j].value, cell) == 0) { freqs[j].count++; found = 1; break; }
            }
            if (!found && freq_count < MAX_UNIQUE) {
                freqs[freq_count].value = strdup(cell);
                freqs[freq_count].count = 1;
                freq_count++;
            }
        }

        // ── Type-specific processing ──
        if (col_types[col_idx] == COL_NUM)
        {
            char *endptr;
            double val = parse_double(cell, &endptr);
            if (endptr != cell && *endptr == '\0')
            {
                sum_val += val;
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;

                // Store for median and histogram
                if (numeric_count >= numeric_capacity)
                {
                    long new_cap = numeric_capacity ? numeric_capacity * 2 : 32768;
                    double *new_ptr = realloc(numeric_values, new_cap * sizeof(double));
                    if (new_ptr)
                    {
                        numeric_values = new_ptr;
                        numeric_capacity = new_cap;
                    }
                    else
                    {
                        goto skip_num_alloc;
                    }
                }
                numeric_values[numeric_count++] = val;
            }
        }
        else if (col_types[col_idx] == COL_DATE && strlen(cell) >= 7)
        {
            int year = 0, month = 0;
            if (sscanf(cell, "%d-%d", &year, &month) == 2 &&
                year >= 1900 && year <= 9999 && month >= 1 && month <= 12)
            {
                // Update global date range
                if (year < min_year || (year == min_year && month < min_month))
                {
                    min_year = year;
                    min_month = month;
                }
                if (year > max_year || (year == max_year && month > max_month))
                {
                    max_year = year;
                    max_month = month;
                }

                // Month key
                char label[16];
                snprintf(label, sizeof(label), "%04d-%02d", year, month);

                // Add or increment group counter
                int found = 0;
                for (int g = 0; g < monthly_group_count; g++)
                {
                    if (strcmp(monthly_groups[g].label, label) == 0)
                    {
                        monthly_groups[g].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && monthly_group_count < MAX_DATE_GROUPS)
                {
                    strcpy(monthly_groups[monthly_group_count].label, label);
                    monthly_groups[monthly_group_count].count = 1;
                    monthly_groups[monthly_group_count].sort_key = year * 100 + month;
                    monthly_group_count++;
                }
            }
        }

        free(cell);
        count_processed++;

        // Show processing progress (every 1% for performance)
        if (count_processed % (total_cells / 100 + 1) == 0)
        {
            int pct = (int)((count_processed * 100LL) / total_cells);
            mvwprintw(win, STATS_H - 3, 2, "Processing %ld / %ld (%d%%)", count_processed, total_cells, pct);
            wrefresh(win);
        }
    }

skip_num_alloc:

    // ────────────────────────────────────────────────
    // Sort frequencies (top-10) and date groups
    // ────────────────────────────────────────────────

    // Sort frequencies in descending order (bubble sort — simple and sufficient)
    for (long i = 0; i < freq_count - 1; i++)
    {
        for (long j = i + 1; j < freq_count; j++)
        {
            if (freqs[i].count < freqs[j].count)
            {
                Freq tmp = freqs[i];
                freqs[i] = freqs[j];
                freqs[j] = tmp;
            }
        }
    }

    // Sort monthly groups chronologically
    for (int i = 0; i < monthly_group_count - 1; i++)
    {
        for (int j = i + 1; j < monthly_group_count; j++)
        {
            if (monthly_groups[i].sort_key > monthly_groups[j].sort_key)
            {
                DateGroup tmp = monthly_groups[i];
                monthly_groups[i] = monthly_groups[j];
                monthly_groups[j] = tmp;
            }
        }
    }

    // ────────────────────────────────────────────────
    // Render left panel — numeric metrics and top-10
    // ────────────────────────────────────────────────

    int y = (filter_active && filter_query[0] != '\0') ? 4 : 3;
    const int label_width = 18;
    const int num_width   = 15;

    mvwprintw(win, y++, 2, "%-*s%*ld", label_width, "Total cells:", num_width, total_cells);
    mvwprintw(win, y++, 2, "%-*s%*ld (%.1f%%)", label_width, "Valid:", num_width, valid_count,
              total_cells > 0 ? (double)valid_count / total_cells * 100 : 0);
    mvwprintw(win, y++, 2, "%-*s%*ld", label_width, "Empty/Invalid:", num_width, empty_count);

    if (col_types[col_idx] == COL_NUM)
    {
        double median = 0.0;
        if (numeric_count > 0)
        {
            qsort(numeric_values, numeric_count, sizeof(double), compare_double);
            if (numeric_count % 2 == 1) {
                median = numeric_values[numeric_count / 2];
            } else {
                median = (numeric_values[numeric_count / 2 - 1] + numeric_values[numeric_count / 2]) / 2.0;
            }
        }

        long mode_count = freq_count > 0 ? freqs[0].count : 0;
        char mode_val[64] = "N/A";
        if (freq_count > 0) {
            strncpy(mode_val, freqs[0].value, sizeof(mode_val)-1);
            mode_val[sizeof(mode_val)-1] = '\0';
        }

        double mode_pct = valid_count > 0 ? (double)mode_count / valid_count * 100 : 0;

        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Sum:", num_width, sum_val);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Mean:", num_width,
                  valid_count > 0 ? sum_val / valid_count : 0);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Median:", num_width, median);
        mvwprintw(win, y++, 2, "%-*s%*.8s %ld (%.1f%%)", label_width, "Mode:", num_width,
                  mode_val, mode_count, mode_pct);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Min:", num_width,
                  min_val < INFINITY ? min_val : 0);
        mvwprintw(win, y++, 2, "%-*s%*.2f", label_width, "Max:", num_width,
                  max_val > -INFINITY ? max_val : 0);
    }

    // Top-10 values
    y += 1;
    mvwprintw(win, y++, 2, "Top 10 most frequent values:");

    long sum_top = 0;
    for (int t = 0; t < 10 && t < freq_count; t++)
    {
        sum_top += freqs[t].count;
        double pct = valid_count > 0 ? (double)freqs[t].count / valid_count * 100 : 0;
        mvwprintw(win, y++, 4, "%-*s %*ld (%.1f%%)",
                  20, freqs[t].value, 12, freqs[t].count, pct);
    }

    if (freq_count > 10)
    {
        double pct_others = valid_count > 0 ? (double)(valid_count - sum_top) / valid_count * 100 : 0;
        mvwprintw(win, y++, 4, "...");
        mvwprintw(win, y++, 4, "%-*s %*ld (%.1f%%)", 20, "Others",
                  12, valid_count - sum_top, pct_others);
    }

    // ────────────────────────────────────────────────
    // Right panel — histogram
    // ────────────────────────────────────────────────

    int hist_y = (filter_active && filter_query[0] != '\0') ? 4 : 3;
    int hist_x = LEFT_W + 3;

    mvwprintw(win, hist_y++, hist_x, "Histogram:");

    if (valid_count == 0)
    {
        mvwprintw(win, hist_y++, hist_x, "(No data to display)");
    }
    else if (col_types[col_idx] == COL_NUM && numeric_count > 0)
    {
        double range = max_val - min_val;
        if (range < 1e-9) range = 1.0;
        double bin_step = range / MAX_BINS;
        long bins[MAX_BINS] = {0};

        for (long k = 0; k < numeric_count; k++)
        {
            int bin_idx = (int)((numeric_values[k] - min_val) / bin_step);
            if (bin_idx < 0) bin_idx = 0;
            if (bin_idx >= MAX_BINS) bin_idx = MAX_BINS - 1;
            bins[bin_idx]++;
        }

        long max_bin = 0;
        for (int b = 0; b < MAX_BINS; b++) {
            if (bins[b] > max_bin) max_bin = bins[b];
        }
        if (max_bin == 0) max_bin = 1;

        int BAR_WIDTH = 35;
        char bar[BAR_WIDTH + 1];

        for (int b = 0; b < MAX_BINS; b++)
        {
            if (bins[b] == 0) continue;

            double low  = min_val + b * bin_step;
            double high = low + bin_step;

            int bar_len = (int)(BAR_WIDTH * (double)bins[b] / max_bin + 0.5);
            memset(bar, '#', bar_len);
            bar[bar_len] = '\0';

            mvwprintw(win, hist_y++, hist_x, "%12.2f → %12.2f |%-*s %8ld",
                      low, high, BAR_WIDTH, bar, bins[b]);
        }
    }
    else if (col_types[col_idx] == COL_DATE && monthly_group_count > 0)
    {
        // Determine grouping scale
        long total_months = (max_year - min_year) * 12LL + (max_month - min_month) + 1;
        int group_type = 0; // 0=months, 1=quarters, 2=years, 3=centuries

        if (total_months <= 20) {
            group_type = 0;
        } else if (total_months <= 80) {
            group_type = 1;
        } else if ((max_year - min_year + 1) <= 20) {
            group_type = 2;
        } else {
            group_type = 3;
        }

        // Aggregate groups
        DateGroup agg_groups[MAX_DATE_GROUPS];
        int agg_count = 0;
        memset(agg_groups, 0, sizeof(agg_groups));

        for (int m = 0; m < monthly_group_count; m++)
        {
            int year  = monthly_groups[m].sort_key / 100;
            int month = monthly_groups[m].sort_key % 100;

            char agg_label[16] = {0};
            int agg_sort_key = 0;

            if (group_type == 0) {
                strcpy(agg_label, monthly_groups[m].label);
                agg_sort_key = monthly_groups[m].sort_key;
            } else if (group_type == 1) {
                int q = (month - 1) / 3 + 1;
                snprintf(agg_label, sizeof(agg_label), "%04d Q%d", year, q);
                agg_sort_key = year * 100 + q;
            } else if (group_type == 2) {
                snprintf(agg_label, sizeof(agg_label), "%04d", year);
                agg_sort_key = year;
            } else {
                int century = year / 100;
                snprintf(agg_label, sizeof(agg_label), "%dXX", century);
                agg_sort_key = century;
            }

            int found = 0;
            for (int g = 0; g < agg_count; g++)
            {
                if (agg_groups[g].sort_key == agg_sort_key)
                {
                    agg_groups[g].count += monthly_groups[m].count;
                    found = 1;
                    break;
                }
            }

            if (!found && agg_count < MAX_DATE_GROUPS)
            {
                strcpy(agg_groups[agg_count].label, agg_label);
                agg_groups[agg_count].count = monthly_groups[m].count;
                agg_groups[agg_count].sort_key = agg_sort_key;
                agg_count++;
            }
        }

        // Sort aggregated groups
        for (int i = 0; i < agg_count - 1; i++)
        {
            for (int j = i + 1; j < agg_count; j++)
            {
                if (agg_groups[i].sort_key > agg_groups[j].sort_key)
                {
                    DateGroup tmp = agg_groups[i];
                    agg_groups[i] = agg_groups[j];
                    agg_groups[j] = tmp;
                }
            }
        }

        // Render aggregated groups
        long max_bin = 0;
        for (int g = 0; g < agg_count; g++) {
            if (agg_groups[g].count > max_bin) max_bin = agg_groups[g].count;
        }
        if (max_bin == 0) max_bin = 1;

        int BAR_WIDTH = 35;
        char bar[BAR_WIDTH + 1];

        for (int g = 0; g < agg_count; g++)
        {
            int bar_len = (int)(BAR_WIDTH * (double)agg_groups[g].count / max_bin + 0.5);
            memset(bar, '#', bar_len);
            bar[bar_len] = '\0';

            mvwprintw(win, hist_y++, hist_x, "%-12s |%-*s %8ld",
                      agg_groups[g].label, BAR_WIDTH, bar, agg_groups[g].count);
        }

        const char *group_name = (group_type == 0) ? "by month" :
                                 (group_type == 1) ? "by quarter" :
                                 (group_type == 2) ? "by year" : "by century";
        mvwprintw(win, hist_y++, hist_x, "(%s)", group_name);
    }
    else
    {
        mvwprintw(win, hist_y++, hist_x, "(Not applicable for this column type)");
    }

    // Close hint
    mvwprintw(win, STATS_H - 2, 2, "Press any key to close");
    wrefresh(win);

    // Wait for keypress
    wgetch(win);

    // ────────────────────────────────────────────────
    // Free memory
    // ────────────────────────────────────────────────

    free(numeric_values);

    for (long i = 0; i < freq_count; i++) {
        free(freqs[i].value);
    }
    free(freqs);
    free(freq_ht);

    delwin(win);
    touchwin(stdscr);
    refresh();
}