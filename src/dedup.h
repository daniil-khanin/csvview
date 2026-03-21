#ifndef DEDUP_H
#define DEDUP_H

/* CLI mode: deduplicate a CSV file, write result to output.
 * by_cols  – comma-separated column names or 1-based numbers,
 *            NULL or "" = use all columns
 * keep_last – 0 = keep first occurrence, 1 = keep last
 * output   – output file path, NULL = auto "deduped_YYYYMMDD_HHMMSS.csv"
 * sep      – field delimiter character
 */
int dedup_file(const char *input_path,
               const char *by_cols,
               int         keep_last,
               const char *output_path,
               char        sep);

/* Progress callback: called periodically during dedup_make_filter.
 * done  – rows processed so far
 * total – total rows (always known for interactive mode)
 * pass  – 1 = first pass (keep=last only), 2 = second pass / only pass
 * ud    – user data pointer passed through unchanged
 */
typedef void (*dedup_progress_fn)(long done, long total, int pass, void *ud);

/* Interactive mode: build a filtered_rows-style array with duplicates removed.
 * Operates on the current view (respects existing filter/sort and column_names[]).
 * by_cols     – comma-separated column names or 1-based numbers, NULL = all columns
 * keep_last   – 0 = keep first, 1 = keep last
 * out_count   – [out] number of rows in returned array
 * removed_out – [out] number of duplicate rows removed
 * progress    – optional callback called every ~50 000 rows, NULL to disable
 * ud          – userdata forwarded to progress callback
 * Returns malloc'd array of real row indices (caller must free), or NULL on error.
 */
int *dedup_make_filter(const char        *by_cols,
                       int                keep_last,
                       int               *out_count,
                       int               *removed_out,
                       dedup_progress_fn  progress,
                       void              *ud);

#endif /* DEDUP_H */
