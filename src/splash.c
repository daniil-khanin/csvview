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
#include <time.h>

/* ── ANSI helpers ───────────────────────────────────────────── */
#define RST  "\033[0m"
#define BOLD "\033[1m"
#define DIM  "\033[2m"
#define CYN  "\033[1;36m"
#define YEL  "\033[1;33m"
#define GRN  "\033[1;32m"
#define WHT  "\033[1;37m"
#define DWH  "\033[2;37m"

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

/* ── Variant A: bar chart ───────────────────────────────────── */
static void show_barchart(void)
{
    /* bar heights 1–10, one per letter */
    int  h[]  = { 5,   8,   10,  3,   6,   7,   9  };
    /* labels above each bar — v?? is filled at runtime */
    char ver[8];
    snprintf(ver, sizeof(ver), "v%d", CSVVIEW_VERSION);
    const char *lbl[] = { "702", "fast", "50M", ver, "MIT", ".csv", "264K" };
    const char *let[] = { "C",   "S",   "V",   "V", "I",   "E",   "W"   };
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
    printf("   " DIM
           "─────────────────────────────────────"
           RST "\n");

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

    /* table header — bold default fg, readable on any theme */
    printf("  " BOLD "│ %-12s │ %7s │ %-10s │ %-8s │" RST "\n",
           "file", "rows", "date", "size");
    printf("  " DIM  "│──────────────│─────────│────────────│──────────│" RST "\n");

    /* data rows */
    for (int r = 0; r < nrows; r++) {
        if (r == scan) {
            /* reverse video: inverts fg/bg — always visible on any theme */
            printf(YEL "▶▶" RST
                   "\033[7m" BOLD
                   " %-12s │ %7s │ %-10s │ %-8s "
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
void show_version(void)
{
    srand((unsigned int)time(NULL));
    if (rand() % 2 == 0)
        show_barchart();
    else
        show_scanner();
}
