#include "themes.h"
#include "csvview_defs.h"   /* GRAPH_COLOR_BASE, relative_line_numbers */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── 256-colour xterm palette references used below ───────────────────────
 *
 *  0   = black          7   = white          8   = bright black (dark gray)
 * 15   = bright white  16   = #000000 (cube) 232-255 = grayscale ramp
 *
 *  Grays:  232=#080808  234=#1c1c1c  235=#262626  236=#303030
 *          238=#444444  242=#6c6c6c  244=#808080  246=#949494
 *          248=#a8a8a8  250=#bcbcbc  253=#dadada  255=#eeeeee
 *
 *  Blues:  17=#00005f  25=#005faf  60=#5f5f87  67=#5f87af  68=#5f87d7
 *         105=#8787ff  111=#87afff  116=#87d7d7  117=#87d7ff
 *
 *  Greens:  107=#87af5f  150=#afd787  151=#afd7af
 *  Reds:    131=#af5f5f  173=#d7875f  210=#ff8787  211=#ff87af
 *  Yellows: 179=#d7af5f  222=#ffd787  223=#ffd7af
 *  Purples: 139=#af87af  141=#af87ff  183=#d7afff  189=#d7d7ff
 *  Cyans:   109=#87afaf  110=#87afd7  116=#87d7d7
 * ─────────────────────────────────────────────────────────────────────── */

/* ── theme table ──────────────────────────────────────────────────────── */

static const Theme themes[] = {

    /* ── dark (default) ─────────────────────────────────────────────── */
    {
        "dark", "Dark (default)",
        /* pair1  headers   */ 250,  0,
        /* pair2  cursor    */   0, 15,
        /* pair3  accent    */   3,  0,   /* yellow */
        /* pair5  accent2   */   6,  0,   /* cyan   */
        /* pair6  dimmed    */ 244,  0,
        /* pair7  search    */   0,  3,   /* black on yellow */
        /* pair8  ghost     */   8,  0,
        /* pair11 anomaly   */   1,  0,   /* red    */
        /* graph series fg  */ { 1, 2, 4, 3, 6, 5, 7,
                                  208, 118, 213, 135, 75, 220, 210, 170, 44, 183,
                                  244 /* Other */ },
        /* default_bg       */ 0
    },

    /* ── light ──────────────────────────────────────────────────────── */
    {
        "light", "Light",
        /* pair1  headers      */ 0,  15,  /* black on bright white      */
        /* pair2  cursor       */ 15, 25,  /* white on blue — keep       */
        /* pair3  accent/col   */ 0,   3,  /* black on yellow            */
        /* pair5  accent2      */ 25, 15,  /* dark blue on white         */
        /* pair6  dimmed/nums  */ 238,15,  /* dark gray on white         */
        /* pair7  search       */ 0,   3,  /* black on yellow            */
        /* pair8  ghost        */ 246,15,
        /* pair11 anomaly      */ 1,  15,  /* red on white               */
        /* graph series fg     */ { 1, 2, 25, 3, 6, 5, 0,
                                    130, 22, 125, 55, 25, 94, 131, 91, 30, 146,
                                    244 /* Other */ },
        /* default_bg          */ 15
    },

    /* ── tokyonight ─────────────────────────────────────────────────── */
    {
        "tokyonight", "Tokyo Night",
        /* pair1  headers   */ 189, 235,
        /* pair2  cursor    */ 235, 111,   /* bg on blue */
        /* pair3  accent    */ 179, 235,   /* yellow */
        /* pair5  accent2   */ 117, 235,   /* cyan */
        /* pair6  dimmed    */  60, 235,   /* comment gray */
        /* pair7  search    */ 235, 179,
        /* pair8  ghost     */  60, 235,
        /* pair11 anomaly   */ 210, 235,   /* red */
        /* graph series fg  */ { 210, 107, 111, 179, 117, 141, 189,
                                  208, 150, 213, 105, 75, 222, 203, 183, 116, 147,
                                  242 /* Other */ },
        /* default_bg       */ 235
    },

    /* ── nord ───────────────────────────────────────────────────────── */
    {
        "nord", "Nord",
        /* pair1  headers   */ 255, 236,
        /* pair2  cursor    */ 236,  68,   /* bg on frost blue */
        /* pair3  accent    */ 222, 236,   /* aurora yellow */
        /* pair5  accent2   */ 110, 236,   /* frost cyan */
        /* pair6  dimmed    */ 244, 236,
        /* pair7  search    */ 236, 222,
        /* pair8  ghost     */ 244, 236,
        /* pair11 anomaly   */ 131, 236,   /* aurora red */
        /* graph series fg  */ { 131, 150, 68, 222, 110, 139, 255,
                                  173, 108, 211, 105, 111, 214, 167, 183, 116, 189,
                                  242 /* Other */ },
        /* default_bg       */ 236
    },

    /* ── catppuccin (Mocha) ─────────────────────────────────────────── */
    /* Distinguished from TokyoNight by: darker bg (234), mauve cursor,
     * peach accent, teal secondary — purple/warm vs blue/cool              */
    {
        "catppuccin", "Catppuccin Mocha",
        /* pair1  headers   */ 189, 234,   /* text on darker bg */
        /* pair2  cursor    */ 234, 183,   /* bg on mauve — purple, not blue */
        /* pair3  accent    */ 216, 234,   /* peach (#ffaf87) — warm, not yellow */
        /* pair5  accent2   */ 122, 234,   /* teal (#87ffd7) — cooler cyan */
        /* pair6  dimmed    */ 242, 234,   /* overlay0, slightly lighter */
        /* pair7  search    */ 234, 216,   /* bg on peach */
        /* pair8  ghost     */ 242, 234,
        /* pair11 anomaly   */ 211, 234,   /* red */
        /* graph series fg  */ { 211, 151, 183, 216, 122, 141, 189,
                                  210, 150, 213, 105, 116, 222, 203, 147, 44, 189,
                                  242 /* Other */ },
        /* default_bg       */ 234
    },

    /* ── solarized light ────────────────────────────────────────────── */
    /* Iconic warm-cream palette. bg≈#ffffd7, text is teal-gray, blue
     * cursor, amber accent.  Distinct from light: warm vs cool white.  */
    {
        "solarized_light", "Solarized Light",
        /* pair1  text      */  66, 230,   /* teal-gray on warm cream  */
        /* pair2  cursor    */ 230,  33,   /* cream on bright blue     */
        /* pair3  accent    */ 136, 230,   /* amber yellow on cream    */
        /* pair5  accent2   */  37, 230,   /* cyan-teal on cream       */
        /* pair6  dimmed    */ 246, 230,   /* mid-gray on cream        */
        /* pair7  search    */ 230, 136,   /* cream on amber           */
        /* pair8  ghost     */ 246, 230,
        /* pair11 anomaly   */ 160, 230,   /* bright red               */
        /* graph series fg  */ { 160, 64, 33, 136, 37, 162, 61,
                                  130, 22, 125, 55, 25, 94, 131, 91, 30, 146,
                                  244 /* Other */ },
        /* default_bg       */ 230
    },

    /* ── paper ──────────────────────────────────────────────────────── */
    /* Clean white minimal theme. Near-white bg, near-black text,
     * teal cursor — cooler and crisper than solarized_light.           */
    {
        "paper", "Paper",
        /* pair1  text      */ 235, 255,   /* almost-black on white    */
        /* pair2  cursor    */ 255,  30,   /* white on teal (#008787)  */
        /* pair3  accent    */  94, 255,   /* dark amber on white      */
        /* pair5  accent2   */  30, 255,   /* teal on white            */
        /* pair6  dimmed    */ 248, 255,   /* mid-gray on white        */
        /* pair7  search    */ 255,  94,   /* white on dark amber      */
        /* pair8  ghost     */ 250, 255,
        /* pair11 anomaly   */ 160, 255,   /* bright red               */
        /* graph series fg  */ { 160, 64, 30, 94, 37, 5, 235,
                                  130, 28, 125, 55, 25, 136, 131, 91, 23, 146,
                                  244 /* Other */ },
        /* default_bg       */ 255
    },

    /* ── turbo vision ───────────────────────────────────────────────── */
    /* Inspired by the classic Borland TurboVision UI (1987).
     * Dark-navy desktop (#0000af), bright-cyan selected items,
     * bright-yellow search — instantly recognisable retro TUI look.   */
    {
        "turbovision", "Turbo Vision",
        /* pair1  text      */  15,  19,   /* bright white on TV-blue  */
        /* pair2  cursor    */   0,  51,   /* black on bright cyan     */
        /* pair3  accent    */  51,  19,   /* bright cyan on navy      */
        /* pair5  accent2   */  11,  19,   /* bright yellow on navy    */
        /* pair6  dimmed    */ 153,  19,   /* pale blue on navy        */
        /* pair7  search    */   0,  11,   /* black on bright yellow   */
        /* pair8  ghost     */ 153,  19,
        /* pair11 anomaly   */   9,  19,   /* bright red               */
        /* graph series fg  */ { 9, 10, 11, 51, 13, 15, 153,
                                  208, 118, 213, 141, 75, 220, 210, 170, 44, 183,
                                  242 /* Other */ },
        /* default_bg       */ 19
    },

    /* ── dracula ────────────────────────────────────────────────────── */
    /* The famous dark theme by Zeno Rocha. Charcoal bg, purple cursor,
     * pink/cyan accents — distinct from nord (icy-blue) & catppuccin.  */
    {
        "dracula", "Dracula",
        /* pair1  text      */ 255, 235,   /* near-white on charcoal   */
        /* pair2  cursor    */ 235, 141,   /* bg on purple             */
        /* pair3  accent    */ 228, 235,   /* bright yellow            */
        /* pair5  accent2   */ 117, 235,   /* cyan                     */
        /* pair6  dimmed    */  61, 235,   /* purple-gray comment      */
        /* pair7  search    */ 235, 228,   /* bg on yellow             */
        /* pair8  ghost     */  61, 235,
        /* pair11 anomaly   */ 203, 235,   /* bright red               */
        /* graph series fg  */ { 203, 84, 141, 228, 117, 212, 255,
                                  208, 118, 213, 105, 75, 222, 210, 170, 44, 183,
                                  242 /* Other */ },
        /* default_bg       */ 235
    },

    /* ── gruvbox ────────────────────────────────────────────────────── */
    /* Gruvbox Dark Hard. Warm retro palette: near-black bg, cream text,
     * orange cursor & accents — feels hand-crafted vs cold blue themes. */
    {
        "gruvbox", "Gruvbox Dark",
        /* pair1  text      */ 223, 234,   /* warm cream on near-black */
        /* pair2  cursor    */ 234, 214,   /* bg on amber-orange       */
        /* pair3  accent    */ 214, 234,   /* amber-orange             */
        /* pair5  accent2   */ 108, 234,   /* aqua                     */
        /* pair6  dimmed    */ 245, 234,   /* warm gray                */
        /* pair7  search    */ 234, 214,   /* bg on amber              */
        /* pair8  ghost     */ 245, 234,
        /* pair11 anomaly   */ 167, 234,   /* muted red                */
        /* graph series fg  */ { 167, 142, 109, 214, 108, 175, 223,
                                  173, 106, 66, 178, 72, 132, 131, 183, 80, 189,
                                  245 /* Other */ },
        /* default_bg       */ 234
    },
};

static const int THEME_COUNT = (int)(sizeof(themes) / sizeof(themes[0]));

const Theme *current_theme = &themes[0];   /* dark by default */

/* ── lookup ────────────────────────────────────────────────────────────── */

const Theme *theme_by_name(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < THEME_COUNT; i++)
        if (strcasecmp(themes[i].name, name) == 0)
            return &themes[i];
    return NULL;
}

/* ── apply ─────────────────────────────────────────────────────────────── */

void theme_apply(const Theme *t)
{
    if (!t) t = &themes[0];
    current_theme = t;

    if (COLORS < 256) {
        /* 8-colour fallback – works on any terminal */
        init_pair(1,  COLOR_WHITE,   COLOR_BLACK);
        init_pair(2,  COLOR_BLACK,   COLOR_WHITE);
        init_pair(3,  COLOR_YELLOW,  COLOR_BLACK);
        init_pair(5,  COLOR_CYAN,    COLOR_BLACK);
        init_pair(6,  COLOR_WHITE,   COLOR_BLACK);
        init_pair(7,  COLOR_BLACK,   COLOR_YELLOW);
        init_pair(8,  COLOR_WHITE,   COLOR_BLACK);
        init_pair(11, COLOR_RED,     COLOR_BLACK);
        int basic_graph[] = {
            COLOR_RED, COLOR_GREEN, COLOR_BLUE,
            COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE
        };
        for (int i = 0; i < 18; i++) {
            if (i == 17)  /* Other: dim white */
                init_pair(GRAPH_COLOR_BASE + i, COLOR_WHITE, COLOR_BLACK);
            else
                init_pair(GRAPH_COLOR_BASE + i, basic_graph[i % 7], COLOR_BLACK);
        }
        return;
    }

    init_pair(1,  t->pair1_fg,  t->pair1_bg);
    init_pair(2,  t->pair2_fg,  t->pair2_bg);
    init_pair(3,  t->pair3_fg,  t->pair3_bg);
    init_pair(5,  t->pair5_fg,  t->pair5_bg);
    init_pair(6,  t->pair6_fg,  t->pair6_bg);
    init_pair(7,  t->pair7_fg,  t->pair7_bg);
    init_pair(8,  t->pair8_fg,  t->pair8_bg);
    init_pair(11, t->pair11_fg, t->pair11_bg);
    for (int i = 0; i < 18; i++)
        init_pair(GRAPH_COLOR_BASE + i, t->graph_fg[i], t->default_bg);
}

/* ── config file path ───────────────────────────────────────────────────── */

static void get_config_path(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(buf, sz, "%s/csvview/config", xdg);
    else if (home && home[0])
        snprintf(buf, sz, "%s/.config/csvview/config", home);
    else
        snprintf(buf, sz, ".csvview.config");
}

/* ── load ───────────────────────────────────────────────────────────────── */

void theme_load_config(void)
{
    char path[1024];
    get_config_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "theme=", 6) == 0) {
            const Theme *t = theme_by_name(line + 6);
            if (t) current_theme = t;
        } else if (strncmp(line, "rn=", 3) == 0) {
            relative_line_numbers = atoi(line + 3);
        }
    }
    fclose(f);
}

/* ── generic key save (rewrite one key, preserve the rest) ─────────────── */

static void config_save_key(const char *key, const char *value)
{
    char path[1024];
    get_config_path(path, sizeof(path));

    /* mkdir -p the directory part */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        for (char *p = dir + 1; *p; p++) {
            if (*p == '/') { *p = '\0'; mkdir(dir, 0755); *p = '/'; }
        }
        mkdir(dir, 0755);
    }

    size_t klen = strlen(key);
    char tmp[4096] = "";
    FILE *f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, key, klen) == 0 && line[klen] == '=') continue;
            strncat(tmp, line, sizeof(tmp) - strlen(tmp) - 1);
        }
        fclose(f);
    }

    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s=%s\n", key, value);
    if (tmp[0]) fputs(tmp, f);
    fclose(f);
}

/* ── save ───────────────────────────────────────────────────────────────── */

void theme_save_config(const char *name)
{
    config_save_key("theme", name);
}

void config_save_rn(int val)
{
    config_save_key("rn", val ? "1" : "0");
}

/* ── list ───────────────────────────────────────────────────────────────── */

const char *theme_list_names(void)
{
    static char buf[512];
    buf[0] = '\0';
    for (int i = 0; i < THEME_COUNT; i++) {
        if (i > 0) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, themes[i].name, sizeof(buf) - strlen(buf) - 1);
    }
    return buf;
}
