#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
/*
 * flux.h -- Ink-style cell-based TUI framework for C99
 * Single-header, zero-dependency (libc + pthreads only).
 *
 * USAGE (define in exactly ONE translation unit):
 *   #define FLUX_IMPL
 *   #include "flux.h"
 *
 * Copyright (c) 2026 xuw (olealgoritme)
 * LICENSE: MIT
 */
#ifndef FLUX_H
#define FLUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ══════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═════════════════════════════════════════════════════════════════ */

#define FLUX_MAX_NODES     512
#define FLUX_MAX_CHILDREN  64
#define FLUX_STYLE_POOL    256
#define FLUX_MAX_LINES     512
#define FLUX_RENDER_BUF    131072
#define FLUX_PATCH_BUF     (FLUX_RENDER_BUF * 2)
#define FLUX_INPUT_MAX     4095
#define FLUX_PASTE_MAX     16384
#define FLUX_REGION_MAX    8

/* ══════════════════════════════════════════════════════════════════
 * ANSI HELPER MACROS
 * ═════════════════════════════════════════════════════════════════ */

#ifndef FLUX_RESET
#define FLUX_RESET     "\x1b[0m"
#endif
#define FLUX_BOLD      "\x1b[1m"
#define FLUX_DIM       "\x1b[2m"
#define FLUX_ITALIC    "\x1b[3m"
#define FLUX_UNDERLINE "\x1b[4m"
#define FLUX_STRIKE    "\x1b[9m"
#define FLUX_FG(r,g,b) "\x1b[38;2;" #r ";" #g ";" #b "m"
#define FLUX_BG(r,g,b) "\x1b[48;2;" #r ";" #g ";" #b "m"

/* ══════════════════════════════════════════════════════════════════
 * MESSAGES
 * ═════════════════════════════════════════════════════════════════ */

typedef enum {
    MSG_NONE = 0,
    MSG_KEY,
    MSG_WINSIZE,
    MSG_QUIT,
    MSG_TICK,
    MSG_CUSTOM,
    MSG_PASTE,
} FluxMsgType;

typedef struct {
    FluxMsgType type;
    union {
        struct { char key[16]; int rune; int ctrl; int alt; } key;
        struct { int cols, rows; }                            winsize;
        struct { int id; void *data; }                        custom;
        struct { char text[FLUX_PASTE_MAX]; int len; }        paste;
    } u;
} FluxMsg;

/* ══════════════════════════════════════════════════════════════════
 * COMMANDS
 * ═════════════════════════════════════════════════════════════════ */

typedef FluxMsg (*FluxCmdFn)(void *ctx);
typedef struct { FluxCmdFn fn; void *ctx; } FluxCmd;
#define FLUX_CMD_NONE ((FluxCmd){NULL,NULL})

/* ══════════════════════════════════════════════════════════════════
 * STYLE
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    int fg_r, fg_g, fg_b;    /* -1 = default/unset */
    int bg_r, bg_g, bg_b;    /* -1 = default/unset */
    uint8_t bold;
    uint8_t dim;
    uint8_t italic;
    uint8_t underline;
    uint8_t strikethrough;
    uint8_t inverse;
} FluxStyle;

#define FLUX_STYLE_NONE ((FluxStyle){-1,-1,-1,-1,-1,-1,0,0,0,0,0,0})

typedef struct {
    FluxStyle styles[FLUX_STYLE_POOL];
    int       count;
} FluxStylePool;

/* ══════════════════════════════════════════════════════════════════
 * CELL
 * ═════════════════════════════════════════════════════════════════ */

#define FLUX_CELL_CH_MAX 8

typedef struct {
    char    ch[FLUX_CELL_CH_MAX];
    int16_t style_id;
    int8_t  width;
} FluxCell;

/* ══════════════════════════════════════════════════════════════════
 * SCREEN BUFFER
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    FluxCell *cells;
    int       rows;
    int       cols;
    int       damage_x1;
    int       damage_y1;
    int       damage_x2;
    int       damage_y2;
} FluxScreen;

/* ══════════════════════════════════════════════════════════════════
 * NODE TYPES
 * ═════════════════════════════════════════════════════════════════ */

typedef enum {
    FLUX_NODE_BOX,
    FLUX_NODE_TEXT,
    FLUX_NODE_RAW,
} FluxNodeType;

typedef enum {
    FLUX_DIR_VERTICAL,
    FLUX_DIR_HORIZONTAL,
} FluxDirection;

typedef enum {
    FLUX_SIZE_AUTO,
    FLUX_SIZE_FIXED,
    FLUX_SIZE_FILL,
    FLUX_SIZE_PERCENT,
} FluxSizeMode;

typedef struct {
    FluxSizeMode mode;
    int          value;
} FluxSize;

/* ══════════════════════════════════════════════════════════════════
 * NODE
 * ═════════════════════════════════════════════════════════════════ */

typedef struct FluxNode FluxNode;
struct FluxNode {
    FluxNodeType type;
    int          id;

    FluxStyle    style;
    int          padding[4];

    FluxDirection direction;
    FluxSize     width;
    FluxSize     height;

    int          cx, cy;
    int          cw, ch;

    char        *text;
    int          text_len;
    int          wrap;

    FluxNode    *children[FLUX_MAX_CHILDREN];
    int          child_count;
    FluxNode    *parent;

    int          dirty;
    int          layout_dirty;

    int          prev_cx, prev_cy, prev_cw, prev_ch;
};

/* ══════════════════════════════════════════════════════════════════
 * NODE POOL
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    FluxNode  nodes[FLUX_MAX_NODES];
    int       count;
    int       next_id;
} FluxNodePool;

/* ══════════════════════════════════════════════════════════════════
 * DIFF PATCH
 * ═════════════════════════════════════════════════════════════════ */

typedef enum {
    FLUX_PATCH_MOVE,
    FLUX_PATCH_STYLE,
    FLUX_PATCH_WRITE,
    FLUX_PATCH_ERASE,
} FluxPatchType;

typedef struct {
    FluxPatchType type;
    union {
        struct { int x, y; }          move;
        struct { int style_id; }      style;
        struct { char text[256]; }    write;
        struct { int count; }         erase;
    } u;
} FluxPatch;

/* ══════════════════════════════════════════════════════════════════
 * STRING BUILDER
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    char *buf;
    int   len;
    int   cap;
} FluxSB;

/* ══════════════════════════════════════════════════════════════════
 * VIEWPORT REGIONS
 * ═════════════════════════════════════════════════════════════════ */

typedef enum {
    FLUX_REGION_FIXED,
    FLUX_REGION_FILL,
} FluxRegionType;

typedef void (*FluxRegionRenderFn)(FluxSB *sb, int width, int height, void *ctx);

typedef struct {
    FluxRegionType type;
    int height;
    FluxRegionRenderFn render;
    void *ctx;
} FluxRegion;

typedef struct {
    FluxRegion regions[FLUX_REGION_MAX];
    int count;
    int width;
    int total_height;
} FluxViewport;

/* ══════════════════════════════════════════════════════════════════
 * WIDGETS
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    const char **frames;
    int frame_count, current;
    const char *label;
} FluxSpinner;

typedef struct {
    char buf[FLUX_INPUT_MAX + 1];
    int  cursor, len;
    const char *placeholder;
    int  submitted;
} FluxInput;

typedef struct {
    const char **icons, **labels;
    int count, active;
} FluxTabs;

/* ── Border ─────────────────────────────────────────────────── */

typedef struct {
    const char *tl, *tr, *bl, *br;
    const char *h,  *v;
    const char *ml, *mr, *mt, *mb, *c;
} FluxBorder;

/* ── Confirm dialog ─────────────────────────────────────────── */

typedef struct {
    const char *title, *message;
    const char *yes_label, *no_label;
    int selected;
    int answered;
    int result;
} FluxConfirm;

/* ── List widget ────────────────────────────────────────────── */

typedef void (*FluxListItemFn)(FluxSB *sb, int index, int selected,
                               int width, void *ctx);
typedef struct {
    int cursor, scroll, count, visible;
    FluxListItemFn render_item;
} FluxList;

/* ── Table widget ───────────────────────────────────────────── */

typedef void (*FluxTableCellFn)(FluxSB *sb, int row, int col,
                                int width, void *ctx);
typedef struct {
    const char **headers;
    const int  *widths;
    int col_count, row_count, visible, scroll, follow;
    FluxTableCellFn render_cell;
} FluxTable;

/* ── Kanban widget ──────────────────────────────────────────── */

#define FLUX_KB_TITLE_MAX  40
#define FLUX_KB_DESC_MAX   80
#define FLUX_KB_MAX_CARDS  32
#define FLUX_KB_MAX_COLS   8
#define FLUX_KB_BROWSE     0
#define FLUX_KB_EDIT       1
#define FLUX_KB_F_TITLE    0
#define FLUX_KB_F_DESC     1
#define FLUX_KB_F_OK       2
#define FLUX_KB_F_CANCEL   3

typedef struct {
    char title[FLUX_KB_TITLE_MAX + 1];
    char desc[FLUX_KB_DESC_MAX + 1];
} FluxKbCard;

typedef struct {
    char name[32];
    FluxKbCard cards[FLUX_KB_MAX_CARDS];
    int count;
} FluxKbCol;

typedef struct {
    FluxKbCol cols[FLUX_KB_MAX_COLS];
    int ncols, cur_col, cur_row, col_width, visible;
    int scroll[FLUX_KB_MAX_COLS];
    int mode, edit_col, edit_row, edit_focus, grabbed;
    FluxInput edit_title, edit_desc;
    int dirty;
} FluxKanban;

/* ══════════════════════════════════════════════════════════════════
 * MODEL / PROGRAM
 * ═════════════════════════════════════════════════════════════════ */

typedef struct FluxModel FluxModel;
struct FluxModel {
    void  *state;
    FluxCmd (*init)  (FluxModel *m);
    FluxCmd (*update)(FluxModel *m, FluxMsg msg);
    void  (*view)  (FluxModel *m, char *buf, int bufsz);
    void  (*free)  (FluxModel *m);
};

typedef struct {
    FluxModel model;
    int alt_screen;
    int mouse;
    int fps;
} FluxProgram;

/* ══════════════════════════════════════════════════════════════════
 * RENDERER STATE (internal)
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    FluxScreen    front;
    FluxScreen    back;
    FluxStylePool styles;
    FluxNodePool  pool;
    FluxNode     *root;
    int           force;
    char          obuf[FLUX_PATCH_BUF];
    int           olen;
} FluxRenderer;

/* ══════════════════════════════════════════════════════════════════
 * EXTERN DECLARATIONS: Spinner frame arrays & border presets
 * ═════════════════════════════════════════════════════════════════ */

#ifdef __GNUC__
#define FLUX_UNUSED_ __attribute__((unused))
#else
#define FLUX_UNUSED_
#endif

extern FLUX_UNUSED_ const char *FLUX_SPINNER_DOT[];
extern FLUX_UNUSED_ const int   FLUX_SPINNER_DOT_N;
extern FLUX_UNUSED_ const char *FLUX_SPINNER_LINE[];
extern FLUX_UNUSED_ const int   FLUX_SPINNER_LINE_N;

extern const FluxBorder FLUX_BORDER_ROUNDED;
extern const FluxBorder FLUX_BORDER_SHARP;
extern const FluxBorder FLUX_BORDER_DOUBLE;
extern const FluxBorder FLUX_BORDER_THICK;
extern const FluxBorder FLUX_BORDER_NONE;

/* ══════════════════════════════════════════════════════════════════
 * TICK MACRO
 * ═════════════════════════════════════════════════════════════════ */

#define FLUX_TICK(ms)  flux_tick(ms)
#define FLUX_CMD_QUIT  flux_cmd_quit()

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Event loop + Terminal queries
 * ═════════════════════════════════════════════════════════════════ */

void    flux_run(FluxProgram *p);
int     flux_key_is(FluxMsg msg, const char *name);
FluxCmd flux_tick(int ms);
FluxCmd flux_cmd_quit(void);
FluxCmd flux_cmd_custom(int id, void *data);
int     flux_cols(void);
int     flux_rows(void);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: String builder
 * ═════════════════════════════════════════════════════════════════ */

void        flux_sb_init(FluxSB *sb, char *backing, int cap);
void        flux_sb_append(FluxSB *sb, const char *s);
void        flux_sb_appendf(FluxSB *sb, const char *fmt, ...);
void        flux_sb_nl(FluxSB *sb);
const char *flux_sb_str(FluxSB *sb);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Text utilities
 * ═════════════════════════════════════════════════════════════════ */

int   flux_strwidth(const char *s);
int   flux_count_lines(const char *s);
int   flux_count_text_lines(const char *s, int len);
int   flux_wrapped_height(const char *text, int text_len,
                                 int wrap_width);
void  flux_pad_lines(char *buf, int bufsz, int target_lines);
void  flux_pad(char *dst, int dstsz, const char *src, int width);
char *flux_fg(char *buf, int r, int g, int b);
char *flux_bg(char *buf, int r, int g, int b);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Layout helpers
 * ═════════════════════════════════════════════════════════════════ */

void flux_divider(FluxSB *sb, int width, const char *color);
void flux_hbox(FluxSB *sb, const char **panels, const int *widths,
                      int count, const char *gap);
void flux_box(char *out_buf, int out_sz, const char *content,
                     const FluxBorder *border, int inner_w,
                     const char *border_clr, const char *content_bg);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Node tree (new cell-based renderer)
 * ═════════════════════════════════════════════════════════════════ */

void      flux_pool_init(FluxNodePool *pool);
FluxNode *flux_node_alloc(FluxNodePool *pool, FluxNodeType type);
void      flux_node_free(FluxNodePool *pool, FluxNode *node);
void      flux_node_add_child(FluxNode *parent, FluxNode *child);
void      flux_node_remove_child(FluxNode *parent, FluxNode *child);
void      flux_node_set_text(FluxNode *node, const char *text);
void      flux_node_set_style(FluxNode *node, FluxStyle style);
void      flux_node_set_size(FluxNode *node, FluxSize w, FluxSize h);
void      flux_node_mark_dirty(FluxNode *node);
void      flux_node_mark_layout_dirty(FluxNode *node);
void      flux_node_clear_dirty(FluxNode *node);
int       flux_node_is_clean(const FluxNode *node);
FluxNode *flux_node_box(FluxNodePool *pool, FluxDirection dir);
FluxNode *flux_node_text(FluxNodePool *pool, const char *text,
                                FluxStyle style);
FluxNode *flux_node_raw(FluxNodePool *pool, const char *ansi_str);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Screen buffer
 * ═════════════════════════════════════════════════════════════════ */

int  flux_screen_init(FluxScreen *scr, int rows, int cols);
void flux_screen_free(FluxScreen *scr);
int  flux_screen_resize(FluxScreen *scr, int rows, int cols);
void flux_screen_clear(FluxScreen *scr);
void flux_screen_set_cell(FluxScreen *scr, int x, int y,
                                 const char *ch, int ch_len,
                                 int style_id, int width);
const FluxCell *flux_screen_get_cell(const FluxScreen *scr,
                                            int x, int y);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Style pool
 * ═════════════════════════════════════════════════════════════════ */

void flux_style_pool_init(FluxStylePool *pool);
int  flux_style_pool_intern(FluxStylePool *pool, const FluxStyle *s);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Layout engine
 * ═════════════════════════════════════════════════════════════════ */

void flux_layout_compute(FluxNode *node, int avail_w, int avail_h);
void flux_layout_resolve_absolute(FluxNode *root,
                                         int screen_w, int screen_h);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Renderer
 * ═════════════════════════════════════════════════════════════════ */

void flux_render_tree(FluxScreen *scr, FluxScreen *prev,
                             FluxStylePool *pool, FluxNode *root);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Diff engine
 * ═════════════════════════════════════════════════════════════════ */

int flux_diff_screens(const FluxScreen *prev, const FluxScreen *next,
                             const FluxStylePool *pool,
                             char *obuf, int obufsz);
int flux_diff_full(const FluxScreen *next, const FluxStylePool *pool,
                          char *obuf, int obufsz);
int flux_diff_has_changes(const FluxScreen *prev,
                                 const FluxScreen *next);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Widgets — Input
 * ═════════════════════════════════════════════════════════════════ */

void flux_input_init(FluxInput *inp, const char *placeholder);
void flux_input_clear(FluxInput *inp);
int  flux_input_update(FluxInput *inp, FluxMsg msg);
void flux_input_render(FluxInput *inp, FluxSB *sb, int width,
                              const char *color, const char *cursor_clr);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Widgets — Spinner
 * ═════════════════════════════════════════════════════════════════ */

void flux_spinner_init(FluxSpinner *s, const char **frames,
                              int nframes, const char *label);
void flux_spinner_tick(FluxSpinner *s);
void flux_spinner_render(FluxSpinner *s, FluxSB *sb,
                                const char *color);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Widgets — Tabs
 * ═════════════════════════════════════════════════════════════════ */

void flux_tabs_init(FluxTabs *t, const char **icons,
                           const char **labels, int count);
int  flux_tabs_update(FluxTabs *t, FluxMsg msg);
void flux_tabs_render(FluxTabs *t, FluxSB *sb,
                             const char *active_clr,
                             const char *dim_clr,
                             const char *hint);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Widgets — Confirm
 * ═════════════════════════════════════════════════════════════════ */

void flux_confirm_init(FluxConfirm *c, const char *title,
                              const char *msg, const char *yes_label,
                              const char *no_label);
int  flux_confirm_update(FluxConfirm *c, FluxMsg msg);
void flux_confirm_render(FluxConfirm *c, FluxSB *sb, int width,
                                const char *border_clr,
                                const char *yes_clr,
                                const char *no_clr);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Widgets — List
 * ═════════════════════════════════════════════════════════════════ */

void flux_list_init(FluxList *l, int count, int visible,
                           FluxListItemFn render);
int  flux_list_update(FluxList *l, FluxMsg msg);
void flux_list_render(FluxList *l, FluxSB *sb, int width,
                             void *ctx);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Widgets — Table
 * ═════════════════════════════════════════════════════════════════ */

void flux_table_init(FluxTable *t, const char **headers,
                            const int *widths, int cols, int visible,
                            FluxTableCellFn render);
void flux_table_set_rows(FluxTable *t, int rows);
int  flux_table_update(FluxTable *t, FluxMsg msg);
void flux_table_render(FluxTable *t, FluxSB *sb, void *ctx,
                               const char *header_clr,
                               const char *border_clr);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Viewport
 * ═════════════════════════════════════════════════════════════════ */

void flux_viewport_init(FluxViewport *vp, int width, int height);
void flux_viewport_add(FluxViewport *vp, FluxRegionType type,
                              int height, FluxRegionRenderFn render,
                              void *ctx);
void flux_viewport_render(FluxViewport *vp, char *buf, int bufsz);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Popup, Sparkline, Bar
 * ═════════════════════════════════════════════════════════════════ */

void flux_popup(FluxSB *sb, int area_w, int area_h,
                       const char *title, const char **items, int count,
                       int selected, const char *hint,
                       const char *border_clr, const char *select_bg,
                       const char *normal_bg, const char *accent_clr);
void flux_sparkline(FluxSB *sb, float *ring, int len, int head,
                           const char *color);
void flux_bar(FluxSB *sb, float value, int width,
                     const char *fill_clr, const char *empty_clr);

/* ══════════════════════════════════════════════════════════════════
 * PUBLIC API: Kanban
 * ═════════════════════════════════════════════════════════════════ */

void flux_kanban_init(FluxKanban *kb, int ncols,
                             const char **col_names, int col_width);
int  flux_kanban_add(FluxKanban *kb, int col,
                            const char *title, const char *desc);
int  flux_kanban_del(FluxKanban *kb, int col, int row);
int  flux_kanban_move(FluxKanban *kb, int col, int row,
                             int to_col);
int  flux_kanban_dirty(FluxKanban *kb);
int  flux_kanban_update(FluxKanban *kb, FluxMsg msg);
void flux_kanban_render(FluxKanban *kb, FluxSB *sb,
                               const char **col_colors,
                               const char *hint);

#ifdef __cplusplus
}
#endif
#endif /* FLUX_H */


/* ══════════════════════════════════════════════════════════════════
 * ══════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 * ══════════════════════════════════════════════════════════════════
 * ══════════════════════════════════════════════════════════════════ */
#if defined(FLUX_IMPL) && !defined(FLUX_IMPL_DONE)
#define FLUX_IMPL_DONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <stdint.h>

/* ══════════════════════════════════════════════════════════════════
 * SPINNER FRAME ARRAYS (definitions)
 * ═════════════════════════════════════════════════════════════════ */

FLUX_UNUSED_ const char *FLUX_SPINNER_DOT[] = {
    "\xe2\xa0\x8b", /* braille dot 1 */
    "\xe2\xa0\x99", /* braille dot 2 */
    "\xe2\xa0\xb9", /* braille dot 3 */
    "\xe2\xa0\xb8", /* braille dot 4 */
    "\xe2\xa0\xbc", /* braille dot 5 */
    "\xe2\xa0\xb4", /* braille dot 6 */
    "\xe2\xa0\xa6", /* braille dot 7 */
    "\xe2\xa0\xa7", /* braille dot 8 */
    "\xe2\xa0\x87", /* braille dot 9 */
    "\xe2\xa0\x8f"  /* braille dot 10 */
};
FLUX_UNUSED_ const int FLUX_SPINNER_DOT_N = 10;

FLUX_UNUSED_ const char *FLUX_SPINNER_LINE[] = {
    "\xe2\xa0\x82", /* braille line 1 */
    "\xe2\xa0\x92", /* braille line 2 */
    "\xe2\xa0\x90", /* braille line 3 */
    "\xe2\xa0\xb0", /* braille line 4 */
    "\xe2\xa0\xa0", /* braille line 5 */
    "\xe2\xa0\xa4", /* braille line 6 */
    "\xe2\xa0\x84", /* braille line 7 */
    "\xe2\xa0\x86"  /* braille line 8 */
};
FLUX_UNUSED_ const int FLUX_SPINNER_LINE_N = 8;

/* ══════════════════════════════════════════════════════════════════
 * BORDER PRESETS (definitions)
 * ═════════════════════════════════════════════════════════════════ */

const FluxBorder FLUX_BORDER_ROUNDED =
    {"╭","╮","╰","╯","─","│","├","┤","┬","┴","┼"};
const FluxBorder FLUX_BORDER_SHARP =
    {"┌","┐","└","┘","─","│","├","┤","┬","┴","┼"};
const FluxBorder FLUX_BORDER_DOUBLE =
    {"╔","╗","╚","╝","═","║","╠","╣","╦","╩","╬"};
const FluxBorder FLUX_BORDER_THICK =
    {"┏","┓","┗","┛","━","┃","┣","┫","┳","┻","╋"};
const FluxBorder FLUX_BORDER_NONE =
    {" "," "," "," "," "," "," "," "," "," "," "};


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 1: Screen + Style  (01_screen_style.c)              ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- Style Pool -- */

void flux_style_pool_init(FluxStylePool *pool) {
    memset(pool, 0, sizeof(*pool));
}

int flux_style_eq(const FluxStyle *a, const FluxStyle *b) {
    return a->fg_r == b->fg_r && a->fg_g == b->fg_g && a->fg_b == b->fg_b
        && a->bg_r == b->bg_r && a->bg_g == b->bg_g && a->bg_b == b->bg_b
        && a->bold == b->bold && a->dim == b->dim
        && a->italic == b->italic && a->underline == b->underline
        && a->strikethrough == b->strikethrough && a->inverse == b->inverse;
}

int flux_style_pool_intern(FluxStylePool *pool, const FluxStyle *s) {
    int i;
    for (i = 0; i < pool->count; i++) {
        if (flux_style_eq(&pool->styles[i], s)) {
            return i;
        }
    }
    if (pool->count >= FLUX_STYLE_POOL) {
        return -1;
    }
    pool->styles[pool->count] = *s;
    return pool->count++;
}

/* -- Style to ANSI -- */

int flux_style_to_ansi(const FluxStyle *s, const FluxStyle *prev,
                              char *buf, int bufsz) {
    int off = 0;
    int need_reset = 0;
    int have_attrs = 0;

    if (bufsz <= 0) {
        return 0;
    }
    buf[0] = '\0';

    if (prev == NULL) {
        need_reset = 1;
    } else {
        if ((prev->bold && !s->bold)
            || (prev->dim && !s->dim)
            || (prev->italic && !s->italic)
            || (prev->underline && !s->underline)
            || (prev->strikethrough && !s->strikethrough)
            || (prev->inverse && !s->inverse)) {
            need_reset = 1;
        }
        if ((prev->fg_r >= 0 && s->fg_r < 0)
            || (prev->bg_r >= 0 && s->bg_r < 0)) {
            need_reset = 1;
        }
    }

    off += snprintf(buf + off, bufsz - off, "\x1b[");
    if (off >= bufsz) {
        return bufsz - 1;
    }

    if (need_reset) {
        off += snprintf(buf + off, bufsz - off, "0");
        if (off >= bufsz) {
            return bufsz - 1;
        }
        have_attrs = 1;

        if (s->bold) {
            off += snprintf(buf + off, bufsz - off, ";1");
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->dim) {
            off += snprintf(buf + off, bufsz - off, ";2");
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->italic) {
            off += snprintf(buf + off, bufsz - off, ";3");
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->underline) {
            off += snprintf(buf + off, bufsz - off, ";4");
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->inverse) {
            off += snprintf(buf + off, bufsz - off, ";7");
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->strikethrough) {
            off += snprintf(buf + off, bufsz - off, ";9");
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->fg_r >= 0) {
            off += snprintf(buf + off, bufsz - off, ";38;2;%d;%d;%d",
                            s->fg_r, s->fg_g, s->fg_b);
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->bg_r >= 0) {
            off += snprintf(buf + off, bufsz - off, ";48;2;%d;%d;%d",
                            s->bg_r, s->bg_g, s->bg_b);
            if (off >= bufsz) { return bufsz - 1; }
        }
    } else {
        if (s->bold && !prev->bold) {
            off += snprintf(buf + off, bufsz - off, "%s1",
                            have_attrs ? ";" : "");
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->dim && !prev->dim) {
            off += snprintf(buf + off, bufsz - off, "%s2",
                            have_attrs ? ";" : "");
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->italic && !prev->italic) {
            off += snprintf(buf + off, bufsz - off, "%s3",
                            have_attrs ? ";" : "");
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->underline && !prev->underline) {
            off += snprintf(buf + off, bufsz - off, "%s4",
                            have_attrs ? ";" : "");
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->inverse && !prev->inverse) {
            off += snprintf(buf + off, bufsz - off, "%s7",
                            have_attrs ? ";" : "");
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->strikethrough && !prev->strikethrough) {
            off += snprintf(buf + off, bufsz - off, "%s9",
                            have_attrs ? ";" : "");
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->fg_r >= 0
            && (s->fg_r != prev->fg_r || s->fg_g != prev->fg_g
                || s->fg_b != prev->fg_b)) {
            off += snprintf(buf + off, bufsz - off, "%s38;2;%d;%d;%d",
                            have_attrs ? ";" : "",
                            s->fg_r, s->fg_g, s->fg_b);
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
        if (s->bg_r >= 0
            && (s->bg_r != prev->bg_r || s->bg_g != prev->bg_g
                || s->bg_b != prev->bg_b)) {
            off += snprintf(buf + off, bufsz - off, "%s48;2;%d;%d;%d",
                            have_attrs ? ";" : "",
                            s->bg_r, s->bg_g, s->bg_b);
            have_attrs = 1;
            if (off >= bufsz) { return bufsz - 1; }
        }
    }

    if (!have_attrs) {
        buf[0] = '\0';
        return 0;
    }

    off += snprintf(buf + off, bufsz - off, "m");
    if (off >= bufsz) {
        return bufsz - 1;
    }
    return off;
}

/* -- Cell comparison -- */

int flux_cell_eq(const FluxCell *a, const FluxCell *b) {
    return a->style_id == b->style_id
        && a->width == b->width
        && memcmp(a->ch, b->ch, FLUX_CELL_CH_MAX) == 0;
}

/* -- Screen buffer -- */

void flux_screen_clear(FluxScreen *scr) {
    int i;
    int total = scr->rows * scr->cols;
    for (i = 0; i < total; i++) {
        memset(scr->cells[i].ch, 0, FLUX_CELL_CH_MAX);
        scr->cells[i].ch[0] = ' ';
        scr->cells[i].style_id = -1;
        scr->cells[i].width = 1;
    }
    scr->damage_x1 = 0;
    scr->damage_y1 = 0;
    scr->damage_x2 = scr->cols;
    scr->damage_y2 = scr->rows;
}

int flux_screen_init(FluxScreen *scr, int rows, int cols) {
    scr->rows = rows;
    scr->cols = cols;
    scr->cells = (FluxCell *)malloc((size_t)rows * (size_t)cols * sizeof(FluxCell));
    if (!scr->cells) {
        scr->rows = 0;
        scr->cols = 0;
        return -1;
    }
    flux_screen_clear(scr);
    return 0;
}

void flux_screen_free(FluxScreen *scr) {
    free(scr->cells);
    scr->cells = NULL;
    scr->rows = 0;
    scr->cols = 0;
}

int flux_screen_resize(FluxScreen *scr, int rows, int cols) {
    FluxCell *new_cells;
    int copy_rows, copy_cols;
    int y;

    if (rows == scr->rows && cols == scr->cols) {
        return 0;
    }

    new_cells = (FluxCell *)malloc((size_t)rows * (size_t)cols * sizeof(FluxCell));
    if (!new_cells) {
        return -1;
    }

    {
        int i;
        int total = rows * cols;
        for (i = 0; i < total; i++) {
            memset(new_cells[i].ch, 0, FLUX_CELL_CH_MAX);
            new_cells[i].ch[0] = ' ';
            new_cells[i].style_id = -1;
            new_cells[i].width = 1;
        }
    }

    copy_rows = scr->rows < rows ? scr->rows : rows;
    copy_cols = scr->cols < cols ? scr->cols : cols;

    for (y = 0; y < copy_rows; y++) {
        memcpy(&new_cells[y * cols],
               &scr->cells[y * scr->cols],
               (size_t)copy_cols * sizeof(FluxCell));
    }

    free(scr->cells);
    scr->cells = new_cells;
    scr->rows = rows;
    scr->cols = cols;

    scr->damage_x1 = 0;
    scr->damage_y1 = 0;
    scr->damage_x2 = cols;
    scr->damage_y2 = rows;

    return 0;
}

void flux_screen_set_cell(FluxScreen *scr, int x, int y,
                                 const char *ch, int ch_len,
                                 int style_id, int width) {
    FluxCell *cell;
    int copy_len;

    if (x < 0 || x >= scr->cols || y < 0 || y >= scr->rows) {
        return;
    }

    cell = &scr->cells[y * scr->cols + x];

    copy_len = ch_len < FLUX_CELL_CH_MAX - 1 ? ch_len : FLUX_CELL_CH_MAX - 1;
    memset(cell->ch, 0, FLUX_CELL_CH_MAX);
    if (ch && copy_len > 0) {
        memcpy(cell->ch, ch, (size_t)copy_len);
    }

    cell->style_id = (int16_t)style_id;
    cell->width = (int8_t)width;

    if (scr->damage_x1 > scr->damage_x2) {
        scr->damage_x1 = x;
        scr->damage_y1 = y;
        scr->damage_x2 = x + 1;
        scr->damage_y2 = y + 1;
    } else {
        if (x < scr->damage_x1) { scr->damage_x1 = x; }
        if (y < scr->damage_y1) { scr->damage_y1 = y; }
        if (x + 1 > scr->damage_x2) { scr->damage_x2 = x + 1; }
        if (y + 1 > scr->damage_y2) { scr->damage_y2 = y + 1; }
    }
}

const FluxCell *flux_screen_get_cell(const FluxScreen *scr, int x, int y) {
    if (x < 0 || x >= scr->cols || y < 0 || y >= scr->rows) {
        return NULL;
    }
    return &scr->cells[y * scr->cols + x];
}

void flux_screen_damage_reset(FluxScreen *scr) {
    scr->damage_x1 = scr->cols;
    scr->damage_y1 = scr->rows;
    scr->damage_x2 = 0;
    scr->damage_y2 = 0;
}

void flux_screen_damage_mark(FluxScreen *scr,
                                    int x, int y, int w, int h) {
    int x2 = x + w;
    int y2 = y + h;

    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    if (x2 > scr->cols) { x2 = scr->cols; }
    if (y2 > scr->rows) { y2 = scr->rows; }

    if (x >= x2 || y >= y2) {
        return;
    }

    if (scr->damage_x1 > scr->damage_x2) {
        scr->damage_x1 = x;
        scr->damage_y1 = y;
        scr->damage_x2 = x2;
        scr->damage_y2 = y2;
    } else {
        if (x < scr->damage_x1) { scr->damage_x1 = x; }
        if (y < scr->damage_y1) { scr->damage_y1 = y; }
        if (x2 > scr->damage_x2) { scr->damage_x2 = x2; }
        if (y2 > scr->damage_y2) { scr->damage_y2 = y2; }
    }
}

void flux_screen_blit(FluxScreen *dst, const FluxScreen *src,
                             int sx, int sy, int dx, int dy, int w, int h) {
    int y;
    int src_x_start, src_y_start;
    int dst_x_start, dst_y_start;
    int copy_w, copy_h;

    src_x_start = sx < 0 ? 0 : sx;
    src_y_start = sy < 0 ? 0 : sy;

    dst_x_start = dx + (src_x_start - sx);
    dst_y_start = dy + (src_y_start - sy);

    copy_w = w - (src_x_start - sx);
    copy_h = h - (src_y_start - sy);

    if (src_x_start + copy_w > src->cols) {
        copy_w = src->cols - src_x_start;
    }
    if (src_y_start + copy_h > src->rows) {
        copy_h = src->rows - src_y_start;
    }

    if (dst_x_start < 0) {
        copy_w += dst_x_start;
        src_x_start -= dst_x_start;
        dst_x_start = 0;
    }
    if (dst_y_start < 0) {
        copy_h += dst_y_start;
        src_y_start -= dst_y_start;
        dst_y_start = 0;
    }
    if (dst_x_start + copy_w > dst->cols) {
        copy_w = dst->cols - dst_x_start;
    }
    if (dst_y_start + copy_h > dst->rows) {
        copy_h = dst->rows - dst_y_start;
    }

    if (copy_w <= 0 || copy_h <= 0) {
        return;
    }

    for (y = 0; y < copy_h; y++) {
        memcpy(&dst->cells[(dst_y_start + y) * dst->cols + dst_x_start],
               &src->cells[(src_y_start + y) * src->cols + src_x_start],
               (size_t)copy_w * sizeof(FluxCell));
    }

    flux_screen_damage_mark(dst, dst_x_start, dst_y_start, copy_w, copy_h);
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 2: Node tree  (02_node_tree.c)                      ║
 * ╚════════════════════════════════════════════════════════════════╝ */

void flux_pool_init(FluxNodePool *pool) {
    memset(pool, 0, sizeof(*pool));
    pool->next_id = 1;
}

FluxNode *flux_node_alloc(FluxNodePool *pool, FluxNodeType type) {
    if (!pool) return NULL;

    int i;
    for (i = 0; i < FLUX_MAX_NODES; i++) {
        if (pool->nodes[i].id == 0) {
            FluxNode *n = &pool->nodes[i];
            memset(n, 0, sizeof(*n));
            n->type = type;
            n->id = pool->next_id++;
            n->dirty = 1;
            n->layout_dirty = 1;
            n->style.fg_r = -1;
            n->style.fg_g = -1;
            n->style.fg_b = -1;
            n->style.bg_r = -1;
            n->style.bg_g = -1;
            n->style.bg_b = -1;
            pool->count++;
            return n;
        }
    }
    return NULL;
}

void flux_node_free(FluxNodePool *pool, FluxNode *node);
void flux_node_remove_child(FluxNode *parent, FluxNode *child);
void flux_node_mark_dirty(FluxNode *node);
void flux_node_mark_layout_dirty(FluxNode *node);

void flux_node_free(FluxNodePool *pool, FluxNode *node) {
    if (!pool || !node || node->id == 0) return;

    while (node->child_count > 0) {
        flux_node_free(pool, node->children[node->child_count - 1]);
    }

    if (node->parent) {
        flux_node_remove_child(node->parent, node);
    }

    if (node->text) {
        free(node->text);
        node->text = NULL;
    }

    node->id = 0;
    pool->count--;
}

void flux_node_add_child(FluxNode *parent, FluxNode *child) {
    if (!parent || !child) return;
    if (parent->child_count >= FLUX_MAX_CHILDREN) return;

    if (child->parent && child->parent != parent) {
        flux_node_remove_child(child->parent, child);
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    flux_node_mark_dirty(parent);
    flux_node_mark_layout_dirty(parent);
}

void flux_node_remove_child(FluxNode *parent, FluxNode *child) {
    if (!parent || !child) return;

    int i;
    for (i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            int j;
            for (j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children[parent->child_count - 1] = NULL;
            parent->child_count--;
            child->parent = NULL;
            flux_node_mark_dirty(parent);
            flux_node_mark_layout_dirty(parent);
            return;
        }
    }
}

void flux_node_set_text(FluxNode *node, const char *text) {
    if (!node) return;

    if (node->text) {
        free(node->text);
        node->text = NULL;
        node->text_len = 0;
    }

    if (text) {
        int len = (int)strlen(text);
        node->text = (char *)malloc(len + 1);
        if (node->text) {
            memcpy(node->text, text, len + 1);
            node->text_len = len;
        }
    }

    flux_node_mark_dirty(node);
}

void flux_node_set_style(FluxNode *node, FluxStyle style) {
    if (!node) return;
    node->style = style;
    flux_node_mark_dirty(node);
}

void flux_node_set_size(FluxNode *node, FluxSize w, FluxSize h) {
    if (!node) return;
    node->width = w;
    node->height = h;
    flux_node_mark_layout_dirty(node);
}

void flux_node_mark_dirty(FluxNode *node) {
    FluxNode *cur;
    if (!node) return;

    for (cur = node; cur != NULL; cur = cur->parent) {
        if (cur->dirty) break;
        cur->dirty = 1;
    }
}

void flux_node_mark_layout_dirty(FluxNode *node) {
    FluxNode *cur;
    if (!node) return;

    for (cur = node; cur != NULL; cur = cur->parent) {
        if (cur->layout_dirty) break;
        cur->layout_dirty = 1;
    }
}

void flux_node_clear_dirty(FluxNode *node) {
    int i;
    if (!node) return;

    node->dirty = 0;
    node->layout_dirty = 0;

    for (i = 0; i < node->child_count; i++) {
        flux_node_clear_dirty(node->children[i]);
    }
}

int flux_node_is_clean(const FluxNode *node) {
    int i;
    if (!node) return 1;
    if (node->dirty || node->layout_dirty) return 0;

    for (i = 0; i < node->child_count; i++) {
        if (!flux_node_is_clean(node->children[i])) return 0;
    }
    return 1;
}

FluxNode *flux_node_box(FluxNodePool *pool, FluxDirection dir) {
    FluxNode *n = flux_node_alloc(pool, FLUX_NODE_BOX);
    if (n) {
        n->direction = dir;
    }
    return n;
}

FluxNode *flux_node_text(FluxNodePool *pool, const char *text,
                                FluxStyle style) {
    FluxNode *n = flux_node_alloc(pool, FLUX_NODE_TEXT);
    if (n) {
        n->style = style;
        if (text) {
            int len = (int)strlen(text);
            n->text = (char *)malloc(len + 1);
            if (n->text) {
                memcpy(n->text, text, len + 1);
                n->text_len = len;
            }
        }
        n->wrap = 1;
    }
    return n;
}

FluxNode *flux_node_raw(FluxNodePool *pool, const char *ansi_str) {
    FluxNode *n = flux_node_alloc(pool, FLUX_NODE_RAW);
    if (n && ansi_str) {
        int len = (int)strlen(ansi_str);
        n->text = (char *)malloc(len + 1);
        if (n->text) {
            memcpy(n->text, ansi_str, len + 1);
            n->text_len = len;
        }
    }
    return n;
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 3: Text utilities + String builder  (07)            ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- Wide codepoint detection -- */

static int _flux_wt_is_wide_cp(unsigned int cp) {
    if (cp >= 0x1F300 && cp <= 0x1FBFF) return 1;
    if (cp >= 0x2600  && cp <= 0x27BF)  return 1;
    if (cp >= 0x2B50  && cp <= 0x2B55)  return 1;
    if (cp >= 0xFE00  && cp <= 0xFE0F)  return 0;
    if (cp >= 0x200D  && cp <= 0x200D)  return 0;
    if (cp >= 0x1100  && cp <= 0x115F)  return 1;
    if (cp >= 0x2E80  && cp <= 0x9FFF)  return 1;
    if (cp >= 0xAC00  && cp <= 0xD7AF)  return 1;
    if (cp >= 0xF900  && cp <= 0xFAFF)  return 1;
    if (cp >= 0xFE10  && cp <= 0xFE6F)  return 1;
    if (cp >= 0xFF01  && cp <= 0xFF60)  return 1;
    if (cp >= 0x20000 && cp <= 0x2FA1F) return 1;
    return 0;
}

/* -- flux_strwidth -- */

int flux_strwidth(const char *s) {
    int w = 0;

    if (!s) return 0;

    while (*s) {
        unsigned char c = (unsigned char)*s;

        if (c == '\x1b') {
            s++;
            if (*s == '[' || *s == '(') {
                s++;
                while (*s && !((*s >= 'A' && *s <= 'Z')
                            || (*s >= 'a' && *s <= 'z')
                            || *s == '~')) {
                    s++;
                }
                if (*s) s++;
            } else if (*s == ']') {
                s++;
                while (*s && *s != '\x07'
                       && !(*s == '\x1b' && *(s + 1) == '\\')) {
                    s++;
                }
                if (*s == '\x07') {
                    s++;
                } else if (*s == '\x1b') {
                    s += 2;
                }
            } else if (*s) {
                s++;
            }
            continue;
        }

        if (c == '\t') {
            w += 4;
            s++;
            continue;
        }

        if (c == '\n' || c == '\r') {
            s++;
            continue;
        }

        if ((c & 0xC0) == 0x80) {
            s++;
            continue;
        }

        if (c < 0x20) {
            s++;
            continue;
        }

        if (c >= 0xF0 && (s[1] & 0xC0) == 0x80) {
            unsigned int cp = ((c & 0x07) << 18)
                | (((unsigned char)s[1] & 0x3F) << 12)
                | (((unsigned char)s[2] & 0x3F) << 6)
                | ((unsigned char)s[3] & 0x3F);
            w += _flux_wt_is_wide_cp(cp) ? 2 : 1;
            s += 4;
        } else if (c >= 0xE0 && (s[1] & 0xC0) == 0x80) {
            unsigned int cp = ((c & 0x0F) << 12)
                | (((unsigned char)s[1] & 0x3F) << 6)
                | ((unsigned char)s[2] & 0x3F);
            w += _flux_wt_is_wide_cp(cp) ? 2 : 1;
            s += 3;
        } else if (c >= 0xC0) {
            w += 1;
            s += 2;
        } else {
            w += 1;
            s++;
        }
    }
    return w;
}

/* -- flux_count_text_lines -- */

int flux_count_text_lines(const char *s, int len) {
    int lines;
    int i;

    if (!s || len <= 0) return 0;

    lines = 1;
    for (i = 0; i < len; i++) {
        if (s[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

/* -- flux_wrapped_height -- */

int flux_wrapped_height(const char *text, int text_len,
                               int wrap_width) {
    int total_lines = 0;
    const char *p = text;
    const char *end = text + text_len;

    if (!text || text_len <= 0) return 0;
    if (wrap_width <= 0) wrap_width = 1;

    while (p < end) {
        const char *line_end = p;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }

        if (line_end == p) {
            total_lines++;
        } else {
            const char *lp = p;
            while (lp < line_end) {
                const char *scan = lp;
                int col = 0;
                const char *last_break = NULL;

                while (scan < line_end && col < wrap_width) {
                    unsigned char c = (unsigned char)*scan;

                    if (c == '\x1b') {
                        scan++;
                        if (scan < line_end
                            && (*scan == '[' || *scan == '(')) {
                            scan++;
                            while (scan < line_end
                                   && !((*scan >= 'A' && *scan <= 'Z')
                                     || (*scan >= 'a' && *scan <= 'z')
                                     || *scan == '~')) {
                                scan++;
                            }
                            if (scan < line_end) scan++;
                        } else if (scan < line_end && *scan == ']') {
                            scan++;
                            while (scan < line_end && *scan != '\x07'
                                   && !(*scan == '\x1b'
                                        && scan + 1 < line_end
                                        && *(scan + 1) == '\\')) {
                                scan++;
                            }
                            if (scan < line_end && *scan == '\x07') {
                                scan++;
                            } else if (scan < line_end
                                       && *scan == '\x1b') {
                                scan += 2;
                            }
                        } else if (scan < line_end) {
                            scan++;
                        }
                        continue;
                    }

                    if (c == '\t') {
                        if (col + 4 > wrap_width && col > 0) break;
                        col += 4;
                        scan++;
                        last_break = scan;
                        continue;
                    }

                    if ((c & 0xC0) == 0x80) {
                        scan++;
                        continue;
                    }

                    if (c < 0x20) {
                        scan++;
                        continue;
                    }

                    {
                        int char_w = 1;
                        int char_bytes = 1;
                        if (c >= 0xF0 && scan + 3 < line_end
                            && (scan[1] & 0xC0) == 0x80) {
                            unsigned int cp = ((c & 0x07) << 18)
                                | (((unsigned char)scan[1] & 0x3F) << 12)
                                | (((unsigned char)scan[2] & 0x3F) << 6)
                                | ((unsigned char)scan[3] & 0x3F);
                            char_w = _flux_wt_is_wide_cp(cp) ? 2 : 1;
                            char_bytes = 4;
                        } else if (c >= 0xE0 && scan + 2 < line_end
                                   && (scan[1] & 0xC0) == 0x80) {
                            unsigned int cp = ((c & 0x0F) << 12)
                                | (((unsigned char)scan[1] & 0x3F) << 6)
                                | ((unsigned char)scan[2] & 0x3F);
                            char_w = _flux_wt_is_wide_cp(cp) ? 2 : 1;
                            char_bytes = 3;
                        } else if (c >= 0xC0) {
                            char_bytes = 2;
                        }

                        if (col + char_w > wrap_width && col > 0) break;

                        if (c == ' ') {
                            last_break = scan + 1;
                        } else if (c == '-') {
                            last_break = scan + char_bytes;
                        }

                        col += char_w;
                        scan += char_bytes;
                    }
                }

                total_lines++;

                if (scan >= line_end) {
                    lp = line_end;
                } else if (last_break && last_break > lp) {
                    lp = last_break;
                } else {
                    lp = scan;
                }
            }
        }

        p = line_end;
        if (p < end && *p == '\n') {
            p++;
        }
    }

    return total_lines > 0 ? total_lines : 1;
}

/* -- FluxSB string builder -- */

void flux_sb_init(FluxSB *sb, char *backing, int cap) {
    sb->buf = backing;
    sb->len = 0;
    sb->cap = cap;
    if (cap > 0) {
        sb->buf[0] = '\0';
    }
}

void flux_sb_append(FluxSB *sb, const char *s) {
    int l;

    if (!s) return;

    l = (int)strlen(s);
    if (sb->len + l >= sb->cap - 1) {
        l = sb->cap - 1 - sb->len;
    }
    if (l <= 0) return;
    memcpy(sb->buf + sb->len, s, (size_t)l);
    sb->len += l;
    sb->buf[sb->len] = '\0';
}

void flux_sb_appendf(FluxSB *sb, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    flux_sb_append(sb, tmp);
}

void flux_sb_nl(FluxSB *sb) {
    flux_sb_append(sb, "\n");
}

const char *flux_sb_str(FluxSB *sb) {
    return sb->buf;
}

/* -- Layout helpers -- */

void flux_divider(FluxSB *sb, int width, const char *color) {
    int i;

    if (color) flux_sb_append(sb, color);
    for (i = 0; i < width; i++) {
        flux_sb_append(sb, "\xe2\x94\x80");
    }
    if (color) flux_sb_append(sb, FLUX_RESET);
}

void flux_pad(char *dst, int dstsz, const char *src, int width) {
    int w;
    int srcbytes;
    int copy;
    int pad;
    int p;

    if (!dst || dstsz <= 0) return;
    if (!src) src = "";

    w = flux_strwidth(src);
    srcbytes = (int)strlen(src);
    copy = srcbytes < dstsz - 1 ? srcbytes : dstsz - 1;
    memcpy(dst, src, (size_t)copy);
    dst += copy;
    dstsz -= copy;

    pad = width - w;
    if (pad > 0 && dstsz > 1) {
        p = pad < dstsz - 1 ? pad : dstsz - 1;
        memset(dst, ' ', (size_t)p);
        dst += p;
        dstsz -= p;
    }
    *dst = '\0';
}

void flux_hbox(FluxSB *sb, const char **panels, const int *widths,
                      int count, const char *gap) {
    #define _FLUX_HB_MAXLINES 64
    #define _FLUX_HB_MAXPANELS 8
    static char _hb_buf[_FLUX_HB_MAXPANELS][4096];
    char *_hb_lines[_FLUX_HB_MAXPANELS][_FLUX_HB_MAXLINES];
    int _hb_nlines[_FLUX_HB_MAXPANELS];
    int maxl = 0;
    int p_idx, i;

    if (!gap) gap = "  ";
    if (count > _FLUX_HB_MAXPANELS) count = _FLUX_HB_MAXPANELS;

    for (p_idx = 0; p_idx < count; p_idx++) {
        char *s;
        char *nl;

        strncpy(_hb_buf[p_idx], panels[p_idx],
                sizeof(_hb_buf[p_idx]) - 1);
        _hb_buf[p_idx][sizeof(_hb_buf[p_idx]) - 1] = '\0';
        _hb_nlines[p_idx] = 0;
        s = _hb_buf[p_idx];

        while (*s && _hb_nlines[p_idx] < _FLUX_HB_MAXLINES) {
            _hb_lines[p_idx][_hb_nlines[p_idx]++] = s;
            nl = strchr(s, '\n');
            if (!nl) break;
            *nl = '\0';
            s = nl + 1;
        }
        if (_hb_nlines[p_idx] > maxl) {
            maxl = _hb_nlines[p_idx];
        }
    }

    for (i = 0; i < maxl; i++) {
        for (p_idx = 0; p_idx < count; p_idx++) {
            char pad_buf[512];
            const char *line;

            line = (i < _hb_nlines[p_idx])
                 ? _hb_lines[p_idx][i] : "";
            flux_pad(pad_buf, (int)sizeof(pad_buf), line,
                     widths[p_idx]);
            flux_sb_append(sb, pad_buf);
            if (p_idx < count - 1) {
                flux_sb_append(sb, gap);
            }
        }
        flux_sb_nl(sb);
    }
    #undef _FLUX_HB_MAXLINES
    #undef _FLUX_HB_MAXPANELS
}

int flux_count_lines(const char *s) {
    int n = 0;

    if (!s || !*s) return 0;

    while (*s) {
        if (*s == '\n') n++;
        s++;
    }

    return n + 1;
}

void flux_pad_lines(char *buf, int bufsz, int target_lines) {
    int cur = 0;
    const char *s;
    int len;

    if (!buf || bufsz <= 0) return;

    s = buf;
    while (*s) {
        if (*s == '\n') cur++;
        s++;
    }
    len = (int)(s - buf);
    while (cur < target_lines && len < bufsz - 2) {
        buf[len++] = '\n';
        cur++;
    }
    buf[len] = '\0';
}

/* -- Box rendering -- */

static void _flux_wt_ap(char **o, int *rem, const char *s) {
    int l = (int)strlen(s);
    if (l > *rem) l = *rem;
    if (l <= 0) return;
    memcpy(*o, s, (size_t)l);
    *o += l;
    *rem -= l;
    **o = '\0';
}

void flux_box(char *out_buf, int out_sz, const char *content,
                     const FluxBorder *border, int inner_w,
                     const char *border_clr, const char *content_bg) {
    #define _FLUX_BOX_MAXLINES 256
    static char _box_tmp[32768];
    char *lptr[_FLUX_BOX_MAXLINES];
    int lwid[_FLUX_BOX_MAXLINES];
    int nl = 0;
    int max_w = 0;
    int iw;
    char *o;
    int rem;
    char *p;
    const char *R = FLUX_RESET;
    int i;

    if (!out_buf || out_sz <= 0) return;
    out_buf[0] = '\0';
    if (!content) content = "";

    strncpy(_box_tmp, content, sizeof(_box_tmp) - 1);
    _box_tmp[sizeof(_box_tmp) - 1] = '\0';

    p = _box_tmp;
    while (*p && nl < _FLUX_BOX_MAXLINES) {
        char *eol;
        lptr[nl] = p;
        eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        lwid[nl] = flux_strwidth(p);
        if (lwid[nl] > max_w) max_w = lwid[nl];
        nl++;
        if (!eol) break;
        p = eol + 1;
    }

    iw = inner_w > 0 ? inner_w : max_w;

    o = out_buf;
    rem = out_sz - 1;

    if (border_clr) _flux_wt_ap(&o, &rem, border_clr);
    _flux_wt_ap(&o, &rem, border->tl);
    for (i = 0; i < iw + 2; i++) {
        _flux_wt_ap(&o, &rem, border->h);
    }
    _flux_wt_ap(&o, &rem, border->tr);
    if (border_clr) _flux_wt_ap(&o, &rem, R);
    _flux_wt_ap(&o, &rem, "\n");

    for (i = 0; i < nl; i++) {
        int pad_val;
        if (border_clr) _flux_wt_ap(&o, &rem, border_clr);
        _flux_wt_ap(&o, &rem, border->v);
        if (border_clr) _flux_wt_ap(&o, &rem, R);
        _flux_wt_ap(&o, &rem, " ");

        if (content_bg) _flux_wt_ap(&o, &rem, content_bg);
        _flux_wt_ap(&o, &rem, lptr[i]);

        pad_val = iw - lwid[i];
        if (pad_val > 0) {
            char sp[512];
            if (pad_val > 511) pad_val = 511;
            memset(sp, ' ', (size_t)pad_val);
            sp[pad_val] = '\0';
            _flux_wt_ap(&o, &rem, sp);
        }
        if (content_bg) _flux_wt_ap(&o, &rem, R);

        _flux_wt_ap(&o, &rem, " ");
        if (border_clr) _flux_wt_ap(&o, &rem, border_clr);
        _flux_wt_ap(&o, &rem, border->v);
        if (border_clr) _flux_wt_ap(&o, &rem, R);
        _flux_wt_ap(&o, &rem, "\n");
    }

    if (border_clr) _flux_wt_ap(&o, &rem, border_clr);
    _flux_wt_ap(&o, &rem, border->bl);
    for (i = 0; i < iw + 2; i++) {
        _flux_wt_ap(&o, &rem, border->h);
    }
    _flux_wt_ap(&o, &rem, border->br);
    if (border_clr) _flux_wt_ap(&o, &rem, R);

    #undef _FLUX_BOX_MAXLINES
}

/* -- ANSI color helpers -- */

char *flux_fg(char *buf, int r, int g, int b) {
    snprintf(buf, 32, "\x1b[38;2;%d;%d;%dm", r, g, b);
    return buf;
}

char *flux_bg(char *buf, int r, int g, int b) {
    snprintf(buf, 32, "\x1b[48;2;%d;%d;%dm", r, g, b);
    return buf;
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 4: Layout engine  (03_layout.c)                     ║
 * ╚════════════════════════════════════════════════════════════════╝ */

void flux_layout_measure_text(FluxNode *node, int max_w) {
    if (!node) return;
    if (!node->text && node->text_len == 0) {
        node->cw = 0;
        node->ch = 0;
        return;
    }

    if (node->type == FLUX_NODE_RAW) {
        int max_line_w = 0;
        int lines = 1;
        const char *p = node->text;
        const char *line_start = p;
        const char *end = node->text + node->text_len;

        while (p < end) {
            if (*p == '\n') {
                char save = *p;
                *(char *)p = '\0';
                int w = flux_strwidth(line_start);
                *(char *)p = save;
                if (w > max_line_w) max_line_w = w;
                lines++;
                p++;
                line_start = p;
            } else {
                p++;
            }
        }
        if (line_start < end) {
            int w = flux_strwidth(line_start);
            if (w > max_line_w) max_line_w = w;
        }

        node->cw = max_line_w;
        if (max_w > 0 && node->cw > max_w) node->cw = max_w;
        node->ch = lines;
        return;
    }

    if (node->wrap && max_w > 0) {
        int natural_w = 0;
        const char *p = node->text;
        const char *end = node->text + node->text_len;
        const char *line_start = p;

        while (p < end) {
            if (*p == '\n') {
                char save = *p;
                *(char *)p = '\0';
                int w = flux_strwidth(line_start);
                *(char *)p = save;
                if (w > natural_w) natural_w = w;
                p++;
                line_start = p;
            } else {
                p++;
            }
        }
        if (line_start < end) {
            int w = flux_strwidth(line_start);
            if (w > natural_w) natural_w = w;
        }

        node->cw = (natural_w < max_w) ? natural_w : max_w;
        if (node->cw < 1) node->cw = 1;
        node->ch = flux_wrapped_height(node->text, node->text_len, node->cw);
    } else {
        int max_line_w = 0;
        const char *p = node->text;
        const char *end = node->text + node->text_len;
        const char *line_start = p;

        while (p < end) {
            if (*p == '\n') {
                char save = *p;
                *(char *)p = '\0';
                int w = flux_strwidth(line_start);
                *(char *)p = save;
                if (w > max_line_w) max_line_w = w;
                p++;
                line_start = p;
            } else {
                p++;
            }
        }
        if (line_start < end) {
            int w = flux_strwidth(line_start);
            if (w > max_line_w) max_line_w = w;
        }

        node->cw = max_line_w;
        if (max_w > 0 && node->cw > max_w) node->cw = max_w;
        node->ch = flux_count_text_lines(node->text, node->text_len);
    }

    if (node->ch < 1) node->ch = 1;
}

void flux_layout_compute(FluxNode *node, int avail_w, int avail_h) {
    if (!node) return;

    node->prev_cx = node->cx;
    node->prev_cy = node->cy;
    node->prev_cw = node->cw;
    node->prev_ch = node->ch;

    int pad_top    = node->padding[0];
    int pad_right  = node->padding[1];
    int pad_bottom = node->padding[2];
    int pad_left   = node->padding[3];
    int pad_h = pad_top + pad_bottom;
    int pad_w = pad_left + pad_right;

    int inner_w = avail_w - pad_w;
    int inner_h = avail_h - pad_h;
    if (inner_w < 0) inner_w = 0;
    if (inner_h < 0) inner_h = 0;

    if (node->type == FLUX_NODE_TEXT || node->type == FLUX_NODE_RAW) {
        flux_layout_measure_text(node, inner_w);

        int content_w = node->cw;
        int content_h = node->ch;

        switch (node->width.mode) {
        case FLUX_SIZE_FIXED:
            node->cw = node->width.value;
            break;
        case FLUX_SIZE_FILL:
            node->cw = avail_w;
            break;
        case FLUX_SIZE_PERCENT:
            node->cw = avail_w * node->width.value / 100;
            break;
        case FLUX_SIZE_AUTO:
        default:
            node->cw = content_w + pad_w;
            break;
        }

        switch (node->height.mode) {
        case FLUX_SIZE_FIXED:
            node->ch = node->height.value;
            break;
        case FLUX_SIZE_FILL:
            node->ch = avail_h;
            break;
        case FLUX_SIZE_PERCENT:
            node->ch = avail_h * node->height.value / 100;
            break;
        case FLUX_SIZE_AUTO:
        default:
            node->ch = content_h + pad_h;
            break;
        }

        if (node->cw > avail_w && avail_w > 0) node->cw = avail_w;
        if (node->ch > avail_h && avail_h > 0) node->ch = avail_h;
        if (node->cw < 0) node->cw = 0;
        if (node->ch < 0) node->ch = 0;

        node->layout_dirty = 0;
        return;
    }

    /* BOX node */
    int outer_w, outer_h;
    int auto_w = (node->width.mode == FLUX_SIZE_AUTO);
    int auto_h = (node->height.mode == FLUX_SIZE_AUTO);

    switch (node->width.mode) {
    case FLUX_SIZE_FIXED:   outer_w = node->width.value; break;
    case FLUX_SIZE_FILL:    outer_w = avail_w;           break;
    case FLUX_SIZE_PERCENT: outer_w = avail_w * node->width.value / 100; break;
    case FLUX_SIZE_AUTO:
    default:                outer_w = avail_w;           break;
    }

    switch (node->height.mode) {
    case FLUX_SIZE_FIXED:   outer_h = node->height.value; break;
    case FLUX_SIZE_FILL:    outer_h = avail_h;            break;
    case FLUX_SIZE_PERCENT: outer_h = avail_h * node->height.value / 100; break;
    case FLUX_SIZE_AUTO:
    default:                outer_h = avail_h;            break;
    }

    if (outer_w > avail_w && avail_w > 0) outer_w = avail_w;
    if (outer_h > avail_h && avail_h > 0) outer_h = avail_h;

    int content_area_w = outer_w - pad_w;
    int content_area_h = outer_h - pad_h;
    if (content_area_w < 0) content_area_w = 0;
    if (content_area_h < 0) content_area_h = 0;

    if (node->child_count == 0) {
        node->cw = auto_w ? pad_w : outer_w;
        node->ch = auto_h ? pad_h : outer_h;
        node->layout_dirty = 0;
        return;
    }

    if (node->direction == FLUX_DIR_VERTICAL) {
        int used_h = 0;
        int fill_count = 0;

        int i;
        for (i = 0; i < node->child_count; i++) {
            FluxNode *child = node->children[i];
            if (!child) continue;

            switch (child->height.mode) {
            case FLUX_SIZE_FIXED:
                used_h += child->height.value;
                break;
            case FLUX_SIZE_PERCENT:
                used_h += content_area_h * child->height.value / 100;
                break;
            case FLUX_SIZE_FILL:
                fill_count++;
                break;
            case FLUX_SIZE_AUTO:
            default:
                flux_layout_compute(child, content_area_w, content_area_h);
                used_h += child->ch;
                break;
            }
        }

        int remaining = content_area_h - used_h;
        if (remaining < 0) remaining = 0;
        int fill_each = (fill_count > 0) ? remaining / fill_count : 0;
        int fill_extra = (fill_count > 0) ? remaining % fill_count : 0;

        int y_offset = pad_top;
        int max_child_w = 0;

        for (i = 0; i < node->child_count; i++) {
            FluxNode *child = node->children[i];
            if (!child) continue;

            int child_avail_h;

            switch (child->height.mode) {
            case FLUX_SIZE_FIXED:
                child_avail_h = child->height.value;
                break;
            case FLUX_SIZE_PERCENT:
                child_avail_h = content_area_h * child->height.value / 100;
                break;
            case FLUX_SIZE_FILL:
                child_avail_h = fill_each;
                if (fill_extra > 0) {
                    child_avail_h++;
                    fill_extra--;
                }
                break;
            case FLUX_SIZE_AUTO:
            default:
                child_avail_h = child->ch;
                break;
            }

            if (child->height.mode != FLUX_SIZE_AUTO) {
                flux_layout_compute(child, content_area_w, child_avail_h);
            }

            child->cx = pad_left;
            child->cy = y_offset;
            y_offset += child->ch;

            if (child->cw > max_child_w) max_child_w = child->cw;
        }

        int total_content_h = y_offset - pad_top;
        node->cw = auto_w ? (max_child_w + pad_w) : outer_w;
        node->ch = auto_h ? (total_content_h + pad_h) : outer_h;

    } else {
        int used_w = 0;
        int fill_count = 0;

        int i;
        for (i = 0; i < node->child_count; i++) {
            FluxNode *child = node->children[i];
            if (!child) continue;

            switch (child->width.mode) {
            case FLUX_SIZE_FIXED:
                used_w += child->width.value;
                break;
            case FLUX_SIZE_PERCENT:
                used_w += content_area_w * child->width.value / 100;
                break;
            case FLUX_SIZE_FILL:
                fill_count++;
                break;
            case FLUX_SIZE_AUTO:
            default:
                flux_layout_compute(child, content_area_w, content_area_h);
                used_w += child->cw;
                break;
            }
        }

        int remaining = content_area_w - used_w;
        if (remaining < 0) remaining = 0;
        int fill_each = (fill_count > 0) ? remaining / fill_count : 0;
        int fill_extra = (fill_count > 0) ? remaining % fill_count : 0;

        int x_offset = pad_left;
        int max_child_h = 0;

        for (i = 0; i < node->child_count; i++) {
            FluxNode *child = node->children[i];
            if (!child) continue;

            int child_avail_w;

            switch (child->width.mode) {
            case FLUX_SIZE_FIXED:
                child_avail_w = child->width.value;
                break;
            case FLUX_SIZE_PERCENT:
                child_avail_w = content_area_w * child->width.value / 100;
                break;
            case FLUX_SIZE_FILL:
                child_avail_w = fill_each;
                if (fill_extra > 0) {
                    child_avail_w++;
                    fill_extra--;
                }
                break;
            case FLUX_SIZE_AUTO:
            default:
                child_avail_w = child->cw;
                break;
            }

            if (child->width.mode != FLUX_SIZE_AUTO) {
                flux_layout_compute(child, child_avail_w, content_area_h);
            }

            child->cx = x_offset;
            child->cy = pad_top;
            x_offset += child->cw;

            if (child->ch > max_child_h) max_child_h = child->ch;
        }

        int total_content_w = x_offset - pad_left;
        node->cw = auto_w ? (total_content_w + pad_w) : outer_w;
        node->ch = auto_h ? (max_child_h + pad_h) : outer_h;
    }

    if (node->cw > avail_w && avail_w > 0) node->cw = avail_w;
    if (node->ch > avail_h && avail_h > 0) node->ch = avail_h;
    if (node->cw < 0) node->cw = 0;
    if (node->ch < 0) node->ch = 0;

    node->layout_dirty = 0;
}

void flux_layout_to_absolute(FluxNode *node) {
    int i;
    if (!node) return;

    for (i = 0; i < node->child_count; i++) {
        FluxNode *child = node->children[i];
        if (!child) continue;

        child->cx += node->cx;
        child->cy += node->cy;

        flux_layout_to_absolute(child);
    }
}

void flux_layout_resolve_absolute(FluxNode *root, int screen_w, int screen_h) {
    if (!root) return;

    flux_layout_compute(root, screen_w, screen_h);

    root->cx = 0;
    root->cy = 0;

    flux_layout_to_absolute(root);
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 5: Node renderer  (04_render_node.c)                ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- UTF-8 helpers -- */

int flux_render__utf8_len(const char *p) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

int flux_render__char_width(const char *p, int byte_len) {
    unsigned int cp = 0;
    unsigned char c = (unsigned char)p[0];

    if (byte_len == 1) return 1;
    if (byte_len == 2) {
        cp = (c & 0x1F) << 6;
        cp |= ((unsigned char)p[1] & 0x3F);
    } else if (byte_len == 3) {
        cp = (c & 0x0F) << 12;
        cp |= ((unsigned char)p[1] & 0x3F) << 6;
        cp |= ((unsigned char)p[2] & 0x3F);
    } else if (byte_len == 4) {
        cp = (c & 0x07) << 18;
        cp |= ((unsigned char)p[1] & 0x3F) << 12;
        cp |= ((unsigned char)p[2] & 0x3F) << 6;
        cp |= ((unsigned char)p[3] & 0x3F);
    }

    if (cp >= 0x4E00 && cp <= 0x9FFF) return 2;
    if (cp >= 0x3400 && cp <= 0x4DBF) return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;
    if (cp >= 0xFF01 && cp <= 0xFF60) return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;
    if (cp >= 0xAC00 && cp <= 0xD7AF) return 2;
    if (cp >= 0x20000 && cp <= 0x2FA1F) return 2;

    return 1;
}

int flux_render__skip_ansi(const char *p, int remaining) {
    int i;
    if (remaining < 2) return 1;
    if (p[1] != '[') return 1;
    for (i = 2; i < remaining; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c >= 0x40 && c <= 0x7E) {
            return i + 1;
        }
    }
    return remaining;
}

/* -- ANSI SGR parser for RAW mode -- */

int flux_render__parse_sgr(const char *p, int remaining, FluxStyle *s) {
    int params[16];
    int param_count = 0;
    int cur = 0;
    int has_cur = 0;
    int i, j;

    for (i = 0; i < remaining; i++) {
        char c = p[i];
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (c - '0');
            has_cur = 1;
        } else if (c == ';') {
            if (has_cur && param_count < 16) {
                params[param_count++] = cur;
            } else if (!has_cur && param_count < 16) {
                params[param_count++] = 0;
            }
            cur = 0;
            has_cur = 0;
        } else if (c == 'm') {
            if (has_cur && param_count < 16) {
                params[param_count++] = cur;
            } else if (param_count == 0) {
                params[param_count++] = 0;
            }
            break;
        } else {
            return i + 1;
        }
    }

    for (j = 0; j < param_count; j++) {
        int code = params[j];
        if (code == 0) {
            s->fg_r = s->fg_g = s->fg_b = -1;
            s->bg_r = s->bg_g = s->bg_b = -1;
            s->bold = s->dim = s->italic = 0;
            s->underline = s->strikethrough = s->inverse = 0;
        } else if (code == 1) {
            s->bold = 1;
        } else if (code == 2) {
            s->dim = 1;
        } else if (code == 3) {
            s->italic = 1;
        } else if (code == 4) {
            s->underline = 1;
        } else if (code == 7) {
            s->inverse = 1;
        } else if (code == 9) {
            s->strikethrough = 1;
        } else if (code == 22) {
            s->bold = 0; s->dim = 0;
        } else if (code == 23) {
            s->italic = 0;
        } else if (code == 24) {
            s->underline = 0;
        } else if (code == 27) {
            s->inverse = 0;
        } else if (code == 29) {
            s->strikethrough = 0;
        } else if (code >= 30 && code <= 37) {
            static const int basic[8][3] = {
                {0,0,0}, {170,0,0}, {0,170,0}, {170,85,0},
                {0,0,170}, {170,0,170}, {0,170,170}, {170,170,170}
            };
            int idx = code - 30;
            s->fg_r = basic[idx][0];
            s->fg_g = basic[idx][1];
            s->fg_b = basic[idx][2];
        } else if (code == 38) {
            if (j + 1 < param_count && params[j + 1] == 2
                && j + 4 < param_count) {
                s->fg_r = params[j + 2];
                s->fg_g = params[j + 3];
                s->fg_b = params[j + 4];
                j += 4;
            } else if (j + 1 < param_count && params[j + 1] == 5
                       && j + 2 < param_count) {
                s->fg_r = params[j + 2];
                s->fg_g = -2;
                s->fg_b = -2;
                j += 2;
            }
        } else if (code == 39) {
            s->fg_r = s->fg_g = s->fg_b = -1;
        } else if (code >= 40 && code <= 47) {
            static const int basic[8][3] = {
                {0,0,0}, {170,0,0}, {0,170,0}, {170,85,0},
                {0,0,170}, {170,0,170}, {0,170,170}, {170,170,170}
            };
            int idx = code - 40;
            s->bg_r = basic[idx][0];
            s->bg_g = basic[idx][1];
            s->bg_b = basic[idx][2];
        } else if (code == 48) {
            if (j + 1 < param_count && params[j + 1] == 2
                && j + 4 < param_count) {
                s->bg_r = params[j + 2];
                s->bg_g = params[j + 3];
                s->bg_b = params[j + 4];
                j += 4;
            } else if (j + 1 < param_count && params[j + 1] == 5
                       && j + 2 < param_count) {
                s->bg_r = params[j + 2];
                s->bg_g = -2;
                s->bg_b = -2;
                j += 2;
            }
        } else if (code == 49) {
            s->bg_r = s->bg_g = s->bg_b = -1;
        } else if (code >= 90 && code <= 97) {
            static const int bright[8][3] = {
                {85,85,85}, {255,85,85}, {85,255,85}, {255,255,85},
                {85,85,255}, {255,85,255}, {85,255,255}, {255,255,255}
            };
            int idx = code - 90;
            s->fg_r = bright[idx][0];
            s->fg_g = bright[idx][1];
            s->fg_b = bright[idx][2];
        } else if (code >= 100 && code <= 107) {
            static const int bright[8][3] = {
                {85,85,85}, {255,85,85}, {85,255,85}, {255,255,85},
                {85,85,255}, {255,85,255}, {85,255,255}, {255,255,255}
            };
            int idx = code - 100;
            s->bg_r = bright[idx][0];
            s->bg_g = bright[idx][1];
            s->bg_b = bright[idx][2];
        }
    }

    return i + 1;
}

/* -- Fill helper -- */

void flux_render__fill_rect(FluxScreen *scr, int x, int y,
                                   int w, int h, int style_id) {
    int row, col;
    for (row = y; row < y + h; row++) {
        for (col = x; col < x + w; col++) {
            flux_screen_set_cell(scr, col, row, " ", 1, style_id, 1);
        }
    }
}

/* -- TEXT node renderer -- */

void flux_render_text_to_screen(FluxScreen *scr, FluxStylePool *pool,
                                       FluxNode *node) {
    int style_id;
    int bx, by, bw, bh;
    int px, py;
    int inner_x, inner_y;
    int inner_w, inner_h;
    const char *p;
    int remaining;

    if (!scr || !pool || !node) return;

    style_id = flux_style_pool_intern(pool, &node->style);

    bx = node->cx;
    by = node->cy;
    bw = node->cw;
    bh = node->ch;

    inner_x = bx + node->padding[3];
    inner_y = by + node->padding[0];
    inner_w = bw - node->padding[3] - node->padding[1];
    inner_h = bh - node->padding[0] - node->padding[2];

    if (inner_w <= 0 || inner_h <= 0) {
        flux_render__fill_rect(scr, bx, by, bw, bh, style_id);
        return;
    }

    flux_render__fill_rect(scr, bx, by, bw, bh, style_id);

    if (!node->text || node->text_len == 0) return;

    px = inner_x;
    py = inner_y;
    p = node->text;
    remaining = node->text_len;

    while (remaining > 0 && (py - inner_y) < inner_h) {
        if (*p == '\n') {
            py++;
            px = inner_x;
            p++;
            remaining--;
            continue;
        }

        if (*p == '\x1b') {
            int skip = flux_render__skip_ansi(p, remaining);
            p += skip;
            remaining -= skip;
            continue;
        }

        if (px >= inner_x + inner_w) {
            py++;
            px = inner_x;
            if ((py - inner_y) >= inner_h) break;
        }

        if (node->wrap && px > inner_x) {
            int word_w = 0;
            const char *wp = p;
            int wr = remaining;
            while (wr > 0 && *wp != ' ' && *wp != '-' && *wp != '\n'
                   && *wp != '\x1b') {
                int blen = flux_render__utf8_len(wp);
                if (blen > wr) break;
                word_w += flux_render__char_width(wp, blen);
                wp += blen;
                wr -= blen;
                if (wr > 0 && *wp == '-') {
                    int hlen = flux_render__utf8_len(wp);
                    word_w += flux_render__char_width(wp, hlen);
                    break;
                }
            }
            if (px + word_w > inner_x + inner_w && word_w <= inner_w) {
                py++;
                px = inner_x;
                if ((py - inner_y) >= inner_h) break;
            }
        }

        {
            int blen = flux_render__utf8_len(p);
            int cw;
            if (blen > remaining) blen = remaining;
            cw = flux_render__char_width(p, blen);

            if (px == inner_x && *p == ' ' && node->wrap) {
                if (p > node->text) {
                    const char *prev = p - 1;
                    if (*prev != '\n') {
                        p += blen;
                        remaining -= blen;
                        continue;
                    }
                }
            }

            if (px + cw > inner_x + inner_w) {
                if (node->wrap) {
                    py++;
                    px = inner_x;
                    if ((py - inner_y) >= inner_h) break;
                    continue;
                } else {
                    p += blen;
                    remaining -= blen;
                    continue;
                }
            }

            flux_screen_set_cell(scr, px, py, p, blen, style_id, cw);

            if (cw == 2) {
                flux_screen_set_cell(scr, px + 1, py, "", 0, style_id, 0);
            }

            px += cw;
            p += blen;
            remaining -= blen;
        }
    }
}

/* -- RAW node renderer -- */

void flux_render_raw_to_screen(FluxScreen *scr, FluxStylePool *pool,
                                      FluxNode *node) {
    FluxStyle cur_style;
    int style_id;
    int bx, by, bw, bh;
    int px, py;
    const char *p;
    int remaining;

    if (!scr || !pool || !node) return;
    if (!node->text || node->text_len == 0) return;

    bx = node->cx;
    by = node->cy;
    bw = node->cw;
    bh = node->ch;

    cur_style = node->style;
    style_id = flux_style_pool_intern(pool, &cur_style);

    flux_render__fill_rect(scr, bx, by, bw, bh, style_id);

    px = bx;
    py = by;
    p = node->text;
    remaining = node->text_len;

    while (remaining > 0 && (py - by) < bh) {
        if (*p == '\x1b' && remaining >= 2 && p[1] == '[') {
            int seq_len;
            const char *scan = p + 2;
            int scan_rem = remaining - 2;
            int is_sgr = 0;

            while (scan_rem > 0) {
                unsigned char c = (unsigned char)*scan;
                if (c >= 0x40 && c <= 0x7E) {
                    is_sgr = (c == 'm');
                    break;
                }
                scan++;
                scan_rem--;
            }

            if (is_sgr) {
                int consumed = flux_render__parse_sgr(p + 2, remaining - 2,
                                                      &cur_style);
                seq_len = 2 + consumed;
                style_id = flux_style_pool_intern(pool, &cur_style);
            } else {
                seq_len = flux_render__skip_ansi(p, remaining);
            }

            p += seq_len;
            remaining -= seq_len;
            continue;
        }

        if (*p == '\x1b') {
            p++;
            remaining--;
            continue;
        }

        if (*p == '\n') {
            py++;
            px = bx;
            p++;
            remaining--;
            continue;
        }

        if (*p == '\r') {
            px = bx;
            p++;
            remaining--;
            continue;
        }

        if (*p == '\t') {
            int tab_stop = ((px - bx + 8) & ~7) + bx;
            while (px < tab_stop && (px - bx) < bw) {
                flux_screen_set_cell(scr, px, py, " ", 1, style_id, 1);
                px++;
            }
            p++;
            remaining--;
            continue;
        }

        if (px >= bx + bw) {
            while (remaining > 0 && *p != '\n') {
                if (*p == '\x1b') {
                    int skip = flux_render__skip_ansi(p, remaining);
                    if (remaining >= 2 && p[1] == '[') {
                        const char *scan2 = p + 2;
                        int sr = remaining - 2;
                        while (sr > 0) {
                            unsigned char c = (unsigned char)*scan2;
                            if (c >= 0x40 && c <= 0x7E) {
                                if (c == 'm') {
                                    flux_render__parse_sgr(p + 2, remaining - 2,
                                                           &cur_style);
                                    style_id = flux_style_pool_intern(pool,
                                                                      &cur_style);
                                }
                                break;
                            }
                            scan2++;
                            sr--;
                        }
                    }
                    p += skip;
                    remaining -= skip;
                } else {
                    int blen = flux_render__utf8_len(p);
                    if (blen > remaining) blen = remaining;
                    p += blen;
                    remaining -= blen;
                }
            }
            continue;
        }

        {
            int blen = flux_render__utf8_len(p);
            int cw;
            if (blen > remaining) blen = remaining;
            cw = flux_render__char_width(p, blen);

            if (px + cw > bx + bw) {
                p += blen;
                remaining -= blen;
                continue;
            }

            flux_screen_set_cell(scr, px, py, p, blen, style_id, cw);
            if (cw == 2) {
                flux_screen_set_cell(scr, px + 1, py, "", 0, style_id, 0);
            }

            px += cw;
            p += blen;
            remaining -= blen;
        }
    }
}

/* -- Recursive node renderer -- */

void flux_render_node(FluxScreen *scr, FluxScreen *prev,
                             FluxStylePool *pool, FluxNode *node) {
    if (!scr || !pool || !node) return;

    if (!node->dirty && prev && prev->cells
        && node->cx == node->prev_cx && node->cy == node->prev_cy
        && node->cw == node->prev_cw && node->ch == node->prev_ch) {
        flux_screen_blit(scr, prev,
                         node->prev_cx, node->prev_cy,
                         node->cx, node->cy,
                         node->cw, node->ch);
        return;
    }

    switch (node->type) {
    case FLUX_NODE_TEXT:
        flux_render_text_to_screen(scr, pool, node);
        break;

    case FLUX_NODE_RAW:
        flux_render_raw_to_screen(scr, pool, node);
        break;

    case FLUX_NODE_BOX: {
        int has_bg = (node->style.bg_r >= 0 || node->style.bg_g >= 0
                      || node->style.bg_b >= 0);
        if (has_bg) {
            int bg_style = flux_style_pool_intern(pool, &node->style);
            flux_render__fill_rect(scr, node->cx, node->cy,
                                   node->cw, node->ch, bg_style);
        }

        {
            int i;
            for (i = 0; i < node->child_count; i++) {
                flux_render_node(scr, prev, pool, node->children[i]);
            }
        }
        break;
    }
    }

    node->prev_cx = node->cx;
    node->prev_cy = node->cy;
    node->prev_cw = node->cw;
    node->prev_ch = node->ch;
}

/* -- Entry point -- */

void flux_render_tree(FluxScreen *scr, FluxScreen *prev,
                             FluxStylePool *pool, FluxNode *root) {
    if (!scr || !pool || !root) return;

    {
        int total = scr->rows * scr->cols;
        int i;
        for (i = 0; i < total; i++) {
            FluxCell *c = &scr->cells[i];
            c->ch[0] = ' ';
            c->ch[1] = '\0';
            c->style_id = -1;
            c->width = 1;
        }
    }

    scr->damage_x1 = scr->cols;
    scr->damage_y1 = scr->rows;
    scr->damage_x2 = 0;
    scr->damage_y2 = 0;

    flux_render_node(scr, prev, pool, root);

    flux_node_clear_dirty(root);
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 6: Diff engine  (05_diff_engine.c)                  ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* Output buffer macros for diff engine (local scope in functions that use them) */

int flux_diff_cursor_move(int cx, int cy, int tx, int ty,
                                 char *buf, int bufsz) {
    int off = 0;
    int dx, dy;

    if (cx == tx && cy == ty) {
        return 0;
    }

    dx = tx - cx;
    dy = ty - cy;

    if (dy == 0) {
        if (dx == 1) {
            off += snprintf(buf + off, bufsz - off, "\x1b[C");
            return off;
        }
        if (dx > 1 && dx <= 9) {
            off += snprintf(buf + off, bufsz - off, "\x1b[%dC", dx);
            return off;
        }
        if (tx == 0) {
            buf[off++] = '\r';
            return off;
        }
        if (tx > 0 && tx <= 9) {
            int cr_len = 1 + 3 + (tx >= 10 ? 2 : 1);
            int abs_len = 4 + (ty + 1 >= 10 ? (ty + 1 >= 100 ? 3 : 2) : 1)
                        + (tx + 1 >= 10 ? (tx + 1 >= 100 ? 3 : 2) : 1);
            if (cr_len < abs_len) {
                off += snprintf(buf + off, bufsz - off, "\r");
                if (tx > 0) {
                    off += snprintf(buf + off, bufsz - off, "\x1b[%dC", tx);
                }
                return off;
            }
        }
    }

    if (tx == 0 && ty == 0) {
        off += snprintf(buf + off, bufsz - off, "\x1b[H");
    } else if (tx == 0) {
        off += snprintf(buf + off, bufsz - off, "\x1b[%dH", ty + 1);
    } else {
        off += snprintf(buf + off, bufsz - off, "\x1b[%d;%dH", ty + 1, tx + 1);
    }
    return off;
}

int flux_diff_emit(const FluxScreen *prev, const FluxScreen *next,
                          const FluxStylePool *pool, char *obuf, int obufsz) {
    int pos = 0;
    int cur_x = 0, cur_y = 0;
    int cur_style_id = -1;
    int rows, cols;
    int y, x;
    int is_last_row;
    int last_row_wrapped = 0;

    /* Local OCAT/OCATF/OCATN macros */
    #define DIFF_OCAT(s) do {                            \
        int _l = (int)strlen(s);                         \
        if (pos + _l < obufsz) {                         \
            memcpy(obuf + pos, s, _l);                   \
            pos += _l;                                   \
        }                                                \
    } while (0)

    #define DIFF_OCATF(...) do {                         \
        char _t[64];                                     \
        snprintf(_t, sizeof _t, __VA_ARGS__);            \
        DIFF_OCAT(_t);                                   \
    } while (0)

    #define DIFF_OCATN(s, n) do {                        \
        if (pos + (n) < obufsz) {                        \
            memcpy(obuf + pos, (s), (n));                \
            pos += (n);                                  \
        }                                                \
    } while (0)

    if (!next || !next->cells || !pool || !obuf || obufsz <= 0) {
        return 0;
    }

    rows = next->rows;
    cols = next->cols;

    if (prev && (prev->rows != rows || prev->cols != cols)) {
        prev = NULL;
    }

    DIFF_OCAT("\x1b[?25l");

    for (y = 0; y < rows; y++) {
        is_last_row = (y == rows - 1);

        for (x = 0; x < cols; x++) {
            const FluxCell *nc;
            const FluxCell *pc;
            const FluxStyle *ns;
            int ch_len;

            nc = flux_screen_get_cell(next, x, y);
            if (!nc) {
                continue;
            }

            if (nc->width == 0) {
                continue;
            }

            if (prev) {
                pc = flux_screen_get_cell(prev, x, y);
                if (pc && flux_cell_eq(pc, nc)) {
                    cur_x = x + nc->width;
                    continue;
                }
            }

            if (is_last_row && !last_row_wrapped) {
                DIFF_OCAT("\x1b[?7l");
                last_row_wrapped = 1;
            }

            {
                char movebuf[32];
                int mlen = flux_diff_cursor_move(cur_x, cur_y, x, y,
                                                 movebuf, sizeof movebuf);
                if (mlen > 0) {
                    DIFF_OCATN(movebuf, mlen);
                }
            }

            if (nc->style_id != cur_style_id) {
                if (nc->style_id >= 0 && nc->style_id < pool->count) {
                    const FluxStyle *prev_style = NULL;
                    char stylebuf[128];
                    int slen;

                    ns = &pool->styles[nc->style_id];
                    if (cur_style_id >= 0 && cur_style_id < pool->count) {
                        prev_style = &pool->styles[cur_style_id];
                    }
                    slen = flux_style_to_ansi(ns, prev_style,
                                              stylebuf, sizeof stylebuf);
                    if (slen > 0) {
                        DIFF_OCATN(stylebuf, slen);
                    }
                } else if (nc->style_id == -1 && cur_style_id != -1) {
                    DIFF_OCAT("\x1b[0m");
                }
                cur_style_id = nc->style_id;
            }

            ch_len = (int)strlen(nc->ch);
            if (ch_len > 0) {
                DIFF_OCATN(nc->ch, ch_len);
            } else {
                DIFF_OCAT(" ");
                ch_len = 1;
            }

            cur_x = x + nc->width;
            cur_y = y;
        }
    }

    if (last_row_wrapped) {
        DIFF_OCAT("\x1b[?7h");
    }

    if (cur_style_id != -1) {
        DIFF_OCAT("\x1b[0m");
    }

    DIFF_OCAT("\x1b[1;1H");

    if (pos < obufsz) {
        obuf[pos] = '\0';
    }

    #undef DIFF_OCAT
    #undef DIFF_OCATF
    #undef DIFF_OCATN

    return pos;
}

int flux_diff_screens(const FluxScreen *prev, const FluxScreen *next,
                             const FluxStylePool *pool,
                             char *obuf, int obufsz) {
    return flux_diff_emit(prev, next, pool, obuf, obufsz);
}

int flux_diff_full(const FluxScreen *next, const FluxStylePool *pool,
                          char *obuf, int obufsz) {
    return flux_diff_emit(NULL, next, pool, obuf, obufsz);
}

int flux_diff_has_changes(const FluxScreen *prev,
                                 const FluxScreen *next) {
    int total, i;

    if (!prev || !next) {
        return 1;
    }
    if (prev->rows != next->rows || prev->cols != next->cols) {
        return 1;
    }
    if (!prev->cells || !next->cells) {
        return 1;
    }

    total = prev->rows * prev->cols;
    for (i = 0; i < total; i++) {
        if (!flux_cell_eq(&prev->cells[i], &next->cells[i])) {
            return 1;
        }
    }
    return 0;
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 7: Widgets -- Input  (08_widget_input.c)            ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- Input UTF-8 helpers -- */

static int _flux_inp_utf8_len(const char *buf, int pos, int len) {
    unsigned char c;

    if (pos >= len) return 0;
    c = (unsigned char)buf[pos];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return (pos + 2 <= len) ? 2 : 1;
    if ((c & 0xF0) == 0xE0) return (pos + 3 <= len) ? 3 : 1;
    if ((c & 0xF8) == 0xF0) return (pos + 4 <= len) ? 4 : 1;
    return 1;
}

static int _flux_inp_prev_char(const char *buf, int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)buf[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

static int _flux_inp_char_width(const char *buf, int pos, int len) {
    char tmp[8];
    int clen = _flux_inp_utf8_len(buf, pos, len);

    if (clen <= 0) return 0;
    if (clen > 7) clen = 7;
    memcpy(tmp, buf + pos, (size_t)clen);
    tmp[clen] = '\0';
    return flux_strwidth(tmp);
}

/* -- Init / Clear -- */

void flux_input_init(FluxInput *inp, const char *placeholder) {
    memset(inp, 0, sizeof(*inp));
    inp->placeholder = placeholder;
}

void flux_input_clear(FluxInput *inp) {
    inp->buf[0] = '\0';
    inp->cursor = 0;
    inp->len = 0;
    inp->submitted = 0;
}

/* -- Update -- */

int flux_input_update(FluxInput *inp, FluxMsg msg) {
    if (msg.type == MSG_PASTE) {
        int avail = FLUX_INPUT_MAX - inp->len;
        int ins = msg.u.paste.len;

        if (ins <= 0) return 1;
        if (ins > avail) ins = avail;
        if (ins <= 0) return 1;

        memmove(inp->buf + inp->cursor + ins,
                inp->buf + inp->cursor,
                (size_t)(inp->len - inp->cursor));
        memcpy(inp->buf + inp->cursor, msg.u.paste.text, (size_t)ins);
        inp->len += ins;
        inp->cursor += ins;
        inp->buf[inp->len] = '\0';
        return 1;
    }

    if (msg.type != MSG_KEY) return 0;

    if (flux_key_is(msg, "enter")) {
        inp->submitted = 1;
        return 1;
    }

    if (flux_key_is(msg, "backspace")) {
        if (inp->cursor > 0) {
            int prev = _flux_inp_prev_char(inp->buf, inp->cursor);
            int del = inp->cursor - prev;
            memmove(inp->buf + prev,
                    inp->buf + inp->cursor,
                    (size_t)(inp->len - inp->cursor));
            inp->len -= del;
            inp->cursor = prev;
            inp->buf[inp->len] = '\0';
        }
        return 1;
    }

    if (flux_key_is(msg, "delete")) {
        if (inp->cursor < inp->len) {
            int clen = _flux_inp_utf8_len(inp->buf, inp->cursor, inp->len);
            memmove(inp->buf + inp->cursor,
                    inp->buf + inp->cursor + clen,
                    (size_t)(inp->len - inp->cursor - clen));
            inp->len -= clen;
            inp->buf[inp->len] = '\0';
        }
        return 1;
    }

    if (flux_key_is(msg, "left")) {
        if (inp->cursor > 0) {
            inp->cursor = _flux_inp_prev_char(inp->buf, inp->cursor);
        }
        return 1;
    }

    if (flux_key_is(msg, "right")) {
        if (inp->cursor < inp->len) {
            int clen = _flux_inp_utf8_len(inp->buf, inp->cursor, inp->len);
            inp->cursor += clen;
        }
        return 1;
    }

    if (flux_key_is(msg, "home") || flux_key_is(msg, "ctrl+a")) {
        inp->cursor = 0;
        return 1;
    }

    if (flux_key_is(msg, "end") || flux_key_is(msg, "ctrl+e")) {
        inp->cursor = inp->len;
        return 1;
    }

    if (flux_key_is(msg, "ctrl+u")) {
        inp->buf[0] = '\0';
        inp->cursor = 0;
        inp->len = 0;
        return 1;
    }

    if (flux_key_is(msg, "ctrl+k")) {
        inp->len = inp->cursor;
        inp->buf[inp->len] = '\0';
        return 1;
    }

    if (flux_key_is(msg, "ctrl+w")) {
        int target = inp->cursor;

        while (target > 0 && inp->buf[target - 1] == ' ') {
            target--;
        }
        while (target > 0 && inp->buf[target - 1] != ' ') {
            target--;
        }

        if (target < inp->cursor) {
            int del = inp->cursor - target;
            memmove(inp->buf + target,
                    inp->buf + inp->cursor,
                    (size_t)(inp->len - inp->cursor));
            inp->len -= del;
            inp->cursor = target;
            inp->buf[inp->len] = '\0';
        }
        return 1;
    }

    if (msg.u.key.rune >= 32 && msg.u.key.rune < 127) {
        if (inp->len < FLUX_INPUT_MAX) {
            memmove(inp->buf + inp->cursor + 1,
                    inp->buf + inp->cursor,
                    (size_t)(inp->len - inp->cursor));
            inp->buf[inp->cursor] = (char)msg.u.key.rune;
            inp->len++;
            inp->cursor++;
            inp->buf[inp->len] = '\0';
        }
        return 1;
    }

    if (msg.u.key.rune >= 128 && msg.u.key.key[0] != '\0') {
        int klen = (int)strlen(msg.u.key.key);
        if (klen > 0 && inp->len + klen <= FLUX_INPUT_MAX) {
            memmove(inp->buf + inp->cursor + klen,
                    inp->buf + inp->cursor,
                    (size_t)(inp->len - inp->cursor));
            memcpy(inp->buf + inp->cursor, msg.u.key.key, (size_t)klen);
            inp->len += klen;
            inp->cursor += klen;
            inp->buf[inp->len] = '\0';
        }
        return 1;
    }

    return 0;
}

/* -- Render -- */

void flux_input_render(FluxInput *inp, FluxSB *sb, int width,
                              const char *color, const char *cursor_clr) {
    int i, col, pad;

    if (width <= 0) return;
    if (!color) color = "";
    if (!cursor_clr) cursor_clr = "\x1b[7m";

    if (inp->len == 0) {
        flux_sb_append(sb, cursor_clr);
        flux_sb_append(sb, " ");
        flux_sb_append(sb, FLUX_RESET);

        if (inp->placeholder && inp->placeholder[0]) {
            flux_sb_append(sb, "\x1b[2m");
            flux_sb_append(sb, color);

            i = 0;
            col = 0;
            while (inp->placeholder[i] && col < width - 1) {
                int clen = _flux_inp_utf8_len(inp->placeholder, i,
                    (int)strlen(inp->placeholder));
                char tmp[8];
                int cw;

                if (clen > 7) clen = 7;
                memcpy(tmp, inp->placeholder + i, (size_t)clen);
                tmp[clen] = '\0';
                cw = flux_strwidth(tmp);
                if (col + cw > width - 1) break;
                flux_sb_append(sb, tmp);
                col += cw;
                i += clen;
            }
            flux_sb_append(sb, FLUX_RESET);

            pad = width - 1 - col;
        } else {
            pad = width - 1;
        }

        while (pad > 0) {
            flux_sb_append(sb, " ");
            pad--;
        }
        return;
    }

    {
        int vis_start;
        int vis_col;
        int cursor_col;
        int pos;

        cursor_col = 0;
        pos = 0;
        while (pos < inp->cursor) {
            cursor_col += _flux_inp_char_width(inp->buf, pos, inp->len);
            pos += _flux_inp_utf8_len(inp->buf, pos, inp->len);
        }

        vis_start = 0;
        if (cursor_col >= width) {
            int skipped_cols = 0;
            int target = cursor_col - width + 1;
            pos = 0;
            while (pos < inp->len && skipped_cols < target) {
                int cw = _flux_inp_char_width(inp->buf, pos, inp->len);
                int clen = _flux_inp_utf8_len(inp->buf, pos, inp->len);
                skipped_cols += cw;
                pos += clen;
            }
            vis_start = pos;
        }

        flux_sb_append(sb, color);
        pos = vis_start;
        vis_col = 0;

        while (pos < inp->len && vis_col < width) {
            int clen = _flux_inp_utf8_len(inp->buf, pos, inp->len);
            int cw = _flux_inp_char_width(inp->buf, pos, inp->len);
            char tmp[8];

            if (vis_col + cw > width) break;
            if (clen > 7) clen = 7;
            memcpy(tmp, inp->buf + pos, (size_t)clen);
            tmp[clen] = '\0';

            if (pos == inp->cursor) {
                flux_sb_append(sb, cursor_clr);
                flux_sb_append(sb, tmp);
                flux_sb_append(sb, FLUX_RESET);
                flux_sb_append(sb, color);
            } else {
                flux_sb_append(sb, tmp);
            }

            vis_col += cw;
            pos += clen;
        }

        if (inp->cursor == inp->len && vis_col < width) {
            flux_sb_append(sb, cursor_clr);
            flux_sb_append(sb, " ");
            flux_sb_append(sb, FLUX_RESET);
            flux_sb_append(sb, color);
            vis_col++;
        }

        pad = width - vis_col;
        while (pad > 0) {
            flux_sb_append(sb, " ");
            pad--;
        }

        flux_sb_append(sb, FLUX_RESET);
    }
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 8: Spinner, Tabs, Confirm, List, Table  (09)        ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- Spinner -- */

void flux_spinner_init(FluxSpinner *s, const char **frames,
                              int nframes, const char *label) {
    s->frames = frames;
    s->frame_count = nframes;
    s->current = 0;
    s->label = label;
}

void flux_spinner_tick(FluxSpinner *s) {
    if (s->frame_count > 0) {
        s->current = (s->current + 1) % s->frame_count;
    }
}

void flux_spinner_render(FluxSpinner *s, FluxSB *sb,
                                const char *color) {
    if (color) {
        flux_sb_append(sb, color);
    }
    if (s->frames && s->frame_count > 0) {
        flux_sb_append(sb, s->frames[s->current]);
    }
    if (color) {
        flux_sb_append(sb, FLUX_RESET);
    }
    flux_sb_append(sb, " ");
    if (s->label) {
        flux_sb_append(sb, s->label);
    }
}

/* -- Tabs -- */

void flux_tabs_init(FluxTabs *t, const char **icons,
                           const char **labels, int count) {
    t->icons = icons;
    t->labels = labels;
    t->count = count;
    t->active = 0;
}

int flux_tabs_update(FluxTabs *t, FluxMsg msg) {
    if (msg.type != MSG_KEY || t->count <= 0) {
        return 0;
    }
    if (strcmp(msg.u.key.key, "tab") == 0) {
        t->active = (t->active + 1) % t->count;
        return 1;
    }
    if (strcmp(msg.u.key.key, "shift+tab") == 0) {
        t->active = (t->active - 1 + t->count) % t->count;
        return 1;
    }
    return 0;
}

void flux_tabs_render(FluxTabs *t, FluxSB *sb,
                             const char *active_clr,
                             const char *dim_clr,
                             const char *hint) {
    int i;
    int tab_starts[64];
    int tab_widths[64];
    int col = 0;
    int max_tabs = t->count < 64 ? t->count : 64;

    for (i = 0; i < max_tabs; i++) {
        if (i > 0) {
            flux_sb_append(sb, " \xe2\x94\x82 ");
            col += 3;
        }
        tab_starts[i] = col;
        if (i == t->active) {
            flux_sb_append(sb, active_clr ? active_clr : "");
            flux_sb_append(sb, "\x1b[1m"); /* bold only, no underline */
        } else {
            flux_sb_append(sb, dim_clr ? dim_clr : "");
        }
        if (t->icons && t->icons[i]) {
            flux_sb_append(sb, t->icons[i]);
            flux_sb_append(sb, " ");
        }
        if (t->labels && t->labels[i]) {
            flux_sb_append(sb, t->labels[i]);
        }
        {
            int tw = 0;
            if (t->icons && t->icons[i]) {
                tw += flux_strwidth(t->icons[i]) + 1;
            }
            if (t->labels && t->labels[i]) {
                tw += flux_strwidth(t->labels[i]);
            }
            tab_widths[i] = tw;
            col += tw;
        }
        flux_sb_append(sb, FLUX_RESET);
    }

    if (hint) {
        flux_sb_append(sb, "  ");
        flux_sb_append(sb, hint);
    }
    flux_sb_nl(sb);

    {
        int pos = 0;
        int active_start = (t->active >= 0 && t->active < max_tabs)
                           ? tab_starts[t->active] : -1;
        int active_width = (t->active >= 0 && t->active < max_tabs)
                           ? tab_widths[t->active] : 0;
        int total_width = col;

        for (pos = 0; pos < total_width; pos++) {
            if (active_start >= 0
                && pos >= active_start
                && pos < active_start + active_width) {
                flux_sb_append(sb, "\xe2\x94\x80");
            } else {
                flux_sb_append(sb, " ");
            }
        }
        flux_sb_nl(sb);
    }
}

/* -- Confirm dialog -- */

void flux_confirm_init(FluxConfirm *c, const char *title,
                              const char *msg, const char *yes_label,
                              const char *no_label) {
    c->title = title;
    c->message = msg;
    c->yes_label = yes_label ? yes_label : "Yes";
    c->no_label = no_label ? no_label : "No";
    c->selected = 0;
    c->answered = 0;
    c->result = 0;
}

int flux_confirm_update(FluxConfirm *c, FluxMsg msg) {
    if (msg.type != MSG_KEY || c->answered) {
        return 0;
    }
    if (strcmp(msg.u.key.key, "left") == 0) {
        c->selected = 0;
        return 1;
    }
    if (strcmp(msg.u.key.key, "right") == 0) {
        c->selected = 1;
        return 1;
    }
    if (strcmp(msg.u.key.key, "tab") == 0
        || strcmp(msg.u.key.key, "shift+tab") == 0) {
        c->selected = c->selected ? 0 : 1;
        return 1;
    }
    if (strcmp(msg.u.key.key, "enter") == 0) {
        c->answered = 1;
        c->result = c->selected;
        return 1;
    }
    if (msg.u.key.key[0] == 'y' && msg.u.key.key[1] == '\0') {
        c->answered = 1;
        c->result = 0;
        c->selected = 0;
        return 1;
    }
    if (msg.u.key.key[0] == 'n' && msg.u.key.key[1] == '\0') {
        c->answered = 1;
        c->result = 1;
        c->selected = 1;
        return 1;
    }
    return 0;
}

void flux_confirm_render(FluxConfirm *c, FluxSB *sb, int width,
                                const char *border_clr,
                                const char *yes_clr,
                                const char *no_clr) {
    int inner_w = width - 4;
    int i;
    int title_len, msg_len, yes_len, no_len;
    int buttons_w, left_pad;
    const char *R = FLUX_RESET;
    const char *h = "\xe2\x94\x80";

    if (inner_w < 10) inner_w = 10;

    title_len = c->title ? flux_strwidth(c->title) : 0;
    msg_len = c->message ? flux_strwidth(c->message) : 0;
    yes_len = flux_strwidth(c->yes_label);
    no_len = flux_strwidth(c->no_label);

    /* Top border */
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x95\xad");
    if (title_len > 0 && title_len + 4 <= inner_w + 2) {
        flux_sb_append(sb, h);
        flux_sb_append(sb, h);
        flux_sb_append(sb, " ");
        if (border_clr) flux_sb_append(sb, R);
        flux_sb_append(sb, "\x1b[1m");
        flux_sb_append(sb, c->title);
        flux_sb_append(sb, R);
        if (border_clr) flux_sb_append(sb, border_clr);
        flux_sb_append(sb, " ");
        for (i = 0; i < inner_w + 2 - title_len - 4; i++) {
            flux_sb_append(sb, h);
        }
    } else {
        for (i = 0; i < inner_w + 2; i++) {
            flux_sb_append(sb, h);
        }
    }
    flux_sb_append(sb, "\xe2\x95\xae");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Empty line */
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    for (i = 0; i < inner_w + 2; i++) {
        flux_sb_append(sb, " ");
    }
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Message line */
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_append(sb, "  ");
    if (c->message) {
        flux_sb_append(sb, c->message);
    }
    {
        int pad_val = inner_w - msg_len;
        for (i = 0; i < pad_val; i++) {
            flux_sb_append(sb, " ");
        }
    }
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Empty line */
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    for (i = 0; i < inner_w + 2; i++) {
        flux_sb_append(sb, " ");
    }
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Button row */
    buttons_w = (4 + yes_len) + 4 + (4 + no_len);
    left_pad = (inner_w + 2 - buttons_w) / 2;
    if (left_pad < 1) left_pad = 1;

    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);

    for (i = 0; i < left_pad; i++) {
        flux_sb_append(sb, " ");
    }

    if (c->selected == 0) {
        if (yes_clr) flux_sb_append(sb, yes_clr);
        flux_sb_append(sb, "\x1b[1m");
        flux_sb_appendf(sb, "[ %s ]", c->yes_label);
        flux_sb_append(sb, R);
    } else {
        flux_sb_appendf(sb, "  %s  ", c->yes_label);
    }

    flux_sb_append(sb, "    ");

    if (c->selected == 1) {
        if (no_clr) flux_sb_append(sb, no_clr);
        flux_sb_append(sb, "\x1b[1m");
        flux_sb_appendf(sb, "[ %s ]", c->no_label);
        flux_sb_append(sb, R);
    } else {
        flux_sb_appendf(sb, "  %s  ", c->no_label);
    }

    {
        int used = left_pad + buttons_w;
        int rpad = inner_w + 2 - used;
        for (i = 0; i < rpad; i++) {
            flux_sb_append(sb, " ");
        }
    }

    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Empty line */
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    for (i = 0; i < inner_w + 2; i++) {
        flux_sb_append(sb, " ");
    }
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Bottom border */
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x95\xb0");
    for (i = 0; i < inner_w + 2; i++) {
        flux_sb_append(sb, h);
    }
    flux_sb_append(sb, "\xe2\x95\xaf");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);
}

/* -- List -- */

void flux_list_init(FluxList *l, int count, int visible,
                           FluxListItemFn render) {
    l->cursor = 0;
    l->scroll = 0;
    l->count = count;
    l->visible = visible;
    l->render_item = render;
}

static void _flux_list_clamp_scroll(FluxList *l) {
    if (l->cursor < l->scroll) {
        l->scroll = l->cursor;
    }
    if (l->cursor >= l->scroll + l->visible) {
        l->scroll = l->cursor - l->visible + 1;
    }
    if (l->scroll < 0) {
        l->scroll = 0;
    }
    if (l->count > l->visible && l->scroll > l->count - l->visible) {
        l->scroll = l->count - l->visible;
    }
}

int flux_list_update(FluxList *l, FluxMsg msg) {
    if (msg.type != MSG_KEY || l->count <= 0) {
        return 0;
    }
    if (strcmp(msg.u.key.key, "up") == 0
        || (msg.u.key.key[0] == 'k' && msg.u.key.key[1] == '\0')) {
        if (l->cursor > 0) {
            l->cursor--;
            _flux_list_clamp_scroll(l);
        }
        return 1;
    }
    if (strcmp(msg.u.key.key, "down") == 0
        || (msg.u.key.key[0] == 'j' && msg.u.key.key[1] == '\0')) {
        if (l->cursor < l->count - 1) {
            l->cursor++;
            _flux_list_clamp_scroll(l);
        }
        return 1;
    }
    if (strcmp(msg.u.key.key, "pgup") == 0) {
        l->cursor -= l->visible;
        if (l->cursor < 0) l->cursor = 0;
        _flux_list_clamp_scroll(l);
        return 1;
    }
    if (strcmp(msg.u.key.key, "pgdown") == 0) {
        l->cursor += l->visible;
        if (l->cursor >= l->count) l->cursor = l->count - 1;
        _flux_list_clamp_scroll(l);
        return 1;
    }
    if (strcmp(msg.u.key.key, "home") == 0) {
        l->cursor = 0;
        _flux_list_clamp_scroll(l);
        return 1;
    }
    if (strcmp(msg.u.key.key, "end") == 0) {
        l->cursor = l->count - 1;
        _flux_list_clamp_scroll(l);
        return 1;
    }
    return 0;
}

void flux_list_render(FluxList *l, FluxSB *sb, int width,
                             void *ctx) {
    int i;
    int end;

    if (!l->render_item || l->count <= 0) {
        return;
    }

    _flux_list_clamp_scroll(l);

    end = l->scroll + l->visible;
    if (end > l->count) end = l->count;

    for (i = l->scroll; i < end; i++) {
        l->render_item(sb, i, (i == l->cursor) ? 1 : 0, width, ctx);
        flux_sb_nl(sb);
    }
}

/* -- Table -- */

void flux_table_init(FluxTable *t, const char **headers,
                            const int *widths, int cols, int visible,
                            FluxTableCellFn render) {
    t->headers = headers;
    t->widths = widths;
    t->col_count = cols;
    t->row_count = 0;
    t->visible = visible;
    t->scroll = 0;
    t->follow = 0;
    t->render_cell = render;
}

void flux_table_set_rows(FluxTable *t, int rows) {
    t->row_count = rows;
    if (t->follow && rows > t->visible) {
        t->scroll = rows - t->visible;
    }
}

int flux_table_update(FluxTable *t, FluxMsg msg) {
    int max_scroll;

    if (msg.type != MSG_KEY) {
        return 0;
    }

    max_scroll = t->row_count - t->visible;
    if (max_scroll < 0) max_scroll = 0;

    if (strcmp(msg.u.key.key, "up") == 0
        || (msg.u.key.key[0] == 'k' && msg.u.key.key[1] == '\0')) {
        if (t->scroll > 0) {
            t->scroll--;
            t->follow = 0;
        }
        return 1;
    }
    if (strcmp(msg.u.key.key, "down") == 0
        || (msg.u.key.key[0] == 'j' && msg.u.key.key[1] == '\0')) {
        if (t->scroll < max_scroll) {
            t->scroll++;
            if (t->scroll >= max_scroll) {
                t->follow = 1;
            }
        }
        return 1;
    }
    if (strcmp(msg.u.key.key, "pgup") == 0) {
        t->scroll -= t->visible;
        if (t->scroll < 0) t->scroll = 0;
        t->follow = 0;
        return 1;
    }
    if (strcmp(msg.u.key.key, "pgdown") == 0) {
        t->scroll += t->visible;
        if (t->scroll > max_scroll) t->scroll = max_scroll;
        if (t->scroll >= max_scroll) t->follow = 1;
        return 1;
    }
    if (strcmp(msg.u.key.key, "home") == 0) {
        t->scroll = 0;
        t->follow = 0;
        return 1;
    }
    if (strcmp(msg.u.key.key, "end") == 0) {
        t->scroll = max_scroll;
        t->follow = 1;
        return 1;
    }
    return 0;
}

static void _flux_table_render_row_sep(FluxTable *t, FluxSB *sb,
                                       const char *border_clr) {
    int c, w;
    const char *R = FLUX_RESET;

    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x9c");
    for (c = 0; c < t->col_count; c++) {
        if (c > 0) {
            flux_sb_append(sb, "\xe2\x94\xbc");
        }
        w = t->widths[c] + 2;
        {
            int k;
            for (k = 0; k < w; k++) {
                flux_sb_append(sb, "\xe2\x94\x80");
            }
        }
    }
    flux_sb_append(sb, "\xe2\x94\xa4");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);
}

static void _flux_table_render_top(FluxTable *t, FluxSB *sb,
                                   const char *border_clr) {
    int c, w;
    const char *R = FLUX_RESET;

    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x8c");
    for (c = 0; c < t->col_count; c++) {
        if (c > 0) {
            flux_sb_append(sb, "\xe2\x94\xac");
        }
        w = t->widths[c] + 2;
        {
            int k;
            for (k = 0; k < w; k++) {
                flux_sb_append(sb, "\xe2\x94\x80");
            }
        }
    }
    flux_sb_append(sb, "\xe2\x94\x90");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);
}

static void _flux_table_render_bottom(FluxTable *t, FluxSB *sb,
                                      const char *border_clr) {
    int c, w;
    const char *R = FLUX_RESET;

    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x94");
    for (c = 0; c < t->col_count; c++) {
        if (c > 0) {
            flux_sb_append(sb, "\xe2\x94\xb4");
        }
        w = t->widths[c] + 2;
        {
            int k;
            for (k = 0; k < w; k++) {
                flux_sb_append(sb, "\xe2\x94\x80");
            }
        }
    }
    flux_sb_append(sb, "\xe2\x94\x98");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);
}

static void _flux_table_render_data_row(FluxTable *t, FluxSB *sb,
                                        int row, void *ctx,
                                        const char *border_clr,
                                        const char *text_clr) {
    int c;
    const char *R = FLUX_RESET;

    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x94\x82");
    if (border_clr) flux_sb_append(sb, R);

    for (c = 0; c < t->col_count; c++) {
        int cell_start;

        flux_sb_append(sb, " ");
        cell_start = sb->len;

        if (text_clr) flux_sb_append(sb, text_clr);
        if (t->render_cell) {
            t->render_cell(sb, row, c, t->widths[c], ctx);
        }
        if (text_clr) flux_sb_append(sb, R);

        {
            char measure_buf[1024];
            int written_len = sb->len - cell_start;
            int vis_w;

            if (written_len > 0 && written_len < (int)sizeof(measure_buf)) {
                memcpy(measure_buf, sb->buf + cell_start, (size_t)written_len);
                measure_buf[written_len] = '\0';
                vis_w = flux_strwidth(measure_buf);
            } else {
                vis_w = 0;
            }
            {
                int pad_val = t->widths[c] - vis_w;
                int k;
                for (k = 0; k < pad_val; k++) {
                    flux_sb_append(sb, " ");
                }
            }
        }

        flux_sb_append(sb, " ");
        if (border_clr) flux_sb_append(sb, border_clr);
        flux_sb_append(sb, "\xe2\x94\x82");
        if (border_clr) flux_sb_append(sb, R);
    }
    flux_sb_nl(sb);
}

void flux_table_render(FluxTable *t, FluxSB *sb, void *ctx,
                               const char *header_clr,
                               const char *border_clr) {
    int r, end;
    const char *R = FLUX_RESET;

    _flux_table_render_top(t, sb, border_clr);

    if (t->headers) {
        if (border_clr) flux_sb_append(sb, border_clr);
        flux_sb_append(sb, "\xe2\x94\x82");
        if (border_clr) flux_sb_append(sb, R);
        {
            int c;
            for (c = 0; c < t->col_count; c++) {
                int hdr_w;

                flux_sb_append(sb, " ");
                if (header_clr) flux_sb_append(sb, header_clr);
                flux_sb_append(sb, "\x1b[1m");
                if (t->headers[c]) {
                    flux_sb_append(sb, t->headers[c]);
                    hdr_w = flux_strwidth(t->headers[c]);
                } else {
                    hdr_w = 0;
                }
                flux_sb_append(sb, R);

                {
                    int pad_val = t->widths[c] - hdr_w;
                    int k;
                    for (k = 0; k < pad_val; k++) {
                        flux_sb_append(sb, " ");
                    }
                }

                flux_sb_append(sb, " ");
                if (border_clr) flux_sb_append(sb, border_clr);
                flux_sb_append(sb, "\xe2\x94\x82");
                if (border_clr) flux_sb_append(sb, R);
            }
        }
        flux_sb_nl(sb);

        _flux_table_render_row_sep(t, sb, border_clr);
    }

    end = t->scroll + t->visible;
    if (end > t->row_count) end = t->row_count;

    for (r = t->scroll; r < end; r++) {
        _flux_table_render_data_row(t, sb, r, ctx, border_clr, NULL);
    }

    _flux_table_render_bottom(t, sb, border_clr);
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 9: Viewport, Popup, Sparkline, Bar, Kanban  (10)    ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- Viewport -- */

void flux_viewport_init(FluxViewport *vp, int width, int height) {
    if (!vp) return;
    memset(vp, 0, sizeof(*vp));
    vp->width = width;
    vp->total_height = height;
    vp->count = 0;
}

void flux_viewport_add(FluxViewport *vp, FluxRegionType type,
                              int height, FluxRegionRenderFn render,
                              void *ctx) {
    FluxRegion *r;

    if (!vp || vp->count >= FLUX_REGION_MAX) return;

    r = &vp->regions[vp->count];
    r->type = type;
    r->height = height;
    r->render = render;
    r->ctx = ctx;
    vp->count++;
}

void flux_viewport_render(FluxViewport *vp, char *buf, int bufsz) {
    #define _VP_TMPBUF 65536
    #define _VP_MAXLINES 512
    char tmpbuf[_VP_TMPBUF];
    FluxSB tmp;
    char *lines[_VP_MAXLINES];
    int line_count;
    int fixed_sum;
    int fill_h;
    int out_line;
    int i, j;
    char *o;
    int rem;

    if (!vp || !buf || bufsz <= 0) return;
    buf[0] = '\0';

    fixed_sum = 0;
    for (i = 0; i < vp->count; i++) {
        if (vp->regions[i].type == FLUX_REGION_FIXED) {
            fixed_sum += vp->regions[i].height;
        }
    }
    fill_h = vp->total_height - fixed_sum;
    if (fill_h < 0) fill_h = 0;

    o = buf;
    rem = bufsz - 1;
    out_line = 0;

    for (i = 0; i < vp->count && out_line < vp->total_height; i++) {
        int region_h;
        char *p;
        char *nl;

        region_h = (vp->regions[i].type == FLUX_REGION_FILL)
                 ? fill_h : vp->regions[i].height;
        if (region_h <= 0) continue;

        flux_sb_init(&tmp, tmpbuf, _VP_TMPBUF);
        if (vp->regions[i].render) {
            vp->regions[i].render(&tmp, vp->width, region_h,
                                  vp->regions[i].ctx);
        }

        line_count = 0;
        p = tmpbuf;
        while (*p && line_count < _VP_MAXLINES) {
            lines[line_count++] = p;
            nl = strchr(p, '\n');
            if (!nl) break;
            *nl = '\0';
            p = nl + 1;
        }

        for (j = 0; j < region_h && out_line < vp->total_height; j++) {
            const char *line_str;
            int len;

            if (out_line > 0) {
                if (rem > 0) {
                    *o++ = '\n';
                    rem--;
                }
            }

            line_str = (j < line_count) ? lines[j] : "";
            len = (int)strlen(line_str);
            if (len > rem) len = rem;
            if (len > 0) {
                memcpy(o, line_str, (size_t)len);
                o += len;
                rem -= len;
            }
            out_line++;
        }
    }

    while (out_line < vp->total_height) {
        if (out_line > 0) {
            if (rem > 0) {
                *o++ = '\n';
                rem--;
            }
        }
        out_line++;
    }

    *o = '\0';
    #undef _VP_TMPBUF
    #undef _VP_MAXLINES
}

/* -- Popup -- */

void flux_popup(FluxSB *sb, int area_w, int area_h,
                       const char *title, const char **items, int count,
                       int selected, const char *hint,
                       const char *border_clr, const char *select_bg,
                       const char *normal_bg, const char *accent_clr) {
    int max_item_w;
    int box_w, box_h;
    int inner_w;
    int pad_top, pad_left;
    int title_w, hint_w;
    int i, j;
    const char *R = FLUX_RESET;

    if (!sb || !items || count <= 0) return;

    max_item_w = 0;
    for (i = 0; i < count; i++) {
        int w = flux_strwidth(items[i]);
        if (w > max_item_w) max_item_w = w;
    }

    inner_w = max_item_w + 4;
    title_w = title ? flux_strwidth(title) : 0;
    hint_w = hint ? flux_strwidth(hint) : 0;

    if (title_w + 4 > inner_w) inner_w = title_w + 4;
    if (hint_w + 4 > inner_w) inner_w = hint_w + 4;

    box_w = inner_w + 2;
    if (box_w > area_w - 4) {
        box_w = area_w - 4;
        if (box_w < 8) box_w = 8;
        inner_w = box_w - 2;
    }

    box_h = count + 2;
    if (box_h > area_h - 2) {
        box_h = area_h - 2;
        if (box_h < 3) box_h = 3;
    }

    pad_top = (area_h - box_h) / 2;
    pad_left = (area_w - box_w) / 2;
    if (pad_top < 0) pad_top = 0;
    if (pad_left < 0) pad_left = 0;

    for (i = 0; i < pad_top; i++) {
        flux_sb_nl(sb);
    }

    /* Top border */
    for (j = 0; j < pad_left; j++) {
        flux_sb_append(sb, " ");
    }
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x95\xad");

    if (title && title_w > 0) {
        int dashes_total = inner_w - title_w - 2;
        int dashes_left = dashes_total / 2;
        int dashes_right = dashes_total - dashes_left;
        if (dashes_left < 1) dashes_left = 1;
        if (dashes_right < 1) dashes_right = 1;
        for (j = 0; j < dashes_left; j++) {
            flux_sb_append(sb, "\xe2\x94\x80");
        }
        flux_sb_append(sb, " ");
        if (accent_clr) flux_sb_append(sb, accent_clr);
        flux_sb_append(sb, title);
        if (border_clr) flux_sb_append(sb, border_clr);
        flux_sb_append(sb, " ");
        for (j = 0; j < dashes_right; j++) {
            flux_sb_append(sb, "\xe2\x94\x80");
        }
    } else {
        for (j = 0; j < inner_w; j++) {
            flux_sb_append(sb, "\xe2\x94\x80");
        }
    }

    flux_sb_append(sb, "\xe2\x95\xae");
    if (border_clr) flux_sb_append(sb, R);
    flux_sb_nl(sb);

    /* Item rows */
    {
        int visible = box_h - 2;
        for (i = 0; i < visible && i < count; i++) {
            int item_w;
            int pad_val;

            for (j = 0; j < pad_left; j++) {
                flux_sb_append(sb, " ");
            }

            if (border_clr) flux_sb_append(sb, border_clr);
            flux_sb_append(sb, "\xe2\x94\x82");
            flux_sb_append(sb, R);

            if (i == selected) {
                if (select_bg) flux_sb_append(sb, select_bg);
                flux_sb_append(sb, " ");
                if (accent_clr) flux_sb_append(sb, accent_clr);
                flux_sb_append(sb, "\xe2\x96\xb8 ");
                if (select_bg) flux_sb_append(sb, select_bg);
            } else {
                if (normal_bg) flux_sb_append(sb, normal_bg);
                flux_sb_append(sb, "   ");
            }

            flux_sb_append(sb, items[i]);

            item_w = flux_strwidth(items[i]);
            pad_val = inner_w - 3 - item_w - 1;
            for (j = 0; j < pad_val; j++) {
                flux_sb_append(sb, " ");
            }
            flux_sb_append(sb, " ");
            flux_sb_append(sb, R);

            if (border_clr) flux_sb_append(sb, border_clr);
            flux_sb_append(sb, "\xe2\x94\x82");
            flux_sb_append(sb, R);
            flux_sb_nl(sb);
        }
    }

    /* Bottom border */
    for (j = 0; j < pad_left; j++) {
        flux_sb_append(sb, " ");
    }
    if (border_clr) flux_sb_append(sb, border_clr);
    flux_sb_append(sb, "\xe2\x95\xb0");

    if (hint && hint_w > 0) {
        int dashes_total = inner_w - hint_w - 2;
        int dashes_left = dashes_total / 2;
        int dashes_right = dashes_total - dashes_left;
        if (dashes_left < 1) dashes_left = 1;
        if (dashes_right < 1) dashes_right = 1;
        for (j = 0; j < dashes_left; j++) {
            flux_sb_append(sb, "\xe2\x94\x80");
        }
        flux_sb_append(sb, " ");
        if (accent_clr) flux_sb_append(sb, accent_clr);
        flux_sb_append(sb, hint);
        if (border_clr) flux_sb_append(sb, border_clr);
        flux_sb_append(sb, " ");
        for (j = 0; j < dashes_right; j++) {
            flux_sb_append(sb, "\xe2\x94\x80");
        }
    } else {
        for (j = 0; j < inner_w; j++) {
            flux_sb_append(sb, "\xe2\x94\x80");
        }
    }

    flux_sb_append(sb, "\xe2\x95\xaf");
    if (border_clr) flux_sb_append(sb, R);

    {
        int rows_used = pad_top + box_h;
        for (i = rows_used; i < area_h; i++) {
            flux_sb_nl(sb);
        }
    }
}

/* -- Sparkline -- */

void flux_sparkline(FluxSB *sb, float *ring, int len, int head,
                           const char *color) {
    static const char *bars[8] = {
        "\xe2\x96\x81",
        "\xe2\x96\x82",
        "\xe2\x96\x83",
        "\xe2\x96\x84",
        "\xe2\x96\x85",
        "\xe2\x96\x86",
        "\xe2\x96\x87",
        "\xe2\x96\x88"
    };
    float vmin, vmax, range;
    int i, idx;

    if (!sb || !ring || len <= 0) return;

    vmin = ring[0];
    vmax = ring[0];
    for (i = 0; i < len; i++) {
        if (ring[i] < vmin) vmin = ring[i];
        if (ring[i] > vmax) vmax = ring[i];
    }
    range = vmax - vmin;

    if (color) flux_sb_append(sb, color);

    for (i = 0; i < len; i++) {
        float v;
        int level;

        idx = (head + i) % len;
        v = ring[idx];

        if (range > 0.0f) {
            level = (int)(((v - vmin) / range) * 7.0f + 0.5f);
        } else {
            level = 3;
        }
        if (level < 0) level = 0;
        if (level > 7) level = 7;

        flux_sb_append(sb, bars[level]);
    }

    if (color) flux_sb_append(sb, FLUX_RESET);
}

/* -- Bar -- */

void flux_bar(FluxSB *sb, float value, int width,
                     const char *fill_clr, const char *empty_clr) {
    int filled, i;

    if (!sb || width <= 0) return;

    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    filled = (int)(value * (float)width + 0.5f);
    if (filled > width) filled = width;

    if (fill_clr) flux_sb_append(sb, fill_clr);
    for (i = 0; i < filled; i++) {
        flux_sb_append(sb, "\xe2\x96\x88");
    }
    if (fill_clr) flux_sb_append(sb, FLUX_RESET);

    if (empty_clr) flux_sb_append(sb, empty_clr);
    for (i = filled; i < width; i++) {
        flux_sb_append(sb, "\xe2\x96\x91");
    }
    if (empty_clr) flux_sb_append(sb, FLUX_RESET);
}

/* -- Kanban -- */

#define _KB_MODE_NAVIGATE 0
#define _KB_MODE_EDIT     1

void flux_kanban_init(FluxKanban *kb, int ncols,
                             const char **col_names, int col_width) {
    int i;

    if (!kb) return;
    memset(kb, 0, sizeof(*kb));

    if (ncols > FLUX_KB_MAX_COLS) ncols = FLUX_KB_MAX_COLS;
    kb->ncols = ncols;
    kb->col_width = col_width > 8 ? col_width : 8;
    kb->visible = 1;

    for (i = 0; i < ncols; i++) {
        if (col_names && col_names[i]) {
            snprintf(kb->cols[i].name, sizeof(kb->cols[i].name),
                     "%s", col_names[i]);
        }
        kb->cols[i].count = 0;
    }
}

int flux_kanban_add(FluxKanban *kb, int col,
                           const char *title, const char *desc) {
    FluxKbCard *card;

    if (!kb || col < 0 || col >= kb->ncols) return -1;
    if (kb->cols[col].count >= FLUX_KB_MAX_CARDS) return -1;

    card = &kb->cols[col].cards[kb->cols[col].count];
    snprintf(card->title, sizeof(card->title), "%s",
             title ? title : "");
    snprintf(card->desc, sizeof(card->desc), "%s",
             desc ? desc : "");
    kb->cols[col].count++;
    kb->dirty = 1;
    return 0;
}

int flux_kanban_del(FluxKanban *kb, int col, int row) {
    FluxKbCol *c;
    int i;

    if (!kb || col < 0 || col >= kb->ncols) return -1;
    c = &kb->cols[col];
    if (row < 0 || row >= c->count) return -1;

    for (i = row; i < c->count - 1; i++) {
        c->cards[i] = c->cards[i + 1];
    }
    c->count--;
    kb->dirty = 1;
    return 0;
}

int flux_kanban_move(FluxKanban *kb, int col, int row,
                            int to_col) {
    FluxKbCard card;
    FluxKbCol *src;

    if (!kb) return -1;
    if (col < 0 || col >= kb->ncols) return -1;
    if (to_col < 0 || to_col >= kb->ncols) return -1;
    if (col == to_col) return 0;

    src = &kb->cols[col];
    if (row < 0 || row >= src->count) return -1;
    if (kb->cols[to_col].count >= FLUX_KB_MAX_CARDS) return -1;

    card = src->cards[row];
    flux_kanban_del(kb, col, row);
    kb->cols[to_col].cards[kb->cols[to_col].count] = card;
    kb->cols[to_col].count++;
    kb->dirty = 1;
    return 0;
}

int flux_kanban_dirty(FluxKanban *kb) {
    int d;
    if (!kb) return 0;
    d = kb->dirty;
    kb->dirty = 0;
    return d;
}

int flux_kanban_update(FluxKanban *kb, FluxMsg msg) {
    FluxKbCol *col;

    if (!kb || !kb->visible) return 0;
    if (msg.type != MSG_KEY) return 0;

    if (kb->mode == _KB_MODE_EDIT) {
        if (flux_key_is(msg, "esc")) {
            kb->mode = _KB_MODE_NAVIGATE;
            return 1;
        }
        if (flux_key_is(msg, "tab")) {
            kb->edit_focus = kb->edit_focus ? 0 : 1;
            return 1;
        }
        if (flux_key_is(msg, "enter")) {
            FluxKbCard *card;
            int ec = kb->edit_col;
            int er = kb->edit_row;

            if (ec >= 0 && ec < kb->ncols
                && er >= 0 && er < kb->cols[ec].count) {
                card = &kb->cols[ec].cards[er];
                memcpy(card->title, kb->edit_title.buf,
                       FLUX_KB_TITLE_MAX);
                card->title[FLUX_KB_TITLE_MAX] = '\0';
                memcpy(card->desc, kb->edit_desc.buf,
                       FLUX_KB_DESC_MAX);
                card->desc[FLUX_KB_DESC_MAX] = '\0';
                kb->dirty = 1;
            } else if (ec >= 0 && ec < kb->ncols
                       && er == kb->cols[ec].count) {
                flux_kanban_add(kb, ec, kb->edit_title.buf,
                                kb->edit_desc.buf);
            }
            kb->mode = _KB_MODE_NAVIGATE;
            return 1;
        }
        if (kb->edit_focus == 0) {
            flux_input_update(&kb->edit_title, msg);
        } else {
            flux_input_update(&kb->edit_desc, msg);
        }
        return 1;
    }

    col = &kb->cols[kb->cur_col];

    if (flux_key_is(msg, "h") || flux_key_is(msg, "left")) {
        if (kb->grabbed && kb->cur_col > 0
            && col->count > 0 && kb->cur_row < col->count) {
            int old_col = kb->cur_col;
            int old_row = kb->cur_row;
            flux_kanban_move(kb, old_col, old_row, old_col - 1);
            kb->cur_col = old_col - 1;
            kb->cur_row = kb->cols[kb->cur_col].count - 1;
        } else if (kb->cur_col > 0) {
            kb->cur_col--;
            if (kb->cur_row >= kb->cols[kb->cur_col].count) {
                kb->cur_row = kb->cols[kb->cur_col].count - 1;
            }
            if (kb->cur_row < 0) kb->cur_row = 0;
        }
        return 1;
    }

    if (flux_key_is(msg, "l") || flux_key_is(msg, "right")) {
        if (kb->grabbed && kb->cur_col < kb->ncols - 1
            && col->count > 0 && kb->cur_row < col->count) {
            int old_col = kb->cur_col;
            int old_row = kb->cur_row;
            flux_kanban_move(kb, old_col, old_row, old_col + 1);
            kb->cur_col = old_col + 1;
            kb->cur_row = kb->cols[kb->cur_col].count - 1;
        } else if (kb->cur_col < kb->ncols - 1) {
            kb->cur_col++;
            if (kb->cur_row >= kb->cols[kb->cur_col].count) {
                kb->cur_row = kb->cols[kb->cur_col].count - 1;
            }
            if (kb->cur_row < 0) kb->cur_row = 0;
        }
        return 1;
    }

    if (flux_key_is(msg, "j") || flux_key_is(msg, "down")) {
        if (kb->cur_row < col->count - 1) {
            kb->cur_row++;
        }
        return 1;
    }

    if (flux_key_is(msg, "k") || flux_key_is(msg, "up")) {
        if (kb->cur_row > 0) {
            kb->cur_row--;
        }
        return 1;
    }

    if (flux_key_is(msg, "enter")) {
        if (col->count > 0 && kb->cur_row < col->count) {
            FluxKbCard *card = &col->cards[kb->cur_row];
            kb->mode = _KB_MODE_EDIT;
            kb->edit_col = kb->cur_col;
            kb->edit_row = kb->cur_row;
            kb->edit_focus = 0;
            flux_input_init(&kb->edit_title, "Title");
            flux_input_init(&kb->edit_desc, "Description");
            snprintf(kb->edit_title.buf, sizeof(kb->edit_title.buf),
                     "%s", card->title);
            kb->edit_title.len = (int)strlen(kb->edit_title.buf);
            kb->edit_title.cursor = kb->edit_title.len;
            snprintf(kb->edit_desc.buf, sizeof(kb->edit_desc.buf),
                     "%s", card->desc);
            kb->edit_desc.len = (int)strlen(kb->edit_desc.buf);
            kb->edit_desc.cursor = kb->edit_desc.len;
        }
        return 1;
    }

    if (flux_key_is(msg, "n")) {
        kb->mode = _KB_MODE_EDIT;
        kb->edit_col = kb->cur_col;
        kb->edit_row = col->count;
        kb->edit_focus = 0;
        flux_input_init(&kb->edit_title, "Title");
        flux_input_init(&kb->edit_desc, "Description");
        return 1;
    }

    if (flux_key_is(msg, "d")) {
        if (col->count > 0 && kb->cur_row < col->count) {
            flux_kanban_del(kb, kb->cur_col, kb->cur_row);
            if (kb->cur_row >= col->count && col->count > 0) {
                kb->cur_row = col->count - 1;
            }
            if (col->count == 0) kb->cur_row = 0;
        }
        return 1;
    }

    if (flux_key_is(msg, "g")) {
        kb->grabbed = !kb->grabbed;
        return 1;
    }

    return 0;
}

static void _flux_kb_render_card(FluxSB *sb, const FluxKbCard *card,
                                 int width, int is_selected, int is_grabbed,
                                 const char *col_color) {
    int inner_w;
    int tw, dw, pad_val;
    int j;
    const char *R = FLUX_RESET;

    inner_w = width - 2;
    if (inner_w < 1) inner_w = 1;

    if (col_color) flux_sb_append(sb, col_color);
    flux_sb_append(sb, "\xe2\x94\x8c");
    for (j = 0; j < inner_w; j++) {
        flux_sb_append(sb, "\xe2\x94\x80");
    }
    flux_sb_append(sb, "\xe2\x94\x90");
    flux_sb_append(sb, R);
    flux_sb_nl(sb);

    if (col_color) flux_sb_append(sb, col_color);
    flux_sb_append(sb, "\xe2\x94\x82");
    flux_sb_append(sb, R);

    if (is_selected) {
        if (is_grabbed) {
            flux_sb_append(sb, "\x1b[7m");
        } else {
            flux_sb_append(sb, "\x1b[1m");
        }
    }

    tw = flux_strwidth(card->title);
    pad_val = inner_w - tw;
    if (pad_val < 0) pad_val = 0;
    flux_sb_append(sb, card->title);
    for (j = 0; j < pad_val; j++) {
        flux_sb_append(sb, " ");
    }

    if (is_selected) flux_sb_append(sb, R);

    if (col_color) flux_sb_append(sb, col_color);
    flux_sb_append(sb, "\xe2\x94\x82");
    flux_sb_append(sb, R);
    flux_sb_nl(sb);

    if (card->desc[0]) {
        if (col_color) flux_sb_append(sb, col_color);
        flux_sb_append(sb, "\xe2\x94\x82");
        flux_sb_append(sb, R);

        flux_sb_append(sb, "\x1b[2m");
        dw = flux_strwidth(card->desc);
        if (dw > inner_w) {
            char trunc[FLUX_KB_DESC_MAX + 1];
            int ti;
            for (ti = 0; ti < inner_w - 1 && card->desc[ti]; ti++) {
                trunc[ti] = card->desc[ti];
            }
            trunc[ti] = '\0';
            flux_sb_append(sb, trunc);
            flux_sb_append(sb, "\xe2\x80\xa6");
        } else {
            flux_sb_append(sb, card->desc);
            pad_val = inner_w - dw;
            for (j = 0; j < pad_val; j++) {
                flux_sb_append(sb, " ");
            }
        }
        flux_sb_append(sb, R);

        if (col_color) flux_sb_append(sb, col_color);
        flux_sb_append(sb, "\xe2\x94\x82");
        flux_sb_append(sb, R);
        flux_sb_nl(sb);
    }

    if (col_color) flux_sb_append(sb, col_color);
    flux_sb_append(sb, "\xe2\x94\x94");
    for (j = 0; j < inner_w; j++) {
        flux_sb_append(sb, "\xe2\x94\x80");
    }
    flux_sb_append(sb, "\xe2\x94\x98");
    flux_sb_append(sb, R);
    flux_sb_nl(sb);
}

void flux_kanban_render(FluxKanban *kb, FluxSB *sb,
                               const char **col_colors,
                               const char *hint) {
    #define _KB_COL_BUF 8192
    #define _KB_MAX_RENDER_COLS 8
    char col_bufs[_KB_MAX_RENDER_COLS][_KB_COL_BUF];
    const char *panels[_KB_MAX_RENDER_COLS];
    int widths[_KB_MAX_RENDER_COLS];
    int i, j;
    const char *R = FLUX_RESET;

    if (!kb || !sb || !kb->visible) return;

    for (i = 0; i < kb->ncols && i < _KB_MAX_RENDER_COLS; i++) {
        FluxSB col_sb;
        FluxKbCol *col_data = &kb->cols[i];
        const char *cc = (col_colors && col_colors[i])
                       ? col_colors[i] : "";
        int name_w, name_pad_l, name_pad_r;

        flux_sb_init(&col_sb, col_bufs[i], _KB_COL_BUF);

        name_w = flux_strwidth(col_data->name);
        name_pad_l = (kb->col_width - name_w) / 2;
        name_pad_r = kb->col_width - name_w - name_pad_l;
        if (name_pad_l < 0) name_pad_l = 0;
        if (name_pad_r < 0) name_pad_r = 0;

        if (cc[0]) flux_sb_append(&col_sb, cc);
        flux_sb_append(&col_sb, "\x1b[1m");
        for (j = 0; j < name_pad_l; j++) {
            flux_sb_append(&col_sb, " ");
        }
        flux_sb_append(&col_sb, col_data->name);
        for (j = 0; j < name_pad_r; j++) {
            flux_sb_append(&col_sb, " ");
        }
        flux_sb_append(&col_sb, R);
        flux_sb_nl(&col_sb);

        if (cc[0]) flux_sb_append(&col_sb, cc);
        for (j = 0; j < kb->col_width; j++) {
            flux_sb_append(&col_sb, "\xe2\x94\x80");
        }
        flux_sb_append(&col_sb, R);
        flux_sb_nl(&col_sb);

        for (j = 0; j < col_data->count; j++) {
            int is_sel = (i == kb->cur_col && j == kb->cur_row);
            int is_grab = is_sel && kb->grabbed;
            _flux_kb_render_card(&col_sb, &col_data->cards[j],
                                 kb->col_width, is_sel, is_grab,
                                 cc);
        }

        if (kb->mode == _KB_MODE_EDIT && kb->edit_col == i) {
            flux_sb_append(&col_sb, "\xe2\x94\x80 ");
            if (cc[0]) flux_sb_append(&col_sb, cc);
            flux_sb_append(&col_sb, "Title: ");
            flux_sb_append(&col_sb, R);
            flux_input_render(&kb->edit_title, &col_sb,
                              kb->col_width - 8,
                              kb->edit_focus == 0 ? cc : "",
                              "\x1b[7m");
            flux_sb_nl(&col_sb);

            flux_sb_append(&col_sb, "\xe2\x94\x80 ");
            if (cc[0]) flux_sb_append(&col_sb, cc);
            flux_sb_append(&col_sb, "Desc:  ");
            flux_sb_append(&col_sb, R);
            flux_input_render(&kb->edit_desc, &col_sb,
                              kb->col_width - 8,
                              kb->edit_focus == 1 ? cc : "",
                              "\x1b[7m");
            flux_sb_nl(&col_sb);
        }

        panels[i] = col_bufs[i];
        widths[i] = kb->col_width;
    }

    flux_hbox(sb, panels, widths, kb->ncols < _KB_MAX_RENDER_COLS
              ? kb->ncols : _KB_MAX_RENDER_COLS, "  ");

    if (hint) {
        flux_sb_append(sb, "\x1b[2m");
        flux_sb_append(sb, hint);
        flux_sb_append(sb, R);
        flux_sb_nl(sb);
    }

    #undef _KB_COL_BUF
    #undef _KB_MAX_RENDER_COLS
}


/* ╔════════════════════════════════════════════════════════════════╗
 * ║  SECTION 10: Event loop  (06_event_loop.c) -- LAST           ║
 * ╚════════════════════════════════════════════════════════════════╝ */

/* -- Globals -- */

static struct termios      _flux_orig_termios;
static volatile sig_atomic_t _flux_cols  = 80;
static volatile sig_atomic_t _flux_rows  = 24;
static volatile sig_atomic_t _flux_winch = 0;
static volatile int        _flux_running = 0;
static int                 _flux_pipe_r  = -1;
static int                 _flux_pipe_w  = -1;

/* -- Terminal queries -- */

int flux_cols(void) { return (int)_flux_cols; }
int flux_rows(void) { return (int)_flux_rows; }

static void _flux_update_winsize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        _flux_cols = ws.ws_col > 0 ? ws.ws_col : 80;
        _flux_rows = ws.ws_row > 0 ? ws.ws_row : 24;
    }
}

/* -- Signal handlers -- */

static void _flux_sigwinch_handler(int sig) {
    (void)sig;
    _flux_winch = 1;
}

static void _flux_sigint_handler(int sig) {
    (void)sig;
    FluxMsg m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_QUIT;
    ssize_t w = write(_flux_pipe_w, &m, sizeof(m));
    (void)w;
}

/* -- Terminal setup / teardown -- */

static void _flux_setup_term(int alt_screen, int mouse) {
    struct termios raw;

    tcgetattr(STDIN_FILENO, &_flux_orig_termios);

    raw = _flux_orig_termios;
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(unsigned)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cflag |=  (unsigned)CS8;
    raw.c_oflag &= ~(unsigned)OPOST;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    _flux_update_winsize();

    if (alt_screen) {
        fputs("\x1b[?1049h", stdout);
    }

    fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);

    fputs("\x1b[?2004h", stdout);

    if (mouse) {
        fputs("\x1b[?1003h\x1b[?1015h\x1b[?1006h", stdout);
    }

    fflush(stdout);

    signal(SIGWINCH, _flux_sigwinch_handler);
    signal(SIGINT,   _flux_sigint_handler);
    signal(SIGTERM,  _flux_sigint_handler);
}

static void _flux_restore_term(int alt_screen, int mouse) {
    if (mouse) {
        fputs("\x1b[?1003l\x1b[?1015l\x1b[?1006l", stdout);
    }

    if (alt_screen) {
        fputs("\x1b[?1049l", stdout);
    }

    fputs("\x1b[?2004l", stdout);

    fputs("\x1b[?25h\x1b[0m", stdout);
    fflush(stdout);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &_flux_orig_termios);
}

/* -- Key parser -- */

static int _flux_parse_key(const char *buf, int len, FluxMsg *out) {
    const unsigned char *u = (const unsigned char *)buf;
    unsigned char b;

    memset(out, 0, sizeof(*out));
    out->type = MSG_KEY;

    if (len <= 0) {
        out->type = MSG_NONE;
        return 0;
    }

    b = u[0];

    if (b >= 1 && b <= 26 && b != '\r' && b != '\t' && b != '\n') {
        out->u.key.ctrl = 1;
        snprintf(out->u.key.key, sizeof(out->u.key.key),
                 "ctrl+%c", 'a' + b - 1);
        return 1;
    }

    if (b == '\r' || b == '\n') {
        snprintf(out->u.key.key, sizeof(out->u.key.key), "enter");
        return 1;
    }
    if (b == '\t') {
        snprintf(out->u.key.key, sizeof(out->u.key.key), "tab");
        return 1;
    }
    if (b == 127) {
        snprintf(out->u.key.key, sizeof(out->u.key.key), "backspace");
        return 1;
    }

    if (b == 27) {
        if (len == 1) {
            snprintf(out->u.key.key, sizeof(out->u.key.key), "esc");
            return 1;
        }

        if (len == 2 && u[1] >= 32) {
            out->u.key.alt = 1;
            snprintf(out->u.key.key, sizeof(out->u.key.key),
                     "alt+%c", (char)u[1]);
            return 2;
        }

        if (len >= 3 && u[1] == '[') {
            switch (u[2]) {
            case 'A':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "up");
                return 3;
            case 'B':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "down");
                return 3;
            case 'C':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "right");
                return 3;
            case 'D':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "left");
                return 3;
            case 'H':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "home");
                return 3;
            case 'F':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "end");
                return 3;
            case 'Z':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "shift+tab");
                return 3;
            default:
                break;
            }

            if (len >= 4 && u[3] == '~') {
                switch (u[2]) {
                case '2':
                    snprintf(out->u.key.key, sizeof(out->u.key.key), "insert");
                    return 4;
                case '3':
                    snprintf(out->u.key.key, sizeof(out->u.key.key), "delete");
                    return 4;
                case '5':
                    snprintf(out->u.key.key, sizeof(out->u.key.key), "pgup");
                    return 4;
                case '6':
                    snprintf(out->u.key.key, sizeof(out->u.key.key), "pgdown");
                    return 4;
                default:
                    break;
                }
            }

            if (len >= 5 && u[4] == '~') {
                int code = (u[2] - '0') * 10 + (u[3] - '0');
                const char *fname = NULL;
                switch (code) {
                case 15: fname = "f5";  break;
                case 17: fname = "f6";  break;
                case 18: fname = "f7";  break;
                case 19: fname = "f8";  break;
                case 20: fname = "f9";  break;
                case 21: fname = "f10"; break;
                case 23: fname = "f11"; break;
                case 24: fname = "f12"; break;
                default: break;
                }
                if (fname) {
                    snprintf(out->u.key.key, sizeof(out->u.key.key),
                             "%s", fname);
                    return 5;
                }
            }

            if (u[2] == '<') {
                int btn = 0;
                int mi;
                int consumed;

                for (mi = 3; mi < len &&
                     u[mi] >= '0' && u[mi] <= '9'; mi++) {
                    btn = btn * 10 + (u[mi] - '0');
                }

                consumed = mi;
                while (consumed < len &&
                       u[consumed] != 'M' && u[consumed] != 'm') {
                    consumed++;
                }
                if (consumed < len) {
                    consumed++;
                }

                if (btn == 64) {
                    snprintf(out->u.key.key, sizeof(out->u.key.key),
                             "scroll_up");
                    return consumed;
                }
                if (btn == 65) {
                    snprintf(out->u.key.key, sizeof(out->u.key.key),
                             "scroll_down");
                    return consumed;
                }

                out->type = MSG_NONE;
                return consumed;
            }

            {
                int ci;
                for (ci = 2; ci < len; ci++) {
                    if (u[ci] >= 0x40 && u[ci] <= 0x7E) {
                        return ci + 1;
                    }
                }
            }
            out->type = MSG_NONE;
            return len;
        }

        if (len >= 3 && u[1] == 'O') {
            switch (u[2]) {
            case 'P':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "f1");
                return 3;
            case 'Q':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "f2");
                return 3;
            case 'R':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "f3");
                return 3;
            case 'S':
                snprintf(out->u.key.key, sizeof(out->u.key.key), "f4");
                return 3;
            default:
                break;
            }
        }

        out->type = MSG_NONE;
        return len;
    }

    if (b < 0x80) {
        out->u.key.key[0] = (char)b;
        out->u.key.key[1] = '\0';
        out->u.key.rune = (int)b;
        return 1;
    }

    {
        int seqlen;
        if (b < 0xC0) {
            seqlen = 1;
        } else if (b < 0xE0) {
            seqlen = 2;
        } else if (b < 0xF0) {
            seqlen = 3;
        } else {
            seqlen = 4;
        }
        if (seqlen > len) {
            seqlen = len;
        }
        if (seqlen > (int)(sizeof(out->u.key.key)) - 1) {
            seqlen = (int)(sizeof(out->u.key.key)) - 1;
        }
        memcpy(out->u.key.key, buf, (size_t)seqlen);
        out->u.key.key[seqlen] = '\0';
        out->u.key.rune = (int)b;
        return seqlen;
    }
}

/* -- Key matching -- */

int flux_key_is(FluxMsg msg, const char *name) {
    return msg.type == MSG_KEY && strcmp(msg.u.key.key, name) == 0;
}

/* -- Built-in commands -- */

static FluxMsg _flux_quit_fn(void *ctx) {
    FluxMsg m;
    (void)ctx;
    memset(&m, 0, sizeof(m));
    m.type = MSG_QUIT;
    return m;
}

FluxCmd flux_cmd_quit(void) {
    FluxCmd c;
    c.fn = _flux_quit_fn;
    c.ctx = NULL;
    return c;
}

typedef struct {
    int ms;
} _FluxTickCtx;

static FluxMsg _flux_tick_fn(void *ctx) {
    _FluxTickCtx *t = (_FluxTickCtx *)ctx;
    struct timespec ts;
    FluxMsg m;

    ts.tv_sec  = t->ms / 1000;
    ts.tv_nsec = (t->ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);

    free(ctx);
    memset(&m, 0, sizeof(m));
    m.type = MSG_TICK;
    return m;
}

FluxCmd flux_tick(int ms) {
    _FluxTickCtx *c = (_FluxTickCtx *)malloc(sizeof(*c));
    FluxCmd cmd;
    if (!c) {
        return FLUX_CMD_NONE;
    }
    c->ms = ms;
    cmd.fn  = _flux_tick_fn;
    cmd.ctx = c;
    return cmd;
}

typedef struct {
    int   id;
    void *data;
} _FluxCustomCtx;

static FluxMsg _flux_custom_fn(void *ctx) {
    _FluxCustomCtx *c = (_FluxCustomCtx *)ctx;
    FluxMsg m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_CUSTOM;
    m.u.custom.id   = c->id;
    m.u.custom.data = c->data;
    free(ctx);
    return m;
}

FluxCmd flux_cmd_custom(int id, void *data) {
    _FluxCustomCtx *c = (_FluxCustomCtx *)malloc(sizeof(*c));
    FluxCmd cmd;
    if (!c) {
        return FLUX_CMD_NONE;
    }
    c->id   = id;
    c->data = data;
    cmd.fn  = _flux_custom_fn;
    cmd.ctx = c;
    return cmd;
}

/* -- Command dispatch (threaded) -- */

typedef struct {
    FluxCmd cmd;
    int     pipe_w;
} _FluxCmdThread;

static void *_flux_cmd_runner(void *arg) {
    _FluxCmdThread *t = (_FluxCmdThread *)arg;
    FluxMsg r = t->cmd.fn(t->cmd.ctx);
    ssize_t w = write(t->pipe_w, &r, sizeof(r));
    (void)w;
    free(t);
    return NULL;
}

static void _flux_dispatch_cmd(FluxCmd cmd, int pipe_w) {
    _FluxCmdThread *t;
    pthread_t tid;
    pthread_attr_t a;

    if (!cmd.fn) {
        return;
    }

    t = (_FluxCmdThread *)malloc(sizeof(*t));
    if (!t) {
        return;
    }
    t->cmd    = cmd;
    t->pipe_w = pipe_w;

    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &a, _flux_cmd_runner, t);
    pthread_attr_destroy(&a);
}

/* -- Line-level diff renderer (backward-compatible path) -- */

static char _flux_prev_frame[FLUX_RENDER_BUF];
static char _flux_cur_frame[FLUX_RENDER_BUF];

static int _flux_split_lines(char *s, char **lines, int maxlines) {
    int n = 0;
    char *p = s;
    char *nl;

    while (*p && n < maxlines) {
        lines[n++] = p;
        nl = strchr(p, '\n');
        if (!nl) {
            break;
        }
        *nl = '\0';
        p = nl + 1;
    }
    return n;
}

static void _flux_render_linediff(FluxModel *model, int force) {
    static char cur_copy[FLUX_RENDER_BUF];
    static char prev_copy[FLUX_RENDER_BUF];
    static char *clines[FLUX_MAX_LINES];
    static char *plines[FLUX_MAX_LINES];
    static char obuf[FLUX_RENDER_BUF * 2];
    int olen = 0;
    int nc, np;
    int rows_now;
    int i, maxlines;

    memset(_flux_cur_frame, 0, FLUX_RENDER_BUF);
    model->view(model, _flux_cur_frame, FLUX_RENDER_BUF - 1);

    if (!force && strcmp(_flux_prev_frame, _flux_cur_frame) == 0) {
        return;
    }

    memcpy(cur_copy, _flux_cur_frame, FLUX_RENDER_BUF);
    nc = _flux_split_lines(cur_copy, clines, FLUX_MAX_LINES);

    memcpy(prev_copy, _flux_prev_frame, FLUX_RENDER_BUF);
    np = _flux_split_lines(prev_copy, plines, FLUX_MAX_LINES);

    rows_now = (int)_flux_rows;
    maxlines = nc < rows_now ? nc : rows_now;

    /* Local macros for line-diff output buffering */
    #define LD_OFLUSH() do { \
        if (olen > 0) { \
            fwrite(obuf, 1, (size_t)olen, stdout); \
            olen = 0; \
        } \
    } while (0)

    #define LD_OCAT(s) do { \
        int _sl = (int)strlen(s); \
        if (olen + _sl >= (int)sizeof(obuf)) { LD_OFLUSH(); } \
        memcpy(obuf + olen, s, (size_t)_sl); \
        olen += _sl; \
    } while (0)

    #define LD_OCATF(...) do { \
        char _tmp[64]; \
        snprintf(_tmp, sizeof(_tmp), __VA_ARGS__); \
        LD_OCAT(_tmp); \
    } while (0)

    LD_OCAT("\x1b[?25l");

    for (i = 0; i < maxlines; i++) {
        if (!force && i < np && strcmp(clines[i], plines[i]) == 0) {
            continue;
        }

        LD_OCATF("\x1b[%d;1H", i + 1);

        if (i + 1 == rows_now) {
            LD_OCAT("\x1b[?7l");
            LD_OCAT(clines[i]);
            LD_OCAT("\x1b[0m");
            LD_OCAT("\x1b[?7h");
        } else {
            LD_OCAT(clines[i]);
            LD_OCAT("\x1b[0m\x1b[K");
        }
    }

    for (i = maxlines; i < np && i < rows_now; i++) {
        LD_OCATF("\x1b[%d;1H", i + 1);
        if (i + 1 == rows_now) {
            LD_OCAT("\x1b[?7l");
            LD_OCAT("\x1b[0m");
            LD_OCAT("\x1b[?7h");
        } else {
            LD_OCAT("\x1b[0m\x1b[K");
        }
    }

    LD_OCAT("\x1b[1;1H");

    LD_OFLUSH();
    fflush(stdout);

    #undef LD_OFLUSH
    #undef LD_OCAT
    #undef LD_OCATF

    memcpy(_flux_prev_frame, _flux_cur_frame, FLUX_RENDER_BUF);
}

/* -- Main event loop -- */

void flux_run(FluxProgram *p) {
    int pipefd[2];
    int fl;
    unsigned char ibuf[64];
    struct timeval tv;
    long frame_us;
    FluxCmd ic;

    if (p->fps <= 0) {
        p->fps = 30;
    }
    if (!p->alt_screen && !p->mouse && !p->fps) {
        p->alt_screen = 1;
    }

    if (pipe(pipefd) != 0) {
        perror("pipe");
        return;
    }
    _flux_pipe_r = pipefd[0];
    _flux_pipe_w = pipefd[1];

    fl = fcntl(_flux_pipe_r, F_GETFL, 0);
    fcntl(_flux_pipe_r, F_SETFL, fl | O_NONBLOCK);

    _flux_setup_term(p->alt_screen, p->mouse);

    _flux_running = 1;
    memset(_flux_prev_frame, 0, FLUX_RENDER_BUF);

    ic = p->model.init(&p->model);
    _flux_render_linediff(&p->model, 1);
    _flux_dispatch_cmd(ic, _flux_pipe_w);

    frame_us = 1000000L / p->fps;

    while (_flux_running) {
        int need_force = 0;
        int ret, mfd;
        fd_set fds;

        if (_flux_winch) {
            FluxMsg wm;
            FluxCmd c;

            _flux_winch = 0;
            _flux_update_winsize();

            memset(&wm, 0, sizeof(wm));
            wm.type = MSG_WINSIZE;
            wm.u.winsize.cols = (int)_flux_cols;
            wm.u.winsize.rows = (int)_flux_rows;

            c = p->model.update(&p->model, wm);
            _flux_dispatch_cmd(c, _flux_pipe_w);
            need_force = 1;
        }

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(_flux_pipe_r, &fds);
        mfd = _flux_pipe_r > STDIN_FILENO ? _flux_pipe_r : STDIN_FILENO;

        tv.tv_sec  = 0;
        tv.tv_usec = (long)frame_us;

        ret = select(mfd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            int n = (int)read(STDIN_FILENO, ibuf, sizeof(ibuf));
            if (n > 0) {
                if (n >= 6 && ibuf[0] == '\x1b' && ibuf[1] == '['
                    && ibuf[2] == '2' && ibuf[3] == '0'
                    && ibuf[4] == '0' && ibuf[5] == '~') {
                    FluxMsg pm;
                    FluxCmd c;
                    int pi = 0;
                    int rest;

                    memset(&pm, 0, sizeof(pm));
                    pm.type = MSG_PASTE;

                    rest = n - 6;
                    if (rest > 0 && rest < FLUX_PASTE_MAX) {
                        memcpy(pm.u.paste.text, ibuf + 6, (size_t)rest);
                        pi = rest;
                    }

                    while (pi < FLUX_PASTE_MAX - 1) {
                        char *endm;
                        int nr;

                        if (pi >= 6) {
                            endm = (char *)memmem(pm.u.paste.text,
                                                   (size_t)pi,
                                                   "\x1b[201~", 6);
                            if (endm) {
                                pi = (int)(endm - pm.u.paste.text);
                                break;
                            }
                        }

                        nr = (int)read(STDIN_FILENO,
                                       pm.u.paste.text + pi,
                                       (size_t)(FLUX_PASTE_MAX - 1 - pi));
                        if (nr <= 0) {
                            break;
                        }
                        pi += nr;
                    }

                    pm.u.paste.text[pi] = '\0';
                    pm.u.paste.len = pi;

                    while (pm.u.paste.len > 0
                           && pm.u.paste.text[pm.u.paste.len - 1] == '\n') {
                        pm.u.paste.len--;
                        pm.u.paste.text[pm.u.paste.len] = '\0';
                    }

                    if (pm.u.paste.len > 0) {
                        c = p->model.update(&p->model, pm);
                        if (c.fn == _flux_quit_fn) {
                            goto done;
                        }
                        _flux_dispatch_cmd(c, _flux_pipe_w);
                    }
                } else {
                    int off = 0;
                    while (off < n) {
                        FluxMsg km;
                        FluxCmd c;
                        int consumed;

                        consumed = _flux_parse_key(
                            (const char *)(ibuf + off),
                            n - off,
                            &km);

                        if (km.type == MSG_QUIT) {
                            goto done;
                        }
                        if (km.type == MSG_NONE) {
                            if (consumed <= 0) {
                                break;
                            }
                            off += consumed;
                            continue;
                        }

                        c = p->model.update(&p->model, km);
                        if (c.fn == _flux_quit_fn) {
                            goto done;
                        }
                        _flux_dispatch_cmd(c, _flux_pipe_w);
                        off += consumed;
                    }
                }
            }
        }

        if (ret > 0 && FD_ISSET(_flux_pipe_r, &fds)) {
            FluxMsg pm;
            while (read(_flux_pipe_r, &pm, sizeof(pm))
                   == (ssize_t)sizeof(pm)) {
                FluxCmd c;

                if (pm.type == MSG_QUIT) {
                    goto done;
                }

                c = p->model.update(&p->model, pm);
                if (c.fn == _flux_quit_fn) {
                    goto done;
                }
                _flux_dispatch_cmd(c, _flux_pipe_w);
            }
        }

        _flux_render_linediff(&p->model, need_force);
    }

done:
    _flux_running = 0;
    _flux_restore_term(p->alt_screen, p->mouse);
    if (p->model.free) {
        p->model.free(&p->model);
    }
    close(_flux_pipe_r);
    close(_flux_pipe_w);
    _flux_pipe_r = -1;
    _flux_pipe_w = -1;
}

#endif /* FLUX_IMPL && !FLUX_IMPL_DONE */
