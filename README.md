# flux.h

Elm Architecture TUI framework for C99. Single header, zero dependencies.

Inspired by [charmbracelet/bubbletea](https://github.com/charmbracelet/bubbletea)
for Go. ~1700 lines with built-in components. Works on any POSIX system (Linux, macOS, FreeBSD, etc.).

## Features

- **Elm Architecture** -- Init / Update / View lifecycle
- **Diff-based rendering** -- only redraws changed lines (no flicker)
- **Async commands** -- background work via pthreads, results delivered as messages
- **Box drawing / borders** -- lipgloss-inspired primitives (rounded, sharp, double, thick)
- **24-bit color** -- `FLUX_FG(r,g,b)` / `FLUX_BG(r,g,b)` macros and runtime helpers
- **UTF-8 aware** -- `flux_strwidth()` computes display width, skipping ANSI escapes
- **String builder** -- `FluxSB` for building view output without manual buffer math
- **Raw mode, alt screen, signal handling** -- all managed by the framework

## Quick Start

```c
// counter.c
#define FLUX_IMPL
#include "flux.h"
#include <stdio.h>

typedef struct { int count; } Counter;

static FluxCmd counter_init(FluxModel *m) {
    ((Counter *)m->state)->count = 0;
    return FLUX_CMD_NONE;
}

static FluxCmd counter_update(FluxModel *m, FluxMsg msg) {
    Counter *s = m->state;
    if (flux_key_is(msg, "up"))    s->count++;
    if (flux_key_is(msg, "down"))  s->count--;
    if (flux_key_is(msg, "q"))     return FLUX_CMD_QUIT;
    return FLUX_CMD_NONE;
}

static void counter_view(FluxModel *m, char *buf, int bufsz) {
    Counter *s = m->state;
    snprintf(buf, bufsz, "Count: %d\n\nup/down to change, q to quit\n", s->count);
}

int main(void) {
    Counter state = {0};
    FluxProgram prog = {
        .model = { .state = &state, .init = counter_init,
                   .update = counter_update, .view = counter_view },
        .alt_screen = 1, .fps = 30,
    };
    flux_run(&prog);
}
```

```sh
cc -O2 -std=c99 -o counter counter.c -lpthread -lm
./counter
```

## Components

Built-in, reusable widgets — each follows the init/update/render pattern:

| Component | Description |
|-----------|-------------|
| `FluxList` | Scrollable list with cursor, j/k/pgup/pgdn, render callback |
| `FluxTabs` | Tab bar with icons, labels, Tab/Shift+Tab/number switching |
| `FluxInput` | Single-line text input with cursor, placeholder, submit |
| `FluxConfirm` | Yes/No dialog with arrow/tab selection, y/n shortcuts |
| `FluxSpinner` | Animated loading indicator (dot, line presets) |
| `FluxTable` | Scrollable table with headers, column widths, follow mode |
| `FluxKanban` | Multi-column card board with grab/move, edit dialog, scroll |

Layout helpers: `flux_bar()`, `flux_sparkline()`, `flux_divider()`, `flux_hbox()`, `flux_box()`, `flux_pad()`, `flux_count_lines()`, `flux_pad_lines()`

## Demo

An 8-tab showcase is included in `demo.c`:

| Tab | Content |
|-----|---------|
| 1 - Dashboard | Live metrics with sparklines, gauges, and status boxes |
| 2 - Explorer | Two-pane filesystem browser with preview |
| 3 - Cards | Selectable card grid with focus ring |
| 4 - Matrix | Falling-rain animation (async tick) |
| 5 - Log | Scrollable log viewer with level coloring |
| 6 - Help | Keybindings and component reference |
| 7 - Agent | Claude Code-like flow: auto-type, think, stream, diff, confirm |
| 8 - Kanban | 3-column board with card editing, grab/move, CSV persistence |

```sh
make
./demo    # Tab/Shift+Tab or 1-8 to switch, q to quit
```

## API Overview

```
FluxModel     state + Init/Update/View/Free function pointers
FluxProgram   model + options (alt_screen, mouse, fps); passed to flux_run()
FluxMsg       tagged union: MSG_KEY, MSG_WINSIZE, MSG_TICK, MSG_QUIT, MSG_CUSTOM
FluxCmd       deferred side-effect: FLUX_CMD_NONE, FLUX_CMD_QUIT, FLUX_TICK(ms)

flux_key_is() match key name ("up", "down", "enter", "q", ...)
flux_cols()   current terminal width
flux_rows()   current terminal height

flux_box()    draw bordered box around multi-line content
flux_pad()    pad/truncate string to exact column width
flux_strwidth() visible width in columns (UTF-8 + ANSI aware)
FluxSB        string builder: flux_sb_init, flux_sb_append, flux_sb_appendf, flux_sb_nl

FluxList      scrollable list with cursor and render callback
FluxTabs      tab bar with switching
FluxInput     single-line text input
FluxConfirm   yes/no dialog
FluxSpinner   animated loading indicator
FluxTable     scrollable table with headers
FluxKanban    multi-column kanban board
```

## Credits

Written by xuw (olealgoritme).

Inspired by [Bubble Tea](https://github.com/charmbracelet/bubbletea) by
[Charm](https://charm.sh). The original Go library pioneered the Elm
Architecture for terminal applications. flux.h follows the same architectural
philosophy but is written from scratch for C99.

## License

Copyright (c) 2026 xuw (olealgoritme). [MIT](LICENSE)
