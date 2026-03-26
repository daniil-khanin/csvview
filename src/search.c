/**
 * search.c
 *
 * Implementation of search over CSV table cell contents
 */

#include "search.h"
#include "utils.h"          // strcasestr_custom, get_column_value, etc.
#include "ui_draw.h"        // spinner_tick / spinner_clear

#include <stdio.h>          // fseek, fgets
#include <stdlib.h>         // malloc, strdup
#include <string.h>         // strlen, strcpy, strtok

// ────────────────────────────────────────────────
// Perform a search across the entire table
// ────────────────────────────────────────────────

void perform_search(RowIndex *rows, FILE *f, int row_count)
{
    // Reset search results
    search_count = 0;
    search_index = -1;

    // If the query is empty — nothing to search
    if (strlen(search_query) == 0) {
        return;
    }

    // Skip the header row if present
    int start_row = use_headers ? 1 : 0;

    for (int r = start_row; r < row_count && search_count < MAX_SEARCH_RESULTS; r++)
    {
        if ((r - start_row) % 5000 == 0 && r > start_row) spinner_tick();

        // Lazy-load the row into cache
        if (!rows[r].line_cache)
        {
            fseek(f, rows[r].offset, SEEK_SET);
            char line_buf[MAX_LINE_LEN];
            if (fgets(line_buf, sizeof(line_buf), f))
            {
                line_buf[strcspn(line_buf, "\n")] = '\0';
                rows[r].line_cache = strdup(line_buf);
            }
            else
            {
                rows[r].line_cache = strdup("");
            }
        }

        int field_count = 0;
        char **fields = parse_csv_line(rows[r].line_cache, &field_count);
        if (!fields) continue;

        for (int c = 0; c < field_count && c < col_count && search_count < MAX_SEARCH_RESULTS; c++)
        {
            if (strcasestr_custom(fields[c], search_query))
            {
                search_results[search_count].row = r;
                search_results[search_count].col = c;
                search_count++;
            }
        }

        free_csv_fields(fields, field_count);
    }

    spinner_clear();
}

// ────────────────────────────────────────────────
// Navigate to a search result
// ────────────────────────────────────────────────

void goto_search_result(int index,
                        int *cur_display_row,
                        int *top_display_row,
                        int *cur_col,
                        int *left_col,
                        int visible_rows,
                        int visible_cols,
                        int row_count)
{
    // Validate the index
    if (index < 0 || index >= search_count) {
        return;
    }

    search_index = index;

    // Actual row index in the file
    int target_real_row = search_results[index].row;
    int target_col = search_results[index].col;

    // Find the visible position of the row (if a filter is active)
    int target_display_row = -1;
    if (filter_active)
    {
        for (int i = 0; i < filtered_count; i++)
        {
            if (filtered_rows[i] == target_real_row)
            {
                target_display_row = i;
                break;
            }
        }

        // If the row did not pass the filter — do not jump
        if (target_display_row == -1) {
            return;
        }
    }
    else
    {
        // No filter — just offset by the header
        target_display_row = target_real_row - (use_headers ? 1 : 0);
    }

    // Set the cursor to the found row and column
    *cur_display_row = target_display_row;
    cur_real_row = target_real_row;
    *cur_col = target_col;

    // Vertical scroll — center the row
    *top_display_row = *cur_display_row - (visible_rows / 2);
    if (*top_display_row < 0) {
        *top_display_row = 0;
    }

    int display_count = filter_active ? filtered_count : (row_count - (use_headers ? 1 : 0));
    if (*top_display_row > display_count - visible_rows) {
        *top_display_row = display_count - visible_rows;
    }

    // Horizontal scroll — center the column (only in the scrollable area)
    if (*cur_col < freeze_cols) {
        // column is in the frozen area — don't change left_col, but keep it at least freeze_cols
        if (*left_col < freeze_cols) *left_col = freeze_cols;
    } else {
        *left_col = *cur_col - (visible_cols / 2);
        if (*left_col < freeze_cols) *left_col = freeze_cols;
        if (*left_col > col_count - visible_cols) *left_col = col_count - visible_cols;
        if (*left_col < 0) *left_col = 0;
    }
}