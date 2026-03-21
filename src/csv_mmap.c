/**
 * csv_mmap.c
 * Memory-mapped file I/O helpers for csvview.
 */
#if defined(__linux__)
#  define _XOPEN_SOURCE 700
#endif

#include "csv_mmap.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

char   *g_mmap_base = NULL;
size_t  g_mmap_size = 0;

int csv_mmap_open(const char *filename)
{
    csv_mmap_close();

    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;

    struct stat sb;
    if (fstat(fd, &sb) < 0) { close(fd); return -1; }

    g_mmap_size = (size_t)sb.st_size;
    if (g_mmap_size == 0) { close(fd); return 0; }

    g_mmap_base = mmap(NULL, g_mmap_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (g_mmap_base == MAP_FAILED) {
        g_mmap_base = NULL;
        g_mmap_size = 0;
        return -1;
    }

#ifdef MADV_SEQUENTIAL
    madvise(g_mmap_base, g_mmap_size, MADV_SEQUENTIAL);
#endif

    return 0;
}

void csv_mmap_close(void)
{
    if (g_mmap_base) {
        munmap(g_mmap_base, g_mmap_size);
        g_mmap_base = NULL;
        g_mmap_size = 0;
    }
}

char *csv_mmap_get_line(long offset, char *buf, int buf_size)
{
    if (!g_mmap_base || offset < 0 || (size_t)offset >= g_mmap_size)
        return NULL;

    const char *start    = g_mmap_base + offset;
    size_t      remaining = g_mmap_size - (size_t)offset;
    const char *nl        = memchr(start, '\n', remaining);
    size_t      len       = nl ? (size_t)(nl - start) : remaining;

    if (len >= (size_t)buf_size) len = (size_t)(buf_size - 1);
    memcpy(buf, start, len);
    buf[len] = '\0';
    if (len > 0 && buf[len - 1] == '\r') buf[len - 1] = '\0';

    return buf;
}

int csv_num_threads(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    return (int)n;
}
