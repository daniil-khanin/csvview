#ifndef BOOKMARKS_H
#define BOOKMARKS_H

/* Show the bookmarks list window (rows + columns).
 * Returns real_row to jump to, or -1 if closed without jumping.
 * If a column bookmark is selected, *out_col receives the column index;
 * otherwise *out_col is set to -1.
 * Deleting a bookmark inside the window calls save_column_settings(csv_filename).
 */
int show_marks_window(const char *csv_filename, int *out_col);

#endif /* BOOKMARKS_H */
