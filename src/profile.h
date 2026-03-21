#ifndef PROFILE_H
#define PROFILE_H

/* Print a data quality profile for every column of a CSV file.
 * sep – field delimiter character
 * Writes to stdout; no ncurses dependency.
 */
int profile_file(const char *input_path, char sep);

#endif /* PROFILE_H */
