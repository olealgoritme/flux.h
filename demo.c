/* demo.c — flux.h SHOWCASE — multi-app
 *
 * Nine apps in one binary, switch with 1-9 or Tab:
 *   1. Agent Session     — Claude/storm-style agent UI
 *   2. Diff Benchmark    — live cell-level diff stats over an animation
 *   3. AI Gallery        — every AI widget in flux.h, multiple states
 *   4. Chat              — long scrollable agent conversation
 *   5. AI Showcase       — B5 storm-parity AI widgets (live)
 *   6. Forms             — B2 interactive form widgets
 *   7. Charts            — B6 sparkline / bar / line / area / scatter / heatmap / gauge
 *   8. Markdown          — B8 markdown + B8 syntax highlight + inline diff
 *   9. Effects           — B7 digits clock / gradient / glow / borders
 *   0. Tools / Status    — B3 alerts / badges / avatars / breadcrumbs / help panel
 *
 * Build: make    Run: ./demo    Quit: q or Ctrl-C
 */

#define FLUX_IMPL
#include "flux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ════════════════════════════════════════════════════════════════════
 * SHARED LAYOUT — responsive width helpers, desktop bg
 * ════════════════════════════════════════════════════════════════════ */

static int demo_inner_w_for_terminal(int cols) {
    int w = cols - 8;
    if (w > 180) w = 180;    /* room for 10 tab labels + hint */
    if (w < 30)  w = 30;     /* every widget is responsive — let it shrink */
    return w;
}

static void demo__spaces(FluxSB *sb, int n) {
    int i;
    for (i = 0; i < n; i++) flux_sb_append(sb, " ");
}

static void demo_blank(FluxSB *sb, int inner_w, int rows) {
    int r;
    if (!sb || rows <= 0) return;
    for (r = 0; r < rows; r++) {
        demo__spaces(sb, inner_w);
        flux_sb_append(sb, "\n");
    }
}

static void demo_padded(FluxSB *sb, int inner_w, const char *text) {
    if (!sb) return;
    /* Use flux_fit so over-long text is truncated with '…' instead of
     * overflowing the row. Caller can pass any text. */
    flux_fit(sb, text ? text : "", inner_w, NULL, FLUX_ALIGN_LEFT);
    flux_sb_append(sb, "\n");
}

static void demo_desktop_row(FluxSB *sb, int cols) {
    int j;
    flux_sb_append(sb, FLUX_THEME_WINDOW_BG);
    for (j = 0; j < cols; j++) flux_sb_append(sb, " ");
    flux_sb_append(sb, FLUX_RESET);
    flux_sb_append(sb, "\n");
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 1: AGENT SESSION
 * ════════════════════════════════════════════════════════════════════ */

/* ── Aligned message helpers — all use flux_message_row so glyph widths
 * and body offsets are guaranteed consistent across the whole screen. */

static void s1_user(FluxSB *sb, int inner_w, const char *text) {
    flux_message_row(sb, ">", FLUX_THEME_TEXT_OFF_FG,
                     text, FLUX_THEME_TEXT_FG, inner_w, 2, 2);
}

static void s1_assistant(FluxSB *sb, int inner_w, const char *text) {
    /* ● in accent, body in primary text */
    flux_message_row(sb, "\xe2\x97\x8f", FLUX_THEME_ACCENT_FG,
                     text, FLUX_THEME_TEXT_FG, inner_w, 2, 2);
}

static void s1_finding(FluxSB *sb, int inner_w, const char *text) {
    flux_message_row(sb, "\xe2\x97\x86", FLUX_THEME_ACCENT_FG,    /* ◆ */
                     text, FLUX_THEME_TEXT_FG, inner_w, 2, 2);
}

static void s1_thinking(FluxSB *sb, int inner_w, int spinner_idx, const char *text) {
    const char *frame = FLUX_SPINNER_DOT[spinner_idx % FLUX_SPINNER_DOT_N];
    flux_message_row(sb, frame, FLUX_THEME_TEXT_DIM_FG,
                     text, FLUX_THEME_TEXT_DIM_FG, inner_w, 2, 2);
}

static void s1_tool(FluxSB *sb, int inner_w, const char *text) {
    flux_message_row(sb, "\xe2\x9a\x99", FLUX_THEME_BRAND_PURPLE_FG,  /* ⚙ */
                     text, FLUX_THEME_TEXT2_FG, inner_w, 2, 2);
}

static void s1_tool_result(FluxSB *sb, int inner_w, const char *text) {
    flux_message_row(sb, "\xe2\x86\xb3", FLUX_THEME_OK_DIM_FG,        /* ↳ */
                     text, FLUX_THEME_TEXT_DIM_FG, inner_w, 2, 2);
}

/* Activity list — 4 rows with progressive states based on spinner idx so
 * the visual feels alive. */
static void s1_activity(FluxSB *sb, int inner_w, int spinner_frame) {
    FluxActivity items[4];
    items[0] = (FluxActivity){ "Reading src/auth.ts",     FLUX_ACT_DONE,    340,  0 };
    items[1] = (FluxActivity){ "Analyzing token logic",   FLUX_ACT_DONE,   1200,  0 };
    items[2] = (FluxActivity){ "Patching refreshToken()", FLUX_ACT_RUNNING,  -1,  spinner_frame };
    items[3] = (FluxActivity){ "Running tests",           FLUX_ACT_PENDING,  -1,  0 };
    flux_activity_list_render(sb, items, 4, inner_w, 4);
}

/* The actual code-fix diff. */
static void s1_diff(FluxSB *sb, int inner_w) {
    FluxDiffLine lines[3];
    lines[0] = (FluxDiffLine){ FLUX_DIFF_REMOVED, "if (Date.now() < token.expiresAt) {" };
    lines[1] = (FluxDiffLine){ FLUX_DIFF_ADDED,   "const buffer = 30_000;" };
    lines[2] = (FluxDiffLine){ FLUX_DIFF_ADDED,   "if (Date.now() < token.expiresAt - buffer) {" };
    flux_diff_block_render(sb, "src/auth.ts", lines, 3, inner_w);
}

/* A read-only code preview block (CONTEXT lines, no +/-). Demonstrates
 * embedded components inside the chat narrative. */
static void s1_codeview(FluxSB *sb, int inner_w) {
    FluxDiffLine lines[5];
    lines[0] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "function isExpired(token) {" };
    lines[1] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "  // BUG: doesn't account for client/server clock skew" };
    lines[2] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "  return Date.now() >= token.expiresAt;" };
    lines[3] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "}" };
    lines[4] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "// called from refreshToken() at line 42" };
    flux_diff_block_render(sb, "src/auth/token.ts (preview)", lines, 5, inner_w);
}

/* A small key-value summary card — shows that arbitrary structured info
 * fits inside the message flow. Uses message rows for alignment. */
static void s1_meta(FluxSB *sb, int inner_w) {
    flux_message_row(sb, "\xe2\x80\xa2", FLUX_THEME_TEXT_OFF_FG,           /* • */
                     "branch:    main", FLUX_THEME_TEXT2_FG, inner_w, 4, 2);
    flux_message_row(sb, "\xe2\x80\xa2", FLUX_THEME_TEXT_OFF_FG,
                     "files:     3 modified, 0 added, 0 deleted", FLUX_THEME_TEXT2_FG, inner_w, 4, 2);
    flux_message_row(sb, "\xe2\x80\xa2", FLUX_THEME_TEXT_OFF_FG,
                     "tests:     25 passing, 0 failing", FLUX_THEME_OK_FG, inner_w, 4, 2);
    flux_message_row(sb, "\xe2\x80\xa2", FLUX_THEME_TEXT_OFF_FG,
                     "coverage:  +0.4%", FLUX_THEME_OK_DIM_FG, inner_w, 4, 2);
}

static void s1_approval(FluxSB *sb, int inner_w, int selected) {
    static FluxButton buttons[2] = {
        { "Allow", FLUX_THEME_OK_FG,  FLUX_THEME_BUTTON_OK_BG, FLUX_THEME_TEXT_DIM_FG },
        { "Deny",  FLUX_THEME_ERR_FG, FLUX_THEME_BUTTON_NO_BG, FLUX_THEME_TEXT_DIM_FG },
    };
    FluxApproval a;
    flux_approval_init(&a, "Apply patch to 3 files?", buttons, 2);
    a.selected = (selected < 0) ? 0 : (selected > 1 ? 1 : selected);
    flux_approval_render(&a, sb, inner_w);
}

static void s1_statusbar(FluxSB *sb, int inner_w) {
    FluxStatusBar s = { "storm agent", 8400, 0.12f, 0.62f, 8 };
    flux_statusbar_render(sb, &s, inner_w);
    flux_sb_append(sb, "\n");
}

/* Multi-turn agent conversation with embedded components. Every row uses
 * flux_message_row, so the gutter is uniform regardless of glyph width. */
static void screen1_render(FluxSB *inner, int inner_w, int spinner_idx, int selected) {
    demo_blank   (inner, inner_w, 1);

    /* Turn 1: user request */
    s1_user      (inner, inner_w, "Fix the token refresh bug in auth.ts");
    demo_blank   (inner, inner_w, 1);

    /* Turn 2: assistant intro */
    s1_assistant (inner, inner_w, "I'll investigate. Let me read the auth module first.");
    demo_blank   (inner, inner_w, 1);

    /* Tool calls + results */
    s1_tool      (inner, inner_w, "tool: read_file(path=\"src/auth/token.ts\")");
    s1_tool_result(inner, inner_w, "→ 87 lines, exports `isExpired`, `refreshToken`");
    demo_blank   (inner, inner_w, 1);

    /* Embedded code preview component */
    s1_codeview  (inner, inner_w);
    demo_blank   (inner, inner_w, 1);

    /* Finding */
    s1_finding   (inner, inner_w,
                  "Found the bug. The refresh timer doesn't account for clock skew...");
    demo_blank   (inner, inner_w, 1);

    /* Activity list (live progress) */
    s1_activity  (inner, inner_w, spinner_idx);
    demo_blank   (inner, inner_w, 1);

    /* Diff card */
    s1_diff      (inner, inner_w);
    demo_blank   (inner, inner_w, 1);

    /* Tool: run tests */
    s1_tool      (inner, inner_w, "tool: bash(\"npm test src/auth\")");
    s1_tool_result(inner, inner_w, "→ 25 passing, 0 failing");
    demo_blank   (inner, inner_w, 1);

    /* Assistant summary */
    s1_assistant (inner, inner_w, "Tests pass. Summary of changes:");
    s1_meta      (inner, inner_w);
    demo_blank   (inner, inner_w, 1);

    /* Thinking (live spinner) */
    s1_thinking  (inner, inner_w, spinner_idx, "Drafting commit message...");
    demo_blank   (inner, inner_w, 1);

    /* Approval */
    s1_approval  (inner, inner_w, selected);
    demo_blank   (inner, inner_w, 1);

    /* Footer */
    s1_statusbar (inner, inner_w);
    demo_blank   (inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 2: DIFF BENCHMARK
 *
 * Maintains two FluxScreen buffers internally. Each tick, paints an
 * animation into the back buffer, then walks both buffers cell-by-cell
 * to compute (changed, unchanged) — these are the real numbers the
 * cell-level diff engine would emit. We render those numbers as text.
 * ════════════════════════════════════════════════════════════════════ */

#define BENCH_W 60
#define BENCH_H 14

typedef struct {
    FluxScreen front, back;
    FluxStylePool pool;
    int initialized;
    /* per-frame stats */
    int last_total;
    int last_changed;
    int last_unchanged;
    /* rolling stats */
    long long acc_total;
    long long acc_changed;
    long long acc_unchanged;
    int frames;
    /* animation state */
    int tick;
    int mode;          /* 0 = minimal (TUI typical), 1 = wave, 2 = particles, 3 = scrollers (worst case) */
} BenchState;
#define BENCH_MODE_N 4

static BenchState g_bench;

static void bench_init(BenchState *b) {
    if (b->initialized) return;
    flux_screen_init(&b->front, BENCH_H, BENCH_W);
    flux_screen_init(&b->back,  BENCH_H, BENCH_W);
    flux_style_pool_init(&b->pool);
    b->initialized = 1;
}

static int bench_intern_style(BenchState *b, int fr, int fg, int fb,
                              int br, int bg, int bb) {
    FluxStyle s = FLUX_STYLE_NONE;
    s.fg_r = fr; s.fg_g = fg; s.fg_b = fb;
    s.bg_r = br; s.bg_g = bg; s.bg_b = bb;
    return flux_style_pool_intern(&b->pool, &s);
}

/* Mode labels for the bench screen (forward-decl, defined again below). */
static const char *BENCH_MODE_NAMES[BENCH_MODE_N] = {
    "minimal (typical TUI)",
    "wave",
    "particles",
    "scrollers (worst case)"
};

/* "Minimal" mode — typical TUI: most cells static, only a spinner + counter
 * change per frame. This is the realistic case where cell-diff shines. */
static void bench_paint_minimal(BenchState *b, int tick) {
    int x, y;
    int sid_panel = bench_intern_style(b, 156, 164, 199, 14, 14, 18);
    int sid_dim   = bench_intern_style(b, 86, 95, 125, 14, 14, 18);
    int sid_acc   = bench_intern_style(b, 174, 198, 255, 14, 14, 18);
    int sid_ok    = bench_intern_style(b, 141, 184, 97, 14, 14, 18);

    flux_screen_clear(&b->back);
    /* Static background fill */
    for (y = 0; y < BENCH_H; y++)
        for (x = 0; x < BENCH_W; x++)
            flux_screen_set_cell(&b->back, x, y, " ", 1, sid_panel, 1);

    /* Static decorative banner */
    const char *title = "  AGENT TASK PROGRESS";
    for (x = 0; x < (int)strlen(title) && x < BENCH_W; x++) {
        char ch[2] = { title[x], 0 };
        flux_screen_set_cell(&b->back, x, 1, ch, 1, sid_acc, 1);
    }

    /* Static activity rows */
    const char *rows[] = {
        "  done   Reading config",
        "  done   Loading model",
        "  done   Connecting to API",
        "         Streaming response",
        "         Validating output",
    };
    int r;
    for (r = 0; r < 5 && r + 3 < BENCH_H; r++) {
        const char *m = rows[r];
        int n = (int)strlen(m);
        for (x = 0; x < n && x < BENCH_W; x++) {
            char ch[2] = { m[x], 0 };
            int sid = (r < 3) ? sid_panel : sid_dim;
            flux_screen_set_cell(&b->back, x, r + 3, ch, 1, sid, 1);
        }
    }

    /* DYNAMIC: a spinner cell at (4, 6) advancing each frame */
    {
        const char *frame = FLUX_SPINNER_DOT[tick % FLUX_SPINNER_DOT_N];
        flux_screen_set_cell(&b->back, 4, 6, frame, (int)strlen(frame), sid_acc, 1);
    }
    /* DYNAMIC: a counter rendering elapsed ticks just after the spinner */
    {
        char num[32];
        snprintf(num, sizeof num, "  tick %d", tick);
        int n = (int)strlen(num);
        for (x = 0; x < n && (6 + x) < BENCH_W; x++) {
            char ch[2] = { num[x], 0 };
            flux_screen_set_cell(&b->back, 6 + x, 6, ch, 1, sid_dim, 1);
        }
    }
    /* DYNAMIC: progress bar bottom — fills based on tick */
    {
        int bar_y = BENCH_H - 2;
        int total_w = BENCH_W - 4;
        int filled = (tick % (total_w * 2 + 1));
        if (filled > total_w) filled = total_w * 2 - filled;
        for (x = 0; x < total_w; x++) {
            const char *g = (x < filled) ? "\xe2\x96\x88" : "\xe2\x96\x92";
            int sid = (x < filled) ? sid_ok : sid_dim;
            flux_screen_set_cell(&b->back, 2 + x, bar_y, g, 3, sid, 1);
        }
    }
}

/* Paint a wave animation into b->back. */
static void bench_paint_wave(BenchState *b, int tick) {
    int x, y;
    flux_screen_clear(&b->back);
    for (x = 0; x < BENCH_W; x++) {
        double t = (double)tick * 0.15;
        double y_f = (double)BENCH_H * 0.5
            + sin((double)x * 0.20 + t) * (BENCH_H * 0.35)
            + cos((double)x * 0.07 - t * 0.7) * (BENCH_H * 0.10);
        int y_i = (int)(y_f + 0.5);
        if (y_i < 0) y_i = 0;
        if (y_i >= BENCH_H) y_i = BENCH_H - 1;
        for (y = 0; y < BENCH_H; y++) {
            int dist = y - y_i;
            int abs_d = dist < 0 ? -dist : dist;
            if (abs_d <= 2) {
                int g_intensity = 255 - abs_d * 50;
                int b_intensity = 100 + abs_d * 40;
                int sid = bench_intern_style(b,
                    100, g_intensity, b_intensity,
                    20 + (x % 5), 22, 30);
                flux_screen_set_cell(&b->back, x, y, "\xe2\x96\x88", 3, sid, 1); /* █ */
            } else {
                int sid = bench_intern_style(b, 60, 64, 93, 14, 14, 18);
                flux_screen_set_cell(&b->back, x, y, " ", 1, sid, 1);
            }
        }
    }
}

/* Paint scrolling text bands (mode 1). */
static void bench_paint_scrollers(BenchState *b, int tick) {
    static const char *MSGS[] = {
        "  flux.h  cell-level diff  ~  zero allocs  ~  ",
        "  pure C99  ~  single header  ~  3000 lines  ~  ",
        "  Elm-arch  ~  TUI for AI agents  ~  ",
        "  truecolor  ~  responsive  ~  diff-emit ~  "
    };
    int x, y;
    flux_screen_clear(&b->back);
    for (y = 0; y < BENCH_H; y++) {
        const char *m = MSGS[y % 4];
        int mlen = (int)strlen(m);
        int offset = (tick * (1 + y % 3)) % mlen;
        for (x = 0; x < BENCH_W; x++) {
            char ch = m[(x + offset) % mlen];
            int sid;
            if (ch == ' ') {
                sid = bench_intern_style(b, 60, 64, 93, 14, 14, 18);
                flux_screen_set_cell(&b->back, x, y, " ", 1, sid, 1);
            } else {
                int hue_r = 130 + (y * 15) % 120;
                int hue_g = 180 - (y * 10) % 80;
                int hue_b = 200;
                char chs[2] = { ch, 0 };
                sid = bench_intern_style(b, hue_r, hue_g, hue_b, 14, 14, 18);
                flux_screen_set_cell(&b->back, x, y, chs, 1, sid, 1);
            }
        }
    }
}

/* Paint a starfield/particle pattern (mode 2). */
static void bench_paint_particles(BenchState *b, int tick) {
    int i, x, y;
    int sid_bg = bench_intern_style(b, 60, 64, 93, 14, 14, 18);
    flux_screen_clear(&b->back);
    for (y = 0; y < BENCH_H; y++)
        for (x = 0; x < BENCH_W; x++)
            flux_screen_set_cell(&b->back, x, y, " ", 1, sid_bg, 1);

    /* deterministic particle list */
    for (i = 0; i < 40; i++) {
        int seed = i * 2654435761u;
        int px = (seed % BENCH_W + tick / (1 + i % 5)) % BENCH_W;
        int py = (i * 17 + tick / (3 + i % 4)) % BENCH_H;
        int br_ = 120 + (i * 13) % 135;
        int bg_ = 100 + (i * 23) % 155;
        int bb_ = 150 + (i * 7) % 105;
        const char *glyph = (i & 1) ? "\xe2\x9c\xa6" /* ✦ */ : "\xe2\x80\xa2" /* • */;
        int sid = bench_intern_style(b, br_, bg_, bb_, 14, 14, 18);
        flux_screen_set_cell(&b->back, px, py, glyph, (int)strlen(glyph), sid, 1);
    }
}

/* Compare front and back; return changed cell count. */
static int bench_count_changes(const FluxScreen *prev, const FluxScreen *next) {
    int total, i, changed = 0;
    if (!prev || !next) return -1;
    if (prev->rows != next->rows || prev->cols != next->cols) return -1;
    total = prev->rows * prev->cols;
    for (i = 0; i < total; i++) {
        if (!flux_cell_eq(&prev->cells[i], &next->cells[i])) changed++;
    }
    return changed;
}

/* Render one row of the bench screen into the buffer at given y from the
 * back buffer. Each cell -> append style escape + glyph. */
static void bench_blit_row(FluxSB *sb, const FluxScreen *scr,
                           const FluxStylePool *pool, int y) {
    int x;
    int last_sid = -2;
    for (x = 0; x < scr->cols; x++) {
        const FluxCell *c = &scr->cells[y * scr->cols + x];
        if (c->style_id != last_sid && c->style_id >= 0 &&
            c->style_id < pool->count) {
            const FluxStyle *st = &pool->styles[c->style_id];
            char esc[64];
            snprintf(esc, sizeof esc,
                "\x1b[38;2;%d;%d;%d;48;2;%d;%d;%dm",
                st->fg_r, st->fg_g, st->fg_b,
                st->bg_r, st->bg_g, st->bg_b);
            flux_sb_append(sb, esc);
            last_sid = c->style_id;
        }
        if (c->ch[0]) flux_sb_append(sb, c->ch);
        else flux_sb_append(sb, " ");
    }
    flux_sb_append(sb, FLUX_RESET);
}

static void screen2_tick(BenchState *b) {
    /* Swap: front <- back via cell-level copy; new back gets next frame */
    FluxScreen tmp = b->front;
    b->front = b->back;
    b->back  = tmp;

    b->tick++;
    if (b->mode == 0)      bench_paint_minimal(b, b->tick);
    else if (b->mode == 1) bench_paint_wave(b, b->tick);
    else if (b->mode == 2) bench_paint_particles(b, b->tick);
    else                   bench_paint_scrollers(b, b->tick);

    int total = BENCH_W * BENCH_H;
    int changed = bench_count_changes(&b->front, &b->back);
    if (changed < 0) changed = total;
    b->last_total = total;
    b->last_changed = changed;
    b->last_unchanged = total - changed;
    b->acc_total += total;
    b->acc_changed += changed;
    b->acc_unchanged += (total - changed);
    b->frames++;
}

static void screen2_render(FluxSB *inner, int inner_w, BenchState *b) {
    /* Header line */
    {
        char H[1024]; FluxSB h;
        flux_sb_init(&h, H, sizeof H);
        flux_sb_append(&h, "  ");
        flux_sb_append(&h, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&h, "\xe2\x97\x86 "); /* ◆ */
        flux_sb_append(&h, FLUX_THEME_TEXT_FG);
        flux_sb_append(&h, FLUX_BOLD);
        flux_sb_append(&h, "Cell-level diff benchmark");
        flux_sb_append(&h, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&h));
    }

    {
        char H[1024]; FluxSB h;
        flux_sb_init(&h, H, sizeof H);
        flux_sb_append(&h, "  ");
        flux_sb_append(&h, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&h, "Each frame, only changed cells would be written to the terminal.");
        flux_sb_append(&h, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&h));
    }
    demo_blank(inner, inner_w, 1);

    /* Animation frame: render BENCH_H rows from b->back, centered horizontally. */
    int left_pad = (inner_w - BENCH_W) / 2;
    if (left_pad < 2) left_pad = 2;
    int right_pad = inner_w - left_pad - BENCH_W;
    if (right_pad < 0) right_pad = 0;

    /* Top frame border */
    {
        char border[256];
        snprintf(border, sizeof border, "%s%s",
                 FLUX_THEME_BORDER_FG, "\xe2\x95\xad");
        flux_sb_append(inner, border);
        for (int i = 0; i < left_pad - 2; i++) flux_sb_append(inner, " ");
        /* line */
        flux_sb_append(inner, FLUX_THEME_BORDER_FG);
        for (int i = 0; i < BENCH_W; i++) flux_sb_append(inner, "\xe2\x94\x80");
        flux_sb_append(inner, "\xe2\x95\xae");
        flux_sb_append(inner, FLUX_RESET);
        /* pad to inner_w */
        int row_w = 1 + (left_pad - 2) + BENCH_W + 1;
        if (row_w < inner_w) demo__spaces(inner, inner_w - row_w);
        flux_sb_append(inner, "\n");
    }
    for (int y = 0; y < BENCH_H; y++) {
        /* left pad inside chrome */
        for (int j = 0; j < left_pad - 1; j++) flux_sb_append(inner, " ");
        flux_sb_append(inner, FLUX_THEME_BORDER_FG);
        flux_sb_append(inner, "\xe2\x94\x82"); /* │ */
        flux_sb_append(inner, FLUX_RESET);
        bench_blit_row(inner, &b->back, &b->pool, y);
        flux_sb_append(inner, FLUX_THEME_BORDER_FG);
        flux_sb_append(inner, "\xe2\x94\x82");
        flux_sb_append(inner, FLUX_RESET);
        int row_w = (left_pad - 1) + 1 + BENCH_W + 1;
        if (row_w < inner_w) demo__spaces(inner, inner_w - row_w);
        flux_sb_append(inner, "\n");
    }
    {
        for (int j = 0; j < left_pad - 1; j++) flux_sb_append(inner, " ");
        flux_sb_append(inner, FLUX_THEME_BORDER_FG);
        flux_sb_append(inner, "\xe2\x95\xb0"); /* ╰ */
        for (int i = 0; i < BENCH_W; i++) flux_sb_append(inner, "\xe2\x94\x80");
        flux_sb_append(inner, "\xe2\x95\xaf"); /* ╯ */
        flux_sb_append(inner, FLUX_RESET);
        int row_w = (left_pad - 1) + 1 + BENCH_W + 1;
        if (row_w < inner_w) demo__spaces(inner, inner_w - row_w);
        flux_sb_append(inner, "\n");
    }

    demo_blank(inner, inner_w, 1);

    /* Stats panel */
    int total = b->last_total > 0 ? b->last_total : (BENCH_W * BENCH_H);
    int changed = b->last_changed;
    int unchanged = b->last_unchanged;
    double pct_changed = total > 0 ? (100.0 * changed / total) : 0.0;
    double pct_skipped = 100.0 - pct_changed;
    double avg_pct_skipped = b->acc_total > 0
        ? (100.0 * (double)b->acc_unchanged / (double)b->acc_total)
        : 0.0;

    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT2_FG);
        flux_sb_appendf(&l, "Frame %d   ", b->frames);
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_appendf(&l, "  total cells: %d", total);
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "    ");
        flux_sb_append(&l, FLUX_THEME_ERR_FG);
        flux_sb_appendf(&l, "changed:  %4d  ", changed);
        flux_sb_append(&l, FLUX_RESET);
        /* bar */
        flux_inline_bar(&l, pct_changed / 100.0, 24,
                        FLUX_THEME_ERR_FG, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_appendf(&l, "  %5.1f%%", pct_changed);
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "    ");
        flux_sb_append(&l, FLUX_THEME_OK_FG);
        flux_sb_appendf(&l, "skipped:  %4d  ", unchanged);
        flux_sb_append(&l, FLUX_RESET);
        flux_inline_bar(&l, pct_skipped / 100.0, 24,
                        FLUX_THEME_OK_FG, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_appendf(&l, "  %5.1f%%", pct_skipped);
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_appendf(&l, "rolling avg over %d frames: ", b->frames);
        flux_sb_append(&l, FLUX_THEME_OK_FG);
        flux_sb_appendf(&l, "%.2f%% skipped", avg_pct_skipped);
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_appendf(&l, "mode: %s   ", BENCH_MODE_NAMES[b->mode]);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "[m] cycle mode");
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 3: AI COMPONENTS GALLERY
 * ════════════════════════════════════════════════════════════════════ */

static void s3_section(FluxSB *sb, int inner_w, const char *title) {
    char L[1024]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_BRAND_PURPLE_FG);
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, title);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    /* underline-like */
    int t_w = (int)strlen(title);
    for (int i = 0; i < inner_w - 4 - t_w; i++) flux_sb_append(&l, "\xe2\x94\x80");
    flux_sb_append(&l, FLUX_RESET);
    demo_padded(sb, inner_w, flux_sb_str(&l));
}

static void s3_brand_demo(FluxSB *sb, int inner_w) {
    char L[1024]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_brand(&l, "\xe2\x9c\xa6", FLUX_THEME_BRAND_PURPLE_FG,
               "storm agent", FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, "    ");
    flux_brand(&l, "\xe2\x97\x86", FLUX_THEME_ACCENT_FG,
               "claude code", FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, "    ");
    flux_brand(&l, "\xe2\x9a\xa1", FLUX_THEME_WARN_FG,
               "thunder ai", FLUX_THEME_TEXT_FG);
    demo_padded(sb, inner_w, flux_sb_str(&l));
}

static void s3_activity_demo(FluxSB *sb, int inner_w, int spinner_frame) {
    FluxActivity items[5];
    items[0] = (FluxActivity){ "Loaded context window",   FLUX_ACT_DONE,    120,  0 };
    items[1] = (FluxActivity){ "Tokenized 8.4K input",    FLUX_ACT_DONE,     45,  0 };
    items[2] = (FluxActivity){ "Calling tool: bash",      FLUX_ACT_RUNNING,  -1,  spinner_frame };
    items[3] = (FluxActivity){ "Awaiting approval",       FLUX_ACT_PENDING,  -1,  0 };
    items[4] = (FluxActivity){ "Failed: rate limited",    FLUX_ACT_FAILED,   -1,  0 };
    flux_activity_list_render(sb, items, 5, inner_w, 4);
}

static void s3_diff_demo(FluxSB *sb, int inner_w) {
    FluxDiffLine lines[5];
    lines[0] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "function refresh() {" };
    lines[1] = (FluxDiffLine){ FLUX_DIFF_REMOVED, "  return token.refresh();" };
    lines[2] = (FluxDiffLine){ FLUX_DIFF_ADDED,   "  if (await tokenIsValid()) return token;" };
    lines[3] = (FluxDiffLine){ FLUX_DIFF_ADDED,   "  return token.refresh();" };
    lines[4] = (FluxDiffLine){ FLUX_DIFF_CONTEXT, "}" };
    flux_diff_block_render(sb, "src/auth/refresh.ts", lines, 5, inner_w);
}

static void s3_buttons_demo(FluxSB *sb, int inner_w) {
    char L[1024]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    /* selected state */
    FluxButton b1 = { "Apply",    FLUX_THEME_OK_FG,  FLUX_THEME_BUTTON_OK_BG, FLUX_THEME_TEXT_DIM_FG };
    FluxButton b2 = { "Reject",   FLUX_THEME_ERR_FG, FLUX_THEME_BUTTON_NO_BG, FLUX_THEME_TEXT_DIM_FG };
    FluxButton b3 = { "Continue", FLUX_THEME_ACCENT_FG, FLUX_THEME_BUTTON_OK_BG, FLUX_THEME_TEXT_DIM_FG };
    flux_button_render(&l, &b1, 1);  /* filled */
    flux_sb_append(&l, "  ");
    flux_button_render(&l, &b2, 0);  /* outlined */
    flux_sb_append(&l, "  ");
    flux_button_render(&l, &b3, 0);  /* outlined */
    flux_sb_append(&l, "    ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "← filled (selected)  outlined  outlined");
    flux_sb_append(&l, FLUX_RESET);
    demo_padded(sb, inner_w, flux_sb_str(&l));
}

static void s3_status_demo(FluxSB *sb, int inner_w) {
    FluxStatusBar s = { "claude-opus", 12300, 0.34f, 0.41f, 12 };
    flux_statusbar_render(sb, &s, inner_w);
    flux_sb_append(sb, "\n");
}

static void s3_message_demo(FluxSB *sb, int inner_w, int spinner_frame) {
    /* user msg */
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, ">");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        flux_sb_append(&l, "List the files in src/");
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(sb, inner_w, flux_sb_str(&l));
    }
    /* assistant msg */
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, "\xe2\x97\x8f"); /* ● */
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        flux_sb_append(&l, "I'll run `ls src/` to enumerate the project files.");
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(sb, inner_w, flux_sb_str(&l));
    }
    /* thinking */
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, FLUX_SPINNER_DOT[spinner_frame % FLUX_SPINNER_DOT_N]);
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, FLUX_ITALIC);
        flux_sb_append(&l, "Thinking...");
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(sb, inner_w, flux_sb_str(&l));
    }
}

static void screen3_render(FluxSB *inner, int inner_w, int spinner_idx, int selected) {
    demo_blank(inner, inner_w, 1);
    s3_section(inner, inner_w, "Brand labels");
    s3_brand_demo(inner, inner_w);
    demo_blank(inner, inner_w, 1);

    s3_section(inner, inner_w, "Message types");
    s3_message_demo(inner, inner_w, spinner_idx);
    demo_blank(inner, inner_w, 1);

    s3_section(inner, inner_w, "Activity rows (4 statuses + spinner)");
    s3_activity_demo(inner, inner_w, spinner_idx);
    demo_blank(inner, inner_w, 1);

    s3_section(inner, inner_w, "Diff block (5 lines, +/-/context)");
    s3_diff_demo(inner, inner_w);
    demo_blank(inner, inner_w, 1);

    s3_section(inner, inner_w, "Buttons");
    s3_buttons_demo(inner, inner_w);
    demo_blank(inner, inner_w, 1);

    s3_section(inner, inner_w, "Approval card");
    s1_approval(inner, inner_w, selected);
    demo_blank(inner, inner_w, 1);

    s3_section(inner, inner_w, "Status bar");
    s3_status_demo(inner, inner_w);
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 4: SCROLLABLE CHAT
 *
 * Long synthetic agent conversation. Scrollable with:
 *   - mouse wheel (MSG_MOUSE wheel events)
 *   - up/down/pgup/pgdn keys
 *   - j/k vim keys
 * Tabs at the top are clickable (mouse left-press inside their cells).
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    int kind;          /* 0=user, 1=assistant, 2=tool, 3=tool_result, 4=thinking */
    const char *body;
} ChatLine;

static const ChatLine CHAT[] = {
    /* Turn 1: refactor request */
    { 0, "Refactor the auth middleware to use the new token store API." },
    { 1, "I'll start by reading the current middleware and the new API surface." },
    { 4, "Need to map old TokenStore.get() to the async lookupSession()..." },
    { 2, "tool: read_file(path=\"src/auth/middleware.ts\")" },
    { 3, "→ 124 lines, exports `requireAuth`, `optionalAuth`" },
    { 2, "tool: read_file(path=\"src/auth/store.ts\")" },
    { 3, "→ 87 lines, async lookupSession(token), returns Session|null" },
    { 1, "Got it. The migration is mostly mechanical — I'll wrap each call site in await." },
    { 0, "Sounds good. Watch out for the cookie expiry race condition." },
    { 4, "Checking: requireAuth currently checks expiry synchronously..." },
    { 2, "tool: search(pattern=\"expiresAt\", path=\"src/auth/\")" },
    { 3, "→ 4 matches in middleware.ts, 2 in store.ts" },
    { 1, "Found it. The expiry check needs to use the same clock skew buffer as refresh." },
    { 2, "tool: edit_file(path=\"src/auth/middleware.ts\", op=\"patch\")" },
    { 3, "→ Patched 3 hunks, 18 insertions, 12 deletions" },
    { 4, "Now the tests..." },
    { 2, "tool: bash(cmd=\"npm test src/auth\")" },
    { 3, "→ 24 passing, 1 failing: \"requireAuth: rejects expired token\"" },
    { 1, "The failure is because the test mocks Date.now() but the buffer is bigger now." },
    { 2, "tool: edit_file(path=\"src/auth/__tests__/middleware.test.ts\")" },
    { 3, "→ Updated 1 mock value: 60_000 → 90_000" },
    { 2, "tool: bash(cmd=\"npm test src/auth\")" },
    { 3, "→ 25 passing, 0 failing" },
    { 1, "All green. Want me to open a PR?" },
    { 0, "Yes, draft it." },
    { 2, "tool: gh_pr_create(title=\"auth: migrate to async TokenStore\", base=\"main\")" },
    { 3, "→ Opened PR #1024" },
    { 1, "Done. PR #1024 is up — single commit, 18+/12-, tests passing." },
    { 0, "Perfect. Add a CHANGELOG entry too." },
    { 2, "tool: edit_file(path=\"CHANGELOG.md\")" },
    { 3, "→ Prepended new entry under [Unreleased]" },
    { 1, "Added. Anything else?" },
    { 0, "Nope, that's it. Thanks!" },
    { 1, "Anytime." },

    /* Turn 2: bug investigation */
    { 0, "Wait — the CI is failing. Can you investigate?" },
    { 1, "On it. Pulling the CI logs now." },
    { 2, "tool: gh_run_view(run_id=12471)" },
    { 3, "→ Job \"integration\" failed at step \"E2E auth flow\"" },
    { 2, "tool: gh_run_view_log(run_id=12471, job=\"integration\")" },
    { 3, "→ 1247 lines retrieved" },
    { 4, "Searching for the actual assertion failure..." },
    { 1, "Found it: the integration suite uses real network calls and the new buffer changed timing." },
    { 2, "tool: read_file(path=\"tests/e2e/auth.spec.ts\", lines=\"45-80\")" },
    { 3, "→ Mock server expires tokens at exactly Date.now()+5000" },
    { 1, "The 30s skew buffer means our middleware now considers them valid for 30s past expiry." },
    { 0, "OK so the mock server is wrong, not our code?" },
    { 1, "Right. The mock should expire at +35000 to account for the buffer. Easy fix." },
    { 2, "tool: edit_file(path=\"tests/e2e/auth.spec.ts\")" },
    { 3, "→ Replaced 5000 → 35000 in 3 places" },
    { 2, "tool: bash(cmd=\"npm run test:e2e\")" },
    { 3, "→ 142 passing, 0 failing" },
    { 1, "Pushed. CI should turn green in ~3 minutes." },

    /* Turn 3: docs request */
    { 0, "Great. Now write docs for the new TokenStore API." },
    { 1, "Sure. Where should they live — README, /docs/, or inline?" },
    { 0, "Put them in docs/auth/token-store.md" },
    { 2, "tool: list_files(path=\"docs/auth/\")" },
    { 3, "→ index.md, sessions.md (no token-store.md yet)" },
    { 4, "I'll model the structure after sessions.md." },
    { 2, "tool: read_file(path=\"docs/auth/sessions.md\")" },
    { 3, "→ 320 lines: Overview, API, Examples, Migration, Troubleshooting" },
    { 1, "Drafting now..." },
    { 2, "tool: write_file(path=\"docs/auth/token-store.md\")" },
    { 3, "→ 287 lines created" },
    { 1, "Draft is up. Let me know if you want different sections." },
    { 0, "Looks good. Add a code example for the migration path from v1 → v2." },
    { 2, "tool: edit_file(path=\"docs/auth/token-store.md\", section=\"Migration\")" },
    { 3, "→ Inserted 32-line code example with before/after" },
    { 1, "Done. Anything else for this PR?" },
    { 0, "No, that's it. Thanks!" },
    { 1, "Anytime." },
};
#define CHAT_N ((int)(sizeof CHAT / sizeof CHAT[0]))

/* (Legacy ChatState removed — FluxScroll in App handles all scroll state.) */

static void chat_emit_msg(FluxSB *sb, int inner_w, const ChatLine *m) {
    const char *glyph; const char *clr; const char *body_clr;
    switch (m->kind) {
        case 0: glyph = ">";            clr = FLUX_THEME_TEXT_OFF_FG;     body_clr = FLUX_THEME_TEXT_FG; break;
        case 1: glyph = "\xe2\x97\x8f"; clr = FLUX_THEME_ACCENT_FG;       body_clr = FLUX_THEME_TEXT_FG; break;       /* ● */
        case 2: glyph = "\xe2\x9a\x99"; clr = FLUX_THEME_BRAND_PURPLE_FG; body_clr = FLUX_THEME_TEXT2_FG; break;     /* ⚙ */
        case 3: glyph = "\xe2\x86\xb3"; clr = FLUX_THEME_OK_DIM_FG;       body_clr = FLUX_THEME_TEXT_DIM_FG; break;  /* ↳ */
        case 4: glyph = "\xe2\x81\x82"; clr = FLUX_THEME_TEXT_OFF_FG;     body_clr = FLUX_THEME_TEXT_DIM_FG; break;  /* ⁂ */
        default: glyph = " ";           clr = FLUX_THEME_TEXT_FG;         body_clr = FLUX_THEME_TEXT_FG;
    }
    /* gutter_w=2 forces every glyph (1- or 2-cell) into a 2-cell gutter,
     * so all message bodies start at the same column — no manual math. */
    flux_message_row(sb, glyph, clr, m->body, body_clr, inner_w, 2, 2);
}

/* (Legacy chat renderer removed — flux_scrollview_render handles all this.) */

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 5: AI SHOWCASE — B5 storm-parity AI widgets
 *
 * Live, animated. State lives in g_ai (advanced from MSG_TICK).
 * Demonstrates: FluxBlinkDot, flux_message_bubble, flux_model_badge,
 * flux_context_window, flux_cost_tracker, FluxOpTree, flux_token_stream,
 * FluxShimmerText, FluxStreamingText, flux_command_block.
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    int             initialized;
    FluxBlinkDot    dots[4];
    FluxShimmerText shimmer;
    FluxStreamingText streamer;
    long            in_tokens;       /* grows per tick */
    long            out_tokens;
    int             grow_dir;        /* +1 / -1 for cost meter sweep */
    int             tick_n;
    /* OpTree backing storage */
    FluxOpItem      op_items[10];
    unsigned char   op_expanded[10];
    FluxOpTree      op_tree;
    /* Per-widget rate gating — 5 spinners at different speeds.
     * The OUTER loop ticks at 120 Hz, but each spinner advances at
     * its OWN rate via FluxRate. */
    FluxSpinner     rate_spinners[5];
    FluxRate        rate_gates[5];
    int             rate_advances[5];   /* count of advances (visible "speed") */
} AiState;

static AiState g_ai;

static const char *AI_STREAM_TEXT =
    "Streaming response... I'll inspect the auth module, then patch "
    "the token-refresh race condition and re-run the test suite.";

static void ai_init(AiState *a) {
    int i;
    if (a->initialized) return;

    flux_blinkdot_init(&a->dots[0], FLUX_DOT_PENDING);
    flux_blinkdot_init(&a->dots[1], FLUX_DOT_RUNNING);
    flux_blinkdot_init(&a->dots[2], FLUX_DOT_STREAMING);
    flux_blinkdot_init(&a->dots[3], FLUX_DOT_COMPLETED);

    flux_shimmer_init(&a->shimmer, "Working on your request");
    a->shimmer.active = 1;

    flux_streaming_init(&a->streamer, AI_STREAM_TEXT);
    a->streamer.speed = 1;
    a->streamer.show_cursor = 1;

    a->in_tokens  = 12340;
    a->out_tokens = 4120;
    a->grow_dir = +1;

    /* Build a small operation tree.  Index | depth | label / status. */
    a->op_items[0] = (FluxOpItem){ "root",  "Plan changes",         "3 sub-tasks", FLUX_OP_RUNNING,   0, -1, 1 };
    a->op_items[1] = (FluxOpItem){ "read",  "read_file",            "src/auth.ts", FLUX_OP_COMPLETED, 1, 340, 0 };
    a->op_items[2] = (FluxOpItem){ "edit",  "patch",                "3 hunks",     FLUX_OP_RUNNING,   1, -1,  1 };
    a->op_items[3] = (FluxOpItem){ "h1",    "hunk @ line 42",       "+5 -3",       FLUX_OP_COMPLETED, 2, 14,  0 };
    a->op_items[4] = (FluxOpItem){ "h2",    "hunk @ line 88",       "+2 -1",       FLUX_OP_COMPLETED, 2, 9,   0 };
    a->op_items[5] = (FluxOpItem){ "h3",    "hunk @ line 120",      "+8 -0",       FLUX_OP_RUNNING,   2, -1,  0 };
    a->op_items[6] = (FluxOpItem){ "test",  "npm test src/auth",    NULL,          FLUX_OP_PENDING,   1, -1,  0 };

    for (i = 0; i < 10; i++) a->op_expanded[i] = 1;
    flux_op_tree_init(&a->op_tree, a->op_items, a->op_expanded, 7);

    /* Multi-rate spinners: 1 / 2 / 5 / 10 / 30 Hz on the same screen. */
    static const int RATE_FPS[5] = { 1, 2, 5, 10, 30 };
    for (i = 0; i < 5; i++) {
        flux_spinner_init(&a->rate_spinners[i],
                          FLUX_SPINNER_DOT, FLUX_SPINNER_DOT_N, NULL);
        flux_rate_set_fps(&a->rate_gates[i], RATE_FPS[i]);
        a->rate_advances[i] = 0;
    }

    a->initialized = 1;
}

static void ai_tick(AiState *a) {
    int i;
    if (!a->initialized) ai_init(a);
    for (i = 0; i < 4; i++) flux_blinkdot_tick(&a->dots[i]);
    flux_shimmer_tick(&a->shimmer);
    flux_streaming_tick(&a->streamer);
    flux_op_tree_tick(&a->op_tree);

    /* Per-widget FPS demo: each spinner only advances when its own
     * FluxRate gate fires. Outer loop calls ai_tick() at 120 Hz, but
     * spinner[0] advances at 1 Hz, spinner[4] at 30 Hz. */
    uint64_t now = flux_now_ms();
    for (i = 0; i < 5; i++) {
        if (flux_rate_due(&a->rate_gates[i], now)) {
            flux_spinner_tick(&a->rate_spinners[i]);
            a->rate_advances[i]++;
        }
    }

    /* Animate cost meter — slowly grow tokens, then reset. */
    a->tick_n++;
    a->in_tokens  += 20;
    a->out_tokens += 7;
    if (a->in_tokens > 180000) {
        a->in_tokens  = 12340;
        a->out_tokens = 4120;
        flux_streaming_init(&a->streamer, AI_STREAM_TEXT);
        a->streamer.speed = 1;
        a->streamer.show_cursor = 1;
    }
}

static void s5_section(FluxSB *sb, int inner_w, const char *title) {
    char L[1024]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_BRAND_PURPLE_FG);
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, title);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    {
        int t_w = (int)strlen(title);
        int i;
        for (i = 0; i < inner_w - 4 - t_w; i++) flux_sb_append(&l, "\xe2\x94\x80");
    }
    flux_sb_append(&l, FLUX_RESET);
    demo_padded(sb, inner_w, flux_sb_str(&l));
}

static void screen5_render(FluxSB *inner, int inner_w, AiState *a) {
    int inner2 = inner_w - 4;  /* indent inside the area */
    if (inner2 < 20) inner2 = inner_w;
    ai_init(a);

    demo_blank(inner, inner_w, 1);

    /* ── Per-widget FPS demo: 5 spinners, same loop, different rates ─ */
    s5_section(inner, inner_w,
        "FluxRate — same 120 Hz loop, 5 spinners @ 1/2/5/10/30 Hz (watch the difference!)");
    {
        static const int FPS[5] = { 1, 2, 5, 10, 30 };
        static const char *DESC[5] = {
            "very slow ",
            "slow      ",
            "medium    ",
            "fast      ",
            "very fast "
        };
        int i;
        for (i = 0; i < 5; i++) {
            char L[512]; FluxSB l;
            flux_sb_init(&l, L, sizeof L);
            flux_sb_append(&l, "  ");
            /* spinner glyph (advances at this widget's own rate) */
            flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
            flux_spinner_render(&a->rate_spinners[i], &l, FLUX_THEME_ACCENT_FG);
            flux_sb_append(&l, FLUX_RESET);
            flux_sb_append(&l, "  ");
            /* speed label */
            flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
            flux_sb_appendf(&l, "%2d Hz  ", FPS[i]);
            flux_sb_append(&l, FLUX_RESET);
            flux_sb_append(&l, FLUX_THEME_TEXT2_FG);
            flux_sb_append(&l, DESC[i]);
            flux_sb_append(&l, FLUX_RESET);
            /* rate gauge: fps × bar cells (visual indicator of speed) */
            flux_sb_append(&l, "  ");
            flux_inline_bar(&l, (float)FPS[i] / 30.0f, 16,
                            FLUX_THEME_ACCENT_FG, FLUX_THEME_TEXT_OFF_FG);
            /* tick counter — proves the gating is real */
            flux_sb_append(&l, "  ");
            flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
            flux_sb_appendf(&l, "advances: %5d", a->rate_advances[i]);
            flux_sb_append(&l, FLUX_RESET);
            demo_padded(inner, inner_w, flux_sb_str(&l));
        }
    }
    demo_blank(inner, inner_w, 1);

    /* ── BlinkDot row ────────────────────────────────────────────── */
    s5_section(inner, inner_w, "FluxBlinkDot — pending / running / streaming / completed");
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        {
            const char *labels[4] = { "pending  ", "running  ", "streaming", "completed" };
            int i;
            for (i = 0; i < 4; i++) {
                flux_blinkdot_render(&a->dots[i], &l);
                flux_sb_append(&l, " ");
                flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
                flux_sb_append(&l, labels[i]);
                flux_sb_append(&l, FLUX_RESET);
                flux_sb_append(&l, "    ");
            }
        }
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Model badges ────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_model_badge — provider · model · context");
    {
        char L[256]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_model_badge(&l, "Anthropic", "claude-opus-4-7", 200000, inner_w - 4);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[256]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_model_badge(&l, "OpenAI", "gpt-4o", 128000, inner_w - 4);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[256]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_model_badge(&l, "Google", "gemini-1.5-pro", 1000000, inner_w - 4);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[256]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_model_badge(&l, "local", "llama-3.1-70b", 32000, inner_w - 4);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Context window (animated) ──────────────────────────────── */
    s5_section(inner, inner_w, "flux_context_window — animated bar (200K budget)");
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_context_window(&l, a->in_tokens + a->out_tokens, 200000L, inner_w - 4);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Token stream ───────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_token_stream — live tok/sec");
    {
        char L[512]; FluxSB l;
        double tps = 30.0 + 25.0 * sin(a->tick_n * 0.05);
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_token_stream(&l, tps, a->out_tokens, 8000L, inner_w - 4);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Cost tracker ───────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_cost_tracker — running USD");
    flux_cost_tracker(inner, a->in_tokens, a->out_tokens,
                      3.0, 15.0, 0.0, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── Message bubbles ────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_message_bubble — user / assistant / system / tool");
    flux_message_bubble(inner, FLUX_ROLE_USER,
        "Refactor the auth middleware.", "12:01", inner_w);
    flux_message_bubble(inner, FLUX_ROLE_ASSISTANT,
        "On it. Reading the module first.", "12:01", inner_w);
    flux_message_bubble(inner, FLUX_ROLE_TOOL,
        "read_file → 124 lines, exports requireAuth/optionalAuth", "12:01", inner_w);
    flux_message_bubble(inner, FLUX_ROLE_SYSTEM,
        "rate-limit warning: 80% of your hourly cap consumed", NULL, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── OpTree ─────────────────────────────────────────────────── */
    s5_section(inner, inner_w, "FluxOpTree — nested tool-call tree (├─ / └─)");
    flux_op_tree_render(&a->op_tree, inner, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── Shimmer ────────────────────────────────────────────────── */
    s5_section(inner, inner_w, "FluxShimmerText — traveling highlight");
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_shimmer_render(&a->shimmer, &l);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Streaming text ─────────────────────────────────────────── */
    s5_section(inner, inner_w, "FluxStreamingText — typewriter reveal");
    {
        char L[2048]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_streaming_render(&a->streamer, &l);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── command_block ──────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_command_block — bordered shell card");
    flux_command_block(inner, "npm test src/auth",
        "PASS  src/auth/middleware.test.ts\n"
        "PASS  src/auth/store.test.ts\n"
        "Tests: 25 passed, 25 total\n"
        "Time:  1.842 s", 0, 1842, inner_w);
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 6: FORMS — B2 interactive form widgets
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    int             initialized;
    FluxCheckbox    cb1, cb2, cb3;
    FluxSwitch      sw;
    FluxRadio       radio;
    FluxStepper     stepper;
    FluxSearchInput search;
    FluxTextArea    ta;
    FluxMaskedInput card;
    int             focus;             /* 0..7 */
} FormState;

static FormState g_form;

static const char *FORM_RADIO_LABELS[] = { "Low", "Medium", "High" };

static void form_init(FormState *f) {
    if (f->initialized) return;
    flux_checkbox_init(&f->cb1, "Enable telemetry",        0);
    flux_checkbox_init(&f->cb2, "Auto-update at startup",  1);
    flux_checkbox_init(&f->cb3, "Beta opt-in",             0);
    flux_switch_init  (&f->sw,  "Notifications",           1);
    flux_radio_init   (&f->radio, FORM_RADIO_LABELS, 3, 1);
    flux_stepper_init (&f->stepper, 8, 1, 64, 1);
    f->stepper.label = "Workers";
    flux_search_init  (&f->search, "Search files…");
    flux_textarea_init(&f->ta,  "Write a short note…");
    flux_masked_init  (&f->card, "####-####-####-####");
    /* Set focus on the first widget; other widgets keep focused=1 from
     * init but only the focused one will receive keys. */
    f->focus = 0;
    f->initialized = 1;
}

static void form_set_focus(FormState *f, int new_focus) {
    if (new_focus < 0) new_focus = 7;
    if (new_focus > 7) new_focus = 0;
    f->focus = new_focus;
    f->cb1.focused    = (new_focus == 0);
    f->cb2.focused    = (new_focus == 1);
    f->cb3.focused    = (new_focus == 2);
    f->sw.focused     = (new_focus == 3);
    f->radio.focused  = (new_focus == 4);
    f->stepper.focused= (new_focus == 5);
    /* FluxSearchInput / FluxMaskedInput / FluxTextArea don't expose a
     * focused flag; we only forward keys to the active one. */
    f->card.focused   = (new_focus == 7);
}

static int form_update(FormState *f, FluxMsg msg) {
    int changed = 0;
    if (!f->initialized) form_init(f);
    if (msg.type == MSG_KEY) {
        if (flux_key_is(msg, "tab")) {
            form_set_focus(f, f->focus + 1); return 1;
        }
        if (flux_key_is(msg, "S-tab") || flux_key_is(msg, "btab")) {
            form_set_focus(f, f->focus - 1); return 1;
        }
    }
    switch (f->focus) {
        case 0: changed |= flux_checkbox_update(&f->cb1, msg); break;
        case 1: changed |= flux_checkbox_update(&f->cb2, msg); break;
        case 2: changed |= flux_checkbox_update(&f->cb3, msg); break;
        case 3: changed |= flux_switch_update  (&f->sw,  msg); break;
        case 4: changed |= flux_radio_update   (&f->radio, msg); break;
        case 5: changed |= flux_stepper_update (&f->stepper, msg); break;
        case 6: changed |= flux_search_update  (&f->search, msg); break;
        case 7: changed |= flux_masked_update  (&f->card, msg); break;
        default: break;
    }
    return changed;
}

static void s6_label(FluxSB *sb, int inner_w, const char *t, int focused) {
    char L[256]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, focused ? FLUX_THEME_ACCENT_FG : FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, focused ? "▸ " : "  ");
    flux_sb_append(&l, t);
    flux_sb_append(&l, FLUX_RESET);
    demo_padded(sb, inner_w, flux_sb_str(&l));
}

static void screen6_render(FluxSB *inner, int inner_w, FormState *f) {
    int field_w = inner_w - 4;
    form_init(f);
    if (field_w < 20) field_w = inner_w;

    demo_blank(inner, inner_w, 1);

    s5_section(inner, inner_w, "Forms — Tab/S-Tab to move focus");
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "Checkboxes (Space to toggle)", f->focus <= 2);
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_checkbox_render(&f->cb1, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_checkbox_render(&f->cb2, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_checkbox_render(&f->cb3, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "Switch (Space to toggle)", f->focus == 3);
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_switch_render(&f->sw, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "Radio (↑/↓, Space)", f->focus == 4);
    {
        char L[2048]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_radio_render(&f->radio, &l, field_w);
        /* Indent each emitted row by 2 spaces. */
        const char *p = flux_sb_str(&l);
        const char *line_start = p;
        while (*p) {
            if (*p == '\n') {
                char one[1024]; FluxSB o;
                int n = (int)(p - line_start);
                if (n > (int)sizeof one - 1) n = sizeof one - 1;
                memcpy(one, line_start, n); one[n] = 0;
                {
                    char L2[1100]; FluxSB l2;
                    flux_sb_init(&l2, L2, sizeof L2);
                    flux_sb_append(&l2, "  ");
                    flux_sb_append(&l2, one);
                    demo_padded(inner, inner_w, flux_sb_str(&l2));
                    (void)o;
                }
                line_start = p + 1;
            }
            p++;
        }
    }
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "Stepper (←/→ or +/-, PgUp/PgDn fast)", f->focus == 5);
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_stepper_render(&f->stepper, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "Search (type, Esc clears)", f->focus == 6);
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_search_render(&f->search, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "Masked input (credit card mask)", f->focus == 7);
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_masked_render(&f->card, &l, field_w);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    s6_label(inner, inner_w, "TextArea (read-only preview)", 0);
    flux_textarea_render(&f->ta, inner, field_w, 4);
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 7: CHARTS — B6 data widgets
 * ════════════════════════════════════════════════════════════════════ */

#define CHART_SAMPLES 64

typedef struct {
    int     initialized;
    float   line[CHART_SAMPLES];
    float   line2[CHART_SAMPLES];
    float   area[CHART_SAMPLES];
    float   bars[8];
    float   hist_samples[200];
    int     hist_bins[12];
    float   heat[6 * 16];
    FluxPoint scatter[80];
    int     anim;
} ChartState;

static ChartState g_chart;

static void chart_init(ChartState *c) {
    int i;
    if (c->initialized) return;
    for (i = 0; i < CHART_SAMPLES; i++) {
        double t = (double)i / CHART_SAMPLES * 6.283185;
        c->line[i]  = (float)(60.0 + 20.0 * sin(t * 1.2));
        c->line2[i] = (float)(60.0 + 25.0 * cos(t * 1.5 + 0.7));
        c->area[i]  = (float)(40.0 + 35.0 * (0.5 + 0.5 * sin(t * 0.9 + 1.2)));
    }
    /* Bars: monthly KPI */
    c->bars[0] = 12; c->bars[1] = 19; c->bars[2] = 28; c->bars[3] = 24;
    c->bars[4] = 35; c->bars[5] = 42; c->bars[6] = 38; c->bars[7] = 47;
    /* Histogram samples — bell-ish */
    {
        unsigned int seed = 1234567u;
        for (i = 0; i < 200; i++) {
            seed = seed * 1664525u + 1013904223u;
            double u1 = (double)((seed >> 8) & 0xffff) / 65536.0;
            seed = seed * 1664525u + 1013904223u;
            double u2 = (double)((seed >> 8) & 0xffff) / 65536.0;
            double g = sqrt(-2.0 * log(u1 + 1e-9)) * cos(6.283185 * u2);
            c->hist_samples[i] = (float)(50.0 + 15.0 * g);
        }
        /* Bucket into 12 bins [0..120]. */
        for (i = 0; i < 12; i++) c->hist_bins[i] = 0;
        for (i = 0; i < 200; i++) {
            int b = (int)(c->hist_samples[i] / 10.0f);
            if (b < 0) b = 0;
            if (b > 11) b = 11;
            c->hist_bins[b]++;
        }
    }
    /* Heatmap 6x16 — gradient diagonal pattern */
    {
        int r, k;
        for (r = 0; r < 6; r++) {
            for (k = 0; k < 16; k++) {
                double v = sin(0.5 * r + 0.4 * k) * 0.5 + 0.5;
                c->heat[r * 16 + k] = (float)v;
            }
        }
    }
    /* Scatter — noisy linear */
    {
        unsigned int seed = 42u;
        for (i = 0; i < 80; i++) {
            seed = seed * 1664525u + 1013904223u;
            float x = (float)((seed >> 8) & 0xff) / 255.0f * 100.0f;
            seed = seed * 1664525u + 1013904223u;
            float noise = ((float)((seed >> 8) & 0xff) / 255.0f - 0.5f) * 30.0f;
            c->scatter[i].x = x;
            c->scatter[i].y = 0.7f * x + 10.0f + noise;
        }
    }
    c->initialized = 1;
}

static void chart_tick(ChartState *c) {
    int i;
    if (!c->initialized) chart_init(c);
    c->anim++;
    /* Slowly rotate the line series so it feels alive. */
    {
        float first = c->line[0];
        for (i = 0; i < CHART_SAMPLES - 1; i++) c->line[i] = c->line[i + 1];
        c->line[CHART_SAMPLES - 1] = first;
    }
}

static void screen7_render(FluxSB *inner, int inner_w, ChartState *c) {
    int chart_w = inner_w - 4;
    if (chart_w < 30) chart_w = inner_w;
    chart_init(c);

    demo_blank(inner, inner_w, 1);

    /* ── Sparkline (using existing flux_sparkline) ─────────────── */
    s5_section(inner, inner_w, "flux_sparkline — compact one-line trend");
    {
        char L[2048]; FluxSB l;
        flux_sb_init(&l, L, sizeof L); flux_sb_append(&l, "  ");
        flux_sparkline(&l, c->line, CHART_SAMPLES, 0,
                       FLUX_THEME_BRAND_PURPLE_FG);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Line chart (multi-series via braille) ─────────────────── */
    s5_section(inner, inner_w, "flux_line_chart_multi — braille (2 series)");
    {
        FluxSeries series[2] = {
            { c->line,  CHART_SAMPLES, FLUX_THEME_BRAND_PURPLE_FG, "p95" },
            { c->line2, CHART_SAMPLES, FLUX_THEME_ACCENT_FG,       "p50" },
        };
        FluxLineChartOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.show_axes = 1;
        opts.title = NULL;
        flux_line_chart_multi(inner, series, 2, chart_w, 8, &opts);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Area chart ────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_area_chart — filled to baseline");
    {
        FluxAreaOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.color = FLUX_THEME_OK_FG;
        opts.show_axes = 1;
        flux_area_chart(inner, c->area, CHART_SAMPLES, chart_w, 7, &opts);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Bar chart ─────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_bar_chart — vertical, with labels");
    {
        const char *labels[8] = { "M", "T", "W", "T", "F", "S", "S", "M" };
        FluxBarChartOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.color = FLUX_THEME_ACCENT_FG;
        opts.show_axes = 1;
        opts.show_values = 1;
        opts.bar_gap = 1;
        flux_bar_chart(inner, c->bars, 8, chart_w, 8, labels, &opts);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Histogram ─────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_histogram — 200 samples, 12 bins");
    {
        FluxHistogramOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.color = FLUX_THEME_BRAND_PURPLE_FG;
        opts.show_axes = 1;
        opts.x_label = "ms";
        flux_histogram(inner, c->hist_bins, 12, chart_w, 7, &opts);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Scatter ───────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_scatter — 80 points + trend line");
    {
        FluxScatterOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.color = FLUX_THEME_WARN_FG;
        opts.show_axes = 1;
        opts.show_trend = 1;
        opts.dot_size = 1;
        flux_scatter(inner, c->scatter, 80, chart_w, 8, &opts);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Heatmap ───────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_heatmap — 6×16 grid");
    {
        FluxHeatmapOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.cell_w = 3;
        flux_heatmap(inner, c->heat, 6, 16, chart_w, &opts);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Gauge ─────────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_gauge — percent meter (animated)");
    {
        float val = (float)(50.0 + 30.0 * sin(c->anim * 0.04));
        FluxGaugeOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.label = "CPU";
        opts.show_value = 1;
        opts.arc = 0;
        flux_gauge(inner, val, 0.0f, 100.0f, chart_w, &opts);
    }
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 8: MARKDOWN / CODE
 * ════════════════════════════════════════════════════════════════════ */

static const char *MD_SAMPLE =
    "# flux.h cheat sheet\n\n"
    "A **single-header** TUI library for C99 — *Elm Architecture*, "
    "cell-level diff, zero malloc in renderers.\n\n"
    "## Highlights\n\n"
    "- 500+ public functions in one `.h`\n"
    "- Animated AI widgets: `FluxBlinkDot`, `FluxStreamingText`, ...\n"
    "- Charts: `flux_bar_chart`, `flux_area_chart`, `flux_heatmap`\n"
    "- Forms: `FluxCheckbox`, `FluxRadio`, `FluxStepper`, `FluxForm`\n\n"
    "### Code\n\n"
    "```c\n"
    "FluxSB sb; char buf[1024];\n"
    "flux_sb_init(&sb, buf, sizeof buf);\n"
    "flux_alert(&sb, FLUX_KIND_INFO, \"Hi\", \"Hello\", 60);\n"
    "```\n\n"
    "> tip: `make` builds the demo, `./demo` runs it.\n";

static const char *C_SAMPLE =
    "/* token refresh — accounts for clock skew */\n"
    "static int is_expired(const Token *t) {\n"
    "    const long buffer_ms = 30000;\n"
    "    return now_ms() >= t->expires_at_ms - buffer_ms;\n"
    "}\n";

static const char *JSON_SAMPLE =
    "{\n"
    "  \"model\": \"claude-opus-4-7\",\n"
    "  \"max_tokens\": 200000,\n"
    "  \"temperature\": 0.7,\n"
    "  \"stream\": true,\n"
    "  \"tools\": [\"bash\", \"read_file\", \"edit_file\"]\n"
    "}\n";

static void screen8_render(FluxSB *inner, int inner_w) {
    int w = inner_w - 4;
    if (w < 20) w = inner_w;

    demo_blank(inner, inner_w, 1);

    s5_section(inner, inner_w, "flux_markdown — # ## - * ` ``` > ");
    flux_markdown(inner, MD_SAMPLE, inner_w);
    demo_blank(inner, inner_w, 1);

    s5_section(inner, inner_w, "flux_syntax_highlight — C");
    flux_syntax_highlight(inner, C_SAMPLE, FLUX_LANG_C, inner_w);
    demo_blank(inner, inner_w, 1);

    s5_section(inner, inner_w, "flux_syntax_highlight — JSON");
    flux_syntax_highlight(inner, JSON_SAMPLE, FLUX_LANG_JSON, inner_w);
    demo_blank(inner, inner_w, 1);

    s5_section(inner, inner_w, "flux_inline_diff — word-level");
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_inline_diff(&l, "hello world",
                          "hello brave new world", inner_w);
        flux_sb_append(inner, flux_sb_str(&l));
    }
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_inline_diff(&l,
            "if (Date.now() < token.expiresAt) {",
            "if (Date.now() < token.expiresAt - buffer) {", inner_w);
        flux_sb_append(inner, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    s5_section(inner, inner_w, "flux_command_block — invocation card");
    flux_command_block(inner, "git log --oneline -3",
        "0f65b88 Merge pull request #1\n"
        "bc5c0aa flux.h v2: cell-based renderer, layout engine, diff engine\n"
        "744b1bb add watt to built with flux.h",
        0, 12, inner_w);
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 9: EFFECTS — B7 visual flair
 *
 * State (animated): live HH:MM:SS clock + traveling gradient bar.
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    int initialized;
    int tick_n;
    char clock_str[16];
} EffectsState;

static EffectsState g_fx;

static void effects_tick(EffectsState *e) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) {
        snprintf(e->clock_str, sizeof e->clock_str, "%02d:%02d:%02d",
                 lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        snprintf(e->clock_str, sizeof e->clock_str, "00:00:00");
    }
    e->tick_n++;
    e->initialized = 1;
}

/* Callback for gradient_border content. */
static void fx_border_content(FluxSB *sb, int inner_w, int inner_h, void *ctx) {
    int r;
    (void)ctx;
    for (r = 0; r < inner_h; r++) {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        if (r == inner_h / 2) {
            const char *txt = "  flux.h — terminal UI made beautiful";
            flux_fit(&l, txt, inner_w, NULL, FLUX_ALIGN_LEFT);
        } else {
            int i;
            for (i = 0; i < inner_w; i++) flux_sb_append(&l, " ");
        }
        flux_sb_append(sb, flux_sb_str(&l));
        flux_sb_append(sb, "\n");
    }
}

static void screen9_render(FluxSB *inner, int inner_w, EffectsState *e) {
    if (!e->initialized) effects_tick(e);

    demo_blank(inner, inner_w, 1);

    /* ── flux_digits clock ─────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_digits — large block-font clock (live)");
    flux_digits(inner, e->clock_str, FLUX_THEME_BRAND_PURPLE_FG, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── gradient text ─────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_gradient_text — per-cell fg gradient");
    flux_gradient_text(inner, "  rainbow text — cells smoothly interpolate",
                        (FluxRGB){180,130,255}, (FluxRGB){100,255,200}, inner_w);
    flux_gradient_text(inner, "  another sweep — purple → cyan → green",
                        (FluxRGB){255, 80,180}, (FluxRGB){ 80,200,255}, inner_w);
    flux_gradient_text(inner, "  warm — orange to yellow",
                        (FluxRGB){255,120, 40}, (FluxRGB){255,230, 80}, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── gradient bar (animated) ───────────────────────────────── */
    s5_section(inner, inner_w, "flux_gradient_bar — animated horizontal sweep");
    {
        float p = (float)(0.5 + 0.5 * sin(e->tick_n * 0.03));
        flux_gradient_bar(inner, p, inner_w,
                          (FluxRGB){180,130,255}, (FluxRGB){100,255,200});
    }
    {
        float p2 = (float)(0.5 + 0.5 * cos(e->tick_n * 0.025));
        flux_gradient_bar(inner, p2, inner_w,
                          (FluxRGB){255,100,150}, (FluxRGB){255,230, 80});
    }
    demo_blank(inner, inner_w, 1);

    /* ── glow text ─────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_glow_text — halo bg behind body");
    flux_glow_text(inner, "READY",   FLUX_THEME_OK_FG,    FLUX_THEME_OK_FG,    inner_w);
    flux_glow_text(inner, "WARNING", FLUX_THEME_WARN_FG,  FLUX_THEME_WARN_FG,  inner_w);
    flux_glow_text(inner, "ERROR",   FLUX_THEME_ERR_FG,   FLUX_THEME_ERR_FG,   inner_w);
    flux_glow_text(inner, "STREAM",  FLUX_THEME_BRAND_PURPLE_FG,
                                     FLUX_THEME_BRAND_PURPLE_FG, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── gradient border ───────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_gradient_border — perimeter gradient");
    {
        int box_w = inner_w - 4;
        if (box_w < 20) box_w = inner_w;
        /* Pad the multi-row box back to inner_w by left-padding 2 cells. */
        char tmp[8192]; FluxSB t;
        flux_sb_init(&t, tmp, sizeof tmp);
        flux_gradient_border(&t, box_w, 5,
                              (FluxRGB){180,130,255}, (FluxRGB){100,255,200},
                              fx_border_content, NULL);
        /* Walk lines, prefix 2 spaces, pad to inner_w. */
        const char *p = flux_sb_str(&t);
        const char *line_start = p;
        while (*p) {
            if (*p == '\n') {
                char one[2048]; FluxSB o;
                int n = (int)(p - line_start);
                if (n > (int)sizeof one - 1) n = sizeof one - 1;
                memcpy(one, line_start, n); one[n] = 0;
                {
                    char L[2400]; FluxSB l;
                    flux_sb_init(&l, L, sizeof L);
                    flux_sb_append(&l, "  ");
                    flux_sb_append(&l, one);
                    /* The box already produces box_w cells; pad remaining. */
                    {
                        int remaining = inner_w - 2 - box_w;
                        int i;
                        for (i = 0; i < remaining; i++) flux_sb_append(&l, " ");
                    }
                    flux_sb_append(&l, "\n");
                    flux_sb_append(inner, flux_sb_str(&l));
                    (void)o;
                }
                line_start = p + 1;
            }
            p++;
        }
    }
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * SCREEN 10: TOOLS / STATUS — B3 widgets
 * ════════════════════════════════════════════════════════════════════ */

static void screen10_render(FluxSB *inner, int inner_w, int spinner_idx) {
    demo_blank(inner, inner_w, 1);

    /* ── Header / footer pair ──────────────────────────────────── */
    s5_section(inner, inner_w, "flux_header / flux_footer");
    flux_header(inner, "Inbox", "12 unread", inner_w);
    {
        FluxKeyHint hints[3];
        hints[0] = (FluxKeyHint){ "↵",  "open" };
        hints[1] = (FluxKeyHint){ "d",  "delete" };
        hints[2] = (FluxKeyHint){ "^C", "quit" };
        flux_footer(inner, hints, 3, inner_w);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Alerts (info / success / warn / error) ────────────────── */
    s5_section(inner, inner_w, "flux_alert — 4 kinds");
    flux_alert(inner, FLUX_KIND_INFO,    "Indexing files",   "Scanning 1,240 files in src/", inner_w);
    flux_alert(inner, FLUX_KIND_SUCCESS, "Tests pass",       "25 passed, 0 failed in 1.8s",  inner_w);
    flux_alert(inner, FLUX_KIND_WARNING, "Disk almost full", "Free space below 10% on /",    inner_w);
    flux_alert(inner, FLUX_KIND_ERROR,   "Build failed",     "linker error in auth.o",       inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── status_msg row ────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_status_msg");
    flux_status_msg(inner, FLUX_KIND_INFO,    "Loading model weights…",  inner_w);
    flux_status_msg(inner, FLUX_KIND_SUCCESS, "Connection established",  inner_w);
    flux_status_msg(inner, FLUX_KIND_WARNING, "Token budget at 82%",      inner_w);
    flux_status_msg(inner, FLUX_KIND_ERROR,   "Could not reach API",      inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── Badges + tags ─────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_badge / flux_tag — inline pills");
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_badge(&l, "NEW",       FLUX_THEME_TEXT_INV_FG, FLUX_THEME_OK_FG);
        flux_sb_append(&l, "  ");
        flux_badge(&l, "BETA",      FLUX_THEME_TEXT_INV_FG, FLUX_THEME_WARN_FG);
        flux_sb_append(&l, "  ");
        flux_badge(&l, "DEPRECATED",FLUX_THEME_TEXT_INV_FG, FLUX_THEME_ERR_FG);
        flux_sb_append(&l, "    ");
        flux_tag(&l, "wip",   FLUX_THEME_WARN_FG);
        flux_sb_append(&l, " ");
        flux_tag(&l, "core",  FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, " ");
        flux_tag(&l, "draft", FLUX_THEME_TEXT_DIM_FG);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Avatars ───────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_avatar — colored initials");
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_avatar(&l, "AC", FLUX_THEME_BRAND_PURPLE_FG);
        flux_sb_append(&l, " Anthropic Claude        ");
        flux_avatar(&l, "OG", FLUX_THEME_OK_FG);
        flux_sb_append(&l, " OpenAI GPT        ");
        flux_avatar(&l, "GG", FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, " Google Gemini");
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Breadcrumb ────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_breadcrumb");
    {
        const char *parts[4] = { "home", "src", "auth", "middleware.ts" };
        flux_breadcrumb(inner, parts, 4, -1, inner_w,
                        FLUX_THEME_TEXT_DIM_FG,
                        FLUX_THEME_ACCENT_FG,
                        FLUX_THEME_TEXT_OFF_FG);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Link (OSC-8) ──────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_link — OSC-8 hyperlink (clickable in modern terminals)");
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  Documentation: ");
        flux_link(&l, "github.com/olealgoritme/flux.h",
                  "https://github.com/olealgoritme/flux.h", NULL);
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Loading row ───────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_loading — spinner + label");
    flux_loading(inner, "Compiling sources…", spinner_idx, inner_w);
    flux_loading(inner, "Indexing 1,240 files", spinner_idx + 3, inner_w);
    demo_blank(inner, inner_w, 1);

    /* ── kbd inline ────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_kbd — inline keycaps");
    {
        char L[1024]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  Press ");
        flux_kbd(&l, "Ctrl+C");
        flux_sb_append(&l, " to copy, ");
        flux_kbd(&l, "Ctrl+V");
        flux_sb_append(&l, " to paste, ");
        flux_kbd(&l, "Esc");
        flux_sb_append(&l, " to dismiss.");
        demo_padded(inner, inner_w, flux_sb_str(&l));
    }
    demo_blank(inner, inner_w, 1);

    /* ── Help panel ────────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_help_panel — categorised key bindings");
    {
        FluxHelpBinding b[7];
        b[0] = (FluxHelpBinding){ "Tab",   "next tab",       "Navigation" };
        b[1] = (FluxHelpBinding){ "1-9",   "jump to tab",    "Navigation" };
        b[2] = (FluxHelpBinding){ "↑/↓",   "scroll",         "Navigation" };
        b[3] = (FluxHelpBinding){ "PgUp",  "scroll page up", "Navigation" };
        b[4] = (FluxHelpBinding){ "←/→",   "approval",       "Action" };
        b[5] = (FluxHelpBinding){ "m",     "cycle bench mode","Action" };
        b[6] = (FluxHelpBinding){ "q",     "quit",           "App" };
        flux_help_panel(inner, b, 7, inner_w, "Key bindings",
                         FLUX_THEME_ACCENT_FG, FLUX_THEME_TEXT_FG,
                         FLUX_THEME_BRAND_PURPLE_FG);
    }
    demo_blank(inner, inner_w, 1);

    /* ── Placeholder ───────────────────────────────────────────── */
    s5_section(inner, inner_w, "flux_placeholder — empty-state");
    flux_placeholder(inner, "○", "No conversations yet",
                      "Press n to start a new chat", inner_w);
    demo_blank(inner, inner_w, 1);
}

/* ════════════════════════════════════════════════════════════════════
 * APP — multi-screen launcher
 * ════════════════════════════════════════════════════════════════════ */

typedef enum {
    SCR_AGENT = 0,
    SCR_BENCH,
    SCR_GALLERY,
    SCR_CHAT,
    SCR_AI,
    SCR_FORMS,
    SCR_CHARTS,
    SCR_MARKDOWN,
    SCR_EFFECTS,
    SCR_TOOLS,
    SCR_N
} Screen;
static const char *SCREEN_NAMES[SCR_N] = {
    "AGENT SESSION", "DIFF BENCHMARK", "AI GALLERY", "SCROLLABLE CHAT",
    "AI SHOWCASE", "FORMS", "CHARTS", "MARKDOWN / CODE",
    "EFFECTS", "TOOLS / STATUS"
};
static const char *SCREEN_LABELS[SCR_N] = {
    "Agent", "Diff", "Gallery", "Chat",
    "AI", "Forms", "Charts", "MD",
    "FX", "Tools"
};

/* (BENCH_MODE_NAMES defined earlier near the bench painters.) */

typedef struct {
    int        spinner_idx;
    int        selected;            /* approval choice */
    int        tick_total;
    FluxTabBar tabs;                /* owns active tab + hit boxes */
    FluxScroll scroll[SCR_N];       /* per-screen scroll state */
} App;

/* (scroll_window / scroll_indicator removed — use flux_scrollview_render
 * and flux_scroll_indicator from flux.h directly.) */

static FluxCmd app_init(FluxModel *m) { (void)m; return FLUX_TICK(33); /* ~30 Hz */ }

static FluxCmd app_update(FluxModel *m, FluxMsg msg) {
    App *a = (App *)m->state;
    Screen prev_screen = (Screen)a->tabs.active;

    if (msg.type == MSG_KEY) {
        /* Always allow Ctrl-C to quit. Bare `q` only quits when not
         * typing into a form field, so the user can still type the
         * letter into Search / TextArea / Masked. */
        if (flux_key_is(msg, "C-c"))
            return FLUX_CMD_QUIT;
        if (a->tabs.active != SCR_FORMS && flux_key_is(msg, "q"))
            return FLUX_CMD_QUIT;
        /* `0` jumps to the 10th tab (Tools/Status). The built-in tab bar
         * only handles 1-9. */
        if (a->tabs.active != SCR_FORMS && flux_key_is(msg, "0") &&
            SCR_N >= 10) {
            a->tabs.active = 9;
        }
        if (flux_key_is(msg, "left"))  { if (a->selected > 0) a->selected--; }
        if (flux_key_is(msg, "right")) { if (a->selected < 1) a->selected++; }
        if (flux_key_is(msg, "m") && a->tabs.active == SCR_BENCH)
            g_bench.mode = (g_bench.mode + 1) % BENCH_MODE_N;
    }

    /* On the Forms tab, route Tab/S-Tab and typing keys to the form first
     * so they don't cycle the global tabbar or trigger `q`-quit. Numeric
     * 1-9 tab jumps still flow through to the tabbar so the user can
     * always escape. */
    int forms_consumed_tab = 0;
    if (a->tabs.active == SCR_FORMS && msg.type == MSG_KEY) {
        form_update(&g_form, msg);
        if (flux_key_is(msg, "tab") || flux_key_is(msg, "S-tab") ||
            flux_key_is(msg, "btab"))
            forms_consumed_tab = 1;
    }

    /* Tab navigation (1-9 / Tab / click) — handled entirely by the widget,
     * unless the Forms tab already consumed Tab. */
    if (!forms_consumed_tab) flux_tabbar_update(&a->tabs, msg);

    /* Scroll for the ACTIVE screen — handled entirely by the widget. */
    flux_scroll_update(&a->scroll[a->tabs.active], msg);

    if (msg.type == MSG_TICK) {
        a->tick_total++;
        a->spinner_idx = (a->spinner_idx + 1) % 10;
        if (a->tabs.active == SCR_BENCH) {
            bench_init(&g_bench);
            screen2_tick(&g_bench);
        }
        /* Tick the animated showcase widgets every frame regardless of
         * which tab is visible — they amortise to O(1) and the user can
         * flip back to the AI tab and see continuity. */
        ai_tick(&g_ai);
        chart_tick(&g_chart);
        effects_tick(&g_fx);
        return FLUX_TICK(8);   /* 120 Hz tick — animations advance fast */
    }

    (void)prev_screen;
    return FLUX_CMD_NONE;
}

static void app_view(FluxModel *m, char *buf, int sz) {
    App *a = (App *)m->state;
    int W = flux_cols(), H = flux_rows();

    FluxSB out; flux_sb_init(&out, buf, sz);

    if (W < 40 || H < 18) {
        flux_sb_appendf(&out,
            "\n\n  flux.h showcase\n\n"
            "  terminal too small: %dx%d (need 40x18)\n"
            "  resize and the demo will redraw automatically.\n\n"
            "  press q to quit\n", W, H);
        return;
    }

    int inner_w  = demo_inner_w_for_terminal(W);
    int chrome_w = inner_w + 2;
    int left_pad = (W - chrome_w) / 2;
    if (left_pad < 0) left_pad = 0;
    int right_pad = W - left_pad - chrome_w;
    if (right_pad < 0) right_pad = 0;

    /* Layout sizing — flux_layout_viewport_h handles the reserve. */
    /* Chrome rows: 4 (top border, title bar, divider, bottom border).
     * Inner UI:    3 (tab strip, divider, scroll indicator). */
    int viewport_h = flux_layout_viewport_h(H,
                                            /*chrome_rows=*/7,
                                            /*margin_rows=*/4,
                                            /*min=*/6, /*max=*/28);
    Screen active = (Screen)a->tabs.active;

    /* Tab y in screen coords: top_pad + 3 chrome rows + 1 (1-based). We
     * compute top_pad up-front so we can pass screen y to the tabbar. */
    int chrome_rows_total = 4 + 1 + 1 + viewport_h + 1;  /* chrome + tab + div + viewport + scroll */
    int top_pad = (H - chrome_rows_total) / 2;
    if (top_pad < 1) top_pad = 1;
    int tab_screen_y       = top_pad + 3 + 1;
    int tab_screen_x_origin = left_pad + 2;   /* +1 chrome border, +1 1-based */

    /* inner buffer holds the post-scroll viewport content + tab strip +
     * indicator. Charts heatmap with truecolor escapes can swell this
     * even after slicing, so 256 KiB stays well clear of the wall. */
    static char ibuf[262144];
    FluxSB inner;
    flux_sb_init(&inner, ibuf, sizeof ibuf);

    /* Tab strip — owned widget, handles render + click hit-boxing. */
    flux_tabbar_render(&a->tabs, &inner, inner_w, tab_screen_x_origin, tab_screen_y);

    /* Divider line under tabs */
    {
        char L[2048]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_DIVIDER_FG);
        for (int i = 0; i < inner_w - 4; i++) flux_sb_append(&l, "\xe2\x94\x80");
        flux_sb_append(&l, FLUX_RESET);
        demo_padded(&inner, inner_w, flux_sb_str(&l));
    }

    /* Build full screen content, then run it through FluxScroll. */
    if (active == SCR_BENCH) {
        bench_init(&g_bench);
        /* Bench is a fixed-size live animation, not scrollable. */
        screen2_render(&inner, inner_w, &g_bench);
    } else {
        /* fbuf is dynamically heap-allocated since chart-heavy screens
         * (heatmap × 6×16 cells with truecolor escapes) can easily
         * exceed 64 KiB. 256 KiB is generous. */
        static char fbuf[262144];
        FluxSB full;
        flux_sb_init(&full, fbuf, sizeof fbuf);
        if (active == SCR_AGENT) {
            screen1_render(&full, inner_w, a->spinner_idx, a->selected);
        } else if (active == SCR_CHAT) {
            for (int i = 0; i < CHAT_N; i++) chat_emit_msg(&full, inner_w, &CHAT[i]);
        } else if (active == SCR_GALLERY) {
            screen3_render(&full, inner_w, a->spinner_idx, a->selected);
        } else if (active == SCR_AI) {
            screen5_render(&full, inner_w, &g_ai);
        } else if (active == SCR_FORMS) {
            screen6_render(&full, inner_w, &g_form);
        } else if (active == SCR_CHARTS) {
            screen7_render(&full, inner_w, &g_chart);
        } else if (active == SCR_MARKDOWN) {
            screen8_render(&full, inner_w);
        } else if (active == SCR_EFFECTS) {
            screen9_render(&full, inner_w, &g_fx);
        } else if (active == SCR_TOOLS) {
            screen10_render(&full, inner_w, a->spinner_idx);
        }
        flux_scrollview_render(&a->scroll[active], &inner,
                               flux_sb_str(&full), inner_w, viewport_h);
        flux_scroll_indicator(&a->scroll[active], &inner, inner_w);
    }

    /* Wrap in chrome */
    static char framebuf[262144];
    flux_window_chrome(framebuf, sizeof framebuf,
                       flux_sb_str(&inner), SCREEN_NAMES[active],
                       inner_w, NULL);

    int chrome_rows = 0;
    for (const char *p = framebuf; *p; p++) if (*p == '\n') chrome_rows++;
    /* Recompute top_pad against actual chrome size. */
    top_pad = (H - chrome_rows) / 2;
    if (top_pad < 1) top_pad = 1;
    /* Re-align tab y for hit-test (in case bench's chrome differs in height). */
    a->tabs.y = top_pad + 3 + 1;

    for (int i = 0; i < top_pad; i++) demo_desktop_row(&out, W);

    const char *p = framebuf;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int linelen = nl ? (int)(nl - p) : (int)strlen(p);
        if (left_pad > 0) {
            flux_sb_append(&out, FLUX_THEME_WINDOW_BG);
            for (int j = 0; j < left_pad; j++) flux_sb_append(&out, " ");
            flux_sb_append(&out, FLUX_RESET);
        }
        for (int j = 0; j < linelen; j++) flux_sb_appendf(&out, "%c", p[j]);
        if (right_pad > 0) {
            flux_sb_append(&out, FLUX_THEME_WINDOW_BG);
            for (int j = 0; j < right_pad; j++) flux_sb_append(&out, " ");
            flux_sb_append(&out, FLUX_RESET);
        }
        flux_sb_append(&out, "\n");
        if (!nl) break;
        p = nl + 1;
    }

    int bottom_pad = H - top_pad - chrome_rows;
    for (int i = 0; i < bottom_pad - 1; i++) demo_desktop_row(&out, W);
}

int main(void) {
    static App s;
    s.spinner_idx = 0;
    s.selected = 0;
    s.tick_total = 0;
    flux_tabbar_init(&s.tabs, SCREEN_LABELS, SCR_N);
    for (int i = 0; i < SCR_N; i++) flux_scroll_init(&s.scroll[i]);

    FluxModel model = {
        .state = &s,
        .init = app_init,
        .update = app_update,
        .view = app_view,
        .free = NULL,
    };
    /* fps + tick are user-configurable. We pick:
     *   .fps = 120  → render cap (max redraws per second)
     *   tick = 8 ms → 120 Hz logical updates (animations advance smoothly)
     * Both can be tuned by the dev — fps via FluxProgram.fps, tick via
     * the FLUX_TICK(ms) command returned from init/update. */
    FluxProgram prog = {
        .model = model,
        .alt_screen = 1,
        .mouse = 1,
        .fps = 120,
    };
    flux_run(&prog);
    return 0;
}
