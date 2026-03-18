# Changelog

## v12 — 2026-03-18

### New features
- **Go-to row**: `:N` jumps directly to row N; `Ctrl+G` prompts for row number
- **File history picker**: `csvview` with no arguments opens a recent-files picker (`~/.csvview_history`)
- **Computed columns** (`:cf <formula>`): fill a column by formula with full expression support
  - Operators: `+ - * / ( )`, unary `-`
  - Functions: `round`, `abs`, `floor`, `ceil`, `mod`, `pow`, `if`, `empty`
  - Filter aggregates: `col_sum`, `col_avg`, `col_min`, `col_max`, `col_count`, `col_median`, `col_percentile`, `col_stddev`, `col_var`, `col_rank`, `col_pct`
  - Whole-file aggregates: same with `_all` suffix (e.g. `col_sum_all`)
- **Column hiding**: `X` in column settings — hide/show any column, saved in `.csvf`
- **Freeze columns**: `z` freezes columns up to current; `:fz N` sets exact count; saved in `.csvf`
- **Pivot table graph**: `G` in pivot mode splits screen — pivot table on left, line/bar/dot chart on right
- **Multiple pivot aggregates**: `SUM+COUNT`, `SUM+AVG`, `MIN+MAX`, `SUM+COUNT+AVG`, etc. shown as sub-columns
- **Pivot drill-down**: `Enter` on a pivot cell opens the main table filtered by that cell's row and column values
- **Pivot table sorting**: static sort by key or value, ascending or descending
- **Adaptive first-column width** in pivot tables
- **File split** (`--split --by=<col>`): split one CSV into N files by column value; supports `--output-dir` and `--drop-col`
- **TSV/PSV/custom delimiter support**: `--sep=<char>`, auto-detected from `.tsv`/`.psv` extensions
- **Reload** (`Ctrl+R`): re-read file from disk, keeping active filter and sort; adjusts cursor
- **Graph enhancements**: jump to min/max with `m`/`M`; anomaly highlighting (`>3σ`); cursor on points; log scale
- **UI polish**: filename shown under action line; row count and file size shown in bottom bar
- **System install**: `make install` / `make uninstall` (default prefix `/usr/local`)
- **Man page**: `man csvview`

### Bug fixes
- Fixed: `:gx off` did not reset X axis back to row numbers
- Fixed: CSV parsing — replaced `strtok` with unified `parse_csv_line()` everywhere
- Fixed: last column not shown when scrolling many-column tables
- Fixed: `:car`/`:cal` double comma in header and column name merge on add
- Fixed: save bug — rows outside visible screen (no `line_cache`) written as empty
- Fixed: `:cf` generated empty lines after header and for hidden rows
- Fixed: pivot first-column overflow into numeric columns (long values)
- Fixed: `--sep=` flag was ignored (file taken from `argv[1]` instead of parsed input)
- Fixed: European decimal separator — numbers like `1234,56` now parsed correctly everywhere
- Fixed: filter + sort — sorting had no effect when a filter was active

---

## v11 — 2026-03-08

Initial public-facing version.

- Interactive TUI: navigate, search, filter, sort, edit cells
- Pivot tables with totals (row/column/grand)
- Line, bar, and dot graphs for numeric columns
- Advanced filter syntax: `=`, `!=`, `>`, `>=`, `<`, `<=`, `AND`, `OR`, `!`, date range matching
- Column statistics (`d`): sum, mean, median, mode, min/max, histogram, top-10 values
- Column type and format settings: string truncation, decimal places, date format
- Multi-file concat (`--cat`): merge CSVs with a source-filename column
- Column operations: add left/right (`:cal`/`:car`), delete (`:cd`), rename (`:cr`), export (`:cs`)
- Export current view to CSV (`:e`)
- Saved filters (`:fs`/`:fl`), quick filter from current cell (`:fq`)
- MIT license
