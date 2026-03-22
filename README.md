# csvview

**Fast interactive CSV viewer and editor for the terminal.**
No Python. No Electron. No dependencies — just C and ncurses.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)](#install)
[![Language](https://img.shields.io/badge/language-C-orange)](#build-from-source)

![Pivot table](assets/screenshot-pivot-1.png)

![Pivot table settings](assets/screenshot-pivot-2.png)

![Pivot table with graph](assets/screenshot-pivot-3.png)

---

## Why csvview?

Most terminal CSV tools either lack interactivity (miller, xsv, csvkit) or require a heavy runtime (VisiData needs Python, TAD needs Electron). csvview is a **native C binary** — instant startup, minimal memory, works on any server over SSH.

| Feature | csvview | VisiData | sc-im | csvlens |
|---|:---:|:---:|:---:|:---:|
| Interactive TUI | + | + | + | + |
| Edit cells | + | + | + | — |
| Filter + sort | + | + | + | filter only |
| Pivot tables | + | + | — | — |
| Computed columns | + | Python expr | formulas | — |
| 50M+ row files | + | slow | — | + |
| No runtime needed | + | — Python | + | + |
| Single binary | + | — | + | + |

---

## Install

### Build from source

```sh
git clone https://github.com/daniil-khanin/csvview.git
cd csvview
make
sudo make install        # installs to /usr/local/bin + man page
```

Requires: `clang` (or `gcc`), `ncurses` (pre-installed on macOS and most Linux distros).

### macOS (Homebrew)

```sh
brew tap daniil-khanin/csvview
brew install csvview
```

---

## Quick start

```sh
csvview data.csv          # open a CSV file
csvview data.tsv          # TSV auto-detected
csvview --sep=\; data.csv # custom delimiter
csvview                   # recent files picker
```

**Essential keys:**

| Key | Action |
|---|---|
| Arrows / `hjkl` | Move cursor |
| `Shift+F` | Filter (e.g. `revenue > 1000 AND status = paid`) |
| `[` / `]` | Sort ascending / descending |
| `d` | Column statistics (sum, mean, median, histogram) |
| `p` | Pivot table |
| `M` | Mark column for multi-series graph |
| `Ctrl+G` | Graph marked columns (or just current column) |
| `Enter` | Edit cell |
| `:N` | Go to row N |
| `?` | Full help |

---

## Features

### Navigation & display
- Vim-style keys (`hjkl`) + arrow keys
- `PageUp`/`PageDown`, `Home`/`End`, `:N` / `Ctrl+G` to jump to row by number
- Freeze columns (`z`, `:fz N`) — pinned during horizontal scroll
- Hide/show columns (`X` in column settings)
- Column width adjustment (`w`/`W`, `A` for auto-fit)
- File history picker (`csvview` with no arguments)

### Filtering
- Advanced syntax: `revenue > 500 AND status = paid`
- Date range matching: `date >= 2026-01`, `date <= 2025-08` (boundary month included)
- Quarter format: `date >= 2025-Q1 AND date <= 2025-Q3`
- Negation: `! cohort = 2016-04`
- **Tab autocomplete** for column names while typing — ghost text shown, `Tab` accepts
- Quick filter from current cell: `:fq`, `:fqn`, `:fqo`, `:fqu`
- Save/load filters (`:fs` / `:fl`) — stored in `<file>.csvf`

### Sorting
- Sort by any column ascending/descending (`[` / `]`)
- Works correctly with active filters
- Saved in `.csvf` alongside filter settings

### Editing
- Edit any cell (`Enter`)
- Add column left/right (`:cal` / `:car`)
- Delete column (`:cd`)
- Rename column (`:cr`)
- Delete row (`:dr N`)
- Save file (`s`)
- Reload from disk (`Ctrl+R`) — keeps filter and sort

### Computed columns
Fill a column by formula:
```
:cf price * qty
:cf round((revenue - cost) / revenue * 100, 2)
:cf if(qty > 0, revenue / qty, 0)
:cf col_pct(revenue)           # % of filtered total
:cf col_rank_all(score)        # rank across whole file
```
Functions: `round`, `abs`, `floor`, `ceil`, `mod`, `pow`, `if`, `empty`
Aggregates (filter): `col_sum`, `col_avg`, `col_min`, `col_max`, `col_count`, `col_median`, `col_percentile`, `col_stddev`, `col_var`, `col_rank`, `col_pct`
Aggregates (whole file): same with `_all` suffix

### Pivot tables
- Group by any row/column combination
- Aggregations: SUM, AVG, COUNT, MIN, MAX, UNIQUE COUNT
- Multi-aggregates: SUM+COUNT, SUM+AVG, MIN+MAX, etc. shown as sub-columns
- Row/column totals and grand total
- Sort by key or value (ascending/descending)
- Date grouping: month / quarter / year
- **Drill-down**: `Enter` on any cell → main table filtered by that cell's values
- Export to CSV (`:e`)
- Parallel aggregation — up to 8 threads; real-time progress bar on large files

### Graphs
- Line, bar, and dot chart types; linear and log scale
- **Multi-series overlay**: press `M` to mark columns, `Ctrl+G` to draw all on a shared Y axis — each series in its own color
- **Series toggle**: press `1`–`9` in graph mode to hide/show individual series; legend shows `[N]-name`
- **Zoom**: `+`/`=` zoom in ~4× around cursor, `-` zoom out, `0` reset to full view
- **Pan**: moving the cursor past the zoom boundary scrolls the window — navigate the entire dataset while zoomed
- **Cursor mode** (`:gp on`): shows value at cursor; in multi-series — one shared `X:` label + `Y:` per series in its color
- Jump to min/max with `m`/`M`; 4 Y-axis labels (top, 1/3, 2/3, bottom)
- **Grid lines**: `:grid y|x|yx|off` — dim horizontal/vertical grid drawn under data
- **Dual Y axis** (`:g2y on/off`): series 1 uses left axis, series 2+ use right axis — each group gets its own scale; labels drawn in series color
- **Scatter plot** (`:gsc x_col [y_col]`): braille dot cloud of one column vs another; shows Pearson r in corner; `←`/`→` cursor shows nearest point (X, Y); `:gsc off` to exit
- **SVG export** (`:gsvg [file] [WxH]`): export current graph view to SVG — respects zoom, grid, hidden series, dual Y axis, scatter mode. White background, print-ready. Default size 900×500; example: `:gsvg report.svg 1200x700`
- Anomaly highlighting (values > 3σ)
- Date column as X axis (`:gx <column>` — Tab autocomplete)
- **Pivot graph**: `G` in pivot mode splits screen — table on left, chart on right
- Parallel value extraction — fast rendering on files with 10M+ rows

### Column statistics (`d`)
- Numeric: sum, mean, median, mode, min/max, stddev, top-10 values + histogram
- Date: distribution by month/quarter/year + histogram

### Multi-file operations
- **Concat** (`--cat`): merge multiple CSVs, adds source-filename column
- **Split** (`--split --by=<col>`): split one CSV into N files by column value

### File formats
- CSV (`,`), TSV (`.tsv` → auto-detected), PSV (`.psv` → auto-detected)
- Custom delimiter: `--sep=<char>` (e.g. `--sep=\;`)
- European decimal separator (`,`) handled everywhere
- **Comment lines** (`#`): VCF, GFF, GTF, scientific `.dat` files — `:comments on` skips `#`-prefixed
  lines from the table and collects them; press `#` to view in a scrollable popup.
  Status bar shows `[ # N ]` when comments are present. Setting saved in `.csvf`.

---

## Command reference

```
:N              Go to row N (1-based)
:e [file]       Export current view to CSV
:dr N           Delete row N
:cr name        Rename current column
:cal [name]     Add column to the LEFT of current
:car [name]     Add column to the RIGHT of current
:cf <formula>   Fill column by formula
:cd name        Delete current column
:cs [file]      Export current column to CSV
:freeze N       Freeze first N columns
:comments on    Skip lines starting with # (collected, view with # key)
:comments off   Show # lines as regular data rows
:fs             Save current filter
:fl             Show saved filters
:fq             Quick filter: current cell = value (AND)
:fqn            Quick filter: current cell != value (AND)
:fqo            Quick filter: current cell = value (OR)
:fqu            Quick filter: reset and apply

Graph commands (in graph mode):
:grid y|x|yx|off        Grid lines (horizontal / vertical / both / off)
:gp on|off              Cursor; multi-series shows per-series Y tooltip
:gt bar|line|dot        Graph type
:gy log|linear          Y-scale
:gx <column>            Date column as X axis
:g2y on|off             Dual Y axis (series 1 = left, rest = right — own scale)
:gsc x_col [y_col]      Scatter plot: dot cloud + Pearson r in corner
:gsc off                Exit scatter mode
:gsvg [file] [WxH]      Export graph to SVG (default: <file>_graph.svg 900x500)
```

---

## Key bindings

```
Arrows / hjkl       Move cursor
PageUp/PageDown     Scroll one page
Home/End            First/last row
/                   Search
n / N               Next/previous result
Shift+F             Advanced filter
f                   Quick filter (legacy syntax)
Enter               Edit cell
s                   Save file
Ctrl+R              Reload from disk
t                   Column settings
w / W               Increase/decrease column width
A                   Auto-fit column width
[ / ]               Sort ascending/descending
{ / }               Add sort level ascending/descending
r / R               Reset sort / reset filter
z                   Freeze/unfreeze up to current column
-                   Hide/show current column
d / D               Column statistics
p / P               Pivot table (P = always open settings)
M                   Mark column for multi-series graph
Ctrl+G              Graph: marked columns + current column

In graph mode:
  ← → / h l        Move cursor (pans when zoomed to edge)
  1–9               Toggle series visibility
  + / =             Zoom in    - Zoom out    0 Reset zoom
  m / M             Jump to min/max value
  :grid y|x|yx|off        Grid lines
  :gp on|off              Cursor on points (multi-series shows per-series Y values)
  :g2y on|off             Dual Y axis
  :gsc x_col [y_col]      Scatter plot; :gsc off to exit
  :gsvg [file] [WxH]      Export graph to SVG

?                   Help
q / Esc             Quit
```

---

## Build & install

```sh
make              # build ./csvview
make install      # install to /usr/local/bin and /usr/local/share/man/man1/
make uninstall    # remove installed files
```

Custom prefix:
```sh
make install PREFIX=/opt/homebrew
```

---

## License

MIT — see [LICENSE](LICENSE).

---

~80% of the code was written by [Claude Code](https://claude.ai/code) (Anthropic), ~20% by the author.
