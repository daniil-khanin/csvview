# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
make              # build → ./csvview
make clean        # remove obj/ and binary
sudo make install # install to /usr/local/bin + man page
```

**macOS note:** requires Homebrew ncurses (`brew install ncurses`). The Makefile auto-detects its prefix via `brew --prefix ncurses`.

```bash
./csvview file.csv          # open file
./csvview --cat a.csv b.csv # concatenate CSVs
./csvview --split --by=col file.csv
./csvview --profile file.csv
./csvview --dedup file.csv
./csvview --sep=';' file.csv
```

There are no tests. Validate by building (`make`) and running manually.

## Architecture

**Single-process, multi-threaded TUI** built on ncursesw (wide-char). All globals are defined in `csvview.c` and declared `extern` in `csvview_defs.h`. Every other `.c` file includes `csvview_defs.h` to access shared state.

### Data flow

1. `csv_mmap.c` — maps the file via `mmap`; provides `csv_mmap_get_line(offset, buf, size)` to read a line by byte offset. `RowIndex[]` stores per-row `{offset, line_cache}`.
2. `csvview.c` — builds `row_indices[]`, detects separator/headers, owns the ncurses main loop and all global state.
3. **Filtering** (`filtering.c`) and **sorting** (`sorting.c`) produce `filtered_rows[]` / `sorted_rows[]` index arrays using pthreads (up to 8 threads).
4. `ui_draw.c` — reads those index arrays + globals to render the table on screen each frame.
5. Feature modules (`pivot.c`, `graph.c`, `graph_svg.c`, `column_stats.c`, `table_edit.c`, `formula.c`, `bookmarks.c`, …) operate on the same shared globals and draw their own ncurses windows/panels.

### Key header

`src/csvview_defs.h` is the single source of truth for:
- All `#define` constants (MAX_ROWS=50M, MAX_COLS=702, MAX_LINE_LEN=8192, CSVVIEW_VERSION)
- All `typedef enum` / `typedef struct`
- `extern` declarations of every global variable

Changing a struct or constant here triggers a full rebuild (all `.c` files depend on all `.h` files per the Makefile rule).

### Field extraction without malloc

Hot paths (filtering, sorting, graph value extraction) use stack buffers and parse fields in-place via pointer arithmetic — no heap allocation per cell. The `no-malloc` pattern must be preserved in performance-critical code.

### Threading model

`pthreads` with a fixed pool (≤8 threads). Used in: `filtering.c`, `sorting.c`, `pivot.c` (Pass 2), `graph.c` (value extraction). Worker structs carry a slice of the row range; results are merged in the main thread after `pthread_join`.

### Format driver (file_format.h)

`g_fmt` (`FileFormatDriver *`) is set by `fmt_detect(filename)` at startup. The driver vtable has three functions:
- `parse_row(buf, &count)` — parse a raw line into `char**` fields (same contract as `parse_csv_line`)
- `build_row(fields, count, col_names, col_types)` — serialize fields back to a line
- `rebuild_header()` — update in-memory row caches after column rename (CSV: updates `rows[0]`; NDJSON: rebuilds all rows)

**All modules** (filtering, sorting, graph, pivot, column_stats, search, ui_draw) call `g_fmt->parse_row` — they are format-agnostic. Only `format_csv.c` and `format_ndjson.c` contain format-specific logic.

**NDJSON** (`src/format_ndjson.c`, `src/json_parse.c/.h`): one JSON object per line, no header row (`has_header_row=0`). Columns discovered by union-scanning first 200 rows. Values serialized with type-aware quoting via `col_types[]`.

### Graph rendering

`graph.c` renders line/bar/dot/scatter/donut/box/heatmap charts inside an ncurses window using braille Unicode characters. `graph_svg.c` exports to SVG. Color pairs start at `GRAPH_COLOR_BASE` (10) to avoid conflicting with theme color pairs defined in `themes.c`.

## Release workflow

1. Bump `CSVVIEW_VERSION` in `csvview_defs.h`
2. Update `CHANGELOG.md`
3. `git commit` + `git push` → `git tag vN` + `git push --tags`
4. Create GitHub Release (`daniil-khanin/csvview`)
5. Update Homebrew formula in `daniil-khanin/homebrew-csvview`
