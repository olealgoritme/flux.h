# flux.h

**A single-header C99 TUI framework for building AI agent interfaces.**

Single-header C99 · Zero runtime dependencies (libc + pthreads) · ~95 widgets · MIT license · Linux / macOS / FreeBSD

---

## What is flux.h

flux.h is a single-header terminal UI library for C99, inspired by the Elm
Architecture (init / update / view) and by storm / React-Ink for the widget
catalogue. Drop one file into your project, define `FLUX_IMPL` once, and you
have a full-fidelity TUI: cell renderer, diff engine, layout, mouse,
keyboard, scrolling, tabs, theme, and ~95 ready-made widgets.

It is built specifically for the kinds of UIs that AI agents and developer
tools need today: chat composers, streaming text, token / cost meters,
diff blocks, approval prompts, op trees, command palettes, status lines,
charts, and markdown viewers. The widgets exist because the apps need
them — not because they map nicely onto a React tree.

Everything is **width-correct**. Every widget honours an explicit
"exactly N display cells per row" contract that is ANSI- and UTF-8-aware,
so user-supplied text can never overflow a panel, smash a border, or
ruin the diff. The cell renderer keeps redraws minimal — typical frames
touch <1% of cells — so 30–120 FPS animations stay smooth over SSH.

---

## Quick start

A 30-line agent. Up/Down to count, `q` to quit.

```c
/* counter.c — cc -O2 -std=c99 -o counter counter.c -lpthread -lm */
#define FLUX_IMPL
#include "flux.h"

typedef struct { int count; } Counter;

static FluxCmd c_init(FluxModel *m) {
    ((Counter *)m->state)->count = 0;
    return FLUX_CMD_NONE;
}

static FluxCmd c_update(FluxModel *m, FluxMsg msg) {
    Counter *s = m->state;
    if (flux_key_is(msg, "up"))   s->count++;
    if (flux_key_is(msg, "down")) s->count--;
    if (flux_key_is(msg, "q"))    return FLUX_CMD_QUIT;
    return FLUX_CMD_NONE;
}

static void c_view(FluxModel *m, char *buf, int bufsz) {
    Counter *s = m->state;
    snprintf(buf, bufsz, "Count: %d\n\nup/down · q to quit\n", s->count);
}

int main(void) {
    Counter state = {0};
    FluxProgram p = {
        .model = { .state=&state, .init=c_init,
                   .update=c_update, .view=c_view },
        .alt_screen = 1, .fps = 30,
    };
    flux_run(&p);
}
```

---

## Why flux.h

- **Single header**, ~20k lines, ~500 public functions, **zero deps** (libc + pthreads).
- **95+ widgets** spanning core, forms, AI agent UIs, charts, markdown, effects.
- **Every widget is responsive** — output is exactly N display cells per row, ANSI + UTF-8 wide-char aware. Pass any text — no overflow possible.
- **Mouse**: SGR 1006 protocol with `PRESS`, `RELEASE`, `MOVE`, `WHEEL_UP`, `WHEEL_DOWN`.
- **Cell-level diff renderer** — typical frames skip ~99% of cells; `flux_diff_screens` emits the minimal ANSI patch.
- **Elm architecture** — `init` / `update` / `view`, immutable messages, async commands via pthreads.
- **Built-in scroll, tabs, layout, theme, window chrome** — wire up a multi-screen app in a few hundred lines.
- **Configurable FPS** — `FluxProgram.fps` controls the runloop cadence; per-widget tickers via `FLUX_TICK(ms)`.
- **No allocation in renderers** — all widgets write into a caller-owned `FluxSB`; stateful widgets take caller-owned backing buffers.

---

## Architecture

```
                      ┌─────────────────────────┐
   stdin (raw mode) ─►│  input parser           │── MSG_KEY ─┐
   SIGWINCH       ───►│  (keys, SGR mouse,      │── MSG_MOUSE│
   stdin paste    ───►│   bracketed paste)      │── MSG_PASTE│
                      └─────────────────────────┘── MSG_WIN ─┤
                                                              │
   ┌──────────────┐         ┌─────────────────┐               │
   │  command     │── MSG ─►│  update(model,  │◄──── MSG_TICK─┤
   │  pipe        │         │         msg)    │               │
   │  (pthreads)  │◄── cmd ─┤  → cmd          │◄── MSG_CUSTOM─┘
   └──────────────┘         └────────┬────────┘
                                     │
                            view(model, buf)
                                     │
                            ┌────────▼────────┐    ┌──────────────┐
                            │  back screen    │───►│  diff vs     │
                            │  (cells)        │    │  front       │
                            └─────────────────┘    └──────┬───────┘
                                                          │ minimal
                                                          ▼ ANSI
                                                       stdout
```

The runloop sits on `poll()`. Inputs become typed `FluxMsg`s. Your
`update` returns a `FluxCmd` (`FLUX_CMD_NONE`, `FLUX_CMD_QUIT`,
`FLUX_TICK(ms)`, or `flux_cmd_custom(id, data)`). Then `view` writes
plain text + ANSI into a buffer. The cell renderer parses that into a
`FluxScreen`, diffs it against the previous frame, and emits only the
changed cells.

```c
typedef enum {
    MSG_NONE, MSG_KEY, MSG_WINSIZE, MSG_QUIT,
    MSG_TICK, MSG_CUSTOM, MSG_PASTE, MSG_MOUSE,
} FluxMsgType;
```

---

## Widget catalog

flux.h ships ~95 widgets. They divide into eight thematic batches plus
the original v2/v3 set. Every widget honours the width contract.

### Core primitives (B1)

Tiny one-shot helpers that callers compose into rows. No state, no
init — call them as you compose your view.

- `flux_newline(sb, n)` — emit `n` bare `\n` bytes
- `flux_spacer(sb, width, rows)` — emit `rows` blank rows of `width` cells
- `flux_text(sb, text, &style)` — coloured / bold / italic text run
- `flux_heading(sb, level, text, width)` — H1 / H2 / H3 headings
- `flux_paragraph(sb, text, width)` — soft-wrap at word boundaries
- `flux_ol(sb, items, n, width, start)` — numbered list
- `flux_ul(sb, items, n, width, glyph)` — bulleted list
- `flux_kbd(sb, label)` — inline `[Ctrl+C]` keycap pill

```c
flux_heading(&sb, 1, "Welcome", 80);
flux_paragraph(&sb, "flux.h ships exactly the widgets agent UIs need.", 80);
flux_ul(&sb, (const char*[]){"single header","zero deps","width-correct"},
        3, 80, NULL);
```

### Forms & input (B2)

Stateful widgets following the `FluxInput` / `FluxConfirm` convention:
`init` once, forward `MSG_KEY` to `update`, call `render` from `view`.

- `FluxCheckbox` — `[x]` / `[ ]` toggle (with indeterminate)
- `FluxSwitch` — `[on]` / `[off]` pill
- `FluxRadio` — `(•)` single-select group
- `FluxSelect` — collapsed value with popover on open
- `FluxTextArea` — multi-line editor (soft wrap, paste, undo-style ops)
- `FluxMaskedInput` — input with format mask (`####-####-####-####`)
- `FluxSearchInput` — input with magnifier glyph + result count
- `FluxChatInput` — multi-line composer with history (Up / Down recall)
- `FluxStepper` — `- N +` numeric input (Shift+arrow for fast step)
- `FluxForm` — multi-field form with per-field validation

```c
FluxChatInput chat;
flux_chat_init(&chat, "Ask anything…");

/* update */ flux_chat_update(&chat, msg);

/* view   */ flux_chat_render(&chat, &sb, width, /*max_rows*/ 4);

/* after  */
const char *text = flux_chat_consume(&chat);
if (text) handle_user_message(text);
```

### Status & display (B3)

Surfaces for state, attention, and decoration. Most are stateless
free functions — `FluxToast` is the one timer-bearing widget.

- `flux_alert(kind, title, body, width)` — bordered info / warn / error / success box
- `flux_badge(text, fg, bg)` — small inline coloured pill
- `flux_tag(text, fg)` — outlined `[label]` chip
- `FluxToast` — transient overlay notification with TTL
- `flux_tooltip(body, x, y, width)` — popover bubble with arrow
- `flux_header(title, right, width)` — top bar with rule
- `flux_footer(hints, n, width)` — bottom strip with key hints
- `flux_avatar(initials, fg)` — `(JS)` parenthesised initials
- `flux_status_msg(kind, msg, width)` — single-row inline status
- `flux_placeholder(icon, title, hint, width)` — empty-state block
- `flux_link(text, url, fg)` — OSC-8 hyperlink (clickable in modern terminals)

```c
flux_alert(&sb, FLUX_KIND_ERROR, "Build failed",
           "exit code 2 — see log for details", 60);
```

### Navigation & modal (B4)

Backdrops, modals, accordions, menus, breadcrumbs, paginators.
Modal-family widgets repaint a full `screen_w × screen_h` viewport so
the diff engine handles z-order naturally — no compositor needed.

- `FluxModal` — popover dialog with backdrop (SM / MD / LG / FULL)
- `flux_overlay(color, alpha, w, h)` — backdrop dim helper
- `FluxAccordion` — multiple collapsible sections (exclusive or not)
- `FluxCollapsible` — single collapsible section
- `FluxContentSwitcher` — `[A | B | C]` segmented control
- `FluxMenu` — vertical menu list with hotkeys + shortcuts
- `FluxCommandPalette` — fuzzy-search command launcher (Cmd+K style)
- `FluxPaginator` — `‹ 1 2 [3] 4 5 ›` (DOTS / NUMBERS / FRACTION styles)
- `flux_breadcrumb(parts, n, active, width, …)` — `home › docs › auth`
- `flux_help_panel(bindings, n, width, title, …)` — keybindings reference

```c
FluxCmdItem cmds[] = {
    {"open", "Open File",  "browse", "File", "Ctrl+O", 0},
    {"save", "Save File",  "write",  "File", "Ctrl+S", 0},
    {"quit", "Quit",       "exit",   "App",  "Ctrl+Q", 0},
};
FluxCommandPalette palette;
flux_command_palette_init(&palette, cmds, 3);

/* somewhere */ palette.is_open = 1;

/* view */
if (palette.is_open) flux_command_palette_render(&palette, &sb, w, h);
else                 render_main(&sb, w, h);
```

### AI agent widgets (B5) — the headline batch

The widgets that motivated v4. Built for chat-driven agents, tool-call
streams, token/cost meters, and op trees.

- `FluxBlinkDot` — animated 1-cell state dot (pending / running / streaming / done / failed / cancelled)
- `flux_command_block(cmd, output, exit, ms, w)` — bordered shell command card
- `FluxCommandDropdown` — searchable command list with fuzzy match
- `flux_context_window(used, total, w)` — token budget bar with K/M formatter
- `flux_cost_tracker(in, out, $/M, $/M, prior, w)` — running session cost card
- `flux_message_bubble(role, text, ts, w)` — role-styled chat bubble (user / assistant / system / tool)
- `flux_model_badge(provider, model, max_tokens, w)` — `◆ Anthropic · claude-opus-4 200K`
- `FluxOpTree` — nested operation tree (collapsible, animated spinners per row)
- `FluxShimmerText` — text with traveling highlight effect (great for "thinking…" indicators)
- `FluxStreamingText` — typewriter reveal with blinking cursor
- `flux_token_stream(rate, total, max, w)` — live tok/sec strip with progress bar

```c
/* A typical agent header row */
flux_model_badge(&sb, "Anthropic", "claude-opus-4", 200000, 40);
flux_sb_append(&sb, "  ");
flux_context_window(&sb, used_tokens, 200000, 40);

/* And inside the message list */
flux_message_bubble(&sb, FLUX_ROLE_USER,      user_text, "12:34", w);
flux_message_bubble(&sb, FLUX_ROLE_ASSISTANT, reply,     "12:34", w);

/* Streaming response with cursor */
flux_streaming_render(&typer, &sb);
flux_sb_nl(&sb);
```

### Data & charts (B6)

A braille-pixel canvas underpins `flux_line_chart`,
`flux_area_chart`, `flux_scatter`. Bars, histograms, and heatmaps use
unicode block characters for sub-cell precision.

- `FluxBraille` — 2×4 sub-pixel canvas (foundation for line/area/scatter)
- `flux_area_chart(values, n, w, h, opts)` — filled sparkline with axes
- `flux_bar_chart(values, n, w, h, labels, opts)` — vertical / horizontal bars
- `flux_line_chart` / `flux_line_chart_multi` — braille line plots
- `flux_scatter(pts, n, w, h, opts)` — braille pixel scatter (with optional trend)
- `flux_histogram(bins, n, w, h, opts)` — bin chart
- `flux_heatmap(matrix, rows, cols, w, opts)` — coloured 2D grid
- `flux_gauge(value, min, max, w, opts)` — bar or semi-circle dial
- `flux_dl(items, n, w, opts)` — `key: value` aligned definition list
- `flux_pretty(json, w, opts)` — JSON / struct pretty-printer with colours
- `FluxRichLog` — colourised scrollable log over a ring buffer
- `FluxTree` — collapsible tree with cursor
- `FluxVirtualList` — windowed render for million-item lists
- `FluxDataGrid` — sortable, paged, resizable grid

```c
float cpu[60] = { /* 60 samples of CPU% */ };
flux_line_chart(&sb, cpu, 60, 80, 12, NULL);
```

### Effects (B7)

Pure-render decoration. Truecolor (24-bit) per cell — terminals without
24-bit colour degrade silently.

- `flux_digits(str, color, width)` — 3-row block-font display (`12:34` etc.)
- `flux_glow_text(text, fg, glow, width)` — text with simulated halo
- `flux_gradient_text(text, start, end, width)` — per-cell fg gradient
- `flux_gradient_bar(progress, width, start, end)` — progress bar with horizontal colour shift
- `flux_gradient_border(w, h, start, end, content_fn, ctx)` — bordered box with gradient frame

```c
flux_gradient_text(&sb, "READY",
    (FluxRGB){180,130,255}, (FluxRGB){100,255,200}, 80);

flux_digits(&sb, "12:34", FLUX_THEME_BRAND_PURPLE_FG, 80);
```

### Markdown, syntax, dev (B8)

Markdown rendering, syntax highlighting, file pickers, calendars, and
dev-tooling overlays.

- `flux_markdown(md, width)` — markdown → ANSI (headings, lists, code, blockquote, links, task lists, HR)
- `FluxMarkdownViewer` — markdown wrapped in a scrollview with optional TOC sidebar
- `flux_syntax_highlight(code, lang, width)` — C / JS / TS / JSON / SH / MD highlighter
- `FluxPerfHud` — fps / cpu / mem mini-overlay (reads `/proc/self/stat`)
- `FluxCalendar` — month grid with arrow-key navigation
- `FluxDatePicker` — calendar-driven date input
- `FluxDirTree` — `opendir` filesystem browser
- `FluxFilePicker` — modal file picker (open / save modes)
- `flux_welcome(title, version, model, hints, n, width)` — splash screen builder
- `flux_loading(label, frame, width)` — spinner + label
- `flux_inline_diff(before, after, width)` — word-level diff inside one row
- `FluxOptionList` / `FluxSelectionList` — multi-select list with `[x]` markers
- `FluxStopwatch` — counts up
- `FluxTimer` — counts down (returns 1 from `_tick` exactly once on finish)

```c
flux_markdown(&sb,
    "# Title\n"
    "\n"
    "**Bold** and *italic* with `inline code`.\n"
    "\n"
    "- list item one\n"
    "- list item two\n"
    "\n"
    "```c\nint main(void) { return 0; }\n```\n", 80);
```

### v2/v3 widgets (still here, still load-bearing)

The widgets that came with the cell renderer in v2/v3 — used by every
non-trivial demo and by `watt`.

- `FluxList` — scrollable list with cursor + render callback
- `FluxTable` — scrollable table with headers, column widths, follow mode
- `FluxKanban` — multi-column card board with grab/move + edit dialog
- `FluxTabs` / `FluxTabBar` — tab bars (legacy + clickable)
- `FluxInput` — single-line text input
- `FluxConfirm` — yes/no dialog
- `FluxSpinner` — animated loading indicator
- `FluxApproval` — multi-button approval prompt (storm-style)
- `FluxScroll` — viewport with scroll offset, wheel + arrow + pgup/pgdn
- `FluxPopup` — dropdown menu
- `FluxStatusBar` — multi-segment status line
- `FluxViewport` — region-based layout with fixed + fill regions
- `flux_box`, `flux_window_chrome`, `flux_brand`, `flux_divider`,
  `flux_hbox`, `flux_pad`, `flux_sparkline`, `flux_bar`,
  `flux_inline_bar`, `flux_message_row`, `flux_diff_block_render`,
  `flux_button_render`, `flux_activity_render` — composition primitives.

---

## Theme

flux.h ships a single coherent dark theme: 24-bit RGB SGR escape strings
defined as macros so they cost nothing at runtime. Every widget defaults
to these tokens; pass `NULL` for any colour parameter to use the theme
default.

| Token                          | Use                                    |
|--------------------------------|----------------------------------------|
| `FLUX_THEME_WINDOW_BG`         | Outermost desktop background           |
| `FLUX_THEME_TITLEBAR_BG`       | Window chrome titlebar                 |
| `FLUX_THEME_PANEL_BG`          | Inner panels and cards                 |
| `FLUX_THEME_CODE_BG`           | Code blocks                            |
| `FLUX_THEME_BUTTON_OK_BG`      | Confirm-style button background        |
| `FLUX_THEME_BUTTON_NO_BG`      | Cancel-style button background         |
| `FLUX_THEME_BORDER_FG`         | Default border colour                  |
| `FLUX_THEME_BORDER_WARN_FG`    | Warning-state border                   |
| `FLUX_THEME_DIVIDER_FG`        | Horizontal rule / divider colour       |
| `FLUX_THEME_TEXT_FG`           | Primary body text                      |
| `FLUX_THEME_TEXT2_FG`          | Secondary body text                    |
| `FLUX_THEME_TEXT_DIM_FG`       | Dim helper text                        |
| `FLUX_THEME_TEXT_OFF_FG`       | "Off" / disabled state                 |
| `FLUX_THEME_TEXT_INV_FG`       | Inverse (for badges on bright bg)      |
| `FLUX_THEME_ACCENT_FG`         | Interactive accent (focus, links)      |
| `FLUX_THEME_ACCENT_DIM_FG`     | Dim variant of accent                  |
| `FLUX_THEME_BRAND_PURPLE_FG`   | Brand colour (assistant / streaming)   |
| `FLUX_THEME_OK_FG`             | Success / completed                    |
| `FLUX_THEME_OK_DIM_FG`         | Dim success                            |
| `FLUX_THEME_WARN_FG`           | Warning / running                      |
| `FLUX_THEME_ERR_FG`            | Error / failed                         |
| `FLUX_THEME_ERR_DIM_FG`        | Dim error                              |
| `FLUX_THEME_DIFF_ADD_FG`       | Added line in diffs                    |
| `FLUX_THEME_DIFF_DEL_FG`       | Removed line in diffs                  |
| `FLUX_THEME_TRAFFIC_R/Y/G_FG`  | macOS-style window control dots        |

Plus aliased syntax-highlight tokens: `FLUX_THEME_SYNTAX_COMMENT_FG`,
`SYNTAX_STRING_FG`, `SYNTAX_KEYWORD_FG`, `SYNTAX_NUMBER_FG`,
`SYNTAX_TYPE_FG`, `SYNTAX_OPERATOR_FG`.

There is no theme switcher (yet). Override per-widget by passing your
own escape string instead of `NULL`.

---

## Mouse & keyboard input

### Keyboard

`MSG_KEY` carries a name string (matched with `flux_key_is`), a Unicode
rune, and ctrl/alt modifier flags.

```c
if (flux_key_is(msg, "up"))    /* arrow up */
if (flux_key_is(msg, "enter")) /* return */
if (flux_key_is(msg, "C-c"))   /* Ctrl+C */
if (flux_key_is(msg, "M-x"))   /* Alt+x */
if (flux_key_is(msg, "tab"))   /* tab */
if (flux_key_is(msg, "q"))     /* literal q */
```

Rune access for general printable input:

```c
if (msg.type == MSG_KEY && msg.u.key.rune >= 0x20)
    insert_char(msg.u.key.rune);
```

`MSG_PASTE` carries up to `FLUX_PASTE_MAX` bytes from a single bracketed
paste — use this in editors where Enter from the user differs from a
newline pasted from the clipboard.

### Mouse

Mouse is opt-in. Set `FluxProgram.mouse = 1` and the runtime enables
SGR 1006 mouse reporting at startup and disables it on exit.

```c
FluxProgram p = { .model = m, .alt_screen = 1, .mouse = 1, .fps = 30 };
```

Then in `update`:

```c
if (msg.type == MSG_MOUSE) {
    int x = msg.u.mouse.x;          /* 1-based */
    int y = msg.u.mouse.y;
    switch (msg.u.mouse.event) {
    case FLUX_MOUSE_PRESS:      handle_press(x, y, msg.u.mouse.button); break;
    case FLUX_MOUSE_RELEASE:    handle_release(x, y); break;
    case FLUX_MOUSE_MOVE:       handle_move(x, y); break;
    case FLUX_MOUSE_WHEEL_UP:   scroll_up();   break;
    case FLUX_MOUSE_WHEEL_DOWN: scroll_down(); break;
    }
}
```

Most widgets handle mouse internally — `FluxTabBar`, `FluxScroll`,
`FluxContentSwitcher`, `FluxMenu`, `FluxList`, `FluxKanban`, `FluxApproval`
all take `MSG_MOUSE` and respond to clicks / wheel events with no
additional wiring.

---

## Scrollable viewports

`FluxScroll` is a scroll offset over arbitrary newline-separated content.
Three pieces fit together:

```c
typedef struct App { FluxScroll scroll; /* … */ } App;

static FluxCmd app_init(FluxModel *m) {
    flux_scroll_init(&((App*)m->state)->scroll);
    return FLUX_CMD_NONE;
}

static FluxCmd app_update(FluxModel *m, FluxMsg msg) {
    App *a = m->state;
    flux_scroll_update(&a->scroll, msg);   /* keys + wheel */
    return FLUX_CMD_NONE;
}

static void app_view(FluxModel *m, char *buf, int bufsz) {
    App *a = m->state;
    int width = flux_cols(), height = flux_rows();

    /* 1. Build full content into one string (any height). */
    char content[64*1024]; FluxSB body; flux_sb_init(&body, content, sizeof content);
    for (int i = 0; i < 1000; i++) flux_sb_appendf(&body, "row %d\n", i);

    /* 2. Render viewport — pads if content < viewport_h, scrolls otherwise. */
    FluxSB out; flux_sb_init(&out, buf, bufsz);
    flux_scrollview_render(&a->scroll, &out, content, width, height - 2);

    /* 3. Indicator strip — `▮ 12/1000   j/k pgup/pgdn`. */
    flux_scroll_indicator(&a->scroll, &out, width);
}
```

Keys handled by `flux_scroll_update`: `up` / `down` / `j` / `k` / `pgup` /
`pgdn` / `home` / `end`, plus mouse wheel. Cell renderer's diff means
scrolling 1000 rows touches only `viewport_h` cells — no flicker.

---

## Tabs

`FluxTabBar` owns the active tab and the click hit-boxes. Number keys
`1`–`9`, `Tab` / `Shift+Tab`, and left-clicks all switch tabs.

```c
static const char *labels[] = { "Agent", "Diff", "Gallery", "Chat" };

typedef struct App { FluxTabBar tabs; } App;

static FluxCmd app_init(FluxModel *m) {
    flux_tabbar_init(&((App*)m->state)->tabs, labels, 4);
    return FLUX_CMD_NONE;
}

static FluxCmd app_update(FluxModel *m, FluxMsg msg) {
    flux_tabbar_update(&((App*)m->state)->tabs, msg);
    return FLUX_CMD_NONE;
}

static void app_view(FluxModel *m, char *buf, int bufsz) {
    App *a = m->state;
    FluxSB sb; flux_sb_init(&sb, buf, bufsz);

    /* Render tab strip starting at column 1, row 1 — coords feed
     * the click hit-box cache so mouse-clicks land on the right tab. */
    flux_tabbar_render(&a->tabs, &sb, flux_cols(), 1, 1);

    switch (a->tabs.active) {
    case 0: render_agent(&sb);   break;
    case 1: render_diff(&sb);    break;
    case 2: render_gallery(&sb); break;
    case 3: render_chat(&sb);    break;
    }
}
```

---

## The width contract

Every flux.h widget that takes a `width` argument guarantees:

1. Each emitted row contains **EXACTLY `width` display cells** of payload.
2. ANSI escape sequences (`\x1b[…m`, OSC-8, etc.) are passed through but **don't count** toward width.
3. UTF-8 is decoded to display cells: ASCII = 1, CJK / emoji = 2.
4. If user content would overflow, it is truncated with `…`.
5. If it would underfill, it is right-padded with spaces.
6. `\n` follows each cell row, never appears mid-row.
7. `width <= 0` is a silent no-op — never asserts, never crashes.

This makes user-supplied text safe by construction. The two primitives
that implement the contract are public — use them when composing your
own widgets:

```c
/* Truncate to at most max_w cells; suffix defaults to "…" on overflow. */
int flux_truncate(const char *text, int max_w, char *out, int out_sz, const char *suffix);

/* Emit exactly target_w cells into sb — pads with spaces or truncates. */
void flux_fit(FluxSB *sb, const char *text, int target_w,
              const char *fg, const char *bg, FluxAlign align);

/* Display width (cells), ANSI- and UTF-8-aware. */
int  flux_strwidth(const char *s);
```

The `tests/stress_test` binary verifies the contract on every public
widget across hundreds of randomised input combinations.

---

## FPS configuration

`FluxProgram.fps` sets the maximum runloop frequency (default 30). The
runloop will wake at most that often even if a widget is animating.

```c
FluxProgram p = {
    .model = m,
    .alt_screen = 1,
    .mouse = 1,
    .fps = 120,        /* high-refresh demo */
};
flux_run(&p);
```

For per-widget animation cadence, return a `FLUX_TICK(ms)` command from
`update` and the runloop will deliver an `MSG_TICK` after `ms`
milliseconds:

```c
static FluxCmd app_init(FluxModel *m) {
    return FLUX_TICK(33);    /* ~30 Hz */
}

static FluxCmd app_update(FluxModel *m, FluxMsg msg) {
    if (msg.type == MSG_TICK) {
        flux_blinkdot_tick(&dot);
        flux_streaming_tick(&typer);
        flux_op_tree_tick(&tree);
        return FLUX_TICK(33);   /* schedule next */
    }
    return FLUX_CMD_NONE;
}
```

`fps` controls the renderer's wakeup floor; `FLUX_TICK(ms)` controls
when your `update` runs. Use a small `ms` for typewriters (80) and a
larger one for status meters (500).

### Per-widget FPS — `FluxRate`

When you tick the loop fast (e.g. 120 Hz for snappy scroll/input), you
usually don't want every animated widget to advance that fast. A spinner
spinning at 120 Hz is unreadable; you want 10 Hz. `FluxRate` is a
12-byte gate that lets each widget pace itself independently of the
outer loop:

```c
typedef struct {
    FluxSpinner spin;       FluxRate spin_rate;     /* 10 Hz */
    FluxBlinkDot dot;       FluxRate dot_rate;      /*  2 Hz */
    FluxShimmerText shim;   FluxRate shim_rate;     /* 30 Hz */
} App;

/* In init: configure once. */
flux_rate_set_fps(&a->spin_rate, 10);
flux_rate_set_fps(&a->dot_rate,   2);
flux_rate_set_fps(&a->shim_rate, 30);

/* In update on MSG_TICK: each widget self-paces. */
uint64_t now = flux_now_ms();
if (flux_rate_due(&a->spin_rate, now)) flux_spinner_tick(&a->spin);
if (flux_rate_due(&a->dot_rate,  now)) flux_blinkdot_tick(&a->dot);
if (flux_rate_due(&a->shim_rate, now)) flux_shimmer_tick(&a->shim);
return FLUX_TICK(8);   /* outer loop stays at 120 Hz */
```

Catch-up safe: if the loop falls behind by 5 frames, each gate fires
**once per call** (not 5 times). No runaway spinner after a sleep/resume.
`flux_rate_due` returns `1` exactly when `period_ms` has elapsed since
the last fire, advancing internally. Convenience macro
`FLUX_FPS_TO_MS(fps)` converts FPS → period.

The demo's **AI Showcase** tab renders 5 spinners at 1, 2, 5, 10, and
30 Hz on the same screen so you can see different speeds advancing
simultaneously from a single 120 Hz outer loop.

---

## Threading model — input is non-blocking; you stay on one thread

`flux_run` is a single-threaded event loop. It uses `select()` with a
timeout = `1_000_000 / fps` µs — never blocks longer than one frame.
Input (keyboard, mouse SGR, paste, signals, custom commands) is
dispatched as soon as bytes arrive on stdin or the self-pipe.

```
┌──────────────────────────────────────────────────────────────┐
│              flux_run (calling thread)                       │
│  ┌────────────┐                                              │
│  │  select()  │← timeout = 1_000_000 / fps  (never blocks)   │
│  └─────┬──────┘                                              │
│        ├──────── stdin ready  → parse → MSG_KEY / MSG_MOUSE  │
│        ├──────── pipe  ready  → MSG_TICK / MSG_CUSTOM        │
│        └──────── timeout       → render if dirty             │
│                  │                                            │
│                  ▼                                            │
│           model.update(msg) → returns FluxCmd                │
│                  │                                            │
│                  ▼                                            │
│           model.view(buf)   → cell-diff → write to stdout    │
└──────────────────────────────────────────────────────────────┘
```

**Implications**:

- **You don't need to spawn a renderer thread.** `flux_run` IS the
  renderer loop — call it from `main` (or any thread) and it owns that
  thread until the program exits.
- **Input is fully non-blocking from the OS perspective.** Multiple
  events that arrive in one read are parsed in an inner loop, so a
  burst of mouse-wheel events doesn't drop frames.
- **`update()` and `view()` run synchronously on the loop thread.** If
  your `update` blocks for 500 ms doing I/O, the loop blocks for 500
  ms. Events queue in the kernel buffer and dispatch when you return.

### Doing real work without blocking the UI

For HTTP, disk I/O, LLM calls, or anything else that takes more than a
few ms, do the work on a worker thread and post the result back via
`flux_cmd_custom(id, data)`. The custom command flows through the same
self-pipe `select()` watches, so the loop wakes immediately:

```c
/* On the worker thread (e.g. HTTP response received): */
struct LlmResult *r = malloc(sizeof *r);
r->text = strdup(response);
flux_post_custom(LLM_DONE, r);   /* wakes the UI loop */

/* In update on the UI loop: */
if (msg.type == MSG_CUSTOM && msg.u.custom.id == LLM_DONE) {
    struct LlmResult *r = msg.u.custom.data;
    flux_streaming_init(&app.streamer, r->text);
    free(r->text); free(r);
    return FLUX_CMD_NONE;
}
```

That keeps `update` sub-millisecond and lets the UI redraw at full FPS
while requests are in flight. Recommended pattern for any agent UI: one
thread per upstream (LLM, tool runner, log tailer, file watcher) →
custom command → state mutation → next render.

**Single-thread by default; multi-thread when you need it.** No
synchronization primitives, no widget locks, no renderer thread to
manage. Workers post results, the loop consumes them, the renderer
diffs cells and writes the minimal patch.

---

## Demo

`demo.c` is a multi-tab showcase that exercises the renderer, layout,
mouse, scroll, and most widgets. Build and run:

```sh
make
./demo            # 1-9 / Tab to switch tabs · q to quit
```

The bundled tabs:

| Tab       | Showcases                                                            |
|-----------|----------------------------------------------------------------------|
| Agent     | Streaming text, op tree, approval prompt, message bubbles, model badge |
| Diff      | Cell-renderer benchmark — falling matrix, gradients, charts          |
| Gallery   | Catalog of widgets: alerts, badges, calendar, gauges, dial           |
| Chat      | Scrollable transcript with `FluxScroll` + chat input                 |

Each tab is rendered into one `FluxSB`. Switching tabs costs one frame
because the diff engine only emits the changed cells.

```c
/* Sketch of the agent tab */
flux_brand(&sb, "✦", FLUX_THEME_BRAND_PURPLE_FG, "Agent", FLUX_THEME_TEXT_FG);
flux_model_badge(&sb, "Anthropic", "claude-opus-4", 200000, w);
flux_message_bubble(&sb, FLUX_ROLE_USER, "Refactor auth.c", "12:34", w);
flux_message_bubble(&sb, FLUX_ROLE_ASSISTANT, reply, "12:34", w);
flux_op_tree_render(&tree, &sb, w);
flux_approval_render(&approval, &sb, w);
```

---

## Build & install

flux.h is a single header. Drop it into your project:

```sh
cp flux.h vendor/flux.h
```

In exactly one C file, define `FLUX_IMPL` before the include:

```c
#define FLUX_IMPL
#include "flux.h"
```

Compile with C99, pthreads, and libm:

```sh
cc -O2 -std=c99 -o app app.c -lpthread -lm
```

Build the bundled demo + tests:

```sh
make           # builds ./demo
make test      # builds + runs tests/stress_test and tests/mouse_parse_test
make clean
```

No autotools, no CMake, no package manager. Tested on Linux and macOS;
known to work on FreeBSD.

---

## Tests

Two test binaries live under `tests/`:

- **`stress_test`** — verifies the width contract for every public widget.
  Generates randomised inputs (long strings, embedded ANSI, CJK, emoji,
  zero / negative widths) and asserts that every emitted row is exactly
  the requested cell width.
- **`mouse_parse_test`** — fuzzer for the SGR 1006 mouse parser.
  Throws malformed escape sequences at the parser to confirm it never
  reads out of bounds and never produces garbage `MSG_MOUSE` events.

```sh
make test
```

Both run in well under a second. Add new widgets to `stress_test.c` as
you ship them.

---

## Deliberately not implemented

Some storm components were skipped because they require capabilities a
TUI can't honestly provide:

| Component        | Why skipped                                                  |
|------------------|--------------------------------------------------------------|
| `Image`          | Requires raster pixel protocols (kitty / iTerm2 / Sixel) — out of scope for v1; cell grid only. |
| `Shadow`         | Real shadows need alpha-blended raster — terminals have none. |
| `Diagram`        | Mermaid-style block diagrams need a layout engine + rasteriser. |
| `canvas/*`       | Pixel canvas primitives — same reason as `Image`.            |
| `AnimatePresence`| React-specific reactive concept; flux.h's diff engine + `FLUX_TICK` cover the same ground without the abstraction. |
| `RevealTransition` / `Transition` | React animation primitives — animate manually via tick + frame counters. |
| `ErrorBoundary`  | C has no exception model; errors propagate via return codes. |

Everything else from storm has a flux.h equivalent, sometimes with a
slightly different name (see `tasks/parity/00_ROADMAP.md` for the full
mapping).

---

## Project status & contributing

Maintained by **olealgoritme**. Used in production by
[`watt`](https://github.com/olealgoritme/watt) — per-process power
monitoring TUI (RAPL/MSR).

Contributions welcome. Open an issue first for anything substantial so
we can discuss API shape — every new widget should honour the width
contract and follow the `init` / `update` / `render` (or pure-render)
convention.

## License

Copyright (c) 2026 xuw (olealgoritme). [MIT](LICENSE)
