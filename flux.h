#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
/*
 * flux.h  —  Elm Architecture TUI framework for C99
 * ========================================================
 * Single-header, zero-dependency (libc + pthreads only).
 *
 * USAGE  (define in exactly ONE translation unit):
 *   #define FLUX_IMPL
 *   #include "flux.h"
 *
 * BUILD:
 *   gcc -O2 -std=c99 -o myapp myapp.c -lpthread
 *
 * ARCHITECTURE  (Elm Architecture):
 *   Init()   → FluxCmd          run once at startup
 *   Update() → (Model, Msg) → FluxCmd   pure state transition
 *   View()   → Model → string          render into buf
 *
 * LICENSE: MIT
 */
#ifndef FLUX_H
#define FLUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* ─────────────────────────────────────────────────────────────
 * MSG
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    MSG_NONE = 0,
    MSG_KEY,
    MSG_WINSIZE,
    MSG_QUIT,
    MSG_TICK,
    MSG_CUSTOM,
} FluxMsgType;

typedef struct {
    FluxMsgType type;
    union {
        struct { char key[16]; int rune; int ctrl; int alt; } key;
        struct { int cols, rows; }                            winsize;
        struct { int id; void *data; }                        custom;
    } u;
} FluxMsg;

/* ─────────────────────────────────────────────────────────────
 * CMD
 * ───────────────────────────────────────────────────────────── */
typedef FluxMsg (*FluxCmdFn)(void *ctx);
typedef struct { FluxCmdFn fn; void *ctx; } FluxCmd;
#define FLUX_CMD_NONE ((FluxCmd){NULL,NULL})

/* ─────────────────────────────────────────────────────────────
 * MODEL
 * ───────────────────────────────────────────────────────────── */
typedef struct FluxModel FluxModel;
struct FluxModel {
    void  *state;
    FluxCmd (*init)  (FluxModel *m);
    FluxCmd (*update)(FluxModel *m, FluxMsg msg);
    void  (*view)  (FluxModel *m, char *buf, int bufsz);
    void  (*free)  (FluxModel *m);   /* optional */
};

/* ─────────────────────────────────────────────────────────────
 * PROGRAM
 * ───────────────────────────────────────────────────────────── */
typedef struct {
    FluxModel model;
    int alt_screen;   /* default 1 */
    int mouse;
    int fps;          /* default 30 */
} FluxProgram;

void flux_run(FluxProgram *p);

/* ─────────────────────────────────────────────────────────────
 * API
 * ───────────────────────────────────────────────────────────── */
int   flux_key_is(FluxMsg msg, const char *name);
FluxCmd flux_cmd_quit(void);
FluxCmd flux_tick(int ms);
FluxCmd flux_cmd_custom(int id, void *data);
int   flux_cols(void);
int   flux_rows(void);

#define FLUX_CMD_QUIT  flux_cmd_quit()
#define FLUX_TICK(ms)  flux_tick(ms)

/* ─────────────────────────────────────────────────────────────
 * ANSI helpers
 * ───────────────────────────────────────────────────────────── */
#define FLUX_RESET      "\x1b[0m"
#define FLUX_BOLD       "\x1b[1m"
#define FLUX_DIM        "\x1b[2m"
#define FLUX_ITALIC     "\x1b[3m"
#define FLUX_UNDERLINE  "\x1b[4m"
#define FLUX_STRIKE     "\x1b[9m"
#define FLUX_FG(r,g,b)  "\x1b[38;2;" #r ";" #g ";" #b "m"
#define FLUX_BG(r,g,b)  "\x1b[48;2;" #r ";" #g ";" #b "m"

char *flux_fg(char *buf, int r, int g, int b);
char *flux_bg(char *buf, int r, int g, int b);

/* ─────────────────────────────────────────────────────────────
 * LIPGLOSS-LITE  (box drawing, padding, layout)
 * ───────────────────────────────────────────────────────────── */

/* Counts terminal columns a string occupies:
 *   - skips ANSI CSI sequences (\x1b[...m  and  \x1b[...~  etc.)
 *   - counts UTF-8 multi-byte as 1 column each               */
int flux_strwidth(const char *s);

/* Write src into dst, padded/truncated to exactly 'width' cols */
void flux_pad(char *dst, int dstsz, const char *src, int width);

typedef struct {
    const char *tl, *tr, *bl, *br;
    const char *h,  *v;
    const char *ml, *mr, *mt, *mb, *c;
} FluxBorder;

extern const FluxBorder FLUX_BORDER_ROUNDED;
extern const FluxBorder FLUX_BORDER_SHARP;
extern const FluxBorder FLUX_BORDER_DOUBLE;
extern const FluxBorder FLUX_BORDER_THICK;
extern const FluxBorder FLUX_BORDER_NONE;

/*
 * Render a box around multi-line content string.
 *
 *  out_buf   : destination buffer (must be large enough)
 *  out_sz    : size of out_buf
 *  content   : newline-separated lines (may contain ANSI escapes)
 *  border    : one of the FLUX_BORDER_* presets
 *  inner_w   : inner content width in columns; 0 = auto
 *  border_clr: ANSI colour escape for border chars, or NULL
 *  content_bg: ANSI BG escape for content area, or NULL
 */
void flux_box(char *out_buf, int out_sz,
            const char *content,
            const FluxBorder *border,
            int inner_w,
            const char *border_clr,
            const char *content_bg);

/* ── string builder helper ────────────────────────────────────
 * Lightweight growable buffer for building View strings.     */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} FluxSB;

void flux_sb_init   (FluxSB *sb, char *backing, int cap); /* use stack buf */
void flux_sb_append(FluxSB *sb, const char *s);
void flux_sb_appendf(FluxSB *sb, const char *fmt, ...);
void flux_sb_nl     (FluxSB *sb);                          /* append "\n"  */
const char *flux_sb_str(FluxSB *sb);

/* ─────────────────────────────────────────────────────────────
 * COMPONENTS — reusable UI widgets
 * ───────────────────────────────────────────────────────────── */

/* ── Layout helpers ─────────────────────────────────────────── */

/* Horizontal divider ─────── */
void flux_divider(FluxSB *sb, int width, const char *color);

/* Progress bar ████░░░░  (value 0.0–1.0) */
void flux_bar(FluxSB *sb, float value, int width,
            const char *fill_clr, const char *empty_clr);

/* Sparkline from float ring buffer  ▁▂▃▄▅▆▇█ */
void flux_sparkline(FluxSB *sb, float *ring, int len, int head,
                  const char *color);

/* Render N multi-line strings side by side (max 8 panels).
 * gap defaults to "  " if NULL.  */
void flux_hbox(FluxSB *sb, const char **panels, const int *widths,
             int count, const char *gap);

/* Count newline-separated lines in a string */
int flux_count_lines(const char *s);

/* Pad content to target line count by appending blank lines.
 * Call on content BEFORE flux_box to equalize box heights. */
void flux_pad_lines(char *buf, int bufsz, int target_lines);

/* ── FluxSpinner — animated loading indicator ─────────────────── */
extern const char *FLUX_SPINNER_DOT[];
extern const int   FLUX_SPINNER_DOT_N;
extern const char *FLUX_SPINNER_LINE[];
extern const int   FLUX_SPINNER_LINE_N;

typedef struct {
    const char **frames;
    int frame_count, current;
    const char *label;
} FluxSpinner;

void flux_spinner_init(FluxSpinner *s, const char **frames, int nframes,
                     const char *label);
void flux_spinner_tick(FluxSpinner *s);
void flux_spinner_render(FluxSpinner *s, FluxSB *sb, const char *color);

/* ── FluxInput — single-line text input ───────────────────────── */
#define FLUX_INPUT_MAX 255

typedef struct {
    char buf[FLUX_INPUT_MAX + 1];
    int  cursor, len;
    const char *placeholder;
    int  submitted;
} FluxInput;

void flux_input_init(FluxInput *inp, const char *placeholder);
void flux_input_clear(FluxInput *inp);
int  flux_input_update(FluxInput *inp, FluxMsg msg);
void flux_input_render(FluxInput *inp, FluxSB *sb, int width,
                     const char *color, const char *cursor_clr);

/* ── FluxConfirm — yes/no confirmation dialog ─────────────────── */
typedef struct {
    const char *title, *message;
    const char *yes_label, *no_label;
    int selected;   /* 0=no 1=yes */
    int answered;
    int result;     /* valid when answered: 0=denied 1=confirmed */
} FluxConfirm;

void flux_confirm_init(FluxConfirm *c, const char *title, const char *msg,
                     const char *yes_label, const char *no_label);
int  flux_confirm_update(FluxConfirm *c, FluxMsg msg);
void flux_confirm_render(FluxConfirm *c, FluxSB *sb, int width,
                       const char *border_clr,
                       const char *yes_clr, const char *no_clr);

/* ── FluxList — scrollable list with cursor ───────────────────── */
typedef void (*FluxListItemFn)(FluxSB *sb, int index, int selected,
                             int width, void *ctx);
typedef struct {
    int cursor, scroll, count, visible;
    FluxListItemFn render_item;
} FluxList;

void flux_list_init(FluxList *l, int count, int visible, FluxListItemFn render);
int  flux_list_update(FluxList *l, FluxMsg msg);
void flux_list_render(FluxList *l, FluxSB *sb, int width, void *ctx);

/* ── FluxTabs — tab bar with switching ────────────────────────── */
typedef struct {
    const char **icons, **labels;
    int count, active;
} FluxTabs;

void flux_tabs_init(FluxTabs *t, const char **icons, const char **labels,
                  int count);
int  flux_tabs_update(FluxTabs *t, FluxMsg msg);
void flux_tabs_render(FluxTabs *t, FluxSB *sb,
                    const char *active_clr, const char *dim_clr,
                    const char *hint);

/* ── FluxTable — scrollable table with headers ────────────────── */
typedef void (*FluxTableCellFn)(FluxSB *sb, int row, int col,
                              int width, void *ctx);
typedef struct {
    const char **headers;
    const int  *widths;
    int col_count, row_count, visible, scroll, follow;
    FluxTableCellFn render_cell;
} FluxTable;

void flux_table_init(FluxTable *t, const char **headers, const int *widths,
                   int cols, int visible, FluxTableCellFn render);
void flux_table_set_rows(FluxTable *t, int rows);
int  flux_table_update(FluxTable *t, FluxMsg msg);
void flux_table_render(FluxTable *t, FluxSB *sb, void *ctx,
                     const char *header_clr, const char *border_clr);

/* ── FluxKanban — multi-column card board ─────────────────────── */
#define FLUX_KB_TITLE_MAX  40
#define FLUX_KB_DESC_MAX   80
#define FLUX_KB_MAX_CARDS  32
#define FLUX_KB_MAX_COLS   8

typedef struct { char title[FLUX_KB_TITLE_MAX+1]; char desc[FLUX_KB_DESC_MAX+1]; } FluxKbCard;
typedef struct { char name[32]; FluxKbCard cards[FLUX_KB_MAX_CARDS]; int count; } FluxKbCol;

#define FLUX_KB_BROWSE  0
#define FLUX_KB_EDIT    1
#define FLUX_KB_F_TITLE   0
#define FLUX_KB_F_DESC    1
#define FLUX_KB_F_OK      2
#define FLUX_KB_F_CANCEL  3

typedef struct {
    FluxKbCol cols[FLUX_KB_MAX_COLS];
    int ncols, cur_col, cur_row, col_width;
    int visible;  /* max visible cards per column (0 = auto from terminal) */
    int scroll[FLUX_KB_MAX_COLS]; /* per-column scroll offset */
    int mode, edit_col, edit_row, edit_focus;
    int grabbed;
    FluxInput edit_title, edit_desc;
    int dirty;
} FluxKanban;

void flux_kanban_init(FluxKanban *kb, int ncols, const char **col_names, int col_width);
int  flux_kanban_update(FluxKanban *kb, FluxMsg msg);
void flux_kanban_render(FluxKanban *kb, FluxSB *sb, const char **col_colors, const char *hint);
int  flux_kanban_add(FluxKanban *kb, int col, const char *title, const char *desc);
void flux_kanban_del(FluxKanban *kb, int col, int row);
int  flux_kanban_move(FluxKanban *kb, int col, int row, int to_col);
int  flux_kanban_dirty(FluxKanban *kb);

/* ─────────────────────────────────────────────────────────────
 * IMPLEMENTATION
 * ───────────────────────────────────────────────────────────── */
#ifdef FLUX_IMPL

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

/* ── globals ─────────────────────────────────────────────────── */
static struct termios _flux_orig_termios;
static volatile int   _flux_cols    = 80;
static volatile int   _flux_rows    = 24;
static volatile int   _flux_running = 0;
static volatile int   _flux_winch   = 0;
static int            _flux_pipe_r  = -1;
static int            _flux_pipe_w  = -1;

int flux_cols(void) { return _flux_cols; }
int flux_rows(void) { return _flux_rows; }

/* ── terminal ────────────────────────────────────────────────── */
static void _flux_update_winsize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        _flux_cols = ws.ws_col > 0 ? ws.ws_col : 80;
        _flux_rows = ws.ws_row > 0 ? ws.ws_row : 24;
    }
}

static void _flux_raw_mode(void) {
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
}

static void _flux_restore_term(int alt, int mouse) {
    if (mouse) fputs("\x1b[?1003l\x1b[?1015l\x1b[?1006l", stdout);
    if (alt)   fputs("\x1b[?1049l", stdout);
    fputs("\x1b[?25h\x1b[0m", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &_flux_orig_termios);
}

static void _flux_init_term(int alt, int mouse) {
    _flux_raw_mode();
    _flux_update_winsize();
    if (alt) fputs("\x1b[?1049h", stdout);
    fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
    if (mouse) fputs("\x1b[?1003h\x1b[?1015h\x1b[?1006h", stdout);
    fflush(stdout);
}

/* ── signals ─────────────────────────────────────────────────── */
static void _flux_sigwinch(int s) { (void)s; _flux_winch = 1; }
static void _flux_sigint  (int s) {
    (void)s;
    FluxMsg m = { .type = MSG_QUIT };
    ssize_t w = write(_flux_pipe_w, &m, sizeof m);
    (void)w;
}

/* ── key parser ──────────────────────────────────────────────── */
static FluxMsg _flux_parse_key(const unsigned char *buf, int n) {
    FluxMsg msg; memset(&msg, 0, sizeof msg);
    msg.type = MSG_KEY;
    if (n <= 0) { msg.type = MSG_NONE; return msg; }
    unsigned char b = buf[0];

    /* Ctrl+letter (1–26, excluding \r \t) */
    if (b >= 1 && b <= 26 && b != '\r' && b != '\t' && b != '\n') {
        msg.u.key.ctrl = 1;
        snprintf(msg.u.key.key, sizeof msg.u.key.key, "ctrl+%c", 'a'+b-1);
        return msg;
    }
    if (b == '\r' || b == '\n') { strcpy(msg.u.key.key, "enter");     return msg; }
    if (b == '\t')               { strcpy(msg.u.key.key, "tab");       return msg; }
    if (b == 127)                { strcpy(msg.u.key.key, "backspace"); return msg; }
    if (b == 27 && n == 1)       { strcpy(msg.u.key.key, "esc");       return msg; }

    if (b == 27 && n >= 2) {
        /* Alt+char */
        if (n == 2 && buf[1] >= 32) {
            msg.u.key.alt = 1;
            snprintf(msg.u.key.key, sizeof msg.u.key.key, "alt+%c", buf[1]);
            return msg;
        }
        if (n >= 3 && buf[1] == '[') {
            switch (buf[2]) {
                case 'A': strcpy(msg.u.key.key, "up");    return msg;
                case 'B': strcpy(msg.u.key.key, "down");  return msg;
                case 'C': strcpy(msg.u.key.key, "right"); return msg;
                case 'D': strcpy(msg.u.key.key, "left");  return msg;
                case 'H': strcpy(msg.u.key.key, "home");  return msg;
                case 'F': strcpy(msg.u.key.key, "end");   return msg;
                case '2': if (n>=4&&buf[3]=='~') { strcpy(msg.u.key.key,"insert"); return msg; } break;
                case '3': if (n>=4&&buf[3]=='~') { strcpy(msg.u.key.key,"delete"); return msg; } break;
                case '5': if (n>=4&&buf[3]=='~') { strcpy(msg.u.key.key,"pgup");   return msg; } break;
                case '6': if (n>=4&&buf[3]=='~') { strcpy(msg.u.key.key,"pgdown"); return msg; } break;
                case 'Z': strcpy(msg.u.key.key, "shift+tab"); return msg;
                case '<': msg.type = MSG_NONE; return msg; /* mouse */
            }
        }
        if (n >= 3 && buf[1] == 'O') {
            switch (buf[2]) {
                case 'P': strcpy(msg.u.key.key,"f1"); return msg;
                case 'Q': strcpy(msg.u.key.key,"f2"); return msg;
                case 'R': strcpy(msg.u.key.key,"f3"); return msg;
                case 'S': strcpy(msg.u.key.key,"f4"); return msg;
            }
        }
        msg.type = MSG_NONE; return msg;
    }

    /* printable / UTF-8 */
    int len = n < (int)(sizeof msg.u.key.key)-1 ? n : (int)(sizeof msg.u.key.key)-1;
    memcpy(msg.u.key.key, buf, len);
    msg.u.key.key[len] = '\0';
    msg.u.key.rune = (int)b;
    return msg;
}

int flux_key_is(FluxMsg msg, const char *name) {
    return msg.type == MSG_KEY && strcmp(msg.u.key.key, name) == 0;
}

/* ── built-in Cmds ───────────────────────────────────────────── */
static FluxMsg _flux_quit_fn(void *ctx) {
    (void)ctx; FluxMsg m; memset(&m,0,sizeof m); m.type = MSG_QUIT; return m;
}
FluxCmd flux_cmd_quit(void) { return (FluxCmd){ _flux_quit_fn, NULL }; }

typedef struct { int ms; } _FluxTickCtx;
static FluxMsg _flux_tick_fn(void *ctx) {
    _FluxTickCtx *t = (_FluxTickCtx*)ctx;
    struct timespec ts = { t->ms/1000, (t->ms%1000)*1000000L };
    nanosleep(&ts, NULL);
    free(ctx);
    FluxMsg m; memset(&m,0,sizeof m); m.type = MSG_TICK; return m;
}
FluxCmd flux_tick(int ms) {
    _FluxTickCtx *c = malloc(sizeof *c); c->ms = ms;
    return (FluxCmd){ _flux_tick_fn, c };
}

typedef struct { int id; void *data; } _FluxCustomCtx;
static FluxMsg _flux_custom_fn(void *ctx) {
    _FluxCustomCtx *c = (_FluxCustomCtx*)ctx;
    FluxMsg m; memset(&m,0,sizeof m); m.type = MSG_CUSTOM;
    m.u.custom.id = c->id; m.u.custom.data = c->data;
    free(ctx); return m;
}
FluxCmd flux_cmd_custom(int id, void *data) {
    _FluxCustomCtx *c = malloc(sizeof *c); c->id=id; c->data=data;
    return (FluxCmd){ _flux_custom_fn, c };
}

/* ── cmd dispatch ────────────────────────────────────────────── */
typedef struct { FluxCmd cmd; int pipe_w; } _FluxCmdThread;
static void *_flux_cmd_runner(void *arg) {
    _FluxCmdThread *t = (_FluxCmdThread*)arg;
    FluxMsg r = t->cmd.fn(t->cmd.ctx);
    ssize_t w = write(t->pipe_w, &r, sizeof r); (void)w;
    free(t); return NULL;
}
static void _flux_dispatch_cmd(FluxCmd cmd, int pipe_w) {
    if (!cmd.fn) return;
    _FluxCmdThread *t = malloc(sizeof *t); t->cmd=cmd; t->pipe_w=pipe_w;
    pthread_t tid; pthread_attr_t a;
    pthread_attr_init(&a); pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED);
    pthread_create(&tid,&a,_flux_cmd_runner,t); pthread_attr_destroy(&a);
}

/* ── line-by-line renderer ───────────────────────────────────────
 *
 * Strategy: build the new frame in a scratch buffer, then write
 * it line-by-line using absolute cursor positioning.  This avoids
 * full-screen clears (\x1b[2J) which cause visible flash.
 *
 * We keep one "previous frame" copy.  For each line:
 *   • if unchanged → skip (no write)
 *   • if changed   → position cursor, erase-to-EOL, write new content
 * ───────────────────────────────────────────────────────────────── */
#define FLUX_RENDER_BUF  131072
#define FLUX_MAX_LINES   512

static char _flux_prev[FLUX_RENDER_BUF];
static char _flux_cur [FLUX_RENDER_BUF];

/* Split s in-place at '\n', fill lines[] with pointers, return count */
static int _flux_split_lines(char *s, char **lines, int maxlines) {
    int n = 0;
    char *p = s;
    while (*p && n < maxlines) {
        lines[n++] = p;
        char *nl = strchr(p, '\n');
        if (!nl) break;
        *nl = '\0';
        p = nl+1;
    }
    return n;
}

static void _flux_render(FluxModel *model, int force) {
    /* Build new frame */
    memset(_flux_cur, 0, FLUX_RENDER_BUF);
    model->view(model, _flux_cur, FLUX_RENDER_BUF-1);

    if (!force && strcmp(_flux_prev, _flux_cur) == 0) return;

    /* Split both frames into lines */
    static char prev_copy[FLUX_RENDER_BUF], cur_copy[FLUX_RENDER_BUF];
    memcpy(prev_copy, _flux_prev, FLUX_RENDER_BUF);
    memcpy(cur_copy,  _flux_cur,  FLUX_RENDER_BUF);

    static char *plines[FLUX_MAX_LINES], *clines[FLUX_MAX_LINES];
    int np = _flux_split_lines(prev_copy, plines, FLUX_MAX_LINES);
    int nc = _flux_split_lines(cur_copy,  clines, FLUX_MAX_LINES);
    int nmax = nc > np ? nc : np;

    /* Write changes line by line */
    /* Use a large output buffer to minimise write() syscalls */
    static char obuf[FLUX_RENDER_BUF*2];
    int olen = 0;
    #define OFLUSH() do { fwrite(obuf,1,olen,stdout); olen=0; } while(0)
    #define OCAT(s)  do { \
        int _l=(int)strlen(s); \
        if(olen+_l>=(int)sizeof(obuf)) OFLUSH(); \
        memcpy(obuf+olen,s,_l); olen+=_l; \
    } while(0)
    #define OCATF(...)  do { \
        char _tmp[64]; snprintf(_tmp,sizeof _tmp,__VA_ARGS__); OCAT(_tmp); \
    } while(0)

    OCAT("\x1b[?25l"); /* ensure cursor stays hidden */

    for (int i = 0; i < nmax; i++) {
        const char *prev_l = (i < np) ? plines[i] : "";
        const char *cur_l  = (i < nc) ? clines[i] : "";
        if (!force && strcmp(prev_l, cur_l) == 0) continue;
        /* position: row i+1, col 1; write content first, then erase rest */
        OCATF("\x1b[%d;1H", i+1);
        OCAT(cur_l);
        OCAT("\x1b[0m\x1b[K");
    }

    /* If new frame has fewer lines, blank the rest */
    for (int i = nc; i < np; i++) {
        OCATF("\x1b[%d;1H\x1b[0m\x1b[K", i+1);
    }

    /* Park cursor at fixed position to prevent visible jumps */
    OCATF("\x1b[%d;1H", nc > 0 ? nc + 1 : 1);

    OFLUSH();
    fflush(stdout);

    #undef OFLUSH
    #undef OCAT
    #undef OCATF

    memcpy(_flux_prev, _flux_cur, FLUX_RENDER_BUF);
}

/* ── event loop ──────────────────────────────────────────────── */
void flux_run(FluxProgram *p) {
    if (p->fps  <= 0) p->fps  = 30;
    if (p->alt_screen == 0 && !p->mouse && !p->fps) p->alt_screen = 1;

    int pipefd[2];
    if (pipe(pipefd)) { perror("pipe"); return; }
    _flux_pipe_r = pipefd[0]; _flux_pipe_w = pipefd[1];
    int fl = fcntl(_flux_pipe_r, F_GETFL, 0);
    fcntl(_flux_pipe_r, F_SETFL, fl | O_NONBLOCK);

    _flux_init_term(p->alt_screen, p->mouse);
    _flux_update_winsize();

    signal(SIGWINCH, _flux_sigwinch);
    signal(SIGINT,   _flux_sigint);
    signal(SIGTERM,  _flux_sigint);

    _flux_running = 1;
    memset(_flux_prev, 0, FLUX_RENDER_BUF);

    FluxCmd ic = p->model.init(&p->model);
    _flux_render(&p->model, 1);
    _flux_dispatch_cmd(ic, _flux_pipe_w);

    unsigned char ibuf[64];
    struct timeval tv;
    long frame_us = 1000000L / p->fps;

    while (_flux_running) {
        int need_force = 0;
        if (_flux_winch) {
            _flux_winch = 0; _flux_update_winsize();
            FluxMsg wm; memset(&wm,0,sizeof wm);
            wm.type = MSG_WINSIZE; wm.u.winsize.cols=_flux_cols; wm.u.winsize.rows=_flux_rows;
            FluxCmd c = p->model.update(&p->model, wm);
            _flux_dispatch_cmd(c, _flux_pipe_w);
            need_force = 1;
        }

        fd_set fds; FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(_flux_pipe_r, &fds);
        int mfd = _flux_pipe_r > STDIN_FILENO ? _flux_pipe_r : STDIN_FILENO;
        tv.tv_sec = 0; tv.tv_usec = (int)frame_us;

        int ret = select(mfd+1, &fds, NULL, NULL, &tv);
        if (ret < 0) { if (errno==EINTR) continue; break; }

        if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            int n = (int)read(STDIN_FILENO, ibuf, sizeof ibuf);
            if (n > 0) {
                FluxMsg km = _flux_parse_key(ibuf, n);
                if (km.type == MSG_QUIT) goto done;
                if (km.type != MSG_NONE) {
                    FluxCmd c = p->model.update(&p->model, km);
                    if (c.fn == _flux_quit_fn) goto done;
                    _flux_dispatch_cmd(c, _flux_pipe_w);
                }
            }
        }

        if (ret > 0 && FD_ISSET(_flux_pipe_r, &fds)) {
            FluxMsg pm;
            while (read(_flux_pipe_r, &pm, sizeof pm) == (ssize_t)sizeof pm) {
                if (pm.type == MSG_QUIT) goto done;
                FluxCmd c = p->model.update(&p->model, pm);
                if (c.fn == _flux_quit_fn) goto done;
                _flux_dispatch_cmd(c, _flux_pipe_w);
            }
        }

        _flux_render(&p->model, need_force);
    }
done:
    _flux_running = 0;
    _flux_restore_term(p->alt_screen, p->mouse);
    if (p->model.free) p->model.free(&p->model);
    close(_flux_pipe_r); close(_flux_pipe_w);
    _flux_pipe_r = _flux_pipe_w = -1;
}

/* ── flux_strwidth ─────────────────────────────────────────────────
 * Count the terminal column width of a string.
 * Rules:
 *   • ANSI CSI sequences (\x1b[...{letter}) → 0 cols
 *   • UTF-8 leading bytes (0xC0-0xFF)       → 1 col  (assume narrow)
 *   • UTF-8 continuation bytes (0x80-0xBF)  → 0 cols
 *   • Everything else (ASCII printable)     → 1 col
 * ───────────────────────────────────────────────────────────────── */
int flux_strwidth(const char *s) {
    int w = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c == '\x1b') {
            s++;
            if (*s == '[' || *s == '(') {
                s++;
                /* consume until we hit a letter (final byte of CSI) */
                while (*s && !( (*s>='A'&&*s<='Z') || (*s>='a'&&*s<='z') || *s=='~' ))
                    s++;
                if (*s) s++;
            } else if (*s) {
                s++; /* two-char ESC sequence */
            }
            continue;
        }
        if (c == '\n' || c == '\r') { s++; continue; }
        /* UTF-8 continuation byte → no column */
        if ((c & 0xC0) == 0x80) { s++; continue; }
        /* ASCII control → skip */
        if (c < 0x20) { s++; continue; }
        w++;
        s++;
    }
    return w;
}

void flux_pad(char *dst, int dstsz, const char *src, int width) {
    int w = flux_strwidth(src);
    int srcbytes = (int)strlen(src);
    int copy = srcbytes < dstsz-1 ? srcbytes : dstsz-1;
    memcpy(dst, src, copy);
    dst += copy;
    dstsz -= copy;
    int pad = width - w;
    if (pad > 0 && dstsz > 1) {
        int p = pad < dstsz-1 ? pad : dstsz-1;
        memset(dst, ' ', p); dst += p; dstsz -= p;
    }
    *dst = '\0';
}

/* ── borders ─────────────────────────────────────────────────── */
const FluxBorder FLUX_BORDER_ROUNDED = {"╭","╮","╰","╯","─","│","├","┤","┬","┴","┼"};
const FluxBorder FLUX_BORDER_SHARP   = {"┌","┐","└","┘","─","│","├","┤","┬","┴","┼"};
const FluxBorder FLUX_BORDER_DOUBLE  = {"╔","╗","╚","╝","═","║","╠","╣","╦","╩","╬"};
const FluxBorder FLUX_BORDER_THICK   = {"┏","┓","┗","┛","━","┃","┣","┫","┳","┻","╋"};
const FluxBorder FLUX_BORDER_NONE    = {"","","","","","","","","","",""};

/* ── flux_box ──────────────────────────────────────────────────── */
static void _ap(char **o, int *rem, const char *s) {
    int l = (int)strlen(s);
    if (l > *rem) l = *rem;
    memcpy(*o, s, l);
    *o += l; *rem -= l; **o = '\0';
}

void flux_box(char *out, int outsz,
            const char *content,
            const FluxBorder *b,
            int inner_w,
            const char *bclr,
            const char *cbg)
{
    static char tmp[32768];
    strncpy(tmp, content, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';

    #define MAXLINES 256
    char  *lptr[MAXLINES];
    int    lwid[MAXLINES];
    int    nl = 0, max_w = 0;

    char *p = tmp;
    while (*p && nl < MAXLINES) {
        lptr[nl] = p;
        char *end = strchr(p, '\n');
        if (end) *end = '\0';
        lwid[nl] = flux_strwidth(p);
        if (lwid[nl] > max_w) max_w = lwid[nl];
        nl++;
        if (!end) break;
        p = end + 1;
    }

    int iw = inner_w > 0 ? inner_w : max_w;

    char *o  = out;
    int  rem = outsz - 1;
    const char *R = FLUX_RESET;

    /* top border */
    if (bclr) _ap(&o, &rem, bclr);
    _ap(&o, &rem, b->tl);
    for (int i = 0; i < iw + 2; i++) _ap(&o, &rem, b->h);
    _ap(&o, &rem, b->tr);
    if (bclr) _ap(&o, &rem, R);
    _ap(&o, &rem, "\n");

    /* content rows */
    for (int i = 0; i < nl; i++) {
        if (bclr) _ap(&o, &rem, bclr);
        _ap(&o, &rem, b->v);
        if (bclr) _ap(&o, &rem, R);
        _ap(&o, &rem, " ");
        if (cbg) _ap(&o, &rem, cbg);
        _ap(&o, &rem, lptr[i]);
        /* pad to iw */
        int pad = iw - lwid[i];
        if (pad > 0) {
            char sp[512];
            if (pad > 511) pad = 511;
            memset(sp, ' ', pad); sp[pad] = '\0';
            _ap(&o, &rem, sp);
        }
        if (cbg) _ap(&o, &rem, R);
        _ap(&o, &rem, " ");
        if (bclr) _ap(&o, &rem, bclr);
        _ap(&o, &rem, b->v);
        if (bclr) _ap(&o, &rem, R);
        _ap(&o, &rem, "\n");
    }

    /* bottom border */
    if (bclr) _ap(&o, &rem, bclr);
    _ap(&o, &rem, b->bl);
    for (int i = 0; i < iw + 2; i++) _ap(&o, &rem, b->h);
    _ap(&o, &rem, b->br);
    if (bclr) _ap(&o, &rem, R);
    #undef MAXLINES
}

/* ── FluxSB string builder ─────────────────────────────────────── */
void flux_sb_init(FluxSB *sb, char *backing, int cap) {
    sb->buf = backing; sb->len = 0; sb->cap = cap;
    if (cap > 0) sb->buf[0] = '\0';
}
void flux_sb_append(FluxSB *sb, const char *s) {
    int l = (int)strlen(s);
    if (sb->len + l >= sb->cap - 1) l = sb->cap - 1 - sb->len;
    if (l <= 0) return;
    memcpy(sb->buf + sb->len, s, l);
    sb->len += l;
    sb->buf[sb->len] = '\0';
}
void flux_sb_appendf(FluxSB *sb, const char *fmt, ...) {
    char tmp[2048]; va_list ap; va_start(ap,fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    flux_sb_append(sb, tmp);
}
void flux_sb_nl(FluxSB *sb) { flux_sb_append(sb, "\n"); }
const char *flux_sb_str(FluxSB *sb) { return sb->buf; }

/* ── colour helpers ──────────────────────────────────────────── */
char *flux_fg(char *buf, int r, int g, int b) {
    sprintf(buf, "\x1b[38;2;%d;%d;%dm", r, g, b); return buf;
}
char *flux_bg(char *buf, int r, int g, int b) {
    sprintf(buf, "\x1b[48;2;%d;%d;%dm", r, g, b); return buf;
}

/* ── Component implementations ───────────────────────────────── */

void flux_divider(FluxSB *sb, int width, const char *color) {
    if (color) flux_sb_append(sb, color);
    for (int i = 0; i < width; i++) flux_sb_append(sb, "─");
    if (color) flux_sb_append(sb, FLUX_RESET);
}

void flux_bar(FluxSB *sb, float value, int width,
            const char *fill_clr, const char *empty_clr) {
    int filled = (int)(value * width + 0.5f);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    if (fill_clr) flux_sb_append(sb, fill_clr);
    for (int i = 0; i < filled; i++) flux_sb_append(sb, "█");
    if (empty_clr) flux_sb_append(sb, empty_clr);
    for (int i = filled; i < width; i++) flux_sb_append(sb, "░");
    flux_sb_append(sb, FLUX_RESET);
}

static const char *_flux_spark_ch[] =
    {" ","▁","▂","▃","▄","▅","▆","▇","█"};

void flux_sparkline(FluxSB *sb, float *ring, int len, int head,
                  const char *color) {
    if (color) flux_sb_append(sb, color);
    for (int i = 0; i < len; i++) {
        int idx = (head - len + i + len * 4) % len;
        int lv = (int)(ring[idx] * 8.0f);
        if (lv < 0) lv = 0; else if (lv > 8) lv = 8;
        flux_sb_append(sb, _flux_spark_ch[lv]);
    }
    flux_sb_append(sb, FLUX_RESET);
}

void flux_hbox(FluxSB *sb, const char **panels, const int *widths,
             int count, const char *gap) {
    if (!gap) gap = "  ";
    if (count > 8) count = 8;
    #define _FLUX_HB_ML 64
    static char _hb[8][4096];
    char *_hl[8][_FLUX_HB_ML];
    int _hn[8];
    int maxl = 0;
    for (int p = 0; p < count; p++) {
        strncpy(_hb[p], panels[p], sizeof _hb[p] - 1);
        _hb[p][sizeof _hb[p] - 1] = '\0';
        _hn[p] = 0;
        char *s = _hb[p];
        while (*s && _hn[p] < _FLUX_HB_ML) {
            _hl[p][_hn[p]++] = s;
            char *nl = strchr(s, '\n');
            if (!nl) break;
            *nl = '\0'; s = nl + 1;
        }
        if (_hn[p] > maxl) maxl = _hn[p];
    }
    for (int i = 0; i < maxl; i++) {
        for (int p = 0; p < count; p++) {
            char pad[512];
            flux_pad(pad, sizeof pad, i < _hn[p] ? _hl[p][i] : "", widths[p]);
            flux_sb_append(sb, pad);
            if (p < count - 1) flux_sb_append(sb, gap);
        }
        flux_sb_nl(sb);
    }
    #undef _FLUX_HB_ML
}

int flux_count_lines(const char *s) {
    int n = 0;
    while (*s) { if (*s == '\n') n++; s++; }
    /* count last line if it doesn't end with \n */
    if (s > s - 1) { /* always at least 1 line if non-empty */ }
    return n > 0 ? n : (*s ? 1 : 0);
}

void flux_pad_lines(char *buf, int bufsz, int target) {
    int cur = 0;
    const char *s = buf;
    while (*s) { if (*s == '\n') cur++; s++; }
    int len = (int)(s - buf);
    while (cur < target && len < bufsz - 2) {
        buf[len++] = '\n';
        cur++;
    }
    buf[len] = '\0';
}

/* ── FluxSpinner impl ─────────────────────────────────────────── */

const char *FLUX_SPINNER_DOT[] =
    {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
const int   FLUX_SPINNER_DOT_N = 10;
const char *FLUX_SPINNER_LINE[] =
    {"⠂","⠒","⠐","⠰","⠠","⠤","⠄","⠆"};
const int   FLUX_SPINNER_LINE_N = 8;

void flux_spinner_init(FluxSpinner *s, const char **frames, int nframes,
                     const char *label) {
    s->frames = frames; s->frame_count = nframes;
    s->current = 0; s->label = label;
}
void flux_spinner_tick(FluxSpinner *s) {
    s->current = (s->current + 1) % s->frame_count;
}
void flux_spinner_render(FluxSpinner *s, FluxSB *sb, const char *color) {
    if (color) flux_sb_append(sb, color);
    flux_sb_append(sb, s->frames[s->current]);
    flux_sb_append(sb, FLUX_RESET);
    if (s->label) { flux_sb_append(sb, " "); flux_sb_append(sb, s->label); }
}

/* ── FluxInput impl ────────────────────────────────────────────── */

void flux_input_init(FluxInput *inp, const char *placeholder) {
    memset(inp, 0, sizeof *inp);
    inp->placeholder = placeholder;
}
void flux_input_clear(FluxInput *inp) {
    const char *ph = inp->placeholder;
    memset(inp, 0, sizeof *inp);
    inp->placeholder = ph;
}
int flux_input_update(FluxInput *inp, FluxMsg msg) {
    if (msg.type != MSG_KEY) return 0;
    inp->submitted = 0;
    if (flux_key_is(msg, "enter"))     { inp->submitted = 1; return 1; }
    if (flux_key_is(msg, "backspace")) {
        if (inp->cursor > 0) {
            memmove(&inp->buf[inp->cursor-1], &inp->buf[inp->cursor],
                    inp->len - inp->cursor + 1);
            inp->cursor--; inp->len--;
        }
        return 1;
    }
    if (flux_key_is(msg, "delete")) {
        if (inp->cursor < inp->len) {
            memmove(&inp->buf[inp->cursor], &inp->buf[inp->cursor+1],
                    inp->len - inp->cursor);
            inp->len--;
        }
        return 1;
    }
    if (flux_key_is(msg,"left"))  { if (inp->cursor>0) inp->cursor--; return 1; }
    if (flux_key_is(msg,"right")) { if (inp->cursor<inp->len) inp->cursor++; return 1; }
    if (flux_key_is(msg,"home"))  { inp->cursor=0; return 1; }
    if (flux_key_is(msg,"end"))   { inp->cursor=inp->len; return 1; }
    /* printable ASCII */
    if (msg.u.key.key[0]>=32 && msg.u.key.key[0]<127 &&
        msg.u.key.key[1]=='\0' && !msg.u.key.ctrl && !msg.u.key.alt) {
        if (inp->len < FLUX_INPUT_MAX) {
            memmove(&inp->buf[inp->cursor+1], &inp->buf[inp->cursor],
                    inp->len - inp->cursor + 1);
            inp->buf[inp->cursor] = msg.u.key.key[0];
            inp->cursor++; inp->len++;
            return 1;
        }
    }
    return 0;
}
void flux_input_render(FluxInput *inp, FluxSB *sb, int width,
                     const char *color, const char *cursor_clr) {
    if (!color) color = "";
    if (!cursor_clr) cursor_clr = "\x1b[7m"; /* inverse video */
    if (inp->len == 0 && inp->placeholder) {
        flux_sb_append(sb, cursor_clr);
        flux_sb_append(sb, " ");
        flux_sb_append(sb, FLUX_RESET);
        flux_sb_append(sb, FLUX_DIM);
        flux_sb_append(sb, inp->placeholder);
        flux_sb_append(sb, FLUX_RESET);
    } else {
        flux_sb_append(sb, color);
        for (int i = 0; i < inp->cursor; i++)
            flux_sb_appendf(sb, "%c", inp->buf[i]);
        flux_sb_append(sb, cursor_clr);
        flux_sb_appendf(sb, "%c", inp->cursor < inp->len ? inp->buf[inp->cursor] : ' ');
        flux_sb_append(sb, FLUX_RESET);
        flux_sb_append(sb, color);
        for (int i = inp->cursor + 1; i < inp->len; i++)
            flux_sb_appendf(sb, "%c", inp->buf[i]);
        flux_sb_append(sb, FLUX_RESET);
    }
    /* pad remaining width */
    int used = inp->len > 0 ? inp->len + 1 : (inp->placeholder ? flux_strwidth(inp->placeholder) + 1 : 1);
    for (int i = used; i < width; i++) flux_sb_append(sb, " ");
}

/* ── FluxConfirm impl ──────────────────────────────────────────── */

void flux_confirm_init(FluxConfirm *c, const char *title, const char *msg,
                     const char *yes_label, const char *no_label) {
    c->title = title; c->message = msg;
    c->yes_label = yes_label ? yes_label : "Yes";
    c->no_label  = no_label  ? no_label  : "No";
    c->selected = 1; c->answered = 0; c->result = 0;
}
int flux_confirm_update(FluxConfirm *c, FluxMsg msg) {
    if (msg.type != MSG_KEY || c->answered) return 0;
    if (flux_key_is(msg,"left") || flux_key_is(msg,"right") ||
        flux_key_is(msg,"tab")  || flux_key_is(msg,"h") || flux_key_is(msg,"l")) {
        c->selected ^= 1; return 1;
    }
    if (flux_key_is(msg,"enter")) {
        c->answered = 1; c->result = c->selected; return 1;
    }
    if (flux_key_is(msg,"y")) {
        c->selected=1; c->answered=1; c->result=1; return 1;
    }
    if (flux_key_is(msg,"n") || flux_key_is(msg,"esc")) {
        c->selected=0; c->answered=1; c->result=0; return 1;
    }
    return 0;
}
void flux_confirm_render(FluxConfirm *c, FluxSB *sb, int width,
                       const char *border_clr,
                       const char *yes_clr, const char *no_clr) {
    if (width <= 0) width = 40;
    if (!yes_clr) yes_clr = "\x1b[38;2;105;220;150m";
    if (!no_clr)  no_clr  = "\x1b[38;2;255;100;100m";
    char content[1024]; content[0] = '\0';
    if (c->title) {
        strncat(content, FLUX_BOLD, sizeof content - strlen(content) - 1);
        strncat(content, c->title, sizeof content - strlen(content) - 1);
        strncat(content, FLUX_RESET "\n", sizeof content - strlen(content) - 1);
    }
    if (c->message) {
        strncat(content, "\n", sizeof content - strlen(content) - 1);
        strncat(content, c->message, sizeof content - strlen(content) - 1);
        strncat(content, "\n", sizeof content - strlen(content) - 1);
    }
    char btns[256];
    const char *yc = c->selected ? yes_clr : FLUX_DIM;
    const char *nc = !c->selected ? no_clr : FLUX_DIM;
    const char *yb = c->selected ? FLUX_BOLD : "";
    const char *nb = !c->selected ? FLUX_BOLD : "";
    snprintf(btns, sizeof btns,
        "\n    %s%s[ %s ]%s    %s%s[ %s ]%s",
        yc, yb, c->yes_label, FLUX_RESET,
        nc, nb, c->no_label, FLUX_RESET);
    strncat(content, btns, sizeof content - strlen(content) - 1);
    char box[2048];
    flux_box(box, sizeof box, content, &FLUX_BORDER_ROUNDED, width,
           border_clr, NULL);
    flux_sb_append(sb, box);
}

/* ── FluxList impl ─────────────────────────────────────────────── */

void flux_list_init(FluxList *l, int count, int visible, FluxListItemFn render) {
    l->cursor = 0; l->scroll = 0;
    l->count = count; l->visible = visible;
    l->render_item = render;
}
int flux_list_update(FluxList *l, FluxMsg msg) {
    if (msg.type != MSG_KEY) return 0;
    if (flux_key_is(msg,"up") || flux_key_is(msg,"k")) {
        if (l->cursor > 0) { l->cursor--; if (l->cursor < l->scroll) l->scroll--; }
        return 1;
    }
    if (flux_key_is(msg,"down") || flux_key_is(msg,"j")) {
        if (l->cursor < l->count-1) {
            l->cursor++;
            if (l->cursor >= l->scroll + l->visible)
                l->scroll = l->cursor - l->visible + 1;
        }
        return 1;
    }
    if (flux_key_is(msg,"pgup")) {
        l->cursor -= l->visible;
        if (l->cursor < 0) l->cursor = 0;
        l->scroll = l->cursor;
        return 1;
    }
    if (flux_key_is(msg,"pgdown")) {
        l->cursor += l->visible;
        if (l->cursor >= l->count) l->cursor = l->count - 1;
        l->scroll = l->cursor - l->visible + 1;
        if (l->scroll < 0) l->scroll = 0;
        return 1;
    }
    if (flux_key_is(msg,"home")) { l->cursor = 0; l->scroll = 0; return 1; }
    if (flux_key_is(msg,"end")) {
        l->cursor = l->count - 1;
        l->scroll = l->count - l->visible;
        if (l->scroll < 0) l->scroll = 0;
        return 1;
    }
    return 0;
}
void flux_list_render(FluxList *l, FluxSB *sb, int width, void *ctx) {
    for (int i = l->scroll; i < l->count && i < l->scroll + l->visible; i++) {
        l->render_item(sb, i, i == l->cursor, width, ctx);
        flux_sb_nl(sb);
    }
}

/* ── FluxTabs impl ─────────────────────────────────────────────── */

void flux_tabs_init(FluxTabs *t, const char **icons, const char **labels,
                  int count) {
    t->icons = icons; t->labels = labels;
    t->count = count; t->active = 0;
}
int flux_tabs_update(FluxTabs *t, FluxMsg msg) {
    if (msg.type != MSG_KEY) return 0;
    if (flux_key_is(msg,"tab")) { t->active = (t->active+1) % t->count; return 1; }
    if (flux_key_is(msg,"shift+tab")) { t->active = (t->active-1+t->count) % t->count; return 1; }
    if (msg.u.key.key[0]>='1' && msg.u.key.key[0]<='9' && msg.u.key.key[1]=='\0') {
        int idx = msg.u.key.key[0] - '1';
        if (idx < t->count) { t->active = idx; return 1; }
    }
    return 0;
}
void flux_tabs_render(FluxTabs *t, FluxSB *sb,
                    const char *active_clr, const char *dim_clr,
                    const char *hint) {
    if (!dim_clr) dim_clr = FLUX_DIM;
    if (!active_clr) active_clr = FLUX_BOLD;
    flux_sb_append(sb, dim_clr);
    flux_sb_append(sb, "  ");
    for (int i = 0; i < t->count; i++) {
        if (i == t->active) {
            flux_sb_append(sb, FLUX_RESET);
            flux_sb_append(sb, active_clr);
            if (t->icons) flux_sb_appendf(sb, " %s ", t->icons[i]);
            flux_sb_appendf(sb, "%s ", t->labels[i]);
            flux_sb_append(sb, FLUX_RESET);
            flux_sb_append(sb, dim_clr);
        } else {
            if (t->icons) flux_sb_appendf(sb, " %s ", t->icons[i]);
            flux_sb_appendf(sb, "%s ", t->labels[i]);
        }
        if (i < t->count - 1) flux_sb_append(sb, "│");
    }
    if (hint) {
        flux_sb_append(sb, FLUX_RESET);
        flux_sb_append(sb, dim_clr);
        flux_sb_append(sb, "  ");
        flux_sb_append(sb, hint);
    }
    flux_sb_append(sb, FLUX_RESET);
    flux_sb_nl(sb);
    flux_sb_append(sb, dim_clr);
    int w = flux_cols();
    for (int i = 0; i < w; i++) flux_sb_append(sb, "─");
    flux_sb_append(sb, FLUX_RESET);
    flux_sb_nl(sb);
}

/* ── FluxTable impl ────────────────────────────────────────────── */

void flux_table_init(FluxTable *t, const char **headers, const int *widths,
                   int cols, int visible, FluxTableCellFn render) {
    t->headers = headers; t->widths = widths;
    t->col_count = cols; t->row_count = 0;
    t->visible = visible; t->scroll = 0;
    t->follow = 0; t->render_cell = render;
}
void flux_table_set_rows(FluxTable *t, int rows) {
    t->row_count = rows;
    if (t->follow) {
        t->scroll = t->row_count - t->visible;
        if (t->scroll < 0) t->scroll = 0;
    }
}
int flux_table_update(FluxTable *t, FluxMsg msg) {
    if (msg.type != MSG_KEY) return 0;
    if (flux_key_is(msg,"up") || flux_key_is(msg,"k")) {
        if (t->scroll > 0) t->scroll--;
        t->follow = 0;
        return 1;
    }
    if (flux_key_is(msg,"down") || flux_key_is(msg,"j")) {
        if (t->scroll + t->visible < t->row_count) t->scroll++;
        t->follow = 0;
        return 1;
    }
    if (flux_key_is(msg,"f")) {
        t->follow ^= 1;
        if (t->follow) {
            t->scroll = t->row_count - t->visible;
            if (t->scroll < 0) t->scroll = 0;
        }
        return 1;
    }
    return 0;
}
void flux_table_render(FluxTable *t, FluxSB *sb, void *ctx,
                     const char *header_clr, const char *border_clr) {
    if (!header_clr) header_clr = FLUX_DIM;
    if (!border_clr) border_clr = FLUX_DIM;
    int tw = 0;
    for (int c = 0; c < t->col_count; c++) tw += t->widths[c] + 2;
    /* header */
    flux_sb_append(sb, header_clr);
    for (int c = 0; c < t->col_count; c++) {
        char pad[256];
        flux_pad(pad, sizeof pad, t->headers[c], t->widths[c]);
        flux_sb_append(sb, pad); flux_sb_append(sb, "  ");
    }
    flux_sb_append(sb, FLUX_RESET); flux_sb_nl(sb);
    flux_sb_append(sb, border_clr);
    for (int i = 0; i < tw; i++) flux_sb_append(sb, "─");
    flux_sb_append(sb, FLUX_RESET); flux_sb_nl(sb);
    /* rows */
    int start = t->scroll;
    if (start < 0) start = 0;
    if (start + t->visible > t->row_count)
        start = t->row_count - t->visible;
    if (start < 0) start = 0;
    for (int r = start; r < t->row_count && r < start + t->visible; r++) {
        for (int c = 0; c < t->col_count; c++) {
            t->render_cell(sb, r, c, t->widths[c], ctx);
            flux_sb_append(sb, "  ");
        }
        flux_sb_nl(sb);
    }
    /* footer */
    flux_sb_append(sb, border_clr);
    for (int i = 0; i < tw; i++) flux_sb_append(sb, "─");
    flux_sb_append(sb, FLUX_RESET); flux_sb_nl(sb);
    int end = start + t->visible;
    if (end > t->row_count) end = t->row_count;
    flux_sb_appendf(sb, "%s  lines %d-%d of %d%s%s\n",
        border_clr, start+1, end, t->row_count,
        t->follow ? "  [following]" : "", FLUX_RESET);
}

/* ── FluxKanban impl ───────────────────────────────────────────── */

void flux_kanban_init(FluxKanban *kb, int ncols, const char **col_names, int col_width) {
    memset(kb, 0, sizeof *kb);
    kb->ncols = ncols > FLUX_KB_MAX_COLS ? FLUX_KB_MAX_COLS : ncols;
    kb->col_width = col_width > 0 ? col_width : 24;
    kb->visible = 6;
    for (int i = 0; i < kb->ncols; i++) {
        strncpy(kb->cols[i].name, col_names[i], sizeof kb->cols[i].name - 1);
        kb->cols[i].name[sizeof kb->cols[i].name - 1] = '\0';
    }
    flux_input_init(&kb->edit_title, "Title...");
    flux_input_init(&kb->edit_desc, "Description...");
}

int flux_kanban_add(FluxKanban *kb, int col, const char *title, const char *desc) {
    if (col < 0 || col >= kb->ncols) return 0;
    FluxKbCol *c = &kb->cols[col];
    if (c->count >= FLUX_KB_MAX_CARDS) return 0;
    strncpy(c->cards[c->count].title, title, FLUX_KB_TITLE_MAX);
    c->cards[c->count].title[FLUX_KB_TITLE_MAX] = '\0';
    strncpy(c->cards[c->count].desc, desc, FLUX_KB_DESC_MAX);
    c->cards[c->count].desc[FLUX_KB_DESC_MAX] = '\0';
    c->count++;
    kb->dirty = 1;
    return 1;
}

void flux_kanban_del(FluxKanban *kb, int col, int row) {
    if (col < 0 || col >= kb->ncols) return;
    FluxKbCol *c = &kb->cols[col];
    if (row < 0 || row >= c->count) return;
    for (int i = row; i < c->count - 1; i++)
        c->cards[i] = c->cards[i + 1];
    c->count--;
    if (kb->cur_col == col && kb->cur_row >= c->count && kb->cur_row > 0)
        kb->cur_row = c->count - 1;
    kb->dirty = 1;
}

int flux_kanban_move(FluxKanban *kb, int col, int row, int to_col) {
    if (col < 0 || col >= kb->ncols) return 0;
    if (to_col < 0 || to_col >= kb->ncols) return 0;
    if (col == to_col) return 0;
    FluxKbCol *src = &kb->cols[col];
    FluxKbCol *dst = &kb->cols[to_col];
    if (row < 0 || row >= src->count) return 0;
    if (dst->count >= FLUX_KB_MAX_CARDS) return 0;
    FluxKbCard card = src->cards[row];
    for (int i = row; i < src->count - 1; i++)
        src->cards[i] = src->cards[i + 1];
    src->count--;
    dst->cards[dst->count] = card;
    dst->count++;
    kb->dirty = 1;
    return 1;
}

int flux_kanban_dirty(FluxKanban *kb) {
    int d = kb->dirty;
    kb->dirty = 0;
    return d;
}

int flux_kanban_update(FluxKanban *kb, FluxMsg msg) {
    if (msg.type != MSG_KEY) return 0;

    /* EDIT mode: intercept everything */
    if (kb->mode == FLUX_KB_EDIT) {
        if (flux_key_is(msg, "esc")) {
            kb->mode = FLUX_KB_BROWSE;
            return 1;
        }
        if (flux_key_is(msg, "tab")) {
            kb->edit_focus = (kb->edit_focus + 1) % 4;
            return 1;
        }
        if (flux_key_is(msg, "shift+tab")) {
            kb->edit_focus = (kb->edit_focus + 3) % 4;
            return 1;
        }
        if (flux_key_is(msg, "enter")) {
            if (kb->edit_focus == FLUX_KB_F_OK) {
                FluxKbCard *card = &kb->cols[kb->edit_col].cards[kb->edit_row];
                int tlen = kb->edit_title.len;
                if (tlen > FLUX_KB_TITLE_MAX) tlen = FLUX_KB_TITLE_MAX;
                memcpy(card->title, kb->edit_title.buf, (size_t)tlen);
                card->title[tlen] = '\0';
                int dlen = kb->edit_desc.len;
                if (dlen > FLUX_KB_DESC_MAX) dlen = FLUX_KB_DESC_MAX;
                memcpy(card->desc, kb->edit_desc.buf, (size_t)dlen);
                card->desc[dlen] = '\0';
                kb->mode = FLUX_KB_BROWSE;
                kb->dirty = 1;
            } else if (kb->edit_focus == FLUX_KB_F_CANCEL) {
                kb->mode = FLUX_KB_BROWSE;
            } else {
                kb->edit_focus = (kb->edit_focus + 1) % 4;
            }
            return 1;
        }
        if (kb->edit_focus == FLUX_KB_F_TITLE)
            flux_input_update(&kb->edit_title, msg);
        else if (kb->edit_focus == FLUX_KB_F_DESC)
            flux_input_update(&kb->edit_desc, msg);
        return 1;
    }

    /* BROWSE mode */
    if (flux_key_is(msg, "up") || flux_key_is(msg, "k")) {
        if (kb->cur_row > 0) {
            kb->cur_row--;
            if (kb->cur_row < kb->scroll[kb->cur_col])
                kb->scroll[kb->cur_col] = kb->cur_row;
        }
        return 1;
    }
    if (flux_key_is(msg, "down") || flux_key_is(msg, "j")) {
        int cnt = kb->cols[kb->cur_col].count;
        int vis = kb->visible > 0 ? kb->visible : 8;
        if (kb->cur_row < cnt - 1) {
            kb->cur_row++;
            if (kb->cur_row >= kb->scroll[kb->cur_col] + vis)
                kb->scroll[kb->cur_col] = kb->cur_row - vis + 1;
        }
        return 1;
    }
    if (flux_key_is(msg, "right") || flux_key_is(msg, "l")) {
        if (kb->grabbed) {
            /* move grabbed card to next column */
            if (kb->cur_col < kb->ncols - 1 && kb->cols[kb->cur_col].count > 0) {
                if (flux_kanban_move(kb, kb->cur_col, kb->cur_row, kb->cur_col + 1)) {
                    kb->cur_col++;
                    kb->cur_row = kb->cols[kb->cur_col].count - 1;
                }
            }
        } else {
            /* navigate to next column */
            if (kb->cur_col < kb->ncols - 1) {
                kb->cur_col++;
                if (kb->cur_row >= kb->cols[kb->cur_col].count)
                    kb->cur_row = kb->cols[kb->cur_col].count > 0
                                ? kb->cols[kb->cur_col].count - 1 : 0;
                /* ensure cursor visible in new column */
                { int v = kb->visible > 0 ? kb->visible : 8;
                  if (kb->cur_row < kb->scroll[kb->cur_col]) kb->scroll[kb->cur_col] = kb->cur_row;
                  if (kb->cur_row >= kb->scroll[kb->cur_col] + v) kb->scroll[kb->cur_col] = kb->cur_row - v + 1; }
            }
        }
        return 1;
    }
    if (flux_key_is(msg, "left") || flux_key_is(msg, "h")) {
        if (kb->grabbed) {
            /* move grabbed card to prev column */
            if (kb->cur_col > 0 && kb->cols[kb->cur_col].count > 0) {
                if (flux_kanban_move(kb, kb->cur_col, kb->cur_row, kb->cur_col - 1)) {
                    kb->cur_col--;
                    kb->cur_row = kb->cols[kb->cur_col].count - 1;
                    /* ensure visible */
                    { int v = kb->visible > 0 ? kb->visible : 8;
                      if (kb->cur_row >= kb->scroll[kb->cur_col] + v) kb->scroll[kb->cur_col] = kb->cur_row - v + 1; }
                }
            }
        } else {
            /* navigate to prev column */
            if (kb->cur_col > 0) {
                kb->cur_col--;
                if (kb->cur_row >= kb->cols[kb->cur_col].count)
                    kb->cur_row = kb->cols[kb->cur_col].count > 0
                                ? kb->cols[kb->cur_col].count - 1 : 0;
                /* ensure cursor visible */
                { int v = kb->visible > 0 ? kb->visible : 8;
                  if (kb->cur_row < kb->scroll[kb->cur_col]) kb->scroll[kb->cur_col] = kb->cur_row;
                  if (kb->cur_row >= kb->scroll[kb->cur_col] + v) kb->scroll[kb->cur_col] = kb->cur_row - v + 1; }
            }
        }
        return 1;
    }
    /* enter: toggle grab (for moving) */
    if (flux_key_is(msg, "enter")) {
        if (kb->cols[kb->cur_col].count > 0)
            kb->grabbed ^= 1;
        return 1;
    }
    /* e: edit card */
    if (flux_key_is(msg, "e")) {
        FluxKbCol *col = &kb->cols[kb->cur_col];
        if (col->count > 0) {
            FluxKbCard *card = &col->cards[kb->cur_row];
            kb->grabbed = 0;
            kb->mode = FLUX_KB_EDIT;
            kb->edit_focus = FLUX_KB_F_TITLE;
            kb->edit_col = kb->cur_col;
            kb->edit_row = kb->cur_row;
            flux_input_clear(&kb->edit_title);
            flux_input_clear(&kb->edit_desc);
            strncpy(kb->edit_title.buf, card->title, FLUX_INPUT_MAX);
            kb->edit_title.len = (int)strlen(kb->edit_title.buf);
            kb->edit_title.cursor = kb->edit_title.len;
            strncpy(kb->edit_desc.buf, card->desc, FLUX_INPUT_MAX);
            kb->edit_desc.len = (int)strlen(kb->edit_desc.buf);
            kb->edit_desc.cursor = kb->edit_desc.len;
        }
        return 1;
    }
    /* esc: ungrab */
    if (flux_key_is(msg, "esc")) {
        kb->grabbed = 0;
        return 1;
    }
    if (flux_key_is(msg, "n")) {
        FluxKbCol *col = &kb->cols[kb->cur_col];
        if (col->count < FLUX_KB_MAX_CARDS) {
            col->cards[col->count].title[0] = '\0';
            col->cards[col->count].desc[0] = '\0';
            col->count++;
            kb->cur_row = col->count - 1;
            kb->grabbed = 0;
            kb->mode = FLUX_KB_EDIT;
            kb->edit_focus = FLUX_KB_F_TITLE;
            kb->edit_col = kb->cur_col;
            kb->edit_row = kb->cur_row;
            flux_input_clear(&kb->edit_title);
            flux_input_clear(&kb->edit_desc);
            kb->dirty = 1;
        }
        return 1;
    }
    if (flux_key_is(msg, "d") || flux_key_is(msg, "delete")) {
        if (kb->cols[kb->cur_col].count > 0) {
            kb->grabbed = 0;
            flux_kanban_del(kb, kb->cur_col, kb->cur_row);
        }
        return 1;
    }
    return 0;
}

void flux_kanban_render(FluxKanban *kb, FluxSB *sb, const char **col_clrs, const char *hint) {
    int vis = kb->visible > 0 ? kb->visible : 8;

    char box_bufs[FLUX_KB_MAX_COLS][4096];
    const char *panels[FLUX_KB_MAX_COLS];
    int widths[FLUX_KB_MAX_COLS];
    /* content strings for equalizing height */
    char contents[FLUX_KB_MAX_COLS][3072];

    for (int c = 0; c < kb->ncols; c++) {
        FluxKbCol *col = &kb->cols[c];
        const char *clr = col_clrs ? col_clrs[c] : "";
        FluxSB csb; flux_sb_init(&csb, contents[c], (int)sizeof contents[c]);

        /* clamp scroll */
        if (kb->scroll[c] > col->count - vis) kb->scroll[c] = col->count - vis;
        if (kb->scroll[c] < 0) kb->scroll[c] = 0;

        /* column header with scroll indicator */
        int has_up = kb->scroll[c] > 0;
        int has_dn = kb->scroll[c] + vis < col->count;
        flux_sb_appendf(&csb, "%s%s %s (%d)%s", clr, FLUX_BOLD, col->name, col->count, FLUX_RESET);
        if (has_up || has_dn) {
            flux_sb_appendf(&csb, " %s", FLUX_DIM);
            if (has_up) flux_sb_append(&csb, "▲");
            if (has_dn) flux_sb_append(&csb, "▼");
            flux_sb_append(&csb, FLUX_RESET);
        }
        flux_sb_nl(&csb);

        /* visible window of cards */
        int end = kb->scroll[c] + vis;
        if (end > col->count) end = col->count;
        int rendered = 0;
        for (int r = kb->scroll[c]; r < end; r++) {
            FluxKbCard *card = &col->cards[r];
            int is_sel = (c == kb->cur_col && r == kb->cur_row && kb->mode == FLUX_KB_BROWSE);
            int is_grab = (is_sel && kb->grabbed);

            /* truncate title */
            char trunc_t[FLUX_KB_TITLE_MAX + 1];
            int max_tw = kb->col_width - 4;
            if (max_tw < 1) max_tw = 1;
            if ((int)strlen(card->title) > max_tw) {
                int cp = max_tw > 2 ? max_tw - 2 : 0;
                memcpy(trunc_t, card->title, (size_t)cp);
                trunc_t[cp] = '.'; trunc_t[cp+1] = '.'; trunc_t[cp+2] = '\0';
            } else {
                strncpy(trunc_t, card->title, sizeof trunc_t - 1);
                trunc_t[sizeof trunc_t - 1] = '\0';
            }

            if (is_grab)
                flux_sb_appendf(&csb, "\x1b[7m%s%s◆ %s%s\x1b[27m", clr, FLUX_BOLD, trunc_t, FLUX_RESET);
            else if (is_sel)
                flux_sb_appendf(&csb, "%s%s▸ %s%s", clr, FLUX_BOLD, trunc_t, FLUX_RESET);
            else
                flux_sb_appendf(&csb, "  %s", trunc_t);
            flux_sb_nl(&csb);

            /* truncate desc */
            char trunc_d[FLUX_KB_DESC_MAX + 1];
            if ((int)strlen(card->desc) > max_tw) {
                int cp = max_tw > 2 ? max_tw - 2 : 0;
                memcpy(trunc_d, card->desc, (size_t)cp);
                trunc_d[cp] = '.'; trunc_d[cp+1] = '.'; trunc_d[cp+2] = '\0';
            } else {
                strncpy(trunc_d, card->desc, sizeof trunc_d - 1);
                trunc_d[sizeof trunc_d - 1] = '\0';
            }

            flux_sb_appendf(&csb, "%s  %s%s", FLUX_DIM, trunc_d, FLUX_RESET);
            flux_sb_nl(&csb);
            rendered++;
        }
        /* pad remaining rows so all columns are same height */
        for (int r = rendered; r < vis; r++) {
            flux_sb_nl(&csb);
            flux_sb_nl(&csb);
        }
    }

    /* equalize content heights then box each column */
    { int ll = 0;
      for (int c = 0; c < kb->ncols; c++) {
          int n = flux_count_lines(contents[c]);
          if (n > ll) ll = n;
      }
      for (int c = 0; c < kb->ncols; c++)
          flux_pad_lines(contents[c], (int)sizeof contents[c], ll);
    }

    for (int c = 0; c < kb->ncols; c++) {
        const char *clr = col_clrs ? col_clrs[c] : "";
        flux_box(box_bufs[c], (int)sizeof box_bufs[c], contents[c],
               &FLUX_BORDER_ROUNDED, kb->col_width, clr, NULL);
        panels[c] = box_bufs[c];
        widths[c] = kb->col_width + 4;
    }

    /* render columns side by side into temp buffer, then indent each line */
    {
        char tmpbuf[8192];
        FluxSB tmp; flux_sb_init(&tmp, tmpbuf, (int)sizeof tmpbuf);
        flux_hbox(&tmp, panels, widths, kb->ncols, "  ");
        char *s = tmpbuf;
        while (*s) {
            flux_sb_append(sb, "  ");
            char *nl = strchr(s, '\n');
            if (nl) { *nl = '\0'; flux_sb_append(sb, s); flux_sb_nl(sb); s = nl + 1; }
            else    { flux_sb_append(sb, s); flux_sb_nl(sb); break; }
        }
    }

    /* edit dialog */
    if (kb->mode == FLUX_KB_EDIT) {
        int dlg_w = 42;
        flux_sb_nl(sb);

        char dlg_content[2048];
        FluxSB dsb; flux_sb_init(&dsb, dlg_content, (int)sizeof dlg_content);

        flux_sb_appendf(&dsb, "%s Edit Card%s", FLUX_BOLD, FLUX_RESET);
        flux_sb_nl(&dsb);
        flux_sb_nl(&dsb);
        flux_sb_append(&dsb, " Title:");
        flux_sb_nl(&dsb);
        flux_sb_append(&dsb, " ");
        flux_input_render(&kb->edit_title, &dsb, dlg_w - 4,
            kb->edit_focus == FLUX_KB_F_TITLE ? (col_clrs ? col_clrs[kb->edit_col] : "") : FLUX_DIM,
            kb->edit_focus == FLUX_KB_F_TITLE ? "\x1b[7m" : FLUX_DIM);
        flux_sb_nl(&dsb);
        flux_sb_nl(&dsb);
        flux_sb_append(&dsb, " Description:");
        flux_sb_nl(&dsb);
        flux_sb_append(&dsb, " ");
        flux_input_render(&kb->edit_desc, &dsb, dlg_w - 4,
            kb->edit_focus == FLUX_KB_F_DESC ? (col_clrs ? col_clrs[kb->edit_col] : "") : FLUX_DIM,
            kb->edit_focus == FLUX_KB_F_DESC ? "\x1b[7m" : FLUX_DIM);
        flux_sb_nl(&dsb);
        flux_sb_nl(&dsb);

        /* buttons */
        {
            char ok_btn[64], cancel_btn[64];
            if (kb->edit_focus == FLUX_KB_F_OK)
                snprintf(ok_btn, sizeof ok_btn, "\x1b[38;2;105;220;150m%s[ OK ]%s", FLUX_BOLD, FLUX_RESET);
            else
                snprintf(ok_btn, sizeof ok_btn, "%s[ OK ]%s", FLUX_DIM, FLUX_RESET);
            if (kb->edit_focus == FLUX_KB_F_CANCEL)
                snprintf(cancel_btn, sizeof cancel_btn, "\x1b[38;2;255;100;100m%s[ Cancel ]%s", FLUX_BOLD, FLUX_RESET);
            else
                snprintf(cancel_btn, sizeof cancel_btn, "%s[ Cancel ]%s", FLUX_DIM, FLUX_RESET);
            flux_sb_appendf(&dsb, "     %s      %s", ok_btn, cancel_btn);
            flux_sb_nl(&dsb);
        }

        /* wrap in box */
        {
            char dlg_box[4096];
            const char *edit_clr = col_clrs ? col_clrs[kb->edit_col] : "";
            flux_box(dlg_box, (int)sizeof dlg_box, dlg_content, &FLUX_BORDER_ROUNDED, dlg_w, edit_clr, NULL);

            char *s = dlg_box;
            while (*s) {
                flux_sb_append(sb, "  ");
                char *nl = strchr(s, '\n');
                if (nl) { *nl = '\0'; flux_sb_append(sb, s); flux_sb_nl(sb); s = nl + 1; }
                else    { flux_sb_append(sb, s); flux_sb_nl(sb); break; }
            }
        }

        flux_sb_appendf(sb, "\n  %sTab: cycle fields  Enter: confirm  Esc: cancel%s\n", FLUX_DIM, FLUX_RESET);
    }

    /* hint line */
    if (hint) { flux_sb_append(sb, hint); flux_sb_nl(sb); }
}

#endif /* FLUX_IMPL */
#ifdef __cplusplus
}
#endif
#endif /* FLUX_H */
