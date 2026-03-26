#ifndef SPLIT_FILE_H
#define SPLIT_FILE_H

/**
 * Splits one CSV file into multiple files based on unique values of a column.
 *
 * Usage:
 *   csvview --split --by=region sales.csv
 *   csvview --split --by=2 sales.csv              (2 = column number, 1-based)
 *   csvview --split --by=region --output-dir=./parts sales.csv
 *
 * Each output file: <basename>_<value>.csv
 * The header is copied to every output file.
 *
 * @param input_path   path to the input CSV file
 * @param by_col       column name or column number (1-based) to group by
 * @param output_dir   directory for output files (NULL = same directory as the input)
 * @param drop_col     1 — remove the split column from output files, 0 — keep it
 * @return 0 — success, otherwise an error code
 */
int split_file(const char *input_path, const char *by_col, const char *output_dir, int drop_col);

#endif /* SPLIT_FILE_H */
