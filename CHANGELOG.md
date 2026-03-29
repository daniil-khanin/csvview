# Changelog

## v19 — 2026-03-29

### New features

- **Donut chart** (`:gpie [col]`): shows value-frequency distribution for any column as an
  ASCII donut with colored slices. Up to 8 slices; remainder merged into "Other". Legend shows
  name, percentage and total n. Pressing `Ctrl+G` on a non-numeric column automatically opens
  the donut chart for that column instead of showing an error.

- **Box plot** (`:gbox [col ...]`): horizontal box-and-whisker for one or more numeric columns
  on a shared X scale. Shows min / Q1 / median / Q3 / max per column with numeric summary line.
  Multiple columns can be given as space-separated names or letters; without args uses the
  current column (or all columns marked with `M`).

- **Correlation heatmap** (`:gheat`): full-screen Pearson r matrix for all visible numeric
  columns. Cells are color-coded: green ≥ 0.7, cyan ≥ 0.3, blue ≈ 0, yellow ≤ −0.3, red ≤ −0.7.
  Diagonal shown as bold 1.00. Color legend at the bottom.

- **Command palette** (`: Tab`): pressing Tab at the start of a `:` command shows a popup
  with matching known commands. Arrow keys navigate; Enter selects.

- **Command history** (`:` + `↑`/`↓`): command history in the `:` prompt, shell-style.
  Up to 50 entries, no duplicates.

- **Percentile breakdown** (`[p]` in column stats): shows P1/P5/P10/P25/P50/P75/P90/P95/P99
  plus IQR, stddev, and mean in a dedicated popup.

- **Value trends** (`[e]` in column stats): splits numeric values into 20 equal time-buckets
  and shows min/mean/max per bucket as a Unicode sparkline ▁▂▃▄▅▆▇█.

- **Correlation matrix** (`:corr`): Pearson r popup for all numeric columns with arrow-key
  navigation. Press Enter on a cell to launch a scatter plot for that pair.

- **Outlier report** (`:outliers [sigma]`): lists rows with |z-score| ≥ threshold (default 3.0).
  Press Enter on a row to jump to it in the table; `f` applies a filter for that value.

### Bug fixes
- Donut/box/heatmap chart area confined strictly inside the table frame (clears cols 1..width-2,
  preserving left and right border characters).
- Status bar shows mode name (`donut`, `boxplot`, `heatmap`) instead of line/bar/dot for new
  chart types.

## v17 — 2026-03-22

### New features

- **Scatter plot** (`:gsc x_col [y_col]`): plots one column against another as a braille dot
  cloud. Pearson r shown in corner (in series color). `←`/`→` cursor finds nearest point and
  shows `X: value  Y: value` tooltip. Multi-series: mark columns with `M`, then `:gsc x_col` —
  each Y series in its own color. `:gsc off` exits scatter mode. Error shown if X = Y column.

- **Dual Y axis** (`:g2y on/off`): in multi-series graph mode, series 1 uses the left axis and
  series 2+ use the right axis, each group independently auto-scaled. Axis labels drawn in the
  corresponding series color. Useful when series have very different magnitudes.

- **Per-series min/max tooltip**: `m`/`M` in multi-series mode places each series' `@` marker at
  its own min/max, with `X:value Y:value` label drawn near the marker in series color — so each
  series shows its own extremum independently.

- **Series numbers in tooltip**: cursor tooltip now uses `1:`, `2:` series numbers instead of
  truncated column names to avoid overlap on narrow screens.

- **SVG export** (`:gsvg [file] [WxH]`): export the current graph view to a print-ready SVG
  file with white background. Respects zoom window, hidden series, grid lines, dual Y axis,
  and scatter mode. Default size 900×500; custom size via e.g. `:gsvg report.svg 1200x700`.
  Line graphs → `<polyline>`, dots/scatter → compact `<path M x,y h0>` (small file even for
  large datasets). Pearson r shown in scatter SVG; legend for multi-series.

### Bug fixes
- Fixed: `?` help key did not work in graph mode or pivot table — now opens the help screen
  from both modes (graph key handler and both pivot event loops).
- Fixed: scatter cursor could not move to the right — `graph_visible_points` was not set by
  `draw_scatter`, leaving a stale value from a previous graph session.
- Fixed: scatter X axis labels used `%.3g` causing inconsistent notation (e.g. `573` vs `1e+03`
  for adjacent ticks); now uses `%g` for consistent formatting.
- Fixed: scatter axes (X/Y lines) and grid were not drawn — added `draw_bresenham` axis calls
  and `graph_grid` support identical to line/dot/bar graphs.

---

## v16 — 2026-03-22

### New features

- **Multi-series graph overlay**: press `M` to mark numeric columns (underlined in header),
  then `Ctrl+G` to draw all marked + current column on one graph with a shared Y axis.
  Each series gets its own color; a color legend is shown at the bottom with `[N]-name` labels.

- **Series visibility toggle**: in graph mode, keys `1`–`9` hide/show individual series.
  Hidden series are dimmed in the legend; all series reset to visible on each new `Ctrl+G`.

- **Zoom and pan**:
  - `+` / `=` — zoom in ~4× around cursor position
  - `-` — zoom out ~2×
  - `0` — reset to full view
  - Moving the cursor past the zoom boundary **pans** the window automatically, so the entire
    dataset is reachable while staying zoomed in.

- **Multi-series cursor** (`:gp on`): each series shows its own `@` marker in its color;
  a single shared tooltip at the top shows `X: <value>` once and `colname: Y` per series.

- **Grid lines** (`:grid y|x|yx|off`): draws dim horizontal (Y) and/or vertical (X) lines
  before the braille data so data overlays the grid cleanly.

- **4 Y-axis labels**: top / 1/3 / 2/3 / bottom positions instead of just min/max.
  Log scale uses log-spaced values; format uses `%.3g` for compact display.

### Bug fixes
- Fixed: multi-series overlay — series 2+ wrote empty braille `⠀` cells over series 1's
  visible characters. Now braille rendering always skips `code==0` cells (canvas is cleared
  with spaces beforehand, so this is safe and also preserves grid lines).

---

## v15 — 2026-03-22

### New features
- **Comment lines support**: files with `#`-prefixed header lines (VCF, GFF, scientific data)
  are now handled correctly. `:comments on/off` skips `#` lines and blank preamble from the
  main table; skipped lines are collected and viewable in a dedicated window (`#` key).
  Setting is saved in `.csvf` and restored on reopen.
- **Multi-column sort**: `{`/`}` adds current column as the next sort level (ascending/descending);
  headers show `↑1`/`↓2` level indicators. `:sort col asc, col2 desc` for named sort.
- **Separator cycling** in column settings (`C`): cycles `, → ; → tab → |`; triggers file reload;
  saved in `.csvf` so custom delimiter is restored on reopen.
- **File history picker improvements**: missing files show an error and are removed from history
  automatically; `d` key for manual removal; list stays open if entries remain.
- **Relative line numbers** (`:rn`): toggle distance-from-cursor numbering (vim-style).

### Bug fixes
- Fixed: saved delimiter not applied when reopening a file — now preloaded before row indexing
- Fixed: `r` resets sort only; `R` resets filter (were both resetting sort)
- Fixed: graph cursor used `j`/`k` conflicting with table navigation — removed, left `h`/`l` + arrows
- Fixed: pivot Y-scale command unified as `:gy log|lin`

---

## v14 — 2026-03-21

### New features
- **Tab autocomplete for column names** in filter input (`f` / `Shift+F`) and command mode (`:`):
  ghost text shown in dim colour, `Tab` accepts, continuing to type overwrites
- **Quarter filter format** `YYYY-Qn`: `date >= 2025-Q1 AND date <= 2025-Q3` now works correctly
- **Parallel pivot aggregation** (Pass 2): up to 8 threads, ~2× speedup on 16M+ row files;
  progress bar updates in real time during aggregation
- **Parallel graph extraction**: background threads fill value arrays; large datasets render faster
- **No-malloc field extraction** throughout pivot and graph paths — eliminates millions of small
  allocations per pass

### Bug fixes
- Fixed: sort segfault on files with mixed string/numeric columns — union `.str` was read when
  `is_num=1`, producing a garbage pointer
- Fixed: `<= YYYY-MM` filter excluded the boundary month (e.g. `<= 2025-08` skipped August rows)
  because `strcmp("2025-08-15", "2025-08") > 0` at the null terminator; fixed with `strncmp(…, 7)`
- Fixed: progress bar stuck at 0% during parallel pivot — `pthread_join` blocked the main thread;
  replaced with polling loop (`nanosleep(100 ms)`) on `volatile int done`
- Fixed: Linux build — added `-lm` linker flag and `#define _XOPEN_SOURCE 700` for `strptime`

---

## v13 — 2026-03-19

### New features
- **50 million row limit** (raised from 10M): `filtered_rows` and `sorted_rows` arrays are now
  dynamically allocated, removed the fixed 10M cap
- **Pivot optimisation** (Pass 1): fields parsed once per row with a stack-local extractor instead
  of allocating all fields; sequential scan order for better cache locality; tuned hash table sizes

### Bug fixes
- Fixed: sort crash on very large files — pre-extract sort keys into a flat `SortKey[]` array
  before `qsort`; comparator no longer calls into `mmap` (was causing random SIGBUS)

---

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
