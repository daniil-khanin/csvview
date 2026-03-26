#ifndef CONCAT_FILES_H
#define CONCAT_FILES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * Merges several CSV files into one, adding a column with the source filename.
 * - Takes the header only from the first file.
 * - Skips the first line (header) in all subsequent files.
 * - Verifies that the column count is the same across all files.
 * - If --output is not specified, generates a name merged_YYYYMMDD_HHMMSS.csv
 * - The new column contains the base filename without path and without .csv
 *
 * @param files             array of paths to input files
 * @param file_count        number of files
 * @param source_col_name   name of the new column (must not be NULL)
 * @param user_output       desired output filename (may be NULL)
 * @param result_filename   [out] receives a pointer to the name of the file that was used
 *                          (the function allocates memory for it — caller must free)
 * @return 0 — success, otherwise an error code (1 — general error, 2 — mismatched column counts, etc.)
 */
int concat_and_save_files(
    char **files,
    int file_count,
    const char *source_col_name,
    const char *user_output,
    char **result_filename
);

#endif /* CONCAT_FILES_H */
