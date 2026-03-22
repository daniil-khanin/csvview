/**
 * splash.c — version splash screen (two variants, chosen randomly)
 *
 * Variant A: bar chart where each bar = one letter of CSVVIEW
 * Variant B: table scanner with highlighted row
 */

#include "splash.h"
#include "csvview_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* ── ANSI helpers ───────────────────────────────────────────── */
#define RST  "\033[0m"
#define BOLD "\033[1m"
#define DIM  "\033[2m"
#define REV  "\033[7m"
#define CYN  "\033[1;36m"
#define YEL  "\033[1;33m"

/* one colour per letter C S V V I E W */
static const char *LC[] = {
    "\033[1;31m",   /* C — red     */
    "\033[1;33m",   /* S — yellow  */
    "\033[1;32m",   /* V — green   */
    "\033[1;36m",   /* V — cyan    */
    "\033[1;34m",   /* I — blue    */
    "\033[1;35m",   /* E — magenta */
    "\033[1;37m",   /* W — white   */
};

/* format binary size into buf (e.g. "264 KB", "1.3 MB") */
static void fmt_binary_size(const char *argv0, char *buf, int buflen)
{
    struct stat st;

    /* 1. try argv0 as-is (works for ./csvview or absolute path) */
    if (argv0 && stat(argv0, &st) == 0)
        goto found;

    /* 2. if no '/' in argv0, search PATH (works for installed binary) */
    if (argv0 && !strchr(argv0, '/')) {
        const char *path_env = getenv("PATH");
        if (path_env) {
            char path_copy[4096];
            strncpy(path_copy, path_env, sizeof(path_copy) - 1);
            path_copy[sizeof(path_copy) - 1] = '\0';
            char *dir = strtok(path_copy, ":");
            while (dir) {
                char candidate[4096];
                snprintf(candidate, sizeof(candidate), "%s/%s", dir, argv0);
                if (stat(candidate, &st) == 0)
                    goto found;
                dir = strtok(NULL, ":");
            }
        }
    }

    snprintf(buf, buflen, "?");
    return;

found:
    {
        long sz = (long)st.st_size;
        if (sz < 1024)
            snprintf(buf, buflen, "%ld B", sz);
        else if (sz < 1024 * 1024)
            snprintf(buf, buflen, "%ld KB", sz / 1024);
        else
            snprintf(buf, buflen, "%.1f MB", sz / (1024.0 * 1024.0));
    }
}

/* ── Variant A: bar chart ───────────────────────────────────── */
static void show_barchart(const char *binary_path)
{
    char sz[16];
    fmt_binary_size(binary_path, sz, sizeof(sz));

    /* bar heights 1–10, one per letter */
    int  h[]  = { 5,   8,   10,  3,   6,   7,   9  };
    char ver[8];
    snprintf(ver, sizeof(ver), "v%d", CSVVIEW_VERSION);
    const char *lbl[] = { "702", "fast", "50M", ver, "MIT", ".csv", sz };
    const char *let[] = { "C",   "S",   "V",   "V", "I",   "E",   "W" };
    int n = 7, max_h = 10;

    printf("\n");

    /* stat labels */
    printf("   ");
    for (int i = 0; i < n; i++)
        printf("%s%-5s" RST, LC[i], lbl[i]);
    printf("\n");

    /* bars — top row = tallest */
    for (int r = 0; r < max_h; r++) {
        printf("   ");
        for (int i = 0; i < n; i++) {
            if (r >= max_h - h[i])
                printf("%s████" RST " ", LC[i]);
            else
                printf("     ");
        }
        printf("\n");
    }

    /* baseline */
    printf("   " DIM "─────────────────────────────────────" RST "\n");

    /* letter labels */
    printf("   ");
    for (int i = 0; i < n; i++)
        printf("%s  %s  " RST, LC[i], let[i]);
    printf("\n\n");

    printf("   " BOLD "Terminal CSV Viewer & Editor  ·  v%d" RST "\n\n",
           CSVVIEW_VERSION);
    printf("   " DIM "50M rows · filters · pivot · graphs · sort" RST "\n\n");
    printf("   " CYN "github.com/daniil-khanin/csvview" RST "\n\n");
}

/* ── Variant B: scanner ─────────────────────────────────────── */
static void show_scanner(void)
{
    const char *file[] = {
        "sales.csv   ", "metrics.csv ", "report.tsv  ",
        "orders.psv  ", "users.csv   "
    };
    const char *rows[] = { " 50,000", "  8,432", "    891", " 23,100", "  1,203" };
    const char *date[] = {
        "2024-01-01", "2025-06-15", "2026-03-22",
        "2026-01-10", "2026-02-28"
    };
    const char *size[] = { "  1.3 GB", "  420 KB", "  128 KB", "   12 MB", "   88 KB" };
    int scan  = 2;   /* highlighted row index */
    int nrows = 5;

    printf("\n");

    /* header — bold, readable on any theme */
    printf("  " BOLD "│ %-12s │ %7s │ %-10s │ %-8s │" RST "\n",
           "file", "rows", "date", "size");
    /* separator — default fg to match data rows */
    printf("  │──────────────│─────────│────────────│──────────│\n");

    /* data rows */
    for (int r = 0; r < nrows; r++) {
        if (r == scan) {
            /* reverse video covers full row including │ borders */
            printf(YEL "▶▶" RST
                   REV BOLD
                   "│ %-12s │ %7s │ %-10s │ %-8s │"
                   RST YEL "◀◀" RST "\n",
                   file[r], rows[r], date[r], size[r]);
        } else {
            printf("  │ %-12s │ %7s │ %-10s │ %-8s │\n",
                   file[r], rows[r], date[r], size[r]);
        }
    }

    printf("\n");
    printf("     " BOLD "[ scanning... ]" RST "\n\n");

    /* C S V V I E W spread with per-letter colours */
    const char *lets[] = { "C", "S", "V", "V", "I", "E", "W" };
    printf("   ");
    for (int i = 0; i < 7; i++)
        printf("%s%s  " RST, LC[i], lets[i]);
    printf(BOLD "·  v%d" RST "\n\n", CSVVIEW_VERSION);

    printf("   " CYN "github.com/daniil-khanin/csvview" RST "\n\n");
}

/* ── Public entry point ─────────────────────────────────────── */
void show_version(const char *binary_path)
{
    srand((unsigned int)time(NULL));
    if (rand() % 2 == 0)
        show_barchart(binary_path);
    else
        show_scanner();
}
