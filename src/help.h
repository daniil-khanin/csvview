/**
 * help.h
 *
 * Help and usage module for csvview
 * Displays detailed instructions on how to use the program
 */

#ifndef HELP_H
#define HELP_H

#include <ncurses.h>    // for mvprintw, attron, etc. (when rendering in ncurses)
#include <stdio.h>      // for printf (when rendering to the console)

// ────────────────────────────────────────────────
// Public functions
// ────────────────────────────────────────────────

/**
 * @brief Displays the full program help
 *
 * Outputs to the terminal (or to an ncurses window if ncurses is active)
 * a detailed guide covering launch options, key bindings, commands and features.
 *
 * @param use_ncurses  1 — render in an ncurses window (centered, with a border)
 *                     0 — plain output to stdout (console)
 */
void show_help(int use_ncurses);

/**
 * @brief Displays brief usage information (for --help / -h)
 *
 * Shows only the main arguments and a short description.
 */
void show_usage(void);

#endif /* HELP_H */
