/*
 * demo.c  —  flux.h SHOWCASE
 *
 * 8 tabs, Tab/Shift+Tab or number keys 1-8 to switch:
 *
 *   1. Dashboard  — animated live "metrics" with sparklines + gauges
 *   2. Explorer   — two-pane filesystem browser with preview
 *   3. Cards      — selectable card grid with focus ring
 *   4. Matrix     — falling-rain animation (async tick)
 *   5. Log        — scrollable log viewer with level colouring
 *   6. Help       — keybindings reference, double-border
 *   7. Agent      — Claude Code agent demo with typing, thinking, diff
 *   8. Kanban     — 3-column kanban board with card editing
 *
 * Build:  gcc -O2 -std=c99 -o demo demo.c -lpthread
 * Run:    ./demo
 */

#define FLUX_IMPL
#include "flux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ═══════════════════════════════════════════════════════════════
 * PALETTE
 * ═══════════════════════════════════════════════════════════════ */
#define CY  "\x1b[38;2;82;213;196m"
#define AM  "\x1b[38;2;255;188;68m"
#define PU  "\x1b[38;2;180;130;255m"
#define GR  "\x1b[38;2;105;220;150m"
#define RE  "\x1b[38;2;255;100;100m"
#define BL  "\x1b[38;2;100;160;255m"
#define OR  "\x1b[38;2;255;140;80m"
#define PK  "\x1b[38;2;255;120;180m"
#define DM  "\x1b[38;2;75;95;115m"
#define WH  "\x1b[38;2;215;225;245m"
#define BD  "\x1b[1m"
#define IT  "\x1b[3m"
#define RS  "\x1b[0m"

#define BG0 "\x1b[48;2;11;14;20m"
#define BG1 "\x1b[48;2;17;22;32m"
#define BG2 "\x1b[48;2;24;32;48m"
#define BG3 "\x1b[48;2;34;48;72m"
#define BGX "\x1b[48;2;45;65;100m"

/* ── util: safe strcat into fixed buf ── */
#define SCAT(dst,src) strncat((dst),(src),sizeof(dst)-strlen(dst)-1)
#define SCATF(dst,...) do { \
    char _t[512]; snprintf(_t,sizeof _t,__VA_ARGS__); SCAT(dst,_t); } while(0)

/* ═══════════════════════════════════════════════════════════════
 * TABS
 * ═══════════════════════════════════════════════════════════════ */
#define SCR_DASH    0
#define SCR_EXPLORER 1
#define SCR_CARDS   2
#define SCR_MATRIX  3
#define SCR_LOG     4
#define SCR_HELP    5
#define SCR_AGENT   6
#define SCR_KANBAN  7
#define SCR_N       8

static const char *TAB_ICONS[]   = {"◈","⊞","▦","⋮","☰","?","◉","▥"};
static const char *TAB_LABELS[]  = {"Dashboard","Explorer","Cards","Matrix","Log","Help","Agent","Kanban"};

/* ═══════════════════════════════════════════════════════════════
 * DASHBOARD state
 * ═══════════════════════════════════════════════════════════════ */
#define SPARK_LEN 24
typedef struct {
    float cpu[SPARK_LEN];
    float mem[SPARK_LEN];
    float net[SPARK_LEN];
    float disk[SPARK_LEN];
    int   head;          /* ring buffer head */
    int   tick;          /* total ticks */
    float cpu_now, mem_now, net_now, disk_now;
} Dash;

/* ═══════════════════════════════════════════════════════════════
 * EXPLORER state
 * ═══════════════════════════════════════════════════════════════ */
#define FS_MAX 12
typedef struct {
    const char *name;
    const char *type;   /* "dir" | "file" */
    const char *size;
    const char *perms;
    const char *preview[8]; /* for files */
    int   preview_lines;
} FsEntry;

static FsEntry FS_ENTRIES[] = {
    {"src/",        "dir",  "4.0K", "drwxr-xr-x", {0}, 0},
    {"include/",    "dir",  "4.0K", "drwxr-xr-x", {0}, 0},
    {"tests/",      "dir",  "4.0K", "drwxr-xr-x", {0}, 0},
    {"Makefile",    "file", "2.1K", "-rw-r--r--",
        {"CC = gcc","CFLAGS = -O2 -std=c99","","all: demo","",
         "\t$(CC) $(CFLAGS) -o demo demo.c -lpthread",""}, 7},
    {"README.md",   "file", "8.4K", "-rw-r--r--",
        {"# flux.h","","Elm Architecture TUI for C99.",
         "Zero dependencies (libc + pthreads).",
         "","## Build","  gcc -O2 demo.c -lpthread"}, 7},
    {"demo.c",      "file", "18K",  "-rw-r--r--",
        {"#define FLUX_IMPL","#include \"flux.h\"","",
         "/* 8-tab showcase */","int main(void) {",
         "    flux_run(&prog);","    return 0;"}, 7},
    {"flux.h", "file", "22K",  "-rw-r--r--",
        {"/* Elm Architecture TUI */","/* for C99, single-header */","",
         "typedef struct FluxModel FluxModel;","struct FluxModel {",
         "    FluxCmd (*update)(...);","    void  (*view)(...);"}, 7},
    {".gitignore",  "file", "128B", "-rw-r--r--",
        {"demo","*.o","*.a",".DS_Store",""}, 4},
    {"LICENSE",     "file", "1.1K", "-rw-r--r--",
        {"MIT License","","Copyright (c) 2025","",
         "Permission is hereby granted, free","of charge, to any person obtaining",""}, 6},
};
#define FS_N 9

typedef struct {
    int cursor;
    int scroll;
} Explorer;

/* ═══════════════════════════════════════════════════════════════
 * CARDS state
 * ═══════════════════════════════════════════════════════════════ */
#define CARD_N 9
typedef struct {
    const char *icon;
    const char *title;
    const char *subtitle;
    const char *tag;
    const char *tag_color;
    int selected;
} Card;

static Card CARDS[CARD_N] = {
    {"◈", "flux.h",   "Elm TUI for C99",     "library",   CY,  0},
    {"⬡", "netwatch",      "Socket monitor",       "tool",      GR,  0},
    {"◉", "Boligrisiko",   "Property risk scanner","proptech",  AM,  0},
    {"⊞", "oaterm",        "GPU-accel terminal",   "infra",     PU,  0},
    {"▦", "NB-Whisper",    "Norwegian ASR",        "ML",        BL,  0},
    {"⋮", "Qwen extractor","PDF TG-rating",        "ML",        OR,  0},
    {"◎", "pyannote",      "Speaker diarization",  "audio",     PK,  0},
    {"❋", "SpaceTimeDB",   "Event-sourced DB",     "database",  RE,  0},
    {"⟡", "AVM engine",    "Property valuation",   "proptech",  AM,  0},
};

typedef struct {
    int cursor;   /* 0..CARD_N-1 */
} Cards;

/* ═══════════════════════════════════════════════════════════════
 * MATRIX state
 * ═══════════════════════════════════════════════════════════════ */
#define MAT_COLS 60
#define MAT_ROWS 20
typedef struct {
    int  pos[MAT_COLS];     /* current row of each column's head */
    int  len[MAT_COLS];     /* trail length */
    int  spd[MAT_COLS];     /* speed (ticks per step) */
    int  ctr[MAT_COLS];     /* tick counter */
    char chr[MAT_COLS][MAT_ROWS]; /* current char per cell */
    int  active;
    unsigned int rng;
} Matrix;

static unsigned int _rng(unsigned int *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s;
}

/* ═══════════════════════════════════════════════════════════════
 * LOG state
 * ═══════════════════════════════════════════════════════════════ */
#define LOG_MAX 200
#define LOG_VIS 18
typedef struct {
    char    level[8];
    char    ts[12];
    char    msg[80];
} LogEntry;

static LogEntry LOG_ENTRIES[LOG_MAX];
static int      LOG_N = 0;

typedef struct {
    int scroll;   /* top visible line */
    int follow;   /* auto-follow new lines */
} LogState;

static const char *LOG_MSGS[] = {
    "Listening on :8080",
    "TLS handshake complete (SNI: api.example.no)",
    "GET /api/v2/listings 200 14ms",
    "POST /api/v2/valuations 201 42ms",
    "Redis connection pool: 8/32 active",
    "Postgres query: SELECT * FROM properties WHERE gnr=... 3ms",
    "Worker #3 idle, waiting for job",
    "Cache miss: property:oslo:frogner:1234",
    "Cache hit: borettslag:3:financials",
    "WebSocket upgrade: client 192.168.1.42",
    "Rate limit exceeded: 192.168.1.99 (429)",
    "Scraped 142 new listings from Finn.no",
    "AVM model inference: 0.8ms/property",
    "PDF extraction: tilstandsrapport detected (TG2)",
    "WARN: slow query >100ms on listings.find()",
    "ERROR: Redis timeout after 5000ms",
    "Reconnecting to Redis...",
    "Redis reconnected successfully",
    "Background job: price index updated",
    "SIGWINCH received, redrawing terminal",
};
#define LOG_MSGS_N 20
static const char *LOG_LEVELS[] = {"INFO","INFO","INFO","INFO","INFO","INFO",
    "DEBUG","DEBUG","DEBUG","DEBUG","WARN","INFO","INFO","INFO","WARN","ERROR",
    "WARN","INFO","INFO","INFO"};

/* ═══════════════════════════════════════════════════════════════
 * AGENT state
 * ═══════════════════════════════════════════════════════════════ */
#define AGENT_INPUT    0
#define AGENT_THINK    1
#define AGENT_RESPOND  2
#define AGENT_DIFF     3
#define AGENT_CONFIRM  4
#define AGENT_DONE     5

static const char *AGENT_MSG =
    "Add a health check endpoint to the server";
static const char *AGENT_RESP =
    "I'll add a /health endpoint that returns server status. "
    "This requires modifying server.c to add the route handler.";
static const char *AGENT_DIFF_LINES[] = {
    " server.c",
    "",
    "  void register_routes(Server *s) {",
    "      route(s, \"/listings\", handler_list);",
    "+     route(s, \"/health\", handler_health);",
    "  }",
    "",
    "+ int handler_health(Req *r, Res *w) {",
    "+     w->status = 200;",
    "+     json_write(w, \"status\", \"ok\");",
    "+     return 0;",
    "+ }",
};
#define AGENT_DIFF_N 12

typedef struct {
    int phase;
    int char_idx;
    int think_ticks;
    int agent_tick_ms;
    FluxSpinner spinner;
    FluxConfirm confirm;
    int confirmed;
} AgentDemo;

static void agent_reset(AgentDemo *ag) {
    int ms = ag->agent_tick_ms;
    memset(ag, 0, sizeof *ag);
    ag->agent_tick_ms = ms > 0 ? ms : 40;
    ag->phase = AGENT_INPUT;
    flux_spinner_init(&ag->spinner, FLUX_SPINNER_DOT, FLUX_SPINNER_DOT_N,
                    DM "Thinking..." RS);
}

static void agent_step(AgentDemo *ag) {
    switch (ag->phase) {
        case AGENT_INPUT:
            ag->char_idx++;
            if (ag->char_idx >= (int)strlen(AGENT_MSG))
                { ag->phase = AGENT_THINK; ag->think_ticks = 0; }
            break;
        case AGENT_THINK:
            flux_spinner_tick(&ag->spinner);
            if (++ag->think_ticks >= 30)
                { ag->phase = AGENT_RESPOND; ag->char_idx = 0; }
            break;
        case AGENT_RESPOND:
            ag->char_idx += 2;
            if (ag->char_idx >= (int)strlen(AGENT_RESP)) {
                ag->char_idx = (int)strlen(AGENT_RESP);
                ag->phase = AGENT_DIFF; ag->think_ticks = 0;
            }
            break;
        case AGENT_DIFF:
            if (++ag->think_ticks >= 20) {
                ag->phase = AGENT_CONFIRM;
                flux_confirm_init(&ag->confirm, "  Apply changes?",
                    "  Modify server.c with the health endpoint",
                    "Allow", "Deny");
            }
            break;
        default: break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * KANBAN CSV persistence
 * ═══════════════════════════════════════════════════════════════ */
#define KB_CSV_PATH "kanban.csv"

static void csv_write_field(FILE *f, const char *s) {
    fputc('"', f);
    while (*s) { if (*s == '"') fputc('"', f); fputc(*s, f); s++; }
    fputc('"', f);
}

static void csv_read_field(char *dst, int dstsz, const char **p) {
    dst[0] = '\0';
    if (!*p || !**p) return;
    int di = 0;
    if (**p == '"') {
        (*p)++;
        while (**p) {
            if (**p == '"') {
                (*p)++;
                if (**p == '"') {
                    if (di < dstsz - 1) dst[di++] = '"';
                    (*p)++;
                } else break;
            } else {
                if (di < dstsz - 1) dst[di++] = **p;
                (*p)++;
            }
        }
        if (**p == ',') (*p)++;
    } else {
        while (**p && **p != ',' && **p != '\n' && **p != '\r') {
            if (di < dstsz - 1) dst[di++] = **p;
            (*p)++;
        }
        if (**p == ',') (*p)++;
    }
    dst[di] = '\0';
}

static void kanban_save(FluxKanban *kb) {
    FILE *f = fopen(KB_CSV_PATH, "w");
    if (!f) return;
    fprintf(f, "column,title,description\n");
    for (int c = 0; c < kb->ncols; c++) {
        for (int r = 0; r < kb->cols[c].count; r++) {
            FluxKbCard *card = &kb->cols[c].cards[r];
            csv_write_field(f, kb->cols[c].name);
            fputc(',', f);
            csv_write_field(f, card->title);
            fputc(',', f);
            csv_write_field(f, card->desc);
            fputc('\n', f);
        }
    }
    fclose(f);
}

static int kanban_load(FluxKanban *kb) {
    FILE *f = fopen(KB_CSV_PATH, "r");
    if (!f) return 0;
    for (int c = 0; c < kb->ncols; c++) kb->cols[c].count = 0;
    char line[512];
    int first = 1;
    while (fgets(line, (int)sizeof line, f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (first) { first = 0; continue; }
        if (len == 0) continue;

        const char *p = line;
        char col_name[32], title[FLUX_KB_TITLE_MAX+1], desc[FLUX_KB_DESC_MAX+1];
        csv_read_field(col_name, (int)sizeof col_name, &p);
        csv_read_field(title, (int)sizeof title, &p);
        csv_read_field(desc, (int)sizeof desc, &p);

        for (int c = 0; c < kb->ncols; c++) {
            if (strcmp(kb->cols[c].name, col_name) == 0) {
                flux_kanban_add(kb, c, title, desc);
                break;
            }
        }
    }
    fclose(f);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 * APP STATE
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    int      screen;
    int      tick;          /* global tick counter */
    int      dash_tick_ms;  /* dashboard animation interval (ms) */
    int      mat_tick_ms;   /* matrix animation interval (ms)   */
    Dash     dash;
    Explorer exp;
    Cards    cards;
    Matrix   mat;
    LogState log;
    AgentDemo agent;
    FluxKanban kb;
} App;

/* ═══════════════════════════════════════════════════════════════
 * HELPERS
 * ═══════════════════════════════════════════════════════════════ */

/* Horizontal bar  ████░░░░  width cols, fill 0.0-1.0 */
static void render_bar(FluxSB *sb, float v, int width,
                       const char *fill_c, const char *empty_c) {
    int filled = (int)(v * width + 0.5f);
    if (filled > width) filled = width;
    flux_sb_append(sb, fill_c);
    for (int i = 0; i < filled;       i++) flux_sb_append(sb, "█");
    flux_sb_append(sb, empty_c);
    for (int i = filled; i < width;   i++) flux_sb_append(sb, "░");
    flux_sb_append(sb, RS);
}

/* Sparkline from ring buffer — 8 levels using Braille dots */
static const char *SPARK_CHARS[] = {" ","▁","▂","▃","▄","▅","▆","▇","█"};
static void render_spark(FluxSB *sb, float *ring, int len, int head,
                         const char *color) {
    flux_sb_append(sb, color);
    for (int i = 0; i < len; i++) {
        int idx = (head - len + i + len*4) % len;
        int lv = (int)(ring[idx] * 8.0f);
        if (lv < 0) { lv = 0; } else if (lv > 8) { lv = 8; }
        flux_sb_append(sb, SPARK_CHARS[lv]);
    }
    flux_sb_append(sb, RS);
}

/* A coloured percentage string */
static void pct_color(char *dst, int dstsz, float v) {
    const char *c = v > 0.8f ? RE : v > 0.5f ? AM : GR;
    snprintf(dst, dstsz, "%s%s%.0f%%%s", c, BD, v*100.0f, RS);
}

/* ═══════════════════════════════════════════════════════════════
 * TAB BAR
 * ═══════════════════════════════════════════════════════════════ */
static void render_tabbar(App *a, FluxSB *sb) {
    flux_sb_append(sb, BG0 DM "  ");
    for (int i = 0; i < SCR_N; i++) {
        if (i == a->screen) {
            flux_sb_appendf(sb, RS BG2 CY BD " %s %s " RS BG0 DM,
                          TAB_ICONS[i], TAB_LABELS[i]);
        } else {
            flux_sb_appendf(sb, " %s %s ", TAB_ICONS[i], TAB_LABELS[i]);
        }
        if (i < SCR_N-1) flux_sb_append(sb, "│");
    }
    flux_sb_appendf(sb, RS BG0 DM
        "  [1-8] jump  [Tab] next  [q] quit" RS "\n");

    /* underline */
    flux_sb_append(sb, DM);
    int W = flux_cols();
    for (int i = 0; i < W; i++) flux_sb_append(sb, "─");
    flux_sb_append(sb, RS "\n");
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 0: DASHBOARD
 * ═══════════════════════════════════════════════════════════════ */
static void dash_update(Dash *d) {
    /* pseudo-random walk with seed from tick */
    static unsigned int s = 0xDEADBEEF;
    float r1 = (_rng(&s) % 1000) / 1000.0f;
    float r2 = (_rng(&s) % 1000) / 1000.0f;
    float r3 = (_rng(&s) % 1000) / 1000.0f;
    float r4 = (_rng(&s) % 1000) / 1000.0f;

    /* lerp toward random target */
    d->cpu_now  = d->cpu_now  * 0.85f + r1 * 0.15f;
    d->mem_now  = d->mem_now  * 0.92f + (0.45f + r2*0.3f) * 0.08f;
    d->net_now  = d->net_now  * 0.7f  + r3 * 0.3f;
    d->disk_now = d->disk_now * 0.97f + (0.3f  + r4*0.1f) * 0.03f;

    d->cpu[d->head]  = d->cpu_now;
    d->mem[d->head]  = d->mem_now;
    d->net[d->head]  = d->net_now;
    d->disk[d->head] = d->disk_now;
    d->head = (d->head + 1) % SPARK_LEN;
    d->tick++;
}

static void render_metric_row(FluxSB *sb,
                               const char *label,
                               float val, float *ring, int head,
                               const char *spark_c, const char *bar_c) {
    char pct[64]; pct_color(pct, sizeof pct, val);

    flux_sb_appendf(sb, "  %s%s%-6s%s  ", spark_c, BD, label, RS);
    flux_sb_append(sb, pct);
    flux_sb_append(sb, "  ");
    render_bar(sb, val, 20, bar_c, DM);
    flux_sb_append(sb, "  ");
    render_spark(sb, ring, SPARK_LEN, head, spark_c);
    flux_sb_nl(sb);
}

static void render_clock(FluxSB *sb, int tick) {
    /* fake HH:MM:SS from tick */
    int ss = tick % 60;
    int mm = (tick / 60) % 60;
    int hh = (tick / 3600) % 24;
    flux_sb_appendf(sb, AM BD "  %02d:%02d:%02d" RS, hh, mm, ss);
}

static void render_dash(App *a, FluxSB *sb) {
    Dash *d = &a->dash;

    /* ── header row ── */
    flux_sb_append(sb, "\n");
    flux_sb_append(sb, CY BD "  ◈ System Monitor  " RS);
    render_clock(sb, d->tick);
    flux_sb_append(sb, DM "   uptime: ");
    flux_sb_appendf(sb, WH "%dd %02dh %02dm" RS
                  DM "   ⚡ " AM BD "%dms" RS DM " [+/-]" RS "\n\n",
                  d->tick/86400, (d->tick/3600)%24, (d->tick/60)%60,
                  a->dash_tick_ms);

    /* ── metrics ── */
    render_metric_row(sb, "CPU",  d->cpu_now,  d->cpu,  d->head, CY, CY);
    render_metric_row(sb, "MEM",  d->mem_now,  d->mem,  d->head, PU, PU);
    render_metric_row(sb, "NET",  d->net_now,  d->net,  d->head, BL, BL);
    render_metric_row(sb, "DISK", d->disk_now, d->disk, d->head, AM, AM);

    /* ── divider ── */
    flux_sb_append(sb, "\n");
    flux_sb_append(sb, DM "  ");
    for (int i = 0; i < 62; i++) flux_sb_append(sb, "─");
    flux_sb_append(sb, RS "\n\n");

    /* ── status boxes (3 across) ── */
    /* box 1: processes */
    char b1[512];
    snprintf(b1, sizeof b1,
        GR BD "  Processes" RS "\n"
        "\n"
        WH "  running  " GR BD "  4" RS "\n"
        WH "  sleeping " DM "  142" RS "\n"
        WH "  zombie   " RE BD "  0" RS "\n"
        "\n"
        WH "  threads  " AM BD "  892" RS);
    char bx1[2048]; flux_box(bx1,sizeof bx1, b1, &FLUX_BORDER_ROUNDED, 22, GR, NULL);

    /* box 2: memory detail */
    char b2[512];
    float used = d->mem_now;
    float buf_v = 0.12f, cache_v = 0.18f;
    char upct[32], bpct[32], cpct[32];
    pct_color(upct, sizeof upct, used);
    pct_color(bpct, sizeof bpct, buf_v);
    pct_color(cpct, sizeof cpct, cache_v);
    snprintf(b2, sizeof b2,
        PU BD "  Memory" RS "\n"
        "\n"
        WH "  used    %s" RS "\n"
        WH "  buffers %s" RS "\n"
        WH "  cache   %s" RS "\n"
        "\n"
        WH "  total   " AM BD "16.0 GB" RS,
        upct, bpct, cpct);
    char bx2[2048]; flux_box(bx2,sizeof bx2, b2, &FLUX_BORDER_ROUNDED, 22, PU, NULL);

    /* box 3: network */
    char b3[512];
    int rx_kb = (int)(d->net_now * 9800 + 100);
    int tx_kb = (int)(d->net_now * 3200 + 50);
    snprintf(b3, sizeof b3,
        BL BD "  Network" RS "\n"
        "\n"
        WH "  ↓ rx  " GR BD "%6d KB/s" RS "\n"
        WH "  ↑ tx  " AM BD "%6d KB/s" RS "\n"
        "\n"
        WH "  iface  " DM "eth0" RS "\n"
        WH "  mtu    " DM "1500" RS,
        rx_kb, tx_kb);
    char bx3[2048]; flux_box(bx3,sizeof bx3, b3, &FLUX_BORDER_ROUNDED, 22, BL, NULL);

    /* print boxes side by side */
    /* split each into lines, print col by col */
    #define MAXL 32
    char c1[2048], c2[2048], c3[2048];
    snprintf(c1,sizeof c1,"%s",bx1); snprintf(c2,sizeof c2,"%s",bx2); snprintf(c3,sizeof c3,"%s",bx3);
    char *l1[MAXL]={0}, *l2[MAXL]={0}, *l3[MAXL]={0};
    int n1=0,n2=0,n3=0;
    for(char *p=strtok(c1,"\n"); p&&n1<MAXL; p=strtok(NULL,"\n")) l1[n1++]=p;
    for(char *p=strtok(c2,"\n"); p&&n2<MAXL; p=strtok(NULL,"\n")) l2[n2++]=p;
    for(char *p=strtok(c3,"\n"); p&&n3<MAXL; p=strtok(NULL,"\n")) l3[n3++]=p;
    int nmax = n1>n2?n1:n2; if(n3>nmax) nmax=n3;
    for (int i=0; i<nmax; i++) {
        char row[1024]; row[0]='\0';
        char col[256];
        flux_pad(col,sizeof col, i<n1?l1[i]:"", 26); SCAT(row,col); SCAT(row,"  ");
        flux_pad(col,sizeof col, i<n2?l2[i]:"", 26); SCAT(row,col); SCAT(row,"  ");
        flux_pad(col,sizeof col, i<n3?l3[i]:"", 26); SCAT(row,col);
        flux_sb_append(sb, "  "); flux_sb_append(sb, row); flux_sb_nl(sb);
    }
    #undef MAXL
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 1: EXPLORER
 * ═══════════════════════════════════════════════════════════════ */
static void render_explorer(App *a, FluxSB *sb) {
    Explorer *e = &a->exp;
    flux_sb_append(sb, "\n");

    /* left pane header */
    int lw = 32, rw = 38;

    char lhdr[256], rhdr[256];
    snprintf(lhdr, sizeof lhdr,
             GR BD " ⊞ Files " RS DM "(%d entries)" RS, FS_N);
    snprintf(rhdr, sizeof rhdr,
             CY BD " ◈ Preview" RS);

    char lpad[256], rpad[256];
    flux_pad(lpad, sizeof lpad, lhdr, lw);
    flux_pad(rpad, sizeof rpad, rhdr, rw);
    flux_sb_appendf(sb, "  %s  │  %s\n", lpad, rpad);
    flux_sb_append(sb, "  ");
    for (int i=0;i<lw;i++) flux_sb_append(sb,DM "─" RS);
    flux_sb_append(sb, "  ┼  ");
    for (int i=0;i<rw;i++) flux_sb_append(sb,DM "─" RS);
    flux_sb_append(sb, "\n");

    /* rows */
    int vis = 14;
    FsEntry *sel = &FS_ENTRIES[e->cursor];

    for (int i = e->scroll; i < FS_N && i < e->scroll+vis; i++) {
        FsEntry *f = &FS_ENTRIES[i];
        int is_cur = (i == e->cursor);
        int is_dir = strcmp(f->type,"dir")==0;

        /* left pane row */
        char row[256]; row[0]='\0';
        if (is_cur) SCAT(row, BGX CY BD);
        else        SCAT(row, BG1 DM);

        char icon[8];
        snprintf(icon, sizeof icon, "%s", is_dir ? "📁" : "  ");
        char entry[64];
        snprintf(entry, sizeof entry, " %s %-18s %5s  %s",
                 is_dir?"▶":"·", f->name, f->size, f->perms);

        char padded[80];
        flux_pad(padded, sizeof padded, entry, lw-1);
        SCAT(row, padded); SCAT(row, RS);

        /* right pane: preview of selected */
        int ri = i - e->scroll;
        const char *prev = "";
        if (i == e->cursor) {
            /* shown per line below */
        }
        (void)prev;

        flux_sb_append(sb, "  ");
        flux_sb_append(sb, row);
        flux_sb_append(sb, "  │  ");

        /* preview lines alongside list */
        if (ri < sel->preview_lines && sel->preview_lines > 0) {
            char prow[80];
            flux_pad(prow, sizeof prow, sel->preview[ri], rw);
            flux_sb_append(sb, DM); flux_sb_append(sb, prow); flux_sb_append(sb, RS);
        } else if (ri == 0 && sel->preview_lines == 0) {
            char drow[80];
            flux_pad(drow, sizeof drow, GR BD " (directory)" RS, rw);
            flux_sb_append(sb, drow);
        } else {
            for(int j=0;j<rw;j++) flux_sb_append(sb," ");
        }
        flux_sb_nl(sb);
    }

    /* scrollbar hint */
    flux_sb_appendf(sb, "\n" DM
        "  ↑/↓ j/k navigate   Enter open   [%d/%d]" RS "\n",
        e->cursor+1, FS_N);
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 2: CARDS
 * ═══════════════════════════════════════════════════════════════ */
static void render_cards(App *a, FluxSB *sb) {
    Cards *c = &a->cards;
    flux_sb_append(sb, "\n");
    flux_sb_appendf(sb,
        "  " CY BD "◈ Project Cards" RS DM
        "   Space: select/deselect   ↑↓←→ navigate\n\n" RS);

    int cols = 3;
    int card_w = 24;

    for (int row = 0; row < (CARD_N+cols-1)/cols; row++) {
        /* build up to `cols` boxes then print side by side */
        char boxes[3][2048]; int nbox=0;
        for (int col=0; col<cols; col++) {
            int idx = row*cols+col;
            if (idx >= CARD_N) break;
            Card *cd = &CARDS[idx];
            int is_cur = (idx == c->cursor);
            int is_sel = cd->selected;
            const char *bc = is_cur ? (is_sel ? GR : CY) : (is_sel ? GR : DM);
            const FluxBorder *brd = is_sel ? &FLUX_BORDER_DOUBLE :
                                  is_cur ? &FLUX_BORDER_THICK  : &FLUX_BORDER_SHARP;

            char content[256];
            snprintf(content, sizeof content,
                "%s%s %s %s" RS "\n"
                "%s%s" RS "\n"
                "\n"
                "%s[%s]" RS,
                bc, BD, cd->icon, cd->title,
                DM, cd->subtitle,
                cd->tag_color, cd->tag);

            flux_box(boxes[nbox], sizeof boxes[nbox], content, brd, card_w, bc, NULL);
            nbox++;
        }

        /* side-by-side */
        char *lines[3][16]; int lnc[3]={0,0,0};
        char tmp[3][2048];
        for(int i=0;i<nbox;i++) {
            strncpy(tmp[i],boxes[i],sizeof tmp[i]-1);
            for(char *p=strtok(tmp[i],"\n"); p&&lnc[i]<16; p=strtok(NULL,"\n"))
                lines[i][lnc[i]++]=p;
        }
        int lmax=lnc[0]; for(int i=1;i<nbox;i++) if(lnc[i]>lmax)lmax=lnc[i];
        for(int i=0;i<lmax;i++) {
            flux_sb_append(sb,"  ");
            for(int j=0;j<nbox;j++) {
                char col[256];
                flux_pad(col,sizeof col, i<lnc[j]?lines[j][i]:"", card_w+2);
                flux_sb_append(sb,col);
                flux_sb_append(sb,"  ");
            }
            flux_sb_nl(sb);
        }
        flux_sb_nl(sb);
    }

    /* selected count */
    int nsel=0; for(int i=0;i<CARD_N;i++) if(CARDS[i].selected) nsel++;
    if (nsel > 0) {
        flux_sb_appendf(sb, "  " GR BD "%d card%s selected" RS "\n",
                      nsel, nsel==1?"":"s");
    }
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 3: MATRIX RAIN
 * ═══════════════════════════════════════════════════════════════ */
static const char MAT_GLYPHS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "アイウエオカキクケコサシスセソタチツテトナニヌネノ"
    "@#$%&*<>{}[]?!/\\|";
#define MAT_GLYPH_ASCII_N 36

static void mat_init(Matrix *m) {
    m->rng = 0xCAFEF00D;
    for (int c=0; c<MAT_COLS; c++) {
        m->pos[c] = -(int)(_rng(&m->rng) % MAT_ROWS);
        m->len[c] = 4 + _rng(&m->rng) % 12;
        m->spd[c] = 1 + _rng(&m->rng) % 3;
        m->ctr[c] = 0;
        for (int r=0; r<MAT_ROWS; r++)
            m->chr[c][r] = MAT_GLYPHS[_rng(&m->rng) % MAT_GLYPH_ASCII_N];
    }
    m->active = 1;
}

static void mat_step(Matrix *m) {
    for (int c=0; c<MAT_COLS; c++) {
        m->ctr[c]++;
        if (m->ctr[c] >= m->spd[c]) {
            m->ctr[c] = 0;
            m->pos[c]++;
            if (m->pos[c] > MAT_ROWS + m->len[c]) {
                m->pos[c] = -(int)(_rng(&m->rng) % (MAT_ROWS/2));
                m->len[c] = 4 + _rng(&m->rng) % 12;
                m->spd[c] = 1 + _rng(&m->rng) % 3;
            }
            /* randomise some characters in trail */
            for (int r=0; r<MAT_ROWS; r++)
                if (_rng(&m->rng) % 8 == 0)
                    m->chr[c][r] = MAT_GLYPHS[_rng(&m->rng) % MAT_GLYPH_ASCII_N];
        }
    }
}

static void render_matrix(App *a, FluxSB *sb) {
    Matrix *m = &a->mat;
    flux_sb_append(sb, "\n");
    flux_sb_appendf(sb,
        "  " GR BD "⋮ Matrix Rain" RS DM
        "   [s] toggle   [r] reset   " AM "⚡ %dms" RS DM " [+/-]\n\n" RS,
        a->mat_tick_ms);

    for (int row=0; row<MAT_ROWS; row++) {
        flux_sb_append(sb, "  ");
        for (int col=0; col<MAT_COLS; col++) {
            int head = m->pos[col];
            int dist = head - row;   /* dist from head: 0=head, 1,2...=trail */
            if (dist == 0) {
                /* head: bright white */
                flux_sb_appendf(sb, WH BD "%c" RS, m->chr[col][row]);
            } else if (dist > 0 && dist < m->len[col]) {
                /* trail: fade from bright to dim green */
                float f = 1.0f - (float)dist / m->len[col];
                int g = (int)(55 + f * 165);
                char esc[32]; snprintf(esc,sizeof esc,"\x1b[38;2;0;%d;30m",g);
                flux_sb_appendf(sb, "%s%c" RS, esc, m->chr[col][row]);
            } else {
                flux_sb_append(sb, BG0 " " RS);
            }
        }
        flux_sb_nl(sb);
    }

    flux_sb_appendf(sb, "\n  " DM "status: %s" RS "\n",
                  m->active ? GR "running" RS : RE "paused" RS);
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 4: LOG VIEWER
 * ═══════════════════════════════════════════════════════════════ */
static void log_push(const char *level, const char *msg, int tick) {
    if (LOG_N >= LOG_MAX) {
        /* shift */
        memmove(&LOG_ENTRIES[0], &LOG_ENTRIES[1],
                sizeof(LogEntry) * (LOG_MAX-1));
        LOG_N = LOG_MAX-1;
    }
    LogEntry *e = &LOG_ENTRIES[LOG_N++];
    strncpy(e->level, level, sizeof e->level - 1);
    snprintf(e->ts, sizeof e->ts, "%02d:%02d:%02d",
             (tick/3600)%24, (tick/60)%60, tick%60);
    strncpy(e->msg, msg, sizeof e->msg - 1);
}

static void render_log(App *a, FluxSB *sb) {
    LogState *l = &a->log;
    flux_sb_append(sb, "\n");
    flux_sb_appendf(sb,
        "  " AM BD "☰ Application Log" RS DM
        "   ↑/↓ scroll   [f] follow: %s   [c] clear\n\n" RS,
        l->follow ? GR BD "ON" RS : RE "OFF" RS);

    /* header */
    flux_sb_appendf(sb, "  %s%-8s  %-9s  %s%-60s" RS "\n",
                  DM, "LEVEL", "TIME", "", "MESSAGE");
    flux_sb_append(sb, DM "  ");
    for(int i=0;i<75;i++) flux_sb_append(sb,"─");
    flux_sb_append(sb, RS "\n");

    if (LOG_N == 0) {
        flux_sb_append(sb, DM "  (no log entries yet)\n" RS);
        return;
    }

    int start = l->scroll;
    if (start < 0) start = 0;
    if (start + LOG_VIS > LOG_N) start = LOG_N - LOG_VIS;
    if (start < 0) start = 0;

    for (int i = start; i < LOG_N && i < start + LOG_VIS; i++) {
        LogEntry *e = &LOG_ENTRIES[i];
        const char *lc;
        if      (strcmp(e->level,"ERROR")==0) lc = RE BD;
        else if (strcmp(e->level,"WARN") ==0) lc = AM BD;
        else if (strcmp(e->level,"DEBUG")==0) lc = DM;
        else                                   lc = GR;

        char lvl[64]; snprintf(lvl,sizeof lvl, "%s%-5s" RS, lc, e->level);
        char ts[32];  snprintf(ts, sizeof ts,  DM "%s" RS, e->ts);

        char padlvl[64], padts[64], padmsg[96];
        flux_pad(padlvl, sizeof padlvl, lvl, 8);
        flux_pad(padts,  sizeof padts,  ts,  9);
        flux_pad(padmsg, sizeof padmsg, e->msg, 62);

        flux_sb_appendf(sb, "  %s  %s  %s%s" RS "\n",
                      padlvl, padts,
                      strcmp(e->level,"ERROR")==0 ? RE :
                      strcmp(e->level,"WARN")==0  ? AM : WH,
                      padmsg);
    }

    /* scroll indicator */
    int total = LOG_N;
    flux_sb_append(sb, DM "  ");
    for(int i=0;i<75;i++) flux_sb_append(sb,"─");
    flux_sb_append(sb, RS "\n");
    flux_sb_appendf(sb, DM "  lines %d-%d of %d%s" RS "\n",
                  start+1, (start+LOG_VIS<total?start+LOG_VIS:total), total,
                  l->follow ? "  [following]" : "");
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 5: HELP
 * ═══════════════════════════════════════════════════════════════ */
static void render_help(App *a, FluxSB *sb) {
    (void)a;
    flux_sb_append(sb, "\n");

    char left[2048]; left[0]='\0';
    SCAT(left, CY BD " ◈ flux.h" RS "\n");
    SCAT(left, DM IT " Elm Architecture TUI for C99" RS "\n\n");
    SCAT(left, WH BD "Architecture:" RS "\n");
    SCAT(left, DM "  Init()   → FluxCmd\n");
    SCAT(left, "  Update() → (Model,Msg) → FluxCmd\n");
    SCAT(left, "  View()   → Model → string\n\n" RS);
    SCAT(left, WH BD "Cmd types:" RS "\n");
    SCAT(left, GR "  FLUX_CMD_NONE\n");
    SCAT(left, GR "  FLUX_CMD_QUIT\n");
    SCAT(left, GR "  FLUX_TICK(ms)\n");
    SCAT(left, GR "  flux_cmd_custom(id,data)" RS "\n\n");
    SCAT(left, WH BD "Msg types:" RS "\n");
    SCAT(left, BL "  MSG_KEY   MSG_WINSIZE\n");
    SCAT(left, BL "  MSG_TICK  MSG_CUSTOM\n" RS);
    SCAT(left, "\n" WH BD "Components:" RS "\n");
    SCAT(left, GR "  FluxList    FluxTabs\n");
    SCAT(left, GR "  FluxInput   FluxConfirm\n");
    SCAT(left, GR "  FluxSpinner FluxTable\n");
    SCAT(left, GR "  FluxKanban" RS "\n");

    char right[2048]; right[0]='\0';
    SCAT(right, AM BD " ◈ Keybindings" RS "\n\n");
    SCAT(right, WH BD "Global:" RS "\n");
    SCAT(right, CY "  1-8" RS DM "   jump to tab\n" RS);
    SCAT(right, CY "  Tab" RS DM "   next tab\n" RS);
    SCAT(right, CY "  q  " RS DM "   quit\n\n" RS);
    SCAT(right, WH BD "Dashboard:" RS "\n");
    SCAT(right, DM "  (live updates automatically)\n\n" RS);
    SCAT(right, WH BD "Explorer:" RS "\n");
    SCAT(right, CY "  ↑↓/jk" RS DM "  navigate\n" RS);
    SCAT(right, CY "  Enter " RS DM "  select\n\n" RS);
    SCAT(right, WH BD "Cards:" RS "\n");
    SCAT(right, CY "  ↑↓←→  " RS DM "navigate\n" RS);
    SCAT(right, CY "  Space  " RS DM "select/deselect\n\n" RS);
    SCAT(right, WH BD "Matrix:" RS "\n");
    SCAT(right, CY "  s" RS DM "  toggle   " RS CY "r" RS DM "  reset\n\n" RS);
    SCAT(right, WH BD "Log:" RS "\n");
    SCAT(right, CY "  ↑↓" RS DM "  scroll   " RS CY "f" RS DM "  follow   " RS CY "c" RS DM "  clear" RS);

    /* equalize content heights before boxing */
    { int ll = flux_count_lines(left), rl = flux_count_lines(right);
      int target = ll > rl ? ll : rl;
      flux_pad_lines(left, sizeof left, target);
      flux_pad_lines(right, sizeof right, target); }

    char lbox[4096];
    flux_box(lbox, sizeof lbox, left, &FLUX_BORDER_DOUBLE, 36, CY, NULL);
    char rbox[4096];
    flux_box(rbox, sizeof rbox, right, &FLUX_BORDER_DOUBLE, 34, AM, NULL);

    /* side by side using flux_hbox */
    {
        const char *panels[] = {lbox, rbox};
        const int widths[] = {40, 38};
        FluxSB hsb; char hbuf[8192];
        flux_sb_init(&hsb, hbuf, sizeof hbuf);
        flux_hbox(&hsb, panels, widths, 2, "  ");
        char *s = hbuf;
        while (*s) {
            flux_sb_append(sb, "  ");
            char *nl = strchr(s, '\n');
            if (nl) { *nl = '\0'; flux_sb_append(sb, s); flux_sb_nl(sb); s = nl+1; }
            else    { flux_sb_append(sb, s); flux_sb_nl(sb); break; }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 6: AGENT DEMO
 * ═══════════════════════════════════════════════════════════════ */
static void render_agent(App *a, FluxSB *sb) {
    AgentDemo *ag = &a->agent;
    flux_sb_append(sb, "\n");
    flux_sb_appendf(sb, "  " CY BD "◉ Agent" RS DM
        "   [r] restart   " AM "⚡ %dms" RS DM " [+/-]\n\n" RS,
        ag->agent_tick_ms);

    /* user message (auto-typed) */
    if (ag->phase >= AGENT_INPUT) {
        int len = (ag->phase == AGENT_INPUT)
                  ? ag->char_idx : (int)strlen(AGENT_MSG);
        flux_sb_append(sb, "  " DM ">" RS " " WH BD);
        for (int i = 0; i < len && AGENT_MSG[i]; i++)
            flux_sb_appendf(sb, "%c", AGENT_MSG[i]);
        if (ag->phase == AGENT_INPUT)
            flux_sb_append(sb, "\x1b[7m \x1b[0m");
        flux_sb_append(sb, RS "\n\n");
    }

    /* thinking spinner */
    if (ag->phase == AGENT_THINK) {
        flux_sb_append(sb, "  ");
        flux_spinner_render(&ag->spinner, sb, CY);
        flux_sb_append(sb, "\n\n");
    }

    /* streamed response */
    if (ag->phase >= AGENT_RESPOND) {
        int len = (ag->phase == AGENT_RESPOND)
                  ? ag->char_idx : (int)strlen(AGENT_RESP);
        flux_sb_append(sb, "  " WH);
        for (int i = 0; i < len && AGENT_RESP[i]; i++) {
            if (AGENT_RESP[i] == '\n')
                flux_sb_append(sb, RS "\n  " WH);
            else
                flux_sb_appendf(sb, "%c", AGENT_RESP[i]);
        }
        if (ag->phase == AGENT_RESPOND)
            flux_sb_append(sb, "\x1b[7m \x1b[0m");
        flux_sb_append(sb, RS "\n\n");
    }

    /* diff view */
    if (ag->phase >= AGENT_DIFF) {
        flux_sb_append(sb, "  ");
        flux_divider(sb, 50, DM);
        flux_sb_nl(sb);
        for (int i = 0; i < AGENT_DIFF_N; i++) {
            const char *line = AGENT_DIFF_LINES[i];
            flux_sb_append(sb, "  ");
            if (line[0] == '+')
                flux_sb_appendf(sb, GR "%s" RS, line);
            else if (line[0] == '-')
                flux_sb_appendf(sb, RE "%s" RS, line);
            else
                flux_sb_appendf(sb, DM "%s" RS, line);
            flux_sb_nl(sb);
        }
        flux_sb_append(sb, "  ");
        flux_divider(sb, 50, DM);
        flux_sb_append(sb, "\n\n");
    }

    /* confirmation dialog */
    if (ag->phase == AGENT_CONFIRM) {
        FluxSB csb; char cbuf[4096];
        flux_sb_init(&csb, cbuf, sizeof cbuf);
        flux_confirm_render(&ag->confirm, &csb, 46, AM, GR, RE);
        char *s = cbuf;
        while (*s) {
            flux_sb_append(sb, "  ");
            char *nl = strchr(s, '\n');
            if (nl) { *nl = '\0'; flux_sb_append(sb, s); flux_sb_nl(sb); s = nl+1; }
            else    { flux_sb_append(sb, s); flux_sb_nl(sb); break; }
        }
    }

    /* done */
    if (ag->phase == AGENT_DONE) {
        if (ag->confirmed)
            flux_sb_append(sb, "  " GR BD "✓ Changes applied to server.c" RS "\n");
        else
            flux_sb_append(sb, "  " RE BD "✗ Changes denied" RS "\n");
        flux_sb_append(sb, "\n  " DM "Press [r] to restart" RS "\n");
    }
}

/* ═══════════════════════════════════════════════════════════════
 * SCREEN 7: KANBAN BOARD
 * ═══════════════════════════════════════════════════════════════ */
static const char *KB_COLORS[] = {AM, BL, GR};

static void render_kanban(App *a, FluxSB *sb) {
    flux_sb_append(sb, "\n");
    flux_kanban_render(&a->kb, sb, KB_COLORS,
        DM "  ↑↓←→ navigate  [Enter] grab/drop  [e]dit  [n]ew  [d]elete" RS "\n");
}

/* ═══════════════════════════════════════════════════════════════
 * VIEW
 * ═══════════════════════════════════════════════════════════════ */
static void app_view(FluxModel *m, char *buf, int sz) {
    App *a = m->state;
    FluxSB sb; flux_sb_init(&sb, buf, sz);

    render_tabbar(a, &sb);

    switch (a->screen) {
        case SCR_DASH:     render_dash    (a, &sb); break;
        case SCR_EXPLORER: render_explorer(a, &sb); break;
        case SCR_CARDS:    render_cards   (a, &sb); break;
        case SCR_MATRIX:   render_matrix  (a, &sb); break;
        case SCR_LOG:      render_log     (a, &sb); break;
        case SCR_HELP:     render_help    (a, &sb); break;
        case SCR_AGENT:    render_agent   (a, &sb); break;
        case SCR_KANBAN:   render_kanban  (a, &sb); break;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * TICK COMMANDS  (run in threads, sleep, post custom msg)
 * ═══════════════════════════════════════════════════════════════ */
#define MSG_TICK_DASH   1
#define MSG_TICK_MAT    2
#define MSG_TICK_LOG    3
#define MSG_TICK_AGENT  4

typedef struct { int ms; int id; } _TickCtx;
static FluxMsg _tick_fn(void *ctx) {
    _TickCtx *t = (_TickCtx*)ctx;
    int ms = t->ms, id = t->id;
    free(t);
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
    FluxMsg m; memset(&m, 0, sizeof m);
    m.type = MSG_CUSTOM; m.u.custom.id = id;
    return m;
}
static FluxCmd tick_with_delay(int id, int ms) {
    _TickCtx *c = malloc(sizeof *c); c->ms = ms; c->id = id;
    return (FluxCmd){ _tick_fn, c };
}

/* ═══════════════════════════════════════════════════════════════
 * UPDATE
 * ═══════════════════════════════════════════════════════════════ */

static FluxCmd app_update(FluxModel *m, FluxMsg msg) {
    App *a = m->state;

    if (msg.type == MSG_KEY) {
        /* Kanban edit mode intercepts ALL keys */
        if (a->screen == SCR_KANBAN && a->kb.mode == FLUX_KB_EDIT) {
            flux_kanban_update(&a->kb, msg);
            if (flux_kanban_dirty(&a->kb)) kanban_save(&a->kb);
            return FLUX_CMD_NONE;
        }

        if (flux_key_is(msg,"ctrl+c") || flux_key_is(msg,"q"))
            return FLUX_CMD_QUIT;
        if (flux_key_is(msg,"tab"))
            { a->screen = (a->screen+1) % SCR_N; return FLUX_CMD_NONE; }
        if (flux_key_is(msg,"shift+tab"))
            { a->screen = (a->screen-1+SCR_N) % SCR_N; return FLUX_CMD_NONE; }
        /* direct jump */
        if (msg.u.key.key[0]>='1' && msg.u.key.key[0]<='8' && msg.u.key.key[1]=='\0')
            { a->screen = msg.u.key.key[0]-'1'; return FLUX_CMD_NONE; }

        /* tick rate: + speeds up (shorter interval), - slows down */
        if (flux_key_is(msg,"+") || flux_key_is(msg,"=")) {
            if (a->screen==SCR_DASH)   { a->dash_tick_ms = a->dash_tick_ms*2/3; if(a->dash_tick_ms<16) a->dash_tick_ms=16; }
            if (a->screen==SCR_MATRIX) { a->mat_tick_ms  = a->mat_tick_ms *2/3; if(a->mat_tick_ms <16) a->mat_tick_ms =16; }
            if (a->screen==SCR_AGENT)  { a->agent.agent_tick_ms = a->agent.agent_tick_ms*2/3; if(a->agent.agent_tick_ms<16) a->agent.agent_tick_ms=16; }
            return FLUX_CMD_NONE;
        }
        if (flux_key_is(msg,"-")) {
            if (a->screen==SCR_DASH)   { a->dash_tick_ms = a->dash_tick_ms*3/2; if(a->dash_tick_ms>2000) a->dash_tick_ms=2000; }
            if (a->screen==SCR_MATRIX) { a->mat_tick_ms  = a->mat_tick_ms *3/2; if(a->mat_tick_ms >2000) a->mat_tick_ms =2000; }
            if (a->screen==SCR_AGENT)  { a->agent.agent_tick_ms = a->agent.agent_tick_ms*3/2; if(a->agent.agent_tick_ms>2000) a->agent.agent_tick_ms=2000; }
            return FLUX_CMD_NONE;
        }

        switch (a->screen) {
            case SCR_EXPLORER: {
                Explorer *e = &a->exp;
                if (flux_key_is(msg,"up")||flux_key_is(msg,"k"))
                    { if(e->cursor>0){e->cursor--;if(e->cursor<e->scroll)e->scroll--;} }
                if (flux_key_is(msg,"down")||flux_key_is(msg,"j"))
                    { if(e->cursor<FS_N-1){e->cursor++;if(e->cursor>=e->scroll+14)e->scroll++;} }
                break;
            }
            case SCR_CARDS: {
                Cards *c = &a->cards;
                int cols=3;
                if (flux_key_is(msg,"up")||flux_key_is(msg,"k"))
                    { c->cursor-=cols; if(c->cursor<0) c->cursor=0; }
                if (flux_key_is(msg,"down")||flux_key_is(msg,"j"))
                    { c->cursor+=cols; if(c->cursor>=CARD_N) c->cursor=CARD_N-1; }
                if (flux_key_is(msg,"left")||flux_key_is(msg,"h"))
                    { if(c->cursor%cols>0) c->cursor--; }
                if (flux_key_is(msg,"right")||flux_key_is(msg,"l"))
                    { if(c->cursor%cols<cols-1 && c->cursor<CARD_N-1) c->cursor++; }
                if (flux_key_is(msg," "))
                    CARDS[c->cursor].selected ^= 1;
                break;
            }
            case SCR_MATRIX: {
                if (flux_key_is(msg,"s")) a->mat.active ^= 1;
                if (flux_key_is(msg,"r")) mat_init(&a->mat);
                break;
            }
            case SCR_LOG: {
                LogState *l = &a->log;
                if (flux_key_is(msg,"up")||flux_key_is(msg,"k"))
                    { l->scroll--; l->follow=0; if(l->scroll<0) l->scroll=0; }
                if (flux_key_is(msg,"down")||flux_key_is(msg,"j"))
                    { l->scroll++; l->follow=0; }
                if (flux_key_is(msg,"f")) l->follow ^= 1;
                if (flux_key_is(msg,"c")) { LOG_N=0; l->scroll=0; }
                break;
            }
            case SCR_AGENT: {
                AgentDemo *ag = &a->agent;
                if (flux_key_is(msg,"r")) { agent_reset(ag); break; }
                if (ag->phase == AGENT_CONFIRM) {
                    flux_confirm_update(&ag->confirm, msg);
                    if (ag->confirm.answered) {
                        ag->confirmed = ag->confirm.result;
                        ag->phase = AGENT_DONE;
                    }
                }
                break;
            }
            case SCR_KANBAN: {
                flux_kanban_update(&a->kb, msg);
                if (flux_kanban_dirty(&a->kb)) kanban_save(&a->kb);
                break;
            }
        }
        return FLUX_CMD_NONE;
    }

    if (msg.type == MSG_CUSTOM) {
        if (msg.u.custom.id == MSG_TICK_DASH) {
            dash_update(&a->dash);
            a->tick++;
            /* also push a log entry occasionally */
            if (a->tick % 5 == 0) {
                int li = a->tick % LOG_MSGS_N;
                log_push(LOG_LEVELS[li], LOG_MSGS[li], a->dash.tick);
                if (a->log.follow) a->log.scroll = LOG_N;
            }
            return tick_with_delay(MSG_TICK_DASH, a->dash_tick_ms);
        }
        if (msg.u.custom.id == MSG_TICK_MAT) {
            if (a->mat.active) mat_step(&a->mat);
            return tick_with_delay(MSG_TICK_MAT, a->mat_tick_ms);
        }
        if (msg.u.custom.id == MSG_TICK_AGENT) {
            agent_step(&a->agent);
            return tick_with_delay(MSG_TICK_AGENT, a->agent.agent_tick_ms);
        }
    }

    return FLUX_CMD_NONE;
}

/* ── init: dispatch both tick loops simultaneously ── */
static FluxCmd app_init(FluxModel *m) {
    App *a = m->state;
    mat_init(&a->mat);
    a->log.follow = 1;
    /* seed dash */
    a->dash.cpu_now = 0.3f; a->dash.mem_now = 0.55f;
    a->dash.net_now = 0.1f; a->dash.disk_now = 0.35f;
    /* push initial log entries */
    for (int i=0; i<8; i++)
        log_push(LOG_LEVELS[i], LOG_MSGS[i], i*3);

    /* We can only return one cmd — dispatch mat immediately,
       return dash.  Both loops then self-reschedule. */
    _flux_dispatch_cmd(tick_with_delay(MSG_TICK_MAT, a->mat_tick_ms), _flux_pipe_w);
    _flux_dispatch_cmd(tick_with_delay(MSG_TICK_AGENT, a->agent.agent_tick_ms), _flux_pipe_w);
    return tick_with_delay(MSG_TICK_DASH, a->dash_tick_ms);
}

/* ═══════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════ */
int main(void) {
    static App state;
    memset(&state, 0, sizeof state);
    state.screen       = SCR_DASH;
    state.dash_tick_ms = 250;
    state.mat_tick_ms  = 60;
    state.agent.agent_tick_ms = 40;
    agent_reset(&state.agent);
    {
        static const char *kb_names[] = {"TODO", "IN PROGRESS", "DONE"};
        flux_kanban_init(&state.kb, 3, kb_names, 24);
        if (!kanban_load(&state.kb)) {
            flux_kanban_add(&state.kb, 0, "Add unit tests", "Cover auth module");
            flux_kanban_add(&state.kb, 0, "Fix memory leak", "Valgrind shows 2KB");
            flux_kanban_add(&state.kb, 0, "Update README", "Add build instructions");
            flux_kanban_add(&state.kb, 1, "Implement auth", "JWT + refresh tokens");
            flux_kanban_add(&state.kb, 1, "API rate limiting", "Token bucket algo");
            flux_kanban_add(&state.kb, 2, "Setup CI/CD", "GitHub Actions");
            flux_kanban_add(&state.kb, 2, "Docker config", "Multi-stage build");
            kanban_save(&state.kb);
        }
        state.kb.dirty = 0;
    }
    state.log.follow   = 1;

    FluxModel model = {
        .state  = &state,
        .init   = app_init,
        .update = app_update,
        .view   = app_view,
        .free   = NULL,
    };
    FluxProgram prog = { .model=model, .alt_screen=1, .mouse=0, .fps=30 };
    flux_run(&prog);
    return 0;
}
