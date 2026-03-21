/**
 * csv_mmap.h
 * Memory-mapped file I/O and thread helpers for csvview.
 */
#ifndef CSV_MMAP_H
#define CSV_MMAP_H

#include <stddef.h>

/* File mapped into memory after csv_mmap_open(). NULL if not mapped. */
extern char   *g_mmap_base;
extern size_t  g_mmap_size;

/* Map the file into memory. Returns 0 on success, -1 on error.
 * If mmap fails, g_mmap_base stays NULL and callers fall back to fseek/fgets. */
int  csv_mmap_open(const char *filename);

/* Unmap the file. Safe to call even if not mapped. */
void csv_mmap_close(void);

/* Copy the line starting at byte offset into buf (up to buf_size-1 chars).
 * Strips trailing \r\n. Returns buf on success, NULL if offset out of range. */
char *csv_mmap_get_line(long offset, char *buf, int buf_size);

/* Number of worker threads to use: min(8, nproc). */
int csv_num_threads(void);

#endif /* CSV_MMAP_H */
