/**
 * sorting.c
 *
 * Implementation of table row sorting by selected column
 */

#include "sorting.h"
#include "utils.h"          // get_column_value, col_name_to_num, etc.
#include "ui_draw.h"        // spinner_tick / spinner_clear
#include "csv_mmap.h"

#include <stdlib.h>         // qsort
#include <string.h>         // strcasecmp, strlen
#include <stdio.h>          // fseek, fgets (if needed)
#include <pthread.h>

// ────────────────────────────────────────────────
// Fast single-field CSV extractor without malloc
// ────────────────────────────────────────────────

static const char *get_field_fast(const char *line, int idx)
{
    static char buf[MAX_LINE_LEN];
    buf[0] = '\0';
    if (!line || idx < 0) return buf;

    int field = 0;
    const char *p = line;
    int in_quote = 0;

    // Skip fields until the desired one
    while (*p && field < idx) {
        if (*p == '"') { in_quote = !in_quote; p++; continue; }
        if (!in_quote && *p == csv_delimiter) field++;
        p++;
    }
    if (!*p || field != idx) return buf;

    // Extract the desired field
    char *out = buf;
    char *out_end = buf + sizeof(buf) - 1;
    in_quote = (*p == '"');
    if (in_quote) p++;

    while (*p && out < out_end) {
        if (in_quote) {
            if (*p == '"') {
                if (*(p + 1) == '"') { *out++ = '"'; p += 2; continue; }
                break; // closing quote
            }
        } else {
            if (*p == csv_delimiter || *p == '\n' || *p == '\r') break;
        }
        *out++ = *p++;
    }
    *out = '\0';
    return buf;
}

/* Thread-safe version: uses caller-provided buf instead of static buffer */
static void get_field_fast_ts(const char *line, int idx, char *buf, int buf_size)
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

    char *out     = buf;
    char *out_end = buf + buf_size - 1;
    in_quote = (*p == '"');
    if (in_quote) p++;

    while (*p && out < out_end) {
        if (in_quote) {
            if (*p == '"') {
                if (*(p + 1) == '"') { *out++ = '"'; p += 2; continue; }
                break;
            }
        } else {
            if (*p == csv_delimiter || *p == '\n' || *p == '\r') break;
        }
        *out++ = *p++;
    }
    *out = '\0';
}

// ────────────────────────────────────────────────
// Array of pre-computed sort keys
// ────────────────────────────────────────────────

typedef struct {
    union { double num; char *str; };
    int is_num;
} SortKey;

static SortKey *g_sort_keys_ml[MAX_SORT_LEVELS];
static int      g_sort_level_count_local = 0;
static int      g_sort_start = 0;

/* Multi-level comparator using pre-extracted keys */
static int compare_by_preextracted_ml(const void *pa, const void *pb)
{
    int ra = *(const int *)pa;
    int rb = *(const int *)pb;

    for (int lv = 0; lv < g_sort_level_count_local; lv++) {
        const SortKey *a = &g_sort_keys_ml[lv][ra - g_sort_start];
        const SortKey *b = &g_sort_keys_ml[lv][rb - g_sort_start];

        int result;
        if (a->is_num && b->is_num) {
            if (a->num < b->num) result = -1;
            else if (a->num > b->num) result =  1;
            else                      result =  0;
        } else {
            const char *sa = a->is_num ? "" : (a->str ? a->str : "");
            const char *sb = b->is_num ? "" : (b->str ? b->str : "");
            result = strcasecmp(sa, sb);
        }
        if (result != 0) return sort_levels[lv].order * result;
    }
    return 0;
}

/**
 * @brief Comparison function for two rows used by qsort (callback)
 *
 * Compares the values of two table rows in column sort_col.
 * Used exclusively as an argument to qsort().
 *
 * Comparison logic:
 *   1. The header row (index 0) is always considered "less than" all data rows (if use_headers == 1)
 *   2. Lazily loads rows into line_cache if not yet cached
 *   3. Retrieves cell values by column name (via get_column_value)
 *   4. Attempts numeric comparison (strtod) if both values are valid numbers
 *   5. Otherwise — case-insensitive lexicographic comparison (strcasecmp)
 *   6. Multiplies result by sort_order (1 = asc, -1 = desc)
 *
 * @param pa  Pointer to the index of the first row (int*)
 * @param pb  Pointer to the index of the second row (int*)
 * @return
 *   -1   — first row should come before the second
 *    0   — rows are equal in sort_col value
 *    1   — first row should come after the second
 *    (result is already multiplied by sort_order)
 *
 * @note
 *   - The function **lazily loads** rows into rows[].line_cache (strdup internally)
 *   - If cache could not be loaded — the row is treated as empty ("")
 *   - If sort_col is invalid or column name not found — compares empty strings
 *   - Allocates and frees memory for val_a / val_b (via get_column_value and strdup)
 *
 * @example
 *   // Example call via qsort (in build_sorted_index)
 *   qsort(sorted_rows, sorted_count, sizeof(int), compare_rows_by_column);
 *
 *   // If sort_col = 2, sort_order = 1, column "Price"
 *   // "10.5" < "100" -> numeric comparison -> -1
 *   // "apple" < "banana" -> string comparison -> -1
 *   // "Apple" < "banana" -> -1 (case-insensitive)
 *
 * @warning
 *   - Depends on global variables: sort_col, sort_order, rows, f, column_names,
 *     use_headers, col_count, MAX_LINE_LEN
 *   - May allocate significant memory on first call (if cache is empty)
 *   - DO NOT call directly — only via qsort()
 *   - If f == NULL or offsets are invalid -> possible file read errors
 *   - For very large files, lazy loading may slow down the first sort
 *
 * @see
 *   - build_sorted_index() — the only place this function is used
 *   - get_column_value() — retrieves a cell value
 *   - strcasecmp() — case-insensitive string comparison
 */

/* Multi-level slow comparator: reads from cache/mmap, used for filter-active sort */
static int compare_rows_multilevel(const void *pa, const void *pb)
{
    int ra = *(const int *)pa;
    int rb = *(const int *)pb;

    if (use_headers) {
        if (ra == 0) return -1;
        if (rb == 0) return 1;
    }

    char line_buf_a[MAX_LINE_LEN], line_buf_b[MAX_LINE_LEN];
    char *line_a = rows[ra].line_cache;
    if (!line_a) {
        if (g_mmap_base) {
            line_a = csv_mmap_get_line(rows[ra].offset, line_buf_a, sizeof(line_buf_a));
        } else if (f) {
            fseek(f, rows[ra].offset, SEEK_SET);
            if (fgets(line_buf_a, sizeof(line_buf_a), f)) {
                line_buf_a[strcspn(line_buf_a, "\r\n")] = '\0';
                line_a = line_buf_a;
            }
        }
        if (!line_a) line_a = "";
    }

    char *line_b = rows[rb].line_cache;
    if (!line_b) {
        if (g_mmap_base) {
            line_b = csv_mmap_get_line(rows[rb].offset, line_buf_b, sizeof(line_buf_b));
        } else if (f) {
            fseek(f, rows[rb].offset, SEEK_SET);
            if (fgets(line_buf_b, sizeof(line_buf_b), f)) {
                line_buf_b[strcspn(line_buf_b, "\r\n")] = '\0';
                line_b = line_buf_b;
            }
        }
        if (!line_b) line_b = "";
    }

    char val_a[256], val_b[256];
    for (int lv = 0; lv < sort_level_count; lv++) {
        int sc = sort_levels[lv].col;
        if (sc < 0 || sc >= col_count) continue;

        get_field_fast_ts(line_a, sc, val_a, sizeof(val_a));
        get_field_fast_ts(line_b, sc, val_b, sizeof(val_b));

        char *end_a, *end_b;
        double num_a = parse_double(val_a, &end_a);
        double num_b = parse_double(val_b, &end_b);
        int is_num_a = (end_a != val_a && *end_a == '\0');
        int is_num_b = (end_b != val_b && *end_b == '\0');

        int result;
        if (is_num_a && is_num_b) {
            if (num_a < num_b) result = -1;
            else if (num_a > num_b) result = 1;
            else result = 0;
        } else {
            result = strcasecmp(val_a, val_b);
        }
        if (result != 0) return sort_levels[lv].order * result;
    }
    return 0;
}

int compare_rows_by_column(const void *pa, const void *pb)
{
    // Extract row indices from qsort arguments
    int ra = *(const int *)pa;
    int rb = *(const int *)pb;

    // The header row (if present) is always considered "less than" all data rows
    // This guarantees that row 0 stays first
    if (use_headers)
    {
        if (ra == 0) return -1;
        if (rb == 0) return 1;
    }

    // Lazily load row A if cache is empty
    char *line_a = rows[ra].line_cache;
    if (!line_a)
    {
        fseek(f, rows[ra].offset, SEEK_SET);
        char buf[MAX_LINE_LEN];
        if (fgets(buf, sizeof(buf), f))
        {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            rows[ra].line_cache = strdup(buf);
            line_a = rows[ra].line_cache;
        }
        else
        {
            line_a = "";  // read error -> empty string
        }
    }

    // Lazily load row B
    char *line_b = rows[rb].line_cache;
    if (!line_b)
    {
        fseek(f, rows[rb].offset, SEEK_SET);
        char buf[MAX_LINE_LEN];
        if (fgets(buf, sizeof(buf), f))
        {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            rows[rb].line_cache = strdup(buf);
            line_b = rows[rb].line_cache;
        }
        else
        {
            line_b = "";
        }
    }

    // Determine the column name for comparison
    const char *col_name = (use_headers && sort_col < col_count && column_names[sort_col])
                           ? column_names[sort_col]
                           : "";

    // Get cell values (always new memory)
    char *val_a = get_column_value(line_a ? line_a : "", col_name, use_headers);
    char *val_b = get_column_value(line_b ? line_b : "", col_name, use_headers);

    // Guard against NULL (unlikely, but just in case)
    if (!val_a) val_a = strdup("");
    if (!val_b) val_b = strdup("");

    // Attempt to interpret as numbers
    char *end_a, *end_b;
    double num_a = parse_double(val_a, &end_a);
    double num_b = parse_double(val_b, &end_b);

    int is_num_a = (end_a != val_a && *end_a == '\0');
    int is_num_b = (end_b != val_b && *end_b == '\0');

    int result;
    if (is_num_a && is_num_b)
    {
        // Numeric comparison
        if (num_a < num_b)      result = -1;
        else if (num_a > num_b) result = 1;
        else                    result = 0;
    }
    else
    {
        // Case-insensitive string comparison
        result = strcasecmp(val_a, val_b);
    }

    // Free temporarily allocated memory
    free(val_a);
    free(val_b);

    // Apply sort direction (asc or desc)
    return sort_order * result;
}

typedef struct {
    int      start_r;
    int      start_ki;
    int      count;
    int      scol;
    SortKey *keys;
} SortKeyWorker;

static void *sort_key_worker(void *arg)
{
    SortKeyWorker *w = arg;
    char line_buf[MAX_LINE_LEN];
    char field_buf[MAX_LINE_LEN];

    for (int i = 0; i < w->count; i++) {
        int r  = w->start_r  + i;
        int ki = w->start_ki + i;

        const char *line;
        if (rows[r].line_cache) {
            line = rows[r].line_cache;
        } else if (g_mmap_base) {
            line = csv_mmap_get_line(rows[r].offset, line_buf, sizeof(line_buf));
            if (!line) { w->keys[ki].is_num = 1; w->keys[ki].num = 0; continue; }
        } else {
            w->keys[ki].is_num = 1; w->keys[ki].num = 0;
            continue;
        }

        get_field_fast_ts(line, w->scol, field_buf, sizeof(field_buf));

        char *end;
        double num = parse_double(field_buf, &end);
        if (end != field_buf && (*end == '\0' || *end == ' ')) {
            w->keys[ki].is_num = 1;
            w->keys[ki].num    = num;
        } else {
            w->keys[ki].is_num = 0;
            w->keys[ki].str    = strdup(field_buf);
        }
    }
    return NULL;
}

/**
 * @brief Builds the sorted_rows[] array — row indices in sorted order
 *
 * Fills the global sorted_rows[] array with data row indices (skipping the header),
 * sorted by the current column sort_col.
 *
 * Working logic:
 *   1. Validates sort_col
 *   2. Determines the data row range (excluding header if use_headers)
 *   3. Fills the array with sequential data row indices
 *   4. Calls qsort() with compare_rows_by_column
 *
 * After call:
 *   - sorted_count = number of sorted rows (data only, no header)
 *   - sorted_rows[0..sorted_count-1] = indices into rows[] in the desired order
 *
 * @note
 *   - If sort_col is invalid (<0 or >= col_count) -> sort is reset (sorted_count = 0)
 *   - The header (index 0) is never included in sorted_rows[]
 *   - Calls compare_rows_by_column, which may lazily load rows into cache
 *   - Efficiency: O(n log n) by number of data rows
 *
 * @example
 *   // Before call
 *   sort_col = 3;          // column D
 *   sort_order = -1;       // descending
 *   use_headers = 1;
 *   row_count = 1001;      // 1 header + 1000 data rows
 *
 *   build_sorted_index();
 *   // After:
 *   // sorted_count == 1000
 *   // sorted_rows[0] — index of the largest row by column D
 *   // sorted_rows[999] — index of the smallest
 *
 * @warning
 *   - Depends on global variables: sort_col, sort_order, col_count,
 *     use_headers, row_count, sorted_rows, sorted_count
 *   - May cause significant memory usage and time on first sort
 *     (due to lazy loading of rows in compare_rows_by_column)
 *   - Does NOT preserve order of equal elements (unstable sort)
 *   - If row_count is very large — may take significant time
 *
 * @see
 *   - compare_rows_by_column() — comparison function called by qsort()
 *   - get_real_row() — uses the result of this function
 *   - qsort() — standard sort function from <stdlib.h>
 */
void build_sorted_index(void)
{
    if (sort_level_count == 0) { sorted_count = 0; return; }
    if (sort_levels[0].col < 0 || sort_levels[0].col >= col_count) { sorted_count = 0; return; }

    int start = use_headers ? 1 : 0;

    if (filter_active && filtered_count > 0)
    {
        // Filter is active: the set is already small, use multi-level comparator.
        sorted_count = filtered_count;
        for (int i = 0; i < sorted_count; i++)
            sorted_rows[i] = filtered_rows[i];

        if (sorted_count > 1) {
            spinner_tick();
            qsort(sorted_rows, sorted_count, sizeof(int), compare_rows_multilevel);
            spinner_clear();
        }
        return;
    }

    // Full sort: pre-compute keys for each level.
    sorted_count = row_count - start;
    for (int i = 0; i < sorted_count; i++)
        sorted_rows[i] = start + i;

    if (sorted_count <= 1) return;

    // Determine how many levels are valid and allocate key arrays
    SortKey *keys_ml[MAX_SORT_LEVELS];
    int valid_levels = 0;
    for (int lv = 0; lv < sort_level_count; lv++) {
        if (sort_levels[lv].col < 0 || sort_levels[lv].col >= col_count) break;
        keys_ml[lv] = malloc((size_t)sorted_count * sizeof(SortKey));
        if (!keys_ml[lv]) break;
        valid_levels++;
    }

    if (valid_levels == 0) {
        // OOM fallback: use old slow method
        spinner_tick();
        qsort(sorted_rows, sorted_count, sizeof(int), compare_rows_by_column);
        spinner_clear();
        return;
    }

    spinner_tick();

    // Extract keys for each level
    for (int lv = 0; lv < valid_levels; lv++) {
        int scol = sort_levels[lv].col;

        if (g_mmap_base) {
            int nthreads = csv_num_threads();
            int chunk    = (sorted_count + nthreads - 1) / nthreads;

            SortKeyWorker *workers = calloc(nthreads, sizeof(SortKeyWorker));
            pthread_t     *tids    = malloc(nthreads * sizeof(pthread_t));

            for (int t = 0; t < nthreads; t++) {
                workers[t].start_r  = start + t * chunk;
                workers[t].start_ki = t * chunk;
                workers[t].count    = chunk;
                if (workers[t].start_r + workers[t].count > row_count)
                    workers[t].count = row_count - workers[t].start_r;
                if (workers[t].count < 0) workers[t].count = 0;
                workers[t].scol = scol;
                workers[t].keys = keys_ml[lv];
                pthread_create(&tids[t], NULL, sort_key_worker, &workers[t]);
            }
            for (int t = 0; t < nthreads; t++) pthread_join(tids[t], NULL);

            free(workers);
            free(tids);
        } else {
            /* Sequential fallback */
            rewind(f);
            char seq_buf[MAX_LINE_LEN];
            for (int r = 0; r < row_count; r++) {
                if (!fgets(seq_buf, sizeof(seq_buf), f)) break;
                if (r < start) continue;

                seq_buf[strcspn(seq_buf, "\r\n")] = '\0';

                int ki = r - start;
                const char *raw = get_field_fast(seq_buf, scol);

                char *end;
                double num = parse_double(raw, &end);
                if (end != raw && (*end == '\0' || *end == ' ')) {
                    keys_ml[lv][ki].is_num = 1;
                    keys_ml[lv][ki].num    = num;
                } else {
                    keys_ml[lv][ki].is_num = 0;
                    keys_ml[lv][ki].str    = strdup(raw);
                }
            }
        }
    }

    // Sort by pre-computed multi-level keys
    for (int lv = 0; lv < valid_levels; lv++)
        g_sort_keys_ml[lv] = keys_ml[lv];
    g_sort_level_count_local = valid_levels;
    g_sort_start = start;
    qsort(sorted_rows, sorted_count, sizeof(int), compare_by_preextracted_ml);
    g_sort_level_count_local = 0;

    // Free string keys for all levels
    for (int lv = 0; lv < valid_levels; lv++) {
        for (int i = 0; i < sorted_count; i++)
            if (!keys_ml[lv][i].is_num) free(keys_ml[lv][i].str);
        free(keys_ml[lv]);
    }

    spinner_clear();
}

/**
 * @brief Converts a visible (screen) row index to the real index in the rows[] array
 *
 * Returns the actual row number in the file (index in rows[]),
 * accounting for the current state:
 *   - active sort (sorted_rows[])
 *   - active filter (filtered_rows[])
 *   - presence of a header (use_headers)
 *
 * Priority logic:
 *   1. If sort is active and the number of sorted rows matches the visible count —
 *      use sorted_rows[display_idx]
 *   2. If filter is active — use filtered_rows[display_idx]
 *   3. Otherwise — natural order + offset for the header
 *
 * @param display_idx   Visible row index on screen (0 = first visible data row)
 *                      May be negative or greater than the visible count
 *
 * @return
 *   Real row index in the rows[] array (0-based, including header if present)
 *   If display_idx < 0 -> returns 0 (first row)
 *   If display_idx is too large -> returns the index of the last visible row
 *
 * @note
 *   - This function is used everywhere that needs to know "what is under the cursor"
 *     (draw_table_body, key handling, statistics, export, etc.)
 *   - If both sort and filter are active simultaneously — sort takes priority
 *     (but in current program logic this is a rare case — filter is usually applied first)
 *   - Correctly handles the case where a header is present (use_headers == 1)
 *
 * @example
 *   // Situation 1: filter active, 50 rows visible
 *   filter_active = 1;
 *   filtered_count = 50;
 *   get_real_row(0)   -> filtered_rows[0]
 *   get_real_row(49)  -> filtered_rows[49]
 *   get_real_row(100) -> filtered_rows[49] (last)
 *
 *   // Situation 2: sort active, 1000 data rows
 *   sort_col = 2;
 *   sorted_count = 1000;
 *   get_real_row(5)   -> sorted_rows[5]
 *
 *   // Situation 3: nothing active, header present
 *   use_headers = 1;
 *   row_count = 1001;
 *   get_real_row(0)   -> 1  (first data row)
 *   get_real_row(999) -> 1000
 *
 * @warning
 *   - Depends on global variables: filter_active, filtered_count, filtered_rows,
 *     sort_col, sorted_count, sorted_rows, use_headers, row_count
 *   - Does NOT check bounds of filtered_rows / sorted_rows arrays — assumes
 *     that sorted_count and filtered_count are always correct
 *   - If sorted_count != number of visible rows during sort — errors are possible
 *     (e.g., if filter was applied after sort)
 *
 * @see
 *   - apply_filter() — fills filtered_rows and filtered_count
 *   - build_sorted_index() — fills sorted_rows and sorted_count
 *   - draw_table_body() — primary consumer of this function
 */

int get_real_row(int display_idx)
{
    // Guard against negative index — always return the first row
    if (display_idx < 0) {
        return 0;
    }

    // Compute total number of visible rows (excluding header)
    int total_visible = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));

    // If requested index is beyond visible rows — return the last one
    if (display_idx >= total_visible)
    {
        return total_visible - 1 + (use_headers ? 1 : 0);
    }

    // Priority 1: if sort is active and count matches visible rows
    // (i.e. sort is applied to the current visible row set)
    if (sort_level_count > 0 && sorted_count == total_visible)
    {
        return sorted_rows[display_idx];
    }

    // Priority 2: if filter is active — use filtered array
    if (filter_active)
    {
        return filtered_rows[display_idx];
    }

    // Natural order: just offset by header (if present)
    return display_idx + (use_headers ? 1 : 0);
}
