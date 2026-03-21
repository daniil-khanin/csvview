#ifndef BOOKMARKS_H
#define BOOKMARKS_H

/* Show the bookmarks list window.
 * Returns real_row to jump to, or -1 if closed without jumping.
 * Deleting a bookmark inside the window calls save_column_settings(csv_filename).
 */
int show_marks_window(const char *csv_filename);

#endif /* BOOKMARKS_H */
