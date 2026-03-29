#ifndef THEMES_H
#define THEMES_H

#include <ncurses.h>

/* Colour-pair semantics used throughout csvview:
 *   1  – column headers
 *   2  – cursor row
 *   3  – accent (status bar, messages)
 *   5  – secondary accent (column-stats, filter hints)
 *   6  – dimmed / secondary text
 *   7  – search-match highlight
 *   8  – autocomplete ghost text
 *  11  – anomaly highlight in graphs
 *  10+ – GRAPH_COLOR_BASE series colours (18 entries, index 17 = "Other" gray)
 */

typedef struct {
    const char *name;
    const char *label;
    short pair1_fg,  pair1_bg;   /* headers                  */
    short pair2_fg,  pair2_bg;   /* cursor row               */
    short pair3_fg,  pair3_bg;   /* accent                   */
    short pair5_fg,  pair5_bg;   /* secondary accent         */
    short pair6_fg,  pair6_bg;   /* dimmed                   */
    short pair7_fg,  pair7_bg;   /* search highlight         */
    short pair8_fg,  pair8_bg;   /* ghost text               */
    short pair11_fg, pair11_bg;  /* anomaly highlight        */
    short graph_fg[18];          /* graph series fg colours (0-16 regular, 17 = Other) */
    short default_bg;            /* theme background         */
} Theme;

extern const Theme *current_theme;

const Theme *theme_by_name(const char *name);
void         theme_apply(const Theme *t);
void         theme_load_config(void);
void         theme_save_config(const char *name);
void         config_save_rn(int val);
const char  *theme_list_names(void);   /* comma-separated list of names */

#endif /* THEMES_H */
