/**
 * help.c
 *
 * Реализация вывода справки и помощи для csvview
 */

#include "help.h"
#include "csvview_defs.h"   // CSVVIEW_VERSION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

void show_usage(void)
{
    printf("CSV Viewer & Editor - Terminal Tool\n");
    printf("Version %d dated March 21, 2026 by Daniil Khanin & Claude\n\n", CSVVIEW_VERSION);

    printf("Usage:\n");
    printf("  csvview <file>                  Open file (auto-detects .tsv/.psv)\n");
    printf("  csvview --sep=<char> <file>      Custom delimiter (e.g. --sep=\\t --sep=;)\n");
    printf("  csvview --help (or -h, -?, /h)  Show this help\n\n");

    printf("Press any key to continue...\n");
}

void show_help(int use_ncurses)
{
    const char *help_text[] = {
        "CSV Viewer & Editor - Terminal Tool",
        "Version 14 dated March 21, 2026 by Daniil Khanin & Claude",
        "",
        "Usage:",
        "  csvview <file>                  Open file (auto-detects .tsv / .psv)",
        "  csvview --sep=<char> <file>      Custom delimiter (--sep=\\t  --sep=;  --sep=|)",
        "  csvview --help (or -h, -?, /h)  Show this help",
        "  csvview --cat [--column=NAME] [--output=FILE] file1 file2 ...",
        "  csvview --split --by=<column> [--output-dir=<dir>] [--drop-col] file",
        "",
        "File format auto-detection:",
        "  .csv -> comma   .tsv -> tab   .psv -> pipe   other -> use --sep",
        "",
        "Key Bindings (main view):",
        "  Arrows / hjkl        Move cursor",
        "  PageUp / PageDown    Scroll one page",
        "  Home / End           First / last visible row",
        "  /                    Start simple search",
        "  n / N                Next / previous search result",
        "  f                    Quick filter (old syntax)  [Tab = autocomplete column]",
        "  Shift+F              Advanced filter (new syntax) [Tab = autocomplete column]",
        "  Enter                Edit current cell",
        "  u                    Undo last cell edit (up to 20 steps)",
        "  s                    Save file",
        "  t                    Open/edit column settings",
        "  w / W                Increase / decrease column width",
        "  A                    Auto-fit current column width",
        "  [ / ]                Sort ascending / descending by current column",
        "  r / R                Reset sorting",
        "  Ctrl+R               Reload file (re-read from disk, keep filters/sort)",
        "  Ctrl+G               Go to row by number (prompts for row number)",
        "  z                    Freeze / unfreeze columns up to current column",
        "  d / D                Show column statistics",
        "  p                    Pivot table (apply saved or open settings)",
        "  P                    Pivot table — always open settings window",
        "  g / G                Graph for current column",
        "  q / Esc              Quit",
        "  ?                    This help",
        "",
        "Command mode (press : then command):",
        "  :N                   Go to row N (e.g. :1000 jumps to row 1000)",
        "  :e [filename]        Export current view (filtered/sorted) to CSV",
        "                       (default: filtered.csv / sorted.csv / table.csv)",
        "  :dr <row_number>     Delete row by visible number (1-based)",
        "  :cr <new_name>       Rename current column",
        "  :cs [filename]       Save current column to CSV (default: column name)",
        "  :cal [name]          Add new column LEFT of current (default 'untitled')",
        "  :car [name]          Add new column RIGHT of current (default 'untitled')",
        "  :cf \"text\"           Fill column with fixed text",
        "  :cf num()            Fill with row numbers (0,1,2…)",
        "  :cf num(N)           Fill starting from N (N,N+1…)",
        "  :cf num(N,M)         Fill with step M (N,N+M,N+2M…)",
        "  :cf <formula>        Compute column values by formula (see Computed columns)",
        "  :cd <column_name>    Delete column (only if cursor is on this exact column)",
        "  :fz N                Freeze first N columns (:fz 0 = unfreeze all)",
        "",
        "Filter quick commands (: then command):",
        "  :fs                  Save current filter",
        "  :fl                  Open list of saved filters",
        "  :fq                  Quick filter: column = \"current cell value\" (AND)",
        "  :fqn                 column != \"current cell value\" (AND)",
        "  :fqo                 column = \"current cell value\" (OR)",
        "  :fqno                column != \"current cell value\" (OR)",
        "  :fqu                 column = \"current cell value\" (reset previous filter)",
        "",
        "Theme:",
        "  :theme <name>        Switch colour theme (saved to ~/.config/csvview/config)",
        "  :theme               List available themes",
        "  Themes: dark, light, tokyonight, nord, catppuccin",
        "  CLI:    csvview --theme=tokyonight file.csv",
        "",
        "Saved filters window:",
        "  ↑↓ / j k             Select filter",
        "  Enter                Insert filter text",
        "  Shift+Enter          Insert and apply immediately",
        "  D / d                Delete selected filter",
        "  q / Esc              Close window",
        "",
        "Saved filters are stored in <yourfile.csv>.csvf",
        "(lines like \"filter: <query>\")",
        "",
        "Advanced filter syntax (Shift+F):",
        "  Examples:",
        "    total > 500",
        "    total >= 100 AND status = paid",
        "    date >= 2026-01",
        "    ! cohort = 2016-04",
        "  Supported: = != > >= < <= AND OR ! (spaces optional)",
        "  For dates: = 2026-01   finds all rows in January 2026",
        "             >= 2026-01  finds January 2026 and later",
        "             = 2025-Q3   finds all rows in Q3 (Jul-Sep)",
        "             >= 2025-Q1 AND date <= 2025-Q3  Q1 through Q3",
        "",
        "Column settings (t):",
        "  ↑↓ / j k             Move between columns",
        "  S / N / D            Set type: String / Number / Date",
        "  Enter                Edit format for current column",
        "    String: truncate length (0 = show full)",
        "    Number: decimal places (-1 = auto)",
        "    Date:   format string (e.g. %Y-%m-%d)",
        "  X                    Hide / show current column (saved in .csvf)",
        "  H                    Toggle use-headers ON/OFF (preview updates live)",
        "  q / Esc              Save & return to table",
        "",
        "Column statistics (d):",
        "  Numbers: sum, mean, median, mode, min/max, top 10 values + histogram",
        "  Dates: distribution by month/quarter/year/century + histogram",
        "  Large files may take 20–60 seconds",
        "",
        "Pivot table (p / P):",
        "  Rows / Columns       Select grouping columns",
        "  Values               Select column to aggregate",
        "  Aggregation          SUM, AVG, COUNT, MIN, MAX, UNIQUE COUNT,",
        "                       SUM+COUNT, SUM+AVG, MIN+MAX, SUM+COUNT+AVG, COUNT+UNIQUE COUNT",
        "  Date grouping        Auto / Month / Quarter / Year / Century",
        "  Row sort / Col sort  Key ↑↓ (alphabetical) or Val ↑↓ (by value)",
        "  Row / Column / Grand totals — Yes/No",
        "  G                    Toggle split-screen graph (right half)",
        "  Enter                Drill-down: open main table filtered by current cell",
        "  :e [file]            Export pivot to CSV (default pivot.csv)",
        "  :q                   Quit pivot mode",
        "  :o                   Reopen settings",
        "  Settings saved in <file>.pivot",
        "",
        "Pivot table graph (G in pivot mode):",
        "  G                    Show / hide graph panel (right half of screen)",
        "  Space                Pin / unpin current series (column or row)",
        "                       Pinned series are always shown regardless of cursor",
        "  a                    Toggle axis: rows→X (default) / cols→X",
        "  s                    Toggle Y-scale: linear / log",
        "  :gt bar|line|dot     Graph type (bar chart / line / dots)",
        "  :gt log|lin          Y-scale (same command, different argument)",
        "  Status bar shows:    [G: <type> <scale> <axis>]",
        "",
        "Graph mode (g):",
        "  ←→ / j k / h l       Move cursor",
        "  m / M                Jump to min/max value",
        "  q / Esc              Exit",
        "  :gt bar|line|dot     Graph type",
        "  :gc red|green|...    Color (red, green, blue, yellow, cyan, magenta, white)",
        "  :gy log|linear       Y-scale",
        "  :ga on|off           Anomaly highlighting (>3σ)",
        "  :gx <column>         Use date column as X-axis  [Tab = autocomplete column]",
        "  :gx off              Back to row numbers",
        "  :gp on|off           Show cursor on points",
        "",
        "Computed columns (:cf <formula>):",
        "  Syntax:  :cf <expression>",
        "  Example: :cf price * qty",
        "           :cf round((revenue - cost) / revenue * 100, 2)",
        "           :cf if(qty > 0, revenue / qty, 0)",
        "           :cf col_sum(amount) - col_sum_all(amount)",
        "  Operators:   + - * /  ( )  unary -",
        "  Comparisons: = != < <= > >=  (used inside if())",
        "  Functions:   round(x,n)  abs(x)  floor(x)  ceil(x)",
        "               mod(x,y)  pow(x,y)  if(cond,t,f)  empty(col)",
        "  Aggregates (current filter):  col_sum  col_avg  col_min  col_max",
        "               col_count  col_median  col_percentile(col,p)",
        "               col_stddev  col_var  col_rank(col)  col_pct(col)",
        "  Aggregates (whole file):      same names with _all suffix",
        "               e.g. col_sum_all(revenue)  col_rank_all(score)",
        "",
        "Split mode (--split):",
        "  csvview --split --by=<column> [--output-dir=<dir>] [--drop-col] file.csv",
        "  - Splits one CSV into N files, one per unique value of <column>",
        "  - Output filenames: <original>_<value>.csv",
        "  - Headers are copied to every output file",
        "  - --output-dir=<dir>   Write output files into <dir> (created if needed)",
        "  - --drop-col           Remove the split column from all output files",
        "  - Progress and summary printed to stdout",
        "",
        "Concat mode (--cat):",
        "  csvview --cat [--column=NAME] [--output=FILE] file1.csv file2.csv ...",
        "  - Adds column with source filename (without .csv)",
        "  - Headers taken from first file only",
        "  - All files must have same number of columns",
        "  - If --output not set → merged_YYYYMMDD_HHMMSS.csv",
        "  - Progress bar shown during merge",
        "",
        "File history (csvview with no arguments):",
        "  csvview without arguments opens a picker with recent files",
        "  ↑↓ / j k             Select file",
        "  Enter                Open selected file",
        "  Home / End           First / last entry",
        "  q / Esc              Quit",
        "  History stored in ~/.csvview_history (max 20 files, newest first)",
        "",
        "On first launch (no .csvf file):",
        "  Column setup window opens automatically",
        "  Status line shows: Headers: ON [H to toggle] / Headers: OFF [H to toggle]",
        "  H                    Toggle headers ON/OFF (preview row updates live)",
        "  S / N / D            Set column type for selected column",
        "  Enter                Edit format for selected column",
        "  q / Esc              Save settings and open the table",
        "",
        NULL
    };    

    if (use_ncurses)
    {
        // Подсчёт строк справки
        int max_lines = 0;
        while (help_text[max_lines]) max_lines++;

        // Размеры окна
        int win_h = LINES - 6;              // почти весь экран
        if (win_h > max_lines + 4) win_h = max_lines + 4;
        int win_w = 120;                    // фиксированная удобная ширина
        int start_y = (LINES - win_h) / 2;
        int start_x = (COLS - win_w) / 2;

        WINDOW *win = newwin(win_h, win_w, start_y, start_x);
        if (!win) return;

        box(win, 0, 0);
        keypad(win, TRUE);

        // Заголовок окна
        wattron(win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(win, 0, (win_w - 20) / 2, " CSVView Help ");
        wattroff(win, COLOR_PAIR(3) | A_BOLD);

        // Подсказка управления (всегда внизу)
        wattron(win, COLOR_PAIR(6));
        mvwprintw(win, win_h - 1, 2,
                  "↑↓ j k — строка | PageUp/Down — страница | Home/End — начало/конец | Esc/q — закрыть");
        wattroff(win, COLOR_PAIR(6));

        int top_line = 0;                   // первая видимая строка текста
        int visible_lines = win_h - 3;      // сколько строк видно

        while (1)
        {
            werase(win);
            box(win, 0, 0);

            // Перерисовываем заголовок
            wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, 0, (win_w - 20) / 2, " CSVView Help ");
            wattroff(win, COLOR_PAIR(3) | A_BOLD);

            // Выводим видимую часть справки с подсветкой разделов
            int line = 1;  // строка внутри окна
            for (int i = top_line; i < max_lines && line < visible_lines; i++)
            {
                const char *text = help_text[i];

                // Подсветка основных заголовков разделов
                if (strstr(text, "Usage:") ||
                    strstr(text, "Key Bindings (main view):") ||
                    strstr(text, "Command mode (press : then command):") ||
                    strstr(text, "Filter quick commands (: then command):") ||
                    strstr(text, "Saved filters window:") ||
                    strstr(text, "Advanced filter syntax (Shift+F):") ||
                    strstr(text, "Column settings (t):") ||
                    strstr(text, "Column statistics (d):") ||
                    strstr(text, "Pivot table (p / P):") ||
                    strstr(text, "Pivot table graph (G in pivot mode):") ||
                    strstr(text, "Computed columns (:cf <formula>):") ||
                    strstr(text, "Split mode (--split):") ||
                    strstr(text, "Graph mode (g):") ||
                    strstr(text, "Concat mode (--cat):") ||
                    strstr(text, "File history (csvview with no arguments):") ||
                    strstr(text, "On first launch (no .csvf file):"))
                {
                    wattron(win, COLOR_PAIR(5) | A_BOLD);  // ярко-зелёный + жирный
                    mvwprintw(win, line++, 2, "%s", text);
                    wattroff(win, COLOR_PAIR(5) | A_BOLD);
                }
                else
                {
                    // Обычный текст — серый/белый
                    wattron(win, COLOR_PAIR(1));
                    mvwprintw(win, line++, 2, "%s", text);
                    wattroff(win, COLOR_PAIR(1));
                }
            }

            // Подсказка управления (всегда видна)
            wattron(win, COLOR_PAIR(6));
            mvwprintw(win, win_h - 1, 2, "↑↓ / j k — scroll | PageUp/PageDown — page | Home/End — top/bottom | Esc/q — close");
            wattroff(win, COLOR_PAIR(6));

            wrefresh(win);

            int ch = wgetch(win);

            if (ch == KEY_UP || ch == 'k' || ch == 'K')
            {
                if (top_line > 0) top_line--;
            }
            else if (ch == KEY_DOWN || ch == 'j' || ch == 'J')
            {
                if (top_line < max_lines - visible_lines) top_line++;
            }
            else if (ch == KEY_PPAGE)
            {
                top_line -= visible_lines;
                if (top_line < 0) top_line = 0;
            }
            else if (ch == KEY_NPAGE)
            {
                top_line += visible_lines;
                if (top_line > max_lines - visible_lines) top_line = max_lines - visible_lines;
            }
            else if (ch == KEY_HOME)
            {
                top_line = 0;
            }
            else if (ch == KEY_END)
            {
                top_line = max_lines - visible_lines;
                if (top_line < 0) top_line = 0;
            }
            else if (ch == 27 || ch == 'q' || ch == 'Q')
            {
                break;
            }
        }

        delwin(win);
        touchwin(stdscr);
        refresh();
    }
    else
    {
        setlocale(LC_ALL, "");

        // Простой вывод в консоль
        for (int i = 0; help_text[i]; i++) {
            printf("%s\n", help_text[i]);
        }
        printf("\nPress Enter to exit...\n");
        getchar();
    }
}