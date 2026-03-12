# Product Backlog (last updated: 2026-03-13)

## Strategy
Niche: **fast CSV editor with analytics for the terminal, zero dependencies**.
C + ncurses — main competitive advantage over VisiData (Python).
Do not add: Parquet/Arrow/Excel, Python/Lua scripting, plugin system.
Goal: close basic gaps → strengthen unique features → scale to large files.

---

## P0 — critical, no release without this
- [x] Date filtering broken: user_cohort=2026-01 didn't work, user_cohort>=2026-01 did

## P1 — close basic gaps (expected by users coming from VisiData)
- [x] Pivot table: full PageUp, PageDown, Home, End, Home/End in line support
- [x] Merge multiple files into one, with an extra column filled from the source filename
- [x] Bug: active filter + sort — sort had no effect
- [x] Split file into multiple files by column value (`--split --by=<col>`)
- [—] Summary row in main table — decided not to do; use d (column stats) instead
- [x] Reload (Ctrl+R): re-read file without restarting; keeps filter and sort, adjusts cursor
- [x] Freeze columns: pin N left columns during horizontal scroll
      z — freeze/unfreeze current column. :fz N — exact count. Saved in .csvf.
- [x] Drill-down from pivot: Enter on a pivot cell → opens main table
      with auto-filter by row and column values of that cell
- [ ] Go-to row: jump to row by number (`:N` or Ctrl+G)

## P2 — strengthen unique advantages
- [x] UI polish: move filename to top under action line as -[ filename ]-, move row count and file size to bottom as -[ 100 000 ]--[ 168 Mb ]-
- [x] Adaptive first-column width in pivot tables
- [x] Graph: jump to min/max with a keypress instead of searching manually
- [x] Pivot graph: split screen — pivot table on the left, graph on the right
- [x] Pivot table sorting (static, via settings)
- [x] Computed columns: `:cf <formula>` — fills a real column by formula.
      Operators: +,-,*,/,(). Functions: round, abs, floor, ceil, mod, pow, if, empty.
      Filter aggregates: col_sum/avg/min/max/count/median/percentile/stddev/var/rank/pct.
      Whole-file aggregates: same with _all suffix.
- [x] TSV/PSV and other delimiters: --sep=<char>, auto-detect by extension (.tsv→tab, .psv→pipe).
      Unified parser parse_csv_line() and serializer build_csv_line() everywhere.
- [ ] Column reordering: move current column left/right.
      Ctrl+Left / Ctrl+Right — shift by one position. Changes order in data and header, saves file.
- [x] Column hiding: X in column settings (t) — hide/show column.
      Hidden columns are not displayed and don't interfere with scrolling. Saved in .csvf.
- [ ] Virtual columns: computed columns for the session only, don't modify the file.
      Useful for quick analysis without polluting the data.
- [x] Pivot → multiple aggregates: SUM+COUNT, SUM+AVG, MIN+MAX, etc. in one table.
      Selected in settings via ←→ on the Aggregation field. Shown as sub-columns.
- [ ] Export pivot to Markdown: `:em` — for reports in GitHub/Notion.
- [ ] Copy cell to clipboard (y — yank).
- [x] File history: csvview with no arguments → picker with recent files (~/.csvview_history).

## P3 — distribution and public release
- [x] License: MIT, LICENSE file added to the repository.
- [x] Man page: csvview.1 written, installs to /usr/local/share/man/man1/.
- [x] System install: `make install` / `make uninstall`, PREFIX variable for custom path.
- [ ] Homebrew formula: create Formula/csvview.rb, publish via tap (daniil-khanin/homebrew-csvview).
      Requires: public repository, tagged release with archive, sha256.
- [ ] Landing page: single-page site with description, install command, terminal screenshots.

## Icebox / Ideas / Later
- [ ] 100M+ rows: block indexing instead of static offsets array.
      Store every N-th offset, seek within a block. Architectural change.
      Opens a segment of files VisiData cannot open at all.
- [ ] Progressive statistics: count/min/max instantly, median/histogram as ready.
      Currently d on a large file = 20–60 sec with no feedback.
- [ ] File diff: --diff file1.csv file2.csv, line-by-line comparison,
      highlight discrepancies. For verifying actual_output.csv vs expected.csv.
- [ ] Interactive pivot sorting (hotkeys directly in the table)
- [ ] Deduplication: :dedup — remove duplicate rows by current column or full row.
- [ ] Sampling: :sample N — random sample of N rows for quick inspection of large files.
- [ ] Scatter plot: two numeric columns → dot chart (x/y)
- [ ] JSON export: :ej

## Bugs
- [x] :gx off command didn't work (should reset X axis to row numbers)
- [x] CSV parsing bug — fixed by unified parse_csv_line() everywhere, strtok removed
- [x] Last column not shown when table has many columns and scrolling
- [x] :car/:cal bug: double comma in header and column names merging on add
- [x] Save bug: rows outside screen (no line_cache) were written as empty
- [x] :cf bug: empty lines after header and every other line for hidden rows
- [x] Pivot: long values in first column overflowed into numeric columns.
      Cause: %-*s doesn't truncate strings longer than the given width.
      Fixed: snprintf into buffer before mvprintw for row labels and column headers.
- [x] --sep= flag ignored: file taken from argv[1] instead of parsed input_files[0].
- [x] European decimal separator: numbers like "1234,56" not parsed as numbers.
      Fixed: parse_double() in utils normalizes comma→dot, used everywhere
      (display, pivot aggregates, column stats, sorting, filtering, graphs, formulas).
