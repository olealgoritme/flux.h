/*
 * cawd_tui.h — Event-driven agent TUI framework
 * =============================================
 *
 * A single-header agent-framework layered on flux.h. Your application
 * wires a handful of callbacks, enables the channels it wants, and
 * cawd_run() owns the loop: rendering the 13-pane workstation UI,
 * dispatching events across bridges, routing tool calls, gating on
 * policy, streaming tokens, and logging everything to the audit trail.
 *
 * Zero dependencies beyond libc + pthreads (same as flux.h).
 *
 * Quick start (~30 lines):
 * ------------------------
 *
 *   #define FLUX_IMPL
 *   #define CAWD_TUI_IMPL
 *   #include "flux.h"
 *   #include "cawd_tui.h"
 *
 *   static void on_message(CawdApp *app, CawdMessage m) {
 *       const char *aid = cawd_agent_spawn(app, m.ch,
 *           (CawdAgentSpec){.name="main", .model="claude-opus-4-7"});
 *       cawd_stream_begin(app, aid);
 *       // ... call your LLM, pipe tokens via cawd_stream_push(...)
 *       cawd_stream_end(app, aid, 1);
 *   }
 *
 *   static int on_tool(CawdApp *app, CawdToolCall tc, char *out, size_t sz) {
 *       if (strcmp(tc.tool, "bash") == 0) return run_bash(tc.args_json, out, sz);
 *       return -1;
 *   }
 *
 *   int main(void) {
 *       CawdApp app;
 *       cawd_init(&app, (CawdConfig){.title="my assistant"});
 *       cawd_on_message  (&app, CAWD_CH_HITL, on_message, CAWD_ON_WORKER);
 *       cawd_on_tool_call(&app, on_tool, CAWD_ON_WORKER);
 *       cawd_channel_enable(&app, CAWD_CH_HITL, NULL);
 *       cawd_policy_add(&app, "bash:rm -rf*", CAWD_DENY, CAWD_CH_ANY);
 *       cawd_policy_add(&app, "bash:*",       CAWD_ASK,  CAWD_CH_HITL);
 *       return cawd_run(&app);
 *   }
 *
 * Threading model:
 * ----------------
 *   - UI thread: flux.h loop. Owns all widget state. <8 ms per frame.
 *   - Bridge threads: one per enabled bridge (TG long-poll, GH webhook,
 *     Slack events, API HTTP server, cron). Post events to the bus.
 *   - Worker pool: 4 pthreads (configurable). Handlers tagged
 *     CAWD_ON_WORKER run here. For LLM calls, disk I/O, anything slow.
 *   - Event bus: lock-free MPSC ring + self-pipe wake on the UI thread.
 *     Zero-copy streaming via per-agent token buffers.
 *
 * License: MIT. Part of the flux.h project.
 */

#ifndef CAWD_TUI_H
#define CAWD_TUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

typedef struct CawdApp CawdApp;

/* =========================================================================
 * Enums
 * ========================================================================= */

/* Execution channels — where an interaction originates and how consent
 * is gathered. Same agent core, different operator rules per channel. */
typedef enum {
    CAWD_CH_HITL       = 0,  /* you at the terminal                      */
    CAWD_CH_TELEGRAM   = 1,  /* remote user via bot, inline kb approvals */
    CAWD_CH_AUTONOMOUS = 2,  /* queue/cron/webhook, policy-gated         */
    CAWD_CH_GITHUB     = 3,  /* PR / issue events                        */
    CAWD_CH_SLACK      = 4,  /* slash commands / mentions                */
    CAWD_CH_API        = 5,  /* inbound HTTP (session-as-REST)           */
    CAWD_CH__COUNT     = 6,
    CAWD_CH_ANY        = 7,  /* pseudo-channel for policy scope          */
} CawdChannel;

/* Where a handler runs. Pick per-handler via cawd_on_* register calls. */
typedef enum {
    CAWD_ON_UI     = 0,  /* UI thread; must be fast (<1 ms).             */
    CAWD_ON_WORKER = 1,  /* worker pool; fine for blocking I/O + LLM.    */
} CawdExec;

/* Agent lifecycle states. Transitions drive badge colors + animations. */
typedef enum {
    CAWD_STATE_PENDING   = 0,
    CAWD_STATE_PLANNING  = 1,
    CAWD_STATE_RUNNING   = 2,
    CAWD_STATE_STREAMING = 3,
    CAWD_STATE_WAITING   = 4,  /* waiting on approval                    */
    CAWD_STATE_PAUSED    = 5,
    CAWD_STATE_DONE      = 6,
    CAWD_STATE_FAILED    = 7,
    CAWD_STATE_CANCELLED = 8,
} CawdAgentState;

/* Policy verdict for a tool call. */
typedef enum {
    CAWD_ALLOW = 0,
    CAWD_DENY  = 1,
    CAWD_ASK   = 2,  /* route to approval flow                           */
} CawdVerdict;

/* Result of an approval prompt. */
typedef enum {
    CAWD_APPROVE        = 0,
    CAWD_APPROVE_ALWAYS = 1,  /* remember for session                    */
    CAWD_REJECT         = 2,
    CAWD_TIMEOUT        = 3,
} CawdDecision;

/* Risk level; shown as a colored chip in approval dialogs. */
typedef enum {
    CAWD_RISK_LOW    = 0,  /* read-only, idempotent                      */
    CAWD_RISK_MEDIUM = 1,  /* writes a file, calls an API                */
    CAWD_RISK_HIGH   = 2,  /* irreversible: rm, force-push, prod deploy  */
} CawdRisk;

/* Toast / alert severity. */
typedef enum {
    CAWD_KIND_INFO    = 0,
    CAWD_KIND_SUCCESS = 1,
    CAWD_KIND_WARN    = 2,
    CAWD_KIND_ERROR   = 3,
} CawdKind;

/* =========================================================================
 * Public event / config structs
 * ========================================================================= */

/* Top-level configuration for cawd_init. All fields optional; zero-init
 * yields sensible defaults. */
typedef struct {
    const char *title;           /* titlebar title. default "cawd"        */
    int         fps;             /* default 120                           */
    int         worker_threads;  /* default 4                             */
    int         custom_ui;       /* 1 = skip built-in UI; use cawd_step() */
    int         alt_screen;      /* default 1                             */
    int         mouse;           /* default 1                             */
} CawdConfig;

/* An inbound user message from any channel. */
typedef struct {
    CawdChannel ch;
    const char *user;            /* e.g. "@jane" or NULL for HITL         */
    const char *text;
    uint64_t    ts_ms;
    void       *meta;            /* channel-specific: chat_id, pr#, etc.  */
} CawdMessage;

/* A tool call request. Handler returns 0 on success, <0 on error, and
 * writes its textual result into `out` (max `out_sz` bytes, NUL-term). */
typedef struct {
    const char *agent_id;
    const char *tool;
    const char *args_json;
    CawdRisk    risk;
    CawdChannel origin;
} CawdToolCall;

/* An approval prompt. Shown modally to the operator. */
typedef struct {
    const char *agent_id;
    const char *summary;         /* one-line description                  */
    const char *preview;         /* multi-line body (diff/cmd/etc.)       */
    const char *language;        /* for syntax highlight: "sh","c","diff" */
    CawdRisk    risk;
    CawdChannel origin;
} CawdApproval;

/* A single token in a streaming response. */
typedef struct {
    const char *agent_id;
    const char *token;
    int         is_final;
} CawdTokenChunk;

/* Spec for cawd_agent_spawn. */
typedef struct {
    const char *id;              /* optional; auto-generated if NULL      */
    const char *name;            /* short label, e.g. "Cartographer"      */
    const char *model;           /* e.g. "claude-opus-4-7"                */
    const char *provider;        /* e.g. "Anthropic"                      */
    const char *system_prompt;
    int         max_tokens;
    const char *parent_agent_id; /* for sub-agents                        */
} CawdAgentSpec;

/* Per-bridge configs. Pass to cawd_channel_enable. */
typedef struct {
    const char *bot_token;
    long        chat_id_whitelist[16];
    int         n_chat_ids;
    int         poll_interval_sec;  /* default 30                         */
} CawdTelegramCfg;

typedef struct {
    int         port;               /* default 8080                       */
    const char *bearer_token;       /* NULL to disable auth               */
    int         rate_limit_rpm;     /* default 60                         */
    const char *ngrok_like_label;   /* shown in UI titlebar pill          */
} CawdApiCfg;

typedef struct {
    const char *pat;                /* GitHub PAT                         */
    const char *repo_whitelist[16];
    int         n_repos;
    int         auto_review_prs;    /* default 0                          */
    int         auto_resolve_issues;/* default 0                          */
} CawdGithubCfg;

typedef struct {
    const char *bot_token;
    const char *signing_secret;
    const char *channel_whitelist[16];
    int         n_channels;
} CawdSlackCfg;

typedef struct {
    int interval_sec;               /* cron tick cadence                  */
} CawdAutonomousCfg;

/* One audit log entry. */
typedef struct {
    uint64_t    ts_ms;
    CawdChannel channel;
    const char *agent_id;
    const char *actor;              /* who triggered                      */
    const char *tool;
    const char *args;
    CawdVerdict verdict;
    const char *operator_;          /* approver (if CAWD_ASK)             */
    int         exit_code;
    int         duration_ms;
    double      cost_usd;
} CawdAuditEntry;

/* Aggregate cost snapshot. */
typedef struct {
    double tokens_in;
    double tokens_out;
    double usd;
    int    requests;
} CawdCost;

/* Dialog spec for cawd_dialog(). */
typedef struct {
    const char *title;
    const char *body;
    const char *language;   /* for syntax highlight on `body`; NULL = plain */
    struct {
        const char *label;
        const char *shortcut;   /* e.g. "a", "enter", NULL                */
        CawdKind    kind;       /* color hint                             */
    } buttons[4];
    int n_buttons;
    int timeout_ms;             /* 0 = no timeout                         */
    CawdRisk risk;
} CawdDialogSpec;

/* =========================================================================
 * Handler signatures
 * ========================================================================= */

typedef void (*CawdOnMessageFn)    (CawdApp *, CawdMessage);
typedef int  (*CawdOnToolCallFn)   (CawdApp *, CawdToolCall, char *out, size_t out_sz);
typedef int  (*CawdOnApprovalFn)   (CawdApp *, CawdApproval);
typedef void (*CawdOnTokenFn)      (CawdApp *, CawdTokenChunk);
typedef void (*CawdOnAgentStateFn) (CawdApp *, const char *agent_id, CawdAgentState);
typedef void (*CawdOnErrorFn)      (CawdApp *, const char *agent_id, const char *msg);

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize an app. Returns 0 on success, <0 on error. */
int  cawd_init     (CawdApp *app, CawdConfig cfg);

/* Run the app. Blocks until quit. With cfg.custom_ui=0 (default) this
 * owns the loop and renders the full workstation. Returns the exit code. */
int  cawd_run      (CawdApp *app);

/* Single-step: drains one round of events, fires ready handlers, returns.
 * For use with cfg.custom_ui=1 when you own the render loop. Returns 0
 * if should continue, non-zero to quit. */
int  cawd_step     (CawdApp *app);

/* Clean shutdown; joins threads, closes bridges. Safe to call once after
 * cawd_run(). */
void cawd_shutdown (CawdApp *app);

/* =========================================================================
 * Channels — enable / disable bridges
 * ========================================================================= */

int  cawd_channel_enable  (CawdApp *, CawdChannel, void *cfg);
void cawd_channel_disable (CawdApp *, CawdChannel);
int  cawd_channel_is_enabled (CawdApp *, CawdChannel);

/* =========================================================================
 * Telegram observability API — pump events from your bot integration so
 * the TG tab shows live sessions, transcripts, tool calls.
 * Call from your TG bot loop / tool runner / model client. All thread-safe.
 * ========================================================================= */

typedef enum {
    CAWD_TG_STATE_RUNNING = 0,
    CAWD_TG_STATE_WAITING_APPROVAL,
    CAWD_TG_STATE_IDLE,
    CAWD_TG_STATE_DONE,
    CAWD_TG_STATE_FAILED,
} CawdTgSessionState;

typedef enum {
    CAWD_TG_CONN_DISCONNECTED = 0,
    CAWD_TG_CONN_CONNECTED,
    CAWD_TG_CONN_RECONNECTING,
} CawdTgConnState;

typedef struct {
    int   request_quota_per_sec;   /* Telegram caps (default 30)        */
    int   long_poll_interval_sec;  /* default 30                        */
} CawdTgRunCfg;

void cawd_tg_set_conn       (CawdApp *, CawdTgConnState st, const char *bot_handle);
void cawd_tg_set_runtime    (CawdApp *, CawdTgRunCfg cfg);

/* Open or get a session for a chat. Idempotent — same chat_id
 * returns the same session id. */
void cawd_tg_session_open   (CawdApp *, const char *chat_id, const char *handle,
                             int is_group, const char *model, int max_tokens,
                             const char *system_prompt);

/* Per-event hooks. Called by user code as the bot/agent does work. */
void cawd_tg_user_msg       (CawdApp *, const char *chat_id, const char *body);
void cawd_tg_agent_msg      (CawdApp *, const char *chat_id, const char *body,
                             int tokens_in, int tokens_out, double cost_usd);
void cawd_tg_thinking       (CawdApp *, const char *chat_id, int tokens);
void cawd_tg_tool_call      (CawdApp *, const char *chat_id, const char *tool,
                             const char *args_short, int duration_ms, int ok);
void cawd_tg_policy_gate    (CawdApp *, const char *chat_id, const char *summary);
void cawd_tg_state          (CawdApp *, const char *chat_id, CawdTgSessionState st);
void cawd_tg_terminate      (CawdApp *, const char *chat_id);
/* Switch model on a live session. */
void cawd_tg_set_session_model(CawdApp *, const char *chat_id, const char *model);

/* =========================================================================
 * Handler registration — each takes a CawdExec saying UI or WORKER thread
 * ========================================================================= */

void cawd_on_message     (CawdApp *, CawdChannel, CawdOnMessageFn,    CawdExec);
void cawd_on_tool_call   (CawdApp *,              CawdOnToolCallFn,   CawdExec);
void cawd_on_approval    (CawdApp *,              CawdOnApprovalFn,   CawdExec);
void cawd_on_token       (CawdApp *,              CawdOnTokenFn,      CawdExec);
void cawd_on_agent_state (CawdApp *,              CawdOnAgentStateFn, CawdExec);
void cawd_on_error       (CawdApp *,              CawdOnErrorFn,      CawdExec);

/* =========================================================================
 * Agents
 * ========================================================================= */

/* Spawn a new agent. Returns an id pointer owned by cawd (don't free).
 * The id is stable for the agent's lifetime. */
const char *cawd_agent_spawn   (CawdApp *, CawdChannel origin, CawdAgentSpec);

void cawd_agent_kill           (CawdApp *, const char *agent_id);
void cawd_agent_pause          (CawdApp *, const char *agent_id);
void cawd_agent_resume         (CawdApp *, const char *agent_id);
const char *cawd_agent_fork    (CawdApp *, const char *agent_id);
void cawd_agent_promote        (CawdApp *, const char *agent_id, CawdChannel dest);

/* Update per-agent metadata (used by the UI to show live stats). */
void cawd_agent_set_current_tool (CawdApp *, const char *agent_id, const char *tool);
void cawd_agent_add_tokens       (CawdApp *, const char *agent_id, int in, int out);
void cawd_agent_set_state        (CawdApp *, const char *agent_id, CawdAgentState);

/* =========================================================================
 * Streaming — pump your LLM's tokens here. Thread-safe, lock-free.
 * ========================================================================= */

void cawd_stream_begin (CawdApp *, const char *agent_id);
void cawd_stream_push  (CawdApp *, const char *agent_id, const char *token);
void cawd_stream_end   (CawdApp *, const char *agent_id, int ok);

/* =========================================================================
 * Approvals / dialogs — prompt the operator, await a decision
 * ========================================================================= */

/* Shows the canonical approval modal (transparent backdrop + risk chip +
 * preview with syntax highlight + 3 buttons: Approve, Approve always,
 * Reject). Blocks the *caller's* logical flow — but the UI thread keeps
 * running because the caller is expected to be on a worker thread. If
 * called from UI thread returns CAWD_TIMEOUT immediately with a
 * complaint on stderr. */
CawdDecision cawd_await_approval (CawdApp *, CawdApproval, int timeout_ms);

/* Generic modal dialog (transparent backdrop + centered card + N
 * colored buttons). Returns index of pressed button, or -1 on timeout. */
int  cawd_dialog (CawdApp *, CawdDialogSpec);

/* =========================================================================
 * Policy engine
 * ========================================================================= */

/* Add a rule. `match` is a glob-style pattern applied to "<tool>:<args>".
 * Rules evaluated in order; first match wins. Scope limits rule to one
 * channel (CAWD_CH_ANY = all). */
void cawd_policy_add              (CawdApp *, const char *match, CawdVerdict, CawdChannel scope);
void cawd_policy_clear            (CawdApp *);

/* Runtime overrides. */
void cawd_policy_override_once    (CawdApp *, const char *match, CawdVerdict);
void cawd_policy_override_session (CawdApp *, const char *match, CawdVerdict);
void cawd_policy_bypass_all       (CawdApp *, int seconds);  /* "yes to all" */

/* Evaluate a specific tool call without executing it — useful for
 * preflight checks in your handler. */
CawdVerdict cawd_policy_check (CawdApp *, CawdToolCall);

/* =========================================================================
 * Audit + cost
 * ========================================================================= */

/* Copy up to `max` most-recent audit entries into `out`. Returns count. */
int cawd_audit_tail (CawdApp *, CawdAuditEntry *out, int max);

/* Per-channel cost. Pass CAWD_CH_ANY for total. */
CawdCost cawd_cost_snapshot (CawdApp *, CawdChannel);

/* =========================================================================
 * UI utilities (nice-to-have — most apps won't need these)
 * ========================================================================= */

void cawd_toast         (CawdApp *, CawdKind, const char *title, const char *body);
void cawd_focus_channel (CawdApp *, CawdChannel);
void cawd_ticker_push   (CawdApp *, CawdChannel, const char *text);
void cawd_quit          (CawdApp *);

#ifdef __cplusplus
}
#endif

/* =========================================================================
 * Implementation
 * ========================================================================= */

#ifdef CAWD_TUI_IMPL

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>

/* =========================================================================
 * Implementation constants
 * ========================================================================= */

#define CAWD_EVENT_RING_CAP     1024
#define CAWD_MAX_AGENTS         128
#define CAWD_MAX_POLICY_RULES   256
#define CAWD_MAX_HANDLERS       16
#define CAWD_AUDIT_RING_CAP     2048
#define CAWD_STRPOOL_CAP        (1 << 20)   /* 1 MiB interned strings    */
#define CAWD_WORK_QUEUE_CAP     256
#define CAWD_DEFAULT_WORKERS    4
#define CAWD_MAX_WORKERS        16
#define CAWD_AGENT_ID_LEN       24
#define CAWD_STREAM_BUF         (1 << 15)   /* 32 KiB per agent          */

/* =========================================================================
 * Internal event types
 * ========================================================================= */

typedef enum {
    CAWD_EV_NONE = 0,
    CAWD_EV_MESSAGE,
    CAWD_EV_TOOL_CALL,
    CAWD_EV_APPROVAL_REQUEST,
    CAWD_EV_APPROVAL_RESPONSE,
    CAWD_EV_TOKEN,
    CAWD_EV_AGENT_STATE,
    CAWD_EV_ERROR,
    CAWD_EV_QUIT,
    CAWD_EV_TICKER,
    CAWD_EV_TOAST,
    CAWD_EV_FOCUS_CHANNEL,
} CawdEventKind;

typedef struct CawdEvent {
    CawdEventKind kind;
    CawdChannel   channel;
    uint64_t      ts_ms;
    union {
        CawdMessage    msg;
        CawdToolCall   tc;
        CawdApproval   ap;
        CawdTokenChunk tok;
        struct { const char *agent_id; CawdAgentState state; } agent_state;
        struct { const char *agent_id; const char *text; }    error;
        struct { CawdKind kind; const char *title; const char *body; } toast;
        struct { const char *text; }                          ticker;
        struct { uint32_t ticket; CawdDecision decision; }    approval_response;
    } u;
} CawdEvent;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static uint64_t cawd__now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static int cawd__pipe_nonblock(int fds[2]) {
    if (pipe(fds) != 0) return -1;
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(fds[i], F_GETFL, 0);
        if (fl < 0) return -1;
        if (fcntl(fds[i], F_SETFL, fl | O_NONBLOCK) < 0) return -1;
    }
    return 0;
}

/* =========================================================================
 * String pool — interns copies of strings handed to us by user code so we
 * don't have to trust that their pointers stay valid. Simple bump allocator
 * backed by a single buffer; deduplicates via linear scan up to 64 entries
 * (cheap: most apps have <20 unique strings in flight).
 * ========================================================================= */

typedef struct {
    char   *buf;
    size_t  cap;
    size_t  used;
    pthread_mutex_t mu;
} CawdStrPool;

static int cawd__strpool_init(CawdStrPool *p, size_t cap) {
    p->buf = (char *)calloc(1, cap);
    if (!p->buf) return -1;
    p->cap = cap; p->used = 0;
    pthread_mutex_init(&p->mu, NULL);
    return 0;
}

static void cawd__strpool_free(CawdStrPool *p) {
    free(p->buf); p->buf = NULL; p->cap = p->used = 0;
    pthread_mutex_destroy(&p->mu);
}

/* Copy `s` into the pool, return stable pointer. NULL-in / NULL-out ok. */
static const char *cawd__strdup(CawdStrPool *p, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    pthread_mutex_lock(&p->mu);
    if (p->used + len > p->cap) {
        pthread_mutex_unlock(&p->mu);
        return NULL;  /* pool full — caller should treat as best-effort */
    }
    char *dst = p->buf + p->used;
    memcpy(dst, s, len);
    p->used += len;
    pthread_mutex_unlock(&p->mu);
    return dst;
}

/* =========================================================================
 * MPSC event ring (mutex-guarded — simpler than lock-free, fast enough
 * at our event rates; tens of events/sec peak).
 * ========================================================================= */

typedef struct {
    CawdEvent buf[CAWD_EVENT_RING_CAP];
    int       head;   /* consumer */
    int       tail;   /* producer */
    pthread_mutex_t mu;
    int       wake_pipe[2];  /* self-pipe to wake UI thread */
} CawdEventRing;

static int cawd__ring_init(CawdEventRing *r) {
    memset(r, 0, sizeof *r);
    pthread_mutex_init(&r->mu, NULL);
    return cawd__pipe_nonblock(r->wake_pipe);
}

static void cawd__ring_free(CawdEventRing *r) {
    pthread_mutex_destroy(&r->mu);
    close(r->wake_pipe[0]); close(r->wake_pipe[1]);
}

/* Push one event. Drops silently on overflow (documented; audit would
 * warn in a real impl). */
static int cawd__ring_push(CawdEventRing *r, const CawdEvent *ev) {
    pthread_mutex_lock(&r->mu);
    int next = (r->tail + 1) % CAWD_EVENT_RING_CAP;
    if (next == r->head) {
        pthread_mutex_unlock(&r->mu);
        return -1;  /* full */
    }
    r->buf[r->tail] = *ev;
    r->tail = next;
    pthread_mutex_unlock(&r->mu);
    /* Wake UI thread. Ignore EAGAIN — pipe already has pending bytes. */
    char byte = 1;
    ssize_t n = write(r->wake_pipe[1], &byte, 1);
    (void)n;
    return 0;
}

static int cawd__ring_pop(CawdEventRing *r, CawdEvent *out) {
    pthread_mutex_lock(&r->mu);
    if (r->head == r->tail) {
        pthread_mutex_unlock(&r->mu);
        return -1;  /* empty */
    }
    *out = r->buf[r->head];
    r->head = (r->head + 1) % CAWD_EVENT_RING_CAP;
    pthread_mutex_unlock(&r->mu);
    return 0;
}

/* Drain any pending wake bytes (called when UI thread is about to
 * consume events anyway — avoids repeated wake-ups). */
static void cawd__ring_drain_wake(CawdEventRing *r) {
    char scratch[64];
    while (read(r->wake_pipe[0], scratch, sizeof scratch) > 0) { /* drain */ }
}

/* =========================================================================
 * Worker pool
 * ========================================================================= */

/* CawdApp already forward-declared at the top of the public API block. */
typedef void (*CawdWorkFn)(CawdApp *app, void *ctx);

typedef struct {
    CawdWorkFn fn;
    void      *ctx;
} CawdWorkItem;

typedef struct {
    pthread_t      threads[CAWD_MAX_WORKERS];
    int            n_threads;
    CawdWorkItem   queue[CAWD_WORK_QUEUE_CAP];
    int            head, tail;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int            running;
    CawdApp       *app;
} CawdWorkerPool;

static void *cawd__worker_loop(void *arg) {
    CawdWorkerPool *p = (CawdWorkerPool *)arg;
    for (;;) {
        pthread_mutex_lock(&p->mu);
        while (p->running && p->head == p->tail)
            pthread_cond_wait(&p->cv, &p->mu);
        if (!p->running && p->head == p->tail) {
            pthread_mutex_unlock(&p->mu);
            return NULL;
        }
        CawdWorkItem wi = p->queue[p->head];
        p->head = (p->head + 1) % CAWD_WORK_QUEUE_CAP;
        pthread_mutex_unlock(&p->mu);
        if (wi.fn) wi.fn(p->app, wi.ctx);
    }
}

static int cawd__pool_init(CawdWorkerPool *p, CawdApp *app, int n) {
    if (n <= 0) n = CAWD_DEFAULT_WORKERS;
    if (n > CAWD_MAX_WORKERS) n = CAWD_MAX_WORKERS;
    memset(p, 0, sizeof *p);
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);
    p->running = 1;
    p->app = app;
    p->n_threads = n;
    for (int i = 0; i < n; i++) {
        if (pthread_create(&p->threads[i], NULL, cawd__worker_loop, p) != 0) {
            p->n_threads = i;
            return -1;
        }
    }
    return 0;
}

static void cawd__pool_shutdown(CawdWorkerPool *p) {
    pthread_mutex_lock(&p->mu);
    p->running = 0;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mu);
    for (int i = 0; i < p->n_threads; i++)
        pthread_join(p->threads[i], NULL);
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->cv);
}

static int cawd__pool_post(CawdWorkerPool *p, CawdWorkFn fn, void *ctx) {
    pthread_mutex_lock(&p->mu);
    int next = (p->tail + 1) % CAWD_WORK_QUEUE_CAP;
    if (next == p->head) {
        pthread_mutex_unlock(&p->mu);
        return -1;  /* full */
    }
    p->queue[p->tail].fn  = fn;
    p->queue[p->tail].ctx = ctx;
    p->tail = next;
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

/* =========================================================================
 * Agent registry
 * ========================================================================= */

typedef struct {
    int            used;
    char           id[CAWD_AGENT_ID_LEN];
    const char    *name;
    const char    *model;
    const char    *provider;
    const char    *system_prompt;
    CawdChannel    origin;
    CawdChannel    current_channel;  /* may differ after promote          */
    CawdAgentState state;
    const char    *current_tool;
    int            tokens_in;
    int            tokens_out;
    double         cost_usd;
    uint64_t       spawned_ms;
    uint64_t       last_update_ms;
    const char    *parent_id;

    /* Streaming buffer. Producer: any thread (stream_push). Consumer:
     * UI thread (in its tick). Guarded by stream_mu. UI reads and
     * advances `stream_consumed` up to `stream_produced`. */
    char           stream_buf[CAWD_STREAM_BUF];
    size_t         stream_produced;
    size_t         stream_consumed;
    int            stream_open;
    pthread_mutex_t stream_mu;
} CawdAgent;

typedef struct {
    CawdAgent       slots[CAWD_MAX_AGENTS];
    int             next_seq;
    pthread_mutex_t mu;
} CawdAgentReg;

static void cawd__agentreg_init(CawdAgentReg *r) {
    memset(r, 0, sizeof *r);
    pthread_mutex_init(&r->mu, NULL);
    for (int i = 0; i < CAWD_MAX_AGENTS; i++)
        pthread_mutex_init(&r->slots[i].stream_mu, NULL);
}

static void cawd__agentreg_free(CawdAgentReg *r) {
    for (int i = 0; i < CAWD_MAX_AGENTS; i++)
        pthread_mutex_destroy(&r->slots[i].stream_mu);
    pthread_mutex_destroy(&r->mu);
}

static CawdAgent *cawd__agent_find_locked(CawdAgentReg *r, const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < CAWD_MAX_AGENTS; i++)
        if (r->slots[i].used && strcmp(r->slots[i].id, id) == 0)
            return &r->slots[i];
    return NULL;
}

/* Returns NULL if registry full. */
static CawdAgent *cawd__agent_alloc(CawdAgentReg *r, char out_id[CAWD_AGENT_ID_LEN]) {
    pthread_mutex_lock(&r->mu);
    for (int i = 0; i < CAWD_MAX_AGENTS; i++) {
        if (!r->slots[i].used) {
            r->slots[i].used = 1;
            r->next_seq++;
            snprintf(r->slots[i].id, sizeof r->slots[i].id,
                     "ag_%06d", r->next_seq);
            strncpy(out_id, r->slots[i].id, CAWD_AGENT_ID_LEN);
            pthread_mutex_unlock(&r->mu);
            return &r->slots[i];
        }
    }
    pthread_mutex_unlock(&r->mu);
    return NULL;
}

/* =========================================================================
 * Policy engine — simple glob matcher (supports * and ?), first-match-wins,
 * per-channel scope, with once/session overrides and bypass window.
 * ========================================================================= */

typedef struct {
    int         used;
    char        match[128];
    CawdVerdict verdict;
    CawdChannel scope;
    int         once;          /* consumed after 1 hit                  */
    uint64_t    session_until_ms; /* 0 = permanent (till clear/shutdown) */
    int         hits;
} CawdPolicyRule;

typedef struct {
    CawdPolicyRule   rules[CAWD_MAX_POLICY_RULES];
    uint64_t         bypass_until_ms;
    pthread_mutex_t  mu;
} CawdPolicyEngine;

static void cawd__pol_init(CawdPolicyEngine *pe) {
    memset(pe, 0, sizeof *pe);
    pthread_mutex_init(&pe->mu, NULL);
}

static void cawd__pol_free(CawdPolicyEngine *pe) {
    pthread_mutex_destroy(&pe->mu);
}

/* Glob match: * matches any run, ? matches one char. ASCII only. */
static int cawd__glob(const char *pat, const char *s) {
    const char *p = pat, *ss = s, *star = NULL, *mark = NULL;
    while (*ss) {
        if (*p == '*') { star = p++; mark = ss; }
        else if (*p == '?' || *p == *ss) { p++; ss++; }
        else if (star) { p = star + 1; ss = ++mark; }
        else return 0;
    }
    while (*p == '*') p++;
    return *p == 0;
}

static int cawd__pol_add(CawdPolicyEngine *pe, const char *match,
                         CawdVerdict v, CawdChannel scope, int once,
                         uint64_t session_until_ms) {
    pthread_mutex_lock(&pe->mu);
    for (int i = 0; i < CAWD_MAX_POLICY_RULES; i++) {
        if (!pe->rules[i].used) {
            pe->rules[i].used = 1;
            strncpy(pe->rules[i].match, match, sizeof pe->rules[i].match - 1);
            pe->rules[i].match[sizeof pe->rules[i].match - 1] = 0;
            pe->rules[i].verdict = v;
            pe->rules[i].scope = scope;
            pe->rules[i].once = once;
            pe->rules[i].session_until_ms = session_until_ms;
            pe->rules[i].hits = 0;
            pthread_mutex_unlock(&pe->mu);
            return 0;
        }
    }
    pthread_mutex_unlock(&pe->mu);
    return -1;
}

static CawdVerdict cawd__pol_check(CawdPolicyEngine *pe, CawdChannel ch,
                                    const char *tool, const char *args) {
    char key[512];
    snprintf(key, sizeof key, "%s:%s", tool ? tool : "", args ? args : "");
    uint64_t now = cawd__now_ms();

    pthread_mutex_lock(&pe->mu);

    /* 1) global bypass window — highest priority */
    if (pe->bypass_until_ms > now) {
        pthread_mutex_unlock(&pe->mu);
        return CAWD_ALLOW;
    }

    /* 2) scan rules in slot order; once-rules consumed on hit */
    for (int i = 0; i < CAWD_MAX_POLICY_RULES; i++) {
        CawdPolicyRule *r = &pe->rules[i];
        if (!r->used) continue;
        if (r->scope != CAWD_CH_ANY && r->scope != ch) continue;
        if (r->session_until_ms && r->session_until_ms <= now) {
            r->used = 0;  /* session expired */
            continue;
        }
        if (cawd__glob(r->match, key)) {
            CawdVerdict v = r->verdict;
            r->hits++;
            if (r->once) r->used = 0;
            pthread_mutex_unlock(&pe->mu);
            return v;
        }
    }
    pthread_mutex_unlock(&pe->mu);
    /* default: ASK. Safer than ALLOW. */
    return CAWD_ASK;
}

/* =========================================================================
 * Audit log ring
 * ========================================================================= */

typedef struct {
    CawdAuditEntry  buf[CAWD_AUDIT_RING_CAP];
    int             head;
    int             count;      /* <= CAP                                */
    pthread_mutex_t mu;
} CawdAuditLog;

static void cawd__audit_init(CawdAuditLog *a) {
    memset(a, 0, sizeof *a);
    pthread_mutex_init(&a->mu, NULL);
}

static void cawd__audit_free(CawdAuditLog *a) {
    pthread_mutex_destroy(&a->mu);
}

static void cawd__audit_append(CawdAuditLog *a, const CawdAuditEntry *e) {
    pthread_mutex_lock(&a->mu);
    a->buf[a->head] = *e;
    a->head = (a->head + 1) % CAWD_AUDIT_RING_CAP;
    if (a->count < CAWD_AUDIT_RING_CAP) a->count++;
    pthread_mutex_unlock(&a->mu);
}

static int cawd__audit_tail(CawdAuditLog *a, CawdAuditEntry *out, int max) {
    pthread_mutex_lock(&a->mu);
    int n = a->count < max ? a->count : max;
    /* Copy from most recent backward. */
    int idx = (a->head - 1 + CAWD_AUDIT_RING_CAP) % CAWD_AUDIT_RING_CAP;
    for (int i = 0; i < n; i++) {
        out[i] = a->buf[idx];
        idx = (idx - 1 + CAWD_AUDIT_RING_CAP) % CAWD_AUDIT_RING_CAP;
    }
    pthread_mutex_unlock(&a->mu);
    return n;
}

/* =========================================================================
 * Handler table — one set per CawdChannel slot (except tool/approval/token
 * which are global).
 * ========================================================================= */

typedef struct {
    CawdOnMessageFn    fn;
    CawdExec           exec;
} CawdMsgHandler;

typedef struct {
    CawdOnToolCallFn   tool_fn;   CawdExec tool_exec;
    CawdOnApprovalFn   appr_fn;   CawdExec appr_exec;
    CawdOnTokenFn      tok_fn;    CawdExec tok_exec;
    CawdOnAgentStateFn state_fn;  CawdExec state_exec;
    CawdOnErrorFn      err_fn;    CawdExec err_exec;
    CawdMsgHandler     msg[CAWD_CH__COUNT];
} CawdHandlers;

/* =========================================================================
 * Concrete CawdApp — the opaque handle users see
 * ========================================================================= */

struct CawdApp {
    CawdConfig       cfg;
    int              running;
    CawdChannel      focused_channel;

    CawdEventRing    ring;
    CawdWorkerPool   pool;
    CawdAgentReg     agents;
    CawdPolicyEngine policy;
    CawdAuditLog     audit;
    CawdStrPool      strs;

    CawdHandlers     handlers;

    /* Channel enable state + bridge thread handle. */
    struct {
        int       enabled;
        pthread_t bridge_thr;
        int       bridge_running;
        void     *cfg_copy;
    } channels[CAWD_CH__COUNT];

    /* Cost ledger (per-channel). */
    CawdCost cost[CAWD_CH__COUNT];
    pthread_mutex_t cost_mu;

    /* Approval ticket allocator + pending queue — used by
     * cawd_await_approval(). */
    uint32_t        next_ticket;
    struct {
        int          used;
        uint32_t     ticket;
        CawdDecision decision;
        pthread_mutex_t mu;
        pthread_cond_t  cv;
    } pending_approvals[32];

    /* Built-in UI state (populated only when !cfg.custom_ui).         */
    struct {
        int                  enabled;         /* 0 when custom_ui      */
        int                  slot_active;     /* 0..CAWD_UI_SLOT_COUNT */
        FluxTabBar           tabs;
        FluxScroll           scroll[13];      /* per-tab scroll offset */
        FluxComposer         composer;
        FluxAppBar           appbar;
        int                  composer_focused;   /* 1 default; ↓ → AppBar */
        FluxCommandPalette   palette;
        FluxDialog           quit_dialog;
        FluxToastCenter      toasts;
        FluxTicker           ticker;
        FluxParticleBurst    particles;
        uint64_t             tick_total;
        uint64_t             start_ms;

        struct CawdTabState *tabs_state[13];
        char                 tab_content[256 * 1024];
    } ui;
};

struct CawdTabState;
typedef void (*CawdTabInitFn)  (CawdApp *);
typedef void (*CawdTabTickFn)  (CawdApp *, uint64_t now_ms);
typedef int  (*CawdTabUpdateFn)(CawdApp *, FluxMsg);
typedef void (*CawdTabRenderFn)(CawdApp *, FluxSB *, int w, int h);

typedef struct {
    CawdTabInitFn   init;
    CawdTabTickFn   tick;
    CawdTabUpdateFn update;
    CawdTabRenderFn render;
} CawdTabVtbl;

/* Slot enum for the 13-channel+view workstation. The first 6 match
 * CawdChannel; 6..12 are UI-only views. */
enum {
    CAWD_UI_SLOT_HITL = 0,
    CAWD_UI_SLOT_TELEGRAM,
    CAWD_UI_SLOT_AUTONOMOUS,
    CAWD_UI_SLOT_GITHUB,
    CAWD_UI_SLOT_SLACK,
    CAWD_UI_SLOT_API,
    CAWD_UI_SLOT_ORCHESTRA,
    CAWD_UI_SLOT_INSPECTOR,
    CAWD_UI_SLOT_POLICIES,
    CAWD_UI_SLOT_AUDIT,
    CAWD_UI_SLOT_ANALYTICS,
    CAWD_UI_SLOT_SETTINGS,
    CAWD_UI_SLOT_HELP,
    CAWD_UI_SLOT_COUNT
};

/* =========================================================================
 * Public API implementation — dispatch
 * ========================================================================= */

/* Translate the channel to pool or call inline on UI thread. */
static void cawd__dispatch_message(CawdApp *app, CawdEvent *ev);
static void cawd__dispatch_tool   (CawdApp *app, CawdEvent *ev);
static void cawd__dispatch_approval(CawdApp *app, CawdEvent *ev);
static void cawd__dispatch_token  (CawdApp *app, CawdEvent *ev);
static void cawd__dispatch_state  (CawdApp *app, CawdEvent *ev);
static void cawd__dispatch_error  (CawdApp *app, CawdEvent *ev);

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

int cawd_init(CawdApp *app, CawdConfig cfg) {
    if (!app) return -1;
    memset(app, 0, sizeof *app);
    app->cfg = cfg;
    if (app->cfg.fps <= 0) app->cfg.fps = 120;
    if (app->cfg.worker_threads <= 0) app->cfg.worker_threads = CAWD_DEFAULT_WORKERS;
    app->running = 1;
    app->focused_channel = CAWD_CH_HITL;

    if (cawd__strpool_init(&app->strs, CAWD_STRPOOL_CAP) != 0) return -2;
    if (cawd__ring_init(&app->ring) != 0) return -3;
    if (cawd__pool_init(&app->pool, app, app->cfg.worker_threads) != 0) return -4;
    cawd__agentreg_init(&app->agents);
    cawd__pol_init(&app->policy);
    cawd__audit_init(&app->audit);
    pthread_mutex_init(&app->cost_mu, NULL);

    for (int i = 0; i < 32; i++) {
        pthread_mutex_init(&app->pending_approvals[i].mu, NULL);
        pthread_cond_init(&app->pending_approvals[i].cv, NULL);
    }
    return 0;
}

void cawd_shutdown(CawdApp *app) {
    if (!app) return;
    app->running = 0;

    /* Stop any bridge threads. */
    for (int i = 0; i < CAWD_CH__COUNT; i++) {
        if (app->channels[i].enabled && app->channels[i].bridge_running) {
            app->channels[i].bridge_running = 0;
            pthread_join(app->channels[i].bridge_thr, NULL);
        }
        free(app->channels[i].cfg_copy);
        app->channels[i].cfg_copy = NULL;
    }

    cawd__pool_shutdown(&app->pool);
    cawd__ring_free(&app->ring);
    cawd__agentreg_free(&app->agents);
    cawd__pol_free(&app->policy);
    cawd__audit_free(&app->audit);
    cawd__strpool_free(&app->strs);
    pthread_mutex_destroy(&app->cost_mu);

    for (int i = 0; i < 32; i++) {
        pthread_mutex_destroy(&app->pending_approvals[i].mu);
        pthread_cond_destroy(&app->pending_approvals[i].cv);
    }
}

int cawd_step(CawdApp *app) {
    if (!app || !app->running) return 1;

    /* Wait up to one frame for an event. Drain, dispatch. */
    struct pollfd pfd;
    pfd.fd = app->ring.wake_pipe[0];
    pfd.events = POLLIN;
    int timeout_ms = 1000 / (app->cfg.fps > 0 ? app->cfg.fps : 120);
    if (timeout_ms < 1) timeout_ms = 1;

    (void)poll(&pfd, 1, timeout_ms);
    cawd__ring_drain_wake(&app->ring);

    CawdEvent ev;
    while (cawd__ring_pop(&app->ring, &ev) == 0) {
        switch (ev.kind) {
        case CAWD_EV_MESSAGE:         cawd__dispatch_message(app, &ev); break;
        case CAWD_EV_TOOL_CALL:       cawd__dispatch_tool(app, &ev); break;
        case CAWD_EV_APPROVAL_REQUEST:cawd__dispatch_approval(app, &ev); break;
        case CAWD_EV_APPROVAL_RESPONSE: {
            /* Wake any caller blocked in cawd_await_approval(). */
            uint32_t t = ev.u.approval_response.ticket;
            for (int i = 0; i < 32; i++) {
                pthread_mutex_lock(&app->pending_approvals[i].mu);
                if (app->pending_approvals[i].used && app->pending_approvals[i].ticket == t) {
                    app->pending_approvals[i].decision = ev.u.approval_response.decision;
                    app->pending_approvals[i].used = 0;
                    pthread_cond_signal(&app->pending_approvals[i].cv);
                }
                pthread_mutex_unlock(&app->pending_approvals[i].mu);
            }
        } break;
        case CAWD_EV_TOKEN:           cawd__dispatch_token(app, &ev); break;
        case CAWD_EV_AGENT_STATE:     cawd__dispatch_state(app, &ev); break;
        case CAWD_EV_ERROR:           cawd__dispatch_error(app, &ev); break;
        case CAWD_EV_QUIT:            app->running = 0; break;
        case CAWD_EV_TICKER:          /* UI layer consumes */ break;
        case CAWD_EV_TOAST:           /* UI layer consumes */ break;
        case CAWD_EV_FOCUS_CHANNEL:   app->focused_channel = ev.channel; break;
        default: break;
        }
    }
    return app->running ? 0 : 1;
}

/* Forward decl: defined in the UI layer at the bottom of this file. */
static int cawd__run_with_ui(CawdApp *app);

int cawd_run(CawdApp *app) {
    if (!app) return -1;
    if (app->cfg.custom_ui) {
        while (cawd_step(app) == 0) { /* loop */ }
        return 0;
    }
    return cawd__run_with_ui(app);
}

void cawd_quit(CawdApp *app) {
    if (!app) return;
    CawdEvent ev = {.kind = CAWD_EV_QUIT};
    cawd__ring_push(&app->ring, &ev);
}

/* =========================================================================
 * Handler registration
 * ========================================================================= */

void cawd_on_message(CawdApp *app, CawdChannel ch, CawdOnMessageFn fn, CawdExec ex) {
    if (!app || ch >= CAWD_CH__COUNT) return;
    app->handlers.msg[ch].fn = fn;
    app->handlers.msg[ch].exec = ex;
}

void cawd_on_tool_call(CawdApp *app, CawdOnToolCallFn fn, CawdExec ex) {
    if (!app) return;
    app->handlers.tool_fn = fn; app->handlers.tool_exec = ex;
}

void cawd_on_approval(CawdApp *app, CawdOnApprovalFn fn, CawdExec ex) {
    if (!app) return;
    app->handlers.appr_fn = fn; app->handlers.appr_exec = ex;
}

void cawd_on_token(CawdApp *app, CawdOnTokenFn fn, CawdExec ex) {
    if (!app) return;
    app->handlers.tok_fn = fn; app->handlers.tok_exec = ex;
}

void cawd_on_agent_state(CawdApp *app, CawdOnAgentStateFn fn, CawdExec ex) {
    if (!app) return;
    app->handlers.state_fn = fn; app->handlers.state_exec = ex;
}

void cawd_on_error(CawdApp *app, CawdOnErrorFn fn, CawdExec ex) {
    if (!app) return;
    app->handlers.err_fn = fn; app->handlers.err_exec = ex;
}

/* =========================================================================
 * Dispatch helpers: run user handler either inline (UI) or via pool.
 * The `ctx` parameter for pool-scheduled handlers is an allocated struct
 * that must be freed by the worker.
 * ========================================================================= */

typedef struct { CawdApp *app; CawdEvent ev; } CawdHandlerCtx;

static void cawd__run_message(CawdApp *app, void *ctx) {
    CawdHandlerCtx *c = (CawdHandlerCtx *)ctx;
    CawdOnMessageFn fn = app->handlers.msg[c->ev.channel].fn;
    if (fn) fn(app, c->ev.u.msg);
    free(c);
}
static void cawd__dispatch_message(CawdApp *app, CawdEvent *ev) {
    CawdOnMessageFn fn = app->handlers.msg[ev->channel].fn;
    CawdExec ex = app->handlers.msg[ev->channel].exec;
    if (!fn) return;
    if (ex == CAWD_ON_UI) { fn(app, ev->u.msg); return; }
    CawdHandlerCtx *c = (CawdHandlerCtx *)malloc(sizeof *c);
    if (!c) return;
    c->app = app; c->ev = *ev;
    cawd__pool_post(&app->pool, cawd__run_message, c);
}

static void cawd__run_tool(CawdApp *app, void *ctx) {
    CawdHandlerCtx *c = (CawdHandlerCtx *)ctx;
    char out[8192] = {0};
    int rc = -1;
    if (app->handlers.tool_fn)
        rc = app->handlers.tool_fn(app, c->ev.u.tc, out, sizeof out);

    /* Write an audit entry whichever way the handler ran. */
    CawdAuditEntry ae;
    memset(&ae, 0, sizeof ae);
    ae.ts_ms = cawd__now_ms();
    ae.channel = c->ev.channel;
    ae.agent_id = cawd__strdup(&app->strs, c->ev.u.tc.agent_id);
    ae.tool     = cawd__strdup(&app->strs, c->ev.u.tc.tool);
    ae.args     = cawd__strdup(&app->strs, c->ev.u.tc.args_json);
    ae.verdict  = CAWD_ALLOW;
    ae.exit_code = rc;
    cawd__audit_append(&app->audit, &ae);
    free(c);
}
static void cawd__dispatch_tool(CawdApp *app, CawdEvent *ev) {
    if (!app->handlers.tool_fn) return;
    /* Always run tool handlers on worker — they may block. */
    CawdHandlerCtx *c = (CawdHandlerCtx *)malloc(sizeof *c);
    if (!c) return;
    c->app = app; c->ev = *ev;
    cawd__pool_post(&app->pool, cawd__run_tool, c);
}

static void cawd__run_approval(CawdApp *app, void *ctx) {
    CawdHandlerCtx *c = (CawdHandlerCtx *)ctx;
    if (app->handlers.appr_fn) app->handlers.appr_fn(app, c->ev.u.ap);
    free(c);
}
static void cawd__dispatch_approval(CawdApp *app, CawdEvent *ev) {
    if (!app->handlers.appr_fn) return;
    if (app->handlers.appr_exec == CAWD_ON_UI) {
        app->handlers.appr_fn(app, ev->u.ap);
        return;
    }
    CawdHandlerCtx *c = (CawdHandlerCtx *)malloc(sizeof *c);
    if (!c) return;
    c->app = app; c->ev = *ev;
    cawd__pool_post(&app->pool, cawd__run_approval, c);
}

static void cawd__dispatch_token(CawdApp *app, CawdEvent *ev) {
    /* Token events always run on UI thread (UI updates streaming widget).
     * User's on_token handler is purely observational — also UI-thread. */
    if (app->handlers.tok_fn) app->handlers.tok_fn(app, ev->u.tok);
}

static void cawd__dispatch_state(CawdApp *app, CawdEvent *ev) {
    if (app->handlers.state_fn)
        app->handlers.state_fn(app, ev->u.agent_state.agent_id, ev->u.agent_state.state);
}

static void cawd__dispatch_error(CawdApp *app, CawdEvent *ev) {
    if (app->handlers.err_fn)
        app->handlers.err_fn(app, ev->u.error.agent_id, ev->u.error.text);
}

/* =========================================================================
 * Channels — enable with stubbed bridges (real I/O later)
 * ========================================================================= */

static void *cawd__bridge_stub(void *arg) {
    CawdApp *app = (CawdApp *)arg;
    (void)app;
    /* Real bridges ship later. This stub sits idle. */
    return NULL;
}

int cawd_channel_enable(CawdApp *app, CawdChannel ch, void *cfg) {
    if (!app || ch >= CAWD_CH__COUNT) return -1;
    if (app->channels[ch].enabled) return 0;
    app->channels[ch].enabled = 1;
    /* Shallow copy the cfg struct — caller owns their own memory
     * originally, but we keep a copy so they can stack-alloc configs. */
    size_t cfg_sz = 0;
    switch (ch) {
    case CAWD_CH_TELEGRAM:   cfg_sz = sizeof(CawdTelegramCfg); break;
    case CAWD_CH_API:        cfg_sz = sizeof(CawdApiCfg); break;
    case CAWD_CH_GITHUB:     cfg_sz = sizeof(CawdGithubCfg); break;
    case CAWD_CH_SLACK:      cfg_sz = sizeof(CawdSlackCfg); break;
    case CAWD_CH_AUTONOMOUS: cfg_sz = sizeof(CawdAutonomousCfg); break;
    case CAWD_CH_HITL:       cfg_sz = 0; break;
    default:                 cfg_sz = 0; break;
    }
    if (cfg && cfg_sz) {
        app->channels[ch].cfg_copy = malloc(cfg_sz);
        if (app->channels[ch].cfg_copy) memcpy(app->channels[ch].cfg_copy, cfg, cfg_sz);
    }
    /* TODO(bridges): spawn real bridge threads here. For Wave 1 we only
     * mark the channel as enabled; the sim worker (in ai_demo.c) will
     * post scripted events for each enabled channel. */
    app->channels[ch].bridge_running = 0;
    (void)cawd__bridge_stub;  /* suppress unused */
    return 0;
}

void cawd_channel_disable(CawdApp *app, CawdChannel ch) {
    if (!app || ch >= CAWD_CH__COUNT) return;
    if (!app->channels[ch].enabled) return;
    app->channels[ch].enabled = 0;
    if (app->channels[ch].bridge_running) {
        app->channels[ch].bridge_running = 0;
        pthread_join(app->channels[ch].bridge_thr, NULL);
    }
    free(app->channels[ch].cfg_copy);
    app->channels[ch].cfg_copy = NULL;
}

int cawd_channel_is_enabled(CawdApp *app, CawdChannel ch) {
    if (!app || ch >= CAWD_CH__COUNT) return 0;
    return app->channels[ch].enabled;
}

/* =========================================================================
 * Agents
 * ========================================================================= */

const char *cawd_agent_spawn(CawdApp *app, CawdChannel origin, CawdAgentSpec sp) {
    if (!app) return NULL;
    char id[CAWD_AGENT_ID_LEN];
    CawdAgent *a = cawd__agent_alloc(&app->agents, id);
    if (!a) return NULL;
    a->name          = cawd__strdup(&app->strs, sp.name);
    a->model         = cawd__strdup(&app->strs, sp.model);
    a->provider      = cawd__strdup(&app->strs, sp.provider);
    a->system_prompt = cawd__strdup(&app->strs, sp.system_prompt);
    a->parent_id     = cawd__strdup(&app->strs, sp.parent_agent_id);
    a->origin = origin;
    a->current_channel = origin;
    a->state = CAWD_STATE_PENDING;
    a->spawned_ms = cawd__now_ms();
    a->last_update_ms = a->spawned_ms;
    return a->id;
}

static CawdAgent *cawd__agent_find(CawdApp *app, const char *id) {
    pthread_mutex_lock(&app->agents.mu);
    CawdAgent *a = cawd__agent_find_locked(&app->agents, id);
    pthread_mutex_unlock(&app->agents.mu);
    return a;
}

static void cawd__post_state(CawdApp *app, const char *id, CawdAgentState s) {
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_AGENT_STATE;
    ev.ts_ms = cawd__now_ms();
    ev.u.agent_state.agent_id = id;
    ev.u.agent_state.state = s;
    cawd__ring_push(&app->ring, &ev);
}

void cawd_agent_kill(CawdApp *app, const char *id) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->state = CAWD_STATE_CANCELLED;
    a->last_update_ms = cawd__now_ms();
    cawd__post_state(app, a->id, CAWD_STATE_CANCELLED);
}

void cawd_agent_pause(CawdApp *app, const char *id) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->state = CAWD_STATE_PAUSED;
    a->last_update_ms = cawd__now_ms();
    cawd__post_state(app, a->id, CAWD_STATE_PAUSED);
}

void cawd_agent_resume(CawdApp *app, const char *id) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->state = CAWD_STATE_RUNNING;
    a->last_update_ms = cawd__now_ms();
    cawd__post_state(app, a->id, CAWD_STATE_RUNNING);
}

const char *cawd_agent_fork(CawdApp *app, const char *id) {
    CawdAgent *src = cawd__agent_find(app, id);
    if (!src) return NULL;
    CawdAgentSpec sp = {0};
    sp.name = src->name;
    sp.model = src->model;
    sp.provider = src->provider;
    sp.system_prompt = src->system_prompt;
    sp.parent_agent_id = src->id;
    return cawd_agent_spawn(app, src->origin, sp);
}

void cawd_agent_promote(CawdApp *app, const char *id, CawdChannel dest) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->current_channel = dest;
    a->last_update_ms = cawd__now_ms();
}

void cawd_agent_set_current_tool(CawdApp *app, const char *id, const char *tool) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->current_tool = cawd__strdup(&app->strs, tool);
    a->last_update_ms = cawd__now_ms();
}

void cawd_agent_add_tokens(CawdApp *app, const char *id, int in, int out) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->tokens_in += in;
    a->tokens_out += out;
    a->last_update_ms = cawd__now_ms();
    /* Update per-channel cost ledger. */
    pthread_mutex_lock(&app->cost_mu);
    app->cost[a->current_channel].tokens_in  += (double)in;
    app->cost[a->current_channel].tokens_out += (double)out;
    /* Simple $3/Mi input, $15/Mi output — calibrate per your pricing. */
    app->cost[a->current_channel].usd += in * 3.0 / 1.0e6 + out * 15.0 / 1.0e6;
    app->cost[a->current_channel].requests++;
    pthread_mutex_unlock(&app->cost_mu);
}

void cawd_agent_set_state(CawdApp *app, const char *id, CawdAgentState s) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    a->state = s;
    a->last_update_ms = cawd__now_ms();
    cawd__post_state(app, a->id, s);
}

/* =========================================================================
 * Streaming
 * ========================================================================= */

void cawd_stream_begin(CawdApp *app, const char *id) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    pthread_mutex_lock(&a->stream_mu);
    a->stream_produced = 0;
    a->stream_consumed = 0;
    a->stream_open = 1;
    pthread_mutex_unlock(&a->stream_mu);
    cawd_agent_set_state(app, id, CAWD_STATE_STREAMING);
}

void cawd_stream_push(CawdApp *app, const char *id, const char *token) {
    if (!token) return;
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    size_t len = strlen(token);
    pthread_mutex_lock(&a->stream_mu);
    if (!a->stream_open) { pthread_mutex_unlock(&a->stream_mu); return; }
    if (a->stream_produced + len < sizeof a->stream_buf) {
        memcpy(a->stream_buf + a->stream_produced, token, len);
        a->stream_produced += len;
    }
    pthread_mutex_unlock(&a->stream_mu);
    /* Post a token event so UI + user handler see the chunk. */
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_TOKEN;
    ev.ts_ms = cawd__now_ms();
    ev.u.tok.agent_id = a->id;
    ev.u.tok.token = cawd__strdup(&app->strs, token);
    ev.u.tok.is_final = 0;
    cawd__ring_push(&app->ring, &ev);
}

void cawd_stream_end(CawdApp *app, const char *id, int ok) {
    CawdAgent *a = cawd__agent_find(app, id);
    if (!a) return;
    pthread_mutex_lock(&a->stream_mu);
    a->stream_open = 0;
    pthread_mutex_unlock(&a->stream_mu);
    cawd_agent_set_state(app, id, ok ? CAWD_STATE_DONE : CAWD_STATE_FAILED);
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_TOKEN;
    ev.ts_ms = cawd__now_ms();
    ev.u.tok.agent_id = a->id;
    ev.u.tok.token = "";
    ev.u.tok.is_final = 1;
    cawd__ring_push(&app->ring, &ev);
}

/* =========================================================================
 * Approvals
 * ========================================================================= */

CawdDecision cawd_await_approval(CawdApp *app, CawdApproval ap, int timeout_ms) {
    if (!app) return CAWD_TIMEOUT;
    /* Allocate a ticket slot. */
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        pthread_mutex_lock(&app->pending_approvals[i].mu);
        if (!app->pending_approvals[i].used) {
            app->pending_approvals[i].used = 1;
            app->pending_approvals[i].ticket = ++app->next_ticket;
            app->pending_approvals[i].decision = CAWD_TIMEOUT;
            slot = i;
            pthread_mutex_unlock(&app->pending_approvals[i].mu);
            break;
        }
        pthread_mutex_unlock(&app->pending_approvals[i].mu);
    }
    if (slot < 0) return CAWD_TIMEOUT;
    uint32_t ticket = app->pending_approvals[slot].ticket;

    /* Post the approval request for the UI to render a modal. */
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_APPROVAL_REQUEST;
    ev.ts_ms = cawd__now_ms();
    ev.u.ap = ap;
    /* The ticket is stashed in agent_state.agent_id via a cheap hack — a
     * proper field would add a 4-byte slot. Instead we rely on the UI
     * layer posting back with the ticket it was handed. For now, the
     * UI layer stashes pending (ticket, approval) in a side table. */
    cawd__ring_push(&app->ring, &ev);

    /* Wait on cv. */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec  += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&app->pending_approvals[slot].mu);
    while (app->pending_approvals[slot].used) {
        int rc = pthread_cond_timedwait(&app->pending_approvals[slot].cv,
                                         &app->pending_approvals[slot].mu, &deadline);
        if (rc == ETIMEDOUT) {
            app->pending_approvals[slot].used = 0;
            app->pending_approvals[slot].decision = CAWD_TIMEOUT;
            break;
        }
    }
    CawdDecision d = app->pending_approvals[slot].decision;
    pthread_mutex_unlock(&app->pending_approvals[slot].mu);
    (void)ticket;
    return d;
}

int cawd_dialog(CawdApp *app, CawdDialogSpec spec) {
    /* Dialog rendering requires the UI layer. Until that ships, return
     * -1 (timeout equivalent). */
    (void)app; (void)spec;
    return -1;
}

/* =========================================================================
 * Policy API
 * ========================================================================= */

void cawd_policy_add(CawdApp *app, const char *match, CawdVerdict v, CawdChannel scope) {
    if (!app || !match) return;
    cawd__pol_add(&app->policy, match, v, scope, 0, 0);
}

void cawd_policy_clear(CawdApp *app) {
    if (!app) return;
    pthread_mutex_lock(&app->policy.mu);
    memset(app->policy.rules, 0, sizeof app->policy.rules);
    app->policy.bypass_until_ms = 0;
    pthread_mutex_unlock(&app->policy.mu);
}

void cawd_policy_override_once(CawdApp *app, const char *match, CawdVerdict v) {
    if (!app || !match) return;
    cawd__pol_add(&app->policy, match, v, CAWD_CH_ANY, 1, 0);
}

void cawd_policy_override_session(CawdApp *app, const char *match, CawdVerdict v) {
    if (!app || !match) return;
    /* Session = 24 h from now. */
    uint64_t until = cawd__now_ms() + 24ull * 3600ull * 1000ull;
    cawd__pol_add(&app->policy, match, v, CAWD_CH_ANY, 0, until);
}

void cawd_policy_bypass_all(CawdApp *app, int seconds) {
    if (!app) return;
    pthread_mutex_lock(&app->policy.mu);
    app->policy.bypass_until_ms = cawd__now_ms() + (uint64_t)seconds * 1000ull;
    pthread_mutex_unlock(&app->policy.mu);
}

CawdVerdict cawd_policy_check(CawdApp *app, CawdToolCall tc) {
    if (!app) return CAWD_DENY;
    return cawd__pol_check(&app->policy, tc.origin, tc.tool, tc.args_json);
}

/* =========================================================================
 * Audit + cost
 * ========================================================================= */

int cawd_audit_tail(CawdApp *app, CawdAuditEntry *out, int max) {
    if (!app || !out || max <= 0) return 0;
    return cawd__audit_tail(&app->audit, out, max);
}

CawdCost cawd_cost_snapshot(CawdApp *app, CawdChannel ch) {
    CawdCost c = {0};
    if (!app) return c;
    pthread_mutex_lock(&app->cost_mu);
    if (ch == CAWD_CH_ANY) {
        for (int i = 0; i < CAWD_CH__COUNT; i++) {
            c.tokens_in  += app->cost[i].tokens_in;
            c.tokens_out += app->cost[i].tokens_out;
            c.usd        += app->cost[i].usd;
            c.requests   += app->cost[i].requests;
        }
    } else if ((int)ch < CAWD_CH__COUNT) {
        c = app->cost[ch];
    }
    pthread_mutex_unlock(&app->cost_mu);
    return c;
}

/* =========================================================================
 * UI utilities (event-bus side — rendering lives in the UI layer)
 * ========================================================================= */

void cawd_toast(CawdApp *app, CawdKind kind, const char *title, const char *body) {
    if (!app) return;
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_TOAST;
    ev.ts_ms = cawd__now_ms();
    ev.u.toast.kind = kind;
    ev.u.toast.title = cawd__strdup(&app->strs, title);
    ev.u.toast.body  = cawd__strdup(&app->strs, body);
    cawd__ring_push(&app->ring, &ev);
}

void cawd_focus_channel(CawdApp *app, CawdChannel ch) {
    if (!app) return;
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_FOCUS_CHANNEL;
    ev.ts_ms = cawd__now_ms();
    ev.channel = ch;
    cawd__ring_push(&app->ring, &ev);
}

void cawd_ticker_push(CawdApp *app, CawdChannel ch, const char *text) {
    if (!app) return;
    CawdEvent ev = {0};
    ev.kind = CAWD_EV_TICKER;
    ev.ts_ms = cawd__now_ms();
    ev.channel = ch;
    ev.u.ticker.text = cawd__strdup(&app->strs, text);
    cawd__ring_push(&app->ring, &ev);
}

/* =========================================================================
 *
 *                        BUILT-IN UI LAYER
 *
 *  Only active when !cfg.custom_ui. Renders the 13-slot workstation:
 *  6 exec channels (HITL, Telegram, Autonomous, GitHub, Slack, API) +
 *  7 cross-cutting views (Orchestra, Inspector, Policies, Audit,
 *  Analytics, Settings, Help). Each slot is a static stage renderer
 *  that callers never touch; replaced in wave-3 with real content.
 *
 * ========================================================================= */

static const char *CAWD__UI_LABELS[CAWD_UI_SLOT_COUNT] = {
    "HITL", "TG", "AUTO", "GH", "SL", "API",
    "Orchestra", "Inspector", "Policies", "Audit",
    "Analytics", "Settings", "Help"
};

static const char *CAWD__UI_TITLES[CAWD_UI_SLOT_COUNT] = {
    "HITL — interactive chat",
    "TELEGRAM — remote user via bot",
    "AUTONOMOUS — queue / cron / webhooks",
    "GITHUB — PR / issue events",
    "SLACK — slash / mentions",
    "API — inbound HTTP requests",
    "ORCHESTRA — cross-channel agent grid",
    "INSPECTOR — deep-dive agent modal",
    "POLICIES — consent rule editor",
    "AUDIT — immutable event log",
    "ANALYTICS — cross-channel stats",
    "SETTINGS — providers / toggles",
    "HELP — keybindings & credits"
};

static const char *CAWD__UI_ICONS[CAWD_UI_SLOT_COUNT] = {
    "\xe2\x97\x86", "\xe2\x9c\x88", "\xe2\x9a\x99", "\xc2\xa4",
    "#",             "\xe2\x87\x84", "\xe2\x97\x8f", "\xe2\x86\x92",
    "\xc2\xb6",     "\xe2\x96\xa4", "\xe2\x96\xb3", "\xe2\x9a\x99", "?"
};

static const char *CAWD__UI_HINTS[CAWD_UI_SLOT_COUNT] = {
    "Chat composer + streaming assistant will land here.",
    "Telegram-styled transcript will land here.",
    "Queue stats, throughput chart, task list will land here.",
    "PR / issue cards with diff preview will land here.",
    "Threaded message view with /claw slash command will land here.",
    "Endpoint list, live request stream, trace detail will land here.",
    "3x2 FluxAgentCard grid + FluxGanttRow will land here.",
    "Press `e` on an orchestra card to deep-dive (coming soon).",
    "FluxTree + FluxForm rule editor will land here.",
    "FluxRichLog of every decision will land here.",
    "Line charts, bar charts, heatmap, SLO gauges will land here.",
    "FluxForm of per-channel toggles + API keys will land here.",
    "Categorised keybindings + what's new will land here."
};

/* ---- Stage renderer stubs (replaced per-slot in wave 3) ----- */

static void cawd__ui_stub_render(FluxSB *sb, int w, int h, int slot) {
    if (!sb || w <= 0) return;
    int content_rows = 3;
    if (h < content_rows) h = content_rows;
    int blanks_top    = (h - content_rows) / 2;
    int blanks_bottom = h - content_rows - blanks_top;
    if (blanks_top < 0) blanks_top = 0;
    if (blanks_bottom < 0) blanks_bottom = 0;

    for (int i = 0; i < blanks_top; i++) {
        for (int j = 0; j < w; j++) flux_sb_append(sb, " ");
        flux_sb_append(sb, "\n");
    }
    char title[96];
    snprintf(title, sizeof title, "%s  coming soon", CAWD__UI_LABELS[slot]);
    flux_placeholder(sb, CAWD__UI_ICONS[slot], title, CAWD__UI_HINTS[slot], w);
    for (int i = 0; i < blanks_bottom; i++) {
        for (int j = 0; j < w; j++) flux_sb_append(sb, " ");
        flux_sb_append(sb, "\n");
    }
}

/* ---- API channel (Wave 3) — ngrok-style live request inspector ----- */

/* One simulated HTTP request in the live-stream ring buffer. */
typedef struct {
    uint64_t ts_ms;
    const char *method;   /* interned pointer to static string */
    const char *path;     /* interned pointer to static string */
    int         status;
    int         latency_ms;
    long        size_bytes;
} CawdApiReq;

static void cawd__api_fmt_url(char *out, size_t cap, const CawdApiCfg *cfg) {
    const char *label = (cfg && cfg->ngrok_like_label) ? cfg->ngrok_like_label
                                                       : "claw-demo.ngrok.io";
    snprintf(out, cap, "https://%s", label);
}

/* Synthesize one pseudo-random request deterministically from `now`. */
static void cawd__api_gen_req(CawdApiReq *r, uint64_t now) {
    static const char *METHODS[5] = { "POST", "POST", "GET", "POST", "GET" };
    static const char *PATHS[5]   = {
        "/v1/chat", "/v1/agents", "/v1/audit", "/v1/tools", "/v1/health"
    };
    uint64_t x = now * 2862933555777941757ULL + 3037000493ULL;
    int ep = (int)((x >> 33) % 5);
    int bucket = (int)((x >> 17) & 0x0F);
    int status = 200;
    if (bucket == 0)        status = 429;
    else if (bucket == 1)   status = 401;
    else if (bucket == 2)   status = 500;
    else if (bucket == 3)   status = 404;
    int lat = (int)(10 + ((x >> 5) % 1200));
    long sz  = (status >= 400) ? (long)((x >> 9) % 128)
                               : (long)(120 + ((x >> 11) % 4096));
    r->ts_ms      = now;
    r->method     = METHODS[ep];
    r->path       = PATHS[ep];
    r->status     = status;
    r->latency_ms = lat;
    r->size_bytes = sz;
}

/* Lerp 3 RGB points green->amber->red along t in [0,1]. */
static void cawd__api_gauge_color(float t, char *out, size_t cap) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r, g, b;
    if (t < 0.5f) {
        float u = t / 0.5f;
        r = (int)(141 + (254 - 141) * u);
        g = (int)(184 + (225 - 184) * u);
        b = (int)( 97 + (156 -  97) * u);
    } else {
        float u = (t - 0.5f) / 0.5f;
        r = (int)(254 + (204 - 254) * u);
        g = (int)(225 + (101 - 225) * u);
        b = (int)(156 + (121 - 156) * u);
    }
    snprintf(out, cap, "\x1b[38;2;%d;%d;%dm", r, g, b);
}

static void cawd__api_row(FluxSB *dst, int w, const char *s) {
    if (w <= 0) return;
    flux_fit(dst, s ? s : "", w, NULL, FLUX_ALIGN_LEFT);
    flux_sb_append(dst, "\n");
}

static void cawd__api_row_blank(FluxSB *dst, int w) {
    for (int i = 0; i < w; i++) flux_sb_append(dst, " ");
    flux_sb_append(dst, "\n");
}

/* Left endpoints column (w cells x `rows` rows). */
static void cawd__api_render_endpoints(FluxSB *out, int w, int rows,
                                       uint64_t now) {
    static const struct { const char *method; const char *path; }
        EPS[5] = {
            { "POST", "/v1/chat"   },
            { "POST", "/v1/agents" },
            { "GET",  "/v1/audit"  },
            { "POST", "/v1/tools"  },
            { "GET",  "/v1/health" },
        };
    if (w <= 0 || rows <= 0) return;
    int used = 0;
    {
        char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, "ENDPOINTS");
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;
    cawd__api_row_blank(out, w); used++;

    for (int i = 0; i < 5 && used < rows; i++) {
        {
            const char *mfg = (strcmp(EPS[i].method, "GET") == 0)
                ? "\x1b[38;2;125;207;255m" : FLUX_THEME_OK_FG;
            char head[128];
            snprintf(head, sizeof head, " %s%s%-4s\x1b[0m %s%s\x1b[0m",
                     FLUX_BOLD, mfg, EPS[i].method,
                     FLUX_THEME_TEXT_FG, EPS[i].path);
            cawd__api_row(out, w, head);
            used++;
        }
        if (used >= rows) break;
        {
            long base = 180L + (long)i * 73L;
            long calls = base + (long)((now / 900ULL) % 47ULL) + i * 11;
            char buf[64];
            snprintf(buf, sizeof buf, "   %ld calls", calls);

            int spark_w = w - 16;
            if (spark_w < 4)  spark_w = 4;
            if (spark_w > 12) spark_w = 12;
            float ring[16];
            for (int k = 0; k < spark_w; k++) {
                uint64_t y = (now / 600ULL) + (uint64_t)k
                           + (uint64_t)i * 7ULL;
                y = y * 2862933555777941757ULL + 3037000493ULL;
                ring[k] = (float)((y >> 28) & 0xFF);
            }
            char L2[1024]; FluxSB l2; flux_sb_init(&l2, L2, sizeof L2);
            flux_sb_append(&l2, FLUX_THEME_TEXT_DIM_FG);
            flux_sb_append(&l2, buf);
            flux_sb_append(&l2, FLUX_RESET);
            flux_sb_append(&l2, " ");
            flux_sparkline(&l2, ring, spark_w, 0, FLUX_THEME_BRAND_PURPLE_FG);
            flux_sb_append(&l2, FLUX_RESET);
            cawd__api_row(out, w, flux_sb_str(&l2));
            used++;
        }
    }
    while (used < rows) { cawd__api_row_blank(out, w); used++; }
}

/* Center live-stream column. Each request is one row. */
static void cawd__api_render_stream(FluxSB *out, int w, int rows,
                                    const CawdApiReq *ring, int ring_cap,
                                    int head, int count) {
    if (w <= 0 || rows <= 0) return;
    int used = 0;
    {
        char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, "LIVE REQUESTS");
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;
    cawd__api_row_blank(out, w); used++;

    int slots = rows - used;
    int shown = count < slots ? count : slots;
    for (int i = 0; i < shown; i++) {
        int idx = (head - 1 - i + ring_cap * 4) % ring_cap;
        const CawdApiReq *r = &ring[idx];

        uint64_t secs = r->ts_ms / 1000ULL;
        int hh = (int)((secs / 3600ULL) % 24ULL);
        int mm = (int)((secs /   60ULL) % 60ULL);
        int ss = (int)( secs             % 60ULL);

        const char *mfg =
            (strcmp(r->method, "GET") == 0)    ? "\x1b[38;2;125;207;255m" :
            (strcmp(r->method, "POST") == 0)   ? FLUX_THEME_OK_FG         :
            (strcmp(r->method, "DELETE") == 0) ? FLUX_THEME_ERR_FG        :
                                                 FLUX_THEME_WARN_FG;
        const char *sfg =
            (r->status >= 500) ? FLUX_THEME_ERR_FG  :
            (r->status >= 400) ? FLUX_THEME_WARN_FG :
            (r->status >= 300) ? "\x1b[38;2;125;207;255m" :
                                 FLUX_THEME_OK_FG;

        char size[32];
        if (r->status >= 400) {
            snprintf(size, sizeof size, "-");
        } else if (r->size_bytes < 1024) {
            snprintf(size, sizeof size, "%ldB", r->size_bytes);
        } else {
            snprintf(size, sizeof size, "%.1fKB", r->size_bytes / 1024.0);
        }

        char buf[512];
        snprintf(buf, sizeof buf,
            " %s%02d:%02d:%02d\x1b[0m  %s%-4s\x1b[0m %s%-12s\x1b[0m"
            "  %s%3d\x1b[0m  %4dms  %s%s\x1b[0m",
            FLUX_THEME_TEXT_DIM_FG, hh, mm, ss,
            mfg, r->method,
            FLUX_THEME_TEXT_FG, r->path,
            sfg, r->status,
            r->latency_ms,
            FLUX_THEME_TEXT_DIM_FG, size);
        cawd__api_row(out, w, buf);
        used++;
    }
    while (used < rows) { cawd__api_row_blank(out, w); used++; }
}

/* Right detail column: selected (most recent) request. */
static void cawd__api_render_detail(FluxSB *out, int w, int rows,
                                    const CawdApiReq *r) {
    if (w <= 0 || rows <= 0) return;
    int used = 0;
    {
        char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, "SELECTED REQUEST");
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;
    cawd__api_row_blank(out, w); used++;

    if (!r) {
        cawd__api_row(out, w, " (no requests yet)");
        used++;
        while (used < rows) { cawd__api_row_blank(out, w); used++; }
        return;
    }
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_BOLD);
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        char t[256]; snprintf(t, sizeof t, "%s %s", r->method, r->path);
        flux_sb_append(&l, t);
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, " Authorization: Bearer sk-***");
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, " Content-Type: application/json");
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;
    cawd__api_row_blank(out, w); used++;

    /* Request body (pretty JSON). */
    {
        char body[256];
        snprintf(body, sizeof body,
                 "{\"model\":\"claude-opus-4-7\",\"path\":\"%s\"}",
                 r->path);
        char P[2048]; FluxSB pb; flux_sb_init(&pb, P, sizeof P);
        if (w > 2) flux_pretty(&pb, body, w - 1, NULL);
        const char *s = flux_sb_str(&pb);
        const char *p = s;
        while (*p && used < rows) {
            const char *nl = strchr(p, '\n');
            int len = nl ? (int)(nl - p) : (int)strlen(p);
            char line[1024];
            if (len >= (int)sizeof line) len = sizeof line - 1;
            memcpy(line, p, len); line[len] = 0;
            cawd__api_row(out, w, line);
            used++;
            if (!nl) break;
            p = nl + 1;
        }
    }
    if (used >= rows) return;

    /* Response separator line. */
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_BORDER_FG);
        flux_sb_append(&l, " --- response ");
        char t[64];
        const char *sfg =
            (r->status >= 500) ? FLUX_THEME_ERR_FG  :
            (r->status >= 400) ? FLUX_THEME_WARN_FG :
                                 FLUX_THEME_OK_FG;
        snprintf(t, sizeof t, "%s%d\x1b[0m", sfg, r->status);
        flux_sb_append(&l, t);
        flux_sb_append(&l, FLUX_THEME_BORDER_FG);
        char lat[32]; snprintf(lat, sizeof lat, " %dms", r->latency_ms);
        flux_sb_append(&l, lat);
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(out, w, flux_sb_str(&l));
        used++;
    }
    if (used >= rows) return;

    /* Response body: syntax-highlighted JSON. */
    {
        char resp[256];
        if (r->status >= 400) {
            snprintf(resp, sizeof resp,
                     "{\"error\":{\"code\":%d}}", r->status);
        } else {
            snprintf(resp, sizeof resp,
                     "{\"id\":\"msg_%02x\",\"bytes\":%ld}",
                     (int)((r->ts_ms >> 8) & 0xFF), r->size_bytes);
        }
        char H[2048]; FluxSB hb; flux_sb_init(&hb, H, sizeof H);
        if (w > 2) flux_syntax_highlight(&hb, resp, FLUX_LANG_JSON, w - 1);
        const char *s = flux_sb_str(&hb);
        const char *p = s;
        while (*p && used < rows) {
            const char *nl = strchr(p, '\n');
            int len = nl ? (int)(nl - p) : (int)strlen(p);
            char line[1024];
            if (len >= (int)sizeof line) len = sizeof line - 1;
            memcpy(line, p, len); line[len] = 0;
            cawd__api_row(out, w, line);
            used++;
            if (!nl) break;
            p = nl + 1;
        }
    }

    while (used < rows) { cawd__api_row_blank(out, w); used++; }
}

/* Stage renderer: API channel. */
static void cawd__ui_stage_api_impl(CawdApp *a, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0 || h <= 0) return;

    const CawdApiCfg *cfg = NULL;
    if (a && a->channels[CAWD_CH_API].cfg_copy) {
        cfg = (const CawdApiCfg *)a->channels[CAWD_CH_API].cfg_copy;
    }

    uint64_t now = flux_now_ms();

    #define CAWD_API_RING_N 32
    static CawdApiReq s_ring[CAWD_API_RING_N];
    static int        s_head    = 0;
    static int        s_count   = 0;
    static uint64_t   s_next_ms = 0;
    static uint64_t   s_seed    = 0;

    #define CAWD_API_RPM_BUCKETS 60
    static float    s_rpm_ring[CAWD_API_RPM_BUCKETS] = {0};
    static uint64_t s_rpm_last_sec = 0;
    static int      s_rpm_head = 0;

    if (s_next_ms == 0) {
        s_next_ms = now + 300;
        s_rpm_last_sec = now / 1000ULL;
    }

    {
        uint64_t this_sec = now / 1000ULL;
        if (s_rpm_last_sec > this_sec) s_rpm_last_sec = this_sec;
        if ((this_sec - s_rpm_last_sec) >= CAWD_API_RPM_BUCKETS) {
            for (int i = 0; i < CAWD_API_RPM_BUCKETS; i++) s_rpm_ring[i] = 0.0f;
            s_rpm_head = 0;
            s_rpm_last_sec = this_sec;
        } else {
            while (s_rpm_last_sec < this_sec) {
                s_rpm_head = (s_rpm_head + 1) % CAWD_API_RPM_BUCKETS;
                s_rpm_ring[s_rpm_head] = 0.0f;
                s_rpm_last_sec++;
            }
        }
    }

    int safety = 0;
    while (now >= s_next_ms && safety++ < 8) {
        CawdApiReq r;
        s_seed = s_seed * 2862933555777941757ULL + (uint64_t)s_next_ms;
        cawd__api_gen_req(&r, s_next_ms ^ s_seed);
        s_ring[s_head] = r;
        s_head = (s_head + 1) % CAWD_API_RING_N;
        if (s_count < CAWD_API_RING_N) s_count++;
        s_rpm_ring[s_rpm_head] += 1.0f;

        uint64_t gap = 180 + ((s_seed >> 20) % 700ULL);
        s_next_ms += gap;
    }

    float rpm_total = 0.0f;
    for (int i = 0; i < CAWD_API_RPM_BUCKETS; i++) rpm_total += s_rpm_ring[i];
    int rpm_current = (int)rpm_total;

    int p50 = 0, p95 = 0;
    if (s_count > 0) {
        int arr[CAWD_API_RING_N];
        for (int i = 0; i < s_count; i++) {
            int idx = (s_head - 1 - i + CAWD_API_RING_N * 4) % CAWD_API_RING_N;
            arr[i] = s_ring[idx].latency_ms;
        }
        for (int i = 1; i < s_count; i++) {
            int x = arr[i], j = i - 1;
            while (j >= 0 && arr[j] > x) { arr[j+1] = arr[j]; j--; }
            arr[j+1] = x;
        }
        p50 = arr[s_count / 2];
        int p95_idx = (s_count * 95) / 100;
        if (p95_idx >= s_count) p95_idx = s_count - 1;
        p95 = arr[p95_idx];
    }

    int active_sessions = 1 + (rpm_current / 8);
    if (active_sessions > 12) active_sessions = 12;

    int err = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_ring[i].status >= 400) err++;
    }
    float ok_pct = (s_count > 0)
        ? (100.0f * (float)(s_count - err) / (float)s_count)
        : 99.9f;

    int rate_limit = (cfg && cfg->rate_limit_rpm > 0) ? cfg->rate_limit_rpm : 60;

    /* Header row. */
    {
        char url[128]; cawd__api_fmt_url(url, sizeof url, cfg);
        char L[1024]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_badge(&l, url, FLUX_THEME_TEXT_INV_FG, FLUX_THEME_WARN_FG);
        flux_sb_append(&l, "  ");
        char stats[256];
        snprintf(stats, sizeof stats,
                 "%s%d active\x1b[0m  %s%d req/min\x1b[0m  %s%.1f%% ok\x1b[0m",
                 FLUX_THEME_TEXT_FG, active_sessions,
                 FLUX_THEME_ACCENT_FG, rpm_current,
                 FLUX_THEME_OK_FG, ok_pct);
        flux_sb_append(&l, stats);
        cawd__api_row(sb, w, flux_sb_str(&l));
    }
    int rows_done = 1;
    if (rows_done >= h) return;

    cawd__api_row_blank(sb, w); rows_done++;
    if (rows_done >= h) return;

    int bottom_rows = 3;
    int body_room = h - rows_done;
    if (body_room < bottom_rows + 2) {
        bottom_rows = (body_room > 2) ? (body_room - 2) : 0;
    }
    int body_h = h - rows_done - bottom_rows;
    if (body_h < 1) body_h = 1;

    int left_w = 24, right_w = 30;
    int gap = 2;
    int center_w = w - left_w - right_w - gap * 2;
    int stacked = 0;
    if (w < 100 || center_w < 30) stacked = 1;

    if (!stacked) {
        static char LB[64 * 1024];  FluxSB lb; flux_sb_init(&lb, LB, sizeof LB);
        static char MB[128 * 1024]; FluxSB mb; flux_sb_init(&mb, MB, sizeof MB);
        static char RB[64 * 1024];  FluxSB rb; flux_sb_init(&rb, RB, sizeof RB);

        cawd__api_render_endpoints(&lb, left_w,  body_h, now);
        cawd__api_render_stream   (&mb, center_w, body_h,
                                   s_ring, CAWD_API_RING_N, s_head, s_count);
        const CawdApiReq *sel = NULL;
        if (s_count > 0) {
            int idx = (s_head - 1 + CAWD_API_RING_N) % CAWD_API_RING_N;
            sel = &s_ring[idx];
        }
        cawd__api_render_detail(&rb, right_w, body_h, sel);

        const char *panels[3] = { flux_sb_str(&lb), flux_sb_str(&mb), flux_sb_str(&rb) };
        const int   widths[3] = { left_w, center_w, right_w };
        flux_hbox(sb, panels, widths, 3, "  ");
        rows_done += body_h;
    } else {
        int ep_h  = 6 < body_h / 2 ? 6 : body_h / 2;
        int det_h = (body_h >= 8) ? 3 : 0;
        if (body_h < ep_h + det_h + 3) { ep_h = 0; det_h = 0; }
        int stream_h = body_h - ep_h - det_h;
        if (stream_h < 1) stream_h = 1;

        if (ep_h > 0) {
            cawd__api_render_endpoints(sb, w, ep_h, now);
            rows_done += ep_h;
        }
        cawd__api_render_stream(sb, w, stream_h,
                                s_ring, CAWD_API_RING_N, s_head, s_count);
        rows_done += stream_h;

        if (det_h > 0) {
            const CawdApiReq *sel = NULL;
            if (s_count > 0) {
                int idx = (s_head - 1 + CAWD_API_RING_N) % CAWD_API_RING_N;
                sel = &s_ring[idx];
            }
            cawd__api_render_detail(sb, w, det_h, sel);
            rows_done += det_h;
        }
    }

    if (bottom_rows <= 0) {
        while (rows_done < h) { cawd__api_row_blank(sb, w); rows_done++; }
        return;
    }

    /* Bottom row A: req/min sparkline + p50/p95 labels. */
    {
        char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, "req/min ");
        flux_sb_append(&l, FLUX_RESET);

        int spark_w = w - 44;
        if (spark_w < 10) spark_w = 10;
        if (spark_w > CAWD_API_RPM_BUCKETS) spark_w = CAWD_API_RPM_BUCKETS;

        float tmp[CAWD_API_RPM_BUCKETS];
        for (int i = 0; i < spark_w; i++) {
            int idx = (s_rpm_head - (spark_w - 1 - i)
                      + CAWD_API_RPM_BUCKETS * 2)
                      % CAWD_API_RPM_BUCKETS;
            tmp[i] = s_rpm_ring[idx];
        }
        flux_sparkline(&l, tmp, spark_w, 0, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, FLUX_RESET);

        char extra[128];
        snprintf(extra, sizeof extra,
                 "  %sp50\x1b[0m %dms  %sp95\x1b[0m %dms",
                 FLUX_THEME_TEXT_DIM_FG, p50,
                 FLUX_THEME_TEXT_DIM_FG, p95);
        flux_sb_append(&l, extra);
        cawd__api_row(sb, w, flux_sb_str(&l));
        rows_done++;
        bottom_rows--;
    }
    if (bottom_rows <= 0) {
        while (rows_done < h) { cawd__api_row_blank(sb, w); rows_done++; }
        return;
    }

    /* Bottom row B: rate-limit gauge, color-lerped inline. */
    {
        float t = (rate_limit > 0)
            ? ((float)rpm_current / (float)rate_limit) : 0.0f;
        if (t > 1.0f) t = 1.0f;
        char color[32]; cawd__api_gauge_color(t, color, sizeof color);

        char L[4096]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        char lbl[64];
        snprintf(lbl, sizeof lbl, "rate %d/%d ", rpm_current, rate_limit);
        flux_sb_append(&l, lbl);
        flux_sb_append(&l, FLUX_RESET);

        int pre = (int)strlen(lbl) + 1;
        int bar_w = w - pre - 2;
        if (bar_w < 6) bar_w = 6;
        int filled = (int)((float)bar_w * t + 0.5f);
        if (filled > bar_w) filled = bar_w;

        flux_sb_append(&l, color);
        for (int i = 0; i < filled; i++) flux_sb_append(&l, "\xe2\x96\x88");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        for (int i = filled; i < bar_w; i++) flux_sb_append(&l, "\xe2\x96\x91");
        flux_sb_append(&l, FLUX_RESET);
        cawd__api_row(sb, w, flux_sb_str(&l));
        rows_done++;
        bottom_rows--;
    }

    while (bottom_rows > 0) {
        cawd__api_row_blank(sb, w); bottom_rows--; rows_done++;
    }
    while (rows_done < h) { cawd__api_row_blank(sb, w); rows_done++; }
}

/* Per-slot stage renderers — each is a tiny function so we can replace
 * them individually in later waves without touching the dispatch table. */

/* ---- HITL stage — Wave 3 renderer ------------------------------ *
 * Renders the human-in-the-loop chat transcript: header with model
 * badge + context window, optional thinking shimmer, user prompt
 * bubble, streaming assistant bubble (reads stream_buf under
 * stream_mu), a tiny op-tree of the agent's current tool, and a
 * tool-call card when current_tool is set. Falls back to a welcoming
 * placeholder when no agent has been spawned yet. Always emits
 * exactly `h` rows of exactly `w` cells. */

/* Forward decls (defined further down in this same TU). */
static void cawd__ui_row_blank(FluxSB *sb, int w);
static void cawd__ui_row_fit  (FluxSB *sb, int w, const char *s);

/* Fill remaining vertical space with blank w-cell rows. */
static void cawd__hitl_pad(FluxSB *sb, int w, int rows) {
    for (int i = 0; i < rows; i++) cawd__ui_row_blank(sb, w);
}

/* Emit one already-formed multi-row block, truncated to at most
 * `max_rows`. Every row inside `block` must already be exactly `w`
 * cells wide + '\n'. */
static int cawd__hitl_emit_block(FluxSB *sb, const char *block, int max_rows) {
    int emitted = 0;
    const char *p = block;
    if (!block) return 0;
    while (*p && emitted < max_rows) {
        const char *nl = strchr(p, '\n');
        int seg = nl ? (int)(nl - p) + 1 : (int)strlen(p);
        char tmp[4096];
        int copy = seg < (int)sizeof tmp - 1 ? seg : (int)sizeof tmp - 1;
        memcpy(tmp, p, (size_t)copy);
        tmp[copy] = '\0';
        flux_sb_append(sb, tmp);
        emitted++;
        if (!nl) break;
        p = nl + 1;
    }
    return emitted;
}

/* Pick the slot whose spawned_ms is greatest; returns index or -1. */
static int cawd__hitl_pick_active_slot(CawdApp *app) {
    int best = -1;
    uint64_t best_ms = 0;
    pthread_mutex_lock(&app->agents.mu);
    for (int i = 0; i < CAWD_MAX_AGENTS; i++) {
        if (!app->agents.slots[i].used) continue;
        if (app->agents.slots[i].origin != CAWD_CH_HITL &&
            app->agents.slots[i].current_channel != CAWD_CH_HITL) continue;
        if (best < 0 || app->agents.slots[i].spawned_ms >= best_ms) {
            best = i;
            best_ms = app->agents.slots[i].spawned_ms;
        }
    }
    pthread_mutex_unlock(&app->agents.mu);
    return best;
}

/* Snapshot an agent's render-relevant fields + stream contents.
 * Stream text is copied into `out_stream` (null-terminated, at most
 * stream_cap-1 chars). Returns 0 on success, <0 if slot vanished. */
static int cawd__hitl_snapshot(CawdApp *app, int slot,
                               CawdAgent *out, char *out_stream,
                               size_t stream_cap) {
    if (slot < 0 || slot >= CAWD_MAX_AGENTS || !out || !out_stream) return -1;
    CawdAgent *a = &app->agents.slots[slot];
    pthread_mutex_lock(&a->stream_mu);
    if (!a->used) { pthread_mutex_unlock(&a->stream_mu); return -1; }
    /* Snapshot scalar fields. */
    *out = *a;
    size_t n = a->stream_produced;
    if (n >= stream_cap) n = stream_cap - 1;
    if (n > 0) memcpy(out_stream, a->stream_buf, n);
    out_stream[n] = '\0';
    pthread_mutex_unlock(&a->stream_mu);
    return 0;
}

/* Render a single-line row containing a shimmer-text "thinking…"
 * prefix. Emits exactly `w` cells + '\n'. */
static void cawd__hitl_thinking_row(FluxSB *sb, int w) {
    static FluxShimmerText s_shimmer;
    static int s_inited = 0;
    if (!s_inited) {
        flux_shimmer_init(&s_shimmer, "thinking\xe2\x80\xa6"); /* thinking… */
        s_shimmer.interval_ms = 120;
        s_inited = 1;
    }
    /* Advance shimmer offset based on wall clock so it animates
     * smoothly regardless of tick granularity. */
    {
        uint64_t now = flux_now_ms();
        int text_w = flux_strwidth(s_shimmer.text);
        int sw = s_shimmer.shimmer_w > 0 ? s_shimmer.shimmer_w : 1;
        int period = text_w + sw * 2;
        if (period < 1) period = 1;
        int step = (int)((now / (uint64_t)s_shimmer.interval_ms) % (uint64_t)period);
        s_shimmer.offset = step - sw;
    }
    char buf[256]; FluxSB b; flux_sb_init(&b, buf, (int)sizeof buf);
    flux_sb_append(&b, "  ");
    flux_shimmer_render(&s_shimmer, &b);
    cawd__ui_row_fit(sb, w, flux_sb_str(&b));
}

/* Blinking cursor glyph. Returns "\xe2\x96\x88" (█) when lit, " " when off. */
static const char *cawd__hitl_cursor_glyph(void) {
    return ((flux_now_ms() / 500) & 1) ? "\xe2\x96\x88" : " ";
}

static void cawd__ui_stage_hitl(CawdApp *a, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0 || h <= 0) return;

    /* ---- Empty state — no agents yet. --------------------------- */
    int slot = cawd__hitl_pick_active_slot(a);
    if (slot < 0) {
        int rows = h;
        int content = 3; /* placeholder emits icon+title+hint */
        int top    = (rows - content) / 2;
        int bottom = rows - content - top;
        if (top < 0) top = 0;
        if (bottom < 0) bottom = 0;
        cawd__hitl_pad(sb, w, top);
        flux_placeholder(sb,
            CAWD__UI_ICONS[0],
            "Ready when you are",
            "type in the composer below", w);
        cawd__hitl_pad(sb, w, bottom);
        return;
    }

    /* ---- Snapshot agent + stream. ------------------------------- */
    CawdAgent ag;
    static char stream_copy[CAWD_STREAM_BUF + 1];
    if (cawd__hitl_snapshot(a, slot, &ag, stream_copy,
                            sizeof stream_copy) < 0) {
        cawd__hitl_pad(sb, w, h);
        return;
    }

    /* Total "used" rows of the viewport tracked explicitly so we
     * can pad or truncate to exactly h rows. */
    int budget = h;

    /* ---- Row 1: channel badge + model badge + context window ---- */
    if (budget > 0) {
        int badge_w = 12;
        int ctx_w   = 32;
        if (badge_w + ctx_w + 2 > w) {
            badge_w = w / 5; if (badge_w < 8) badge_w = 8;
            ctx_w   = w - badge_w - 2;
            if (ctx_w < 10) ctx_w = 10;
        }
        int model_w = w - badge_w - ctx_w - 2;
        if (model_w < 14) {
            /* Tight: fold badge + context only. */
            ctx_w = w - badge_w - 1;
            model_w = 0;
        }

        char BB[512]; FluxSB bb; flux_sb_init(&bb, BB, (int)sizeof bb);
        FluxBadgeStatus bs = (ag.state == CAWD_STATE_STREAMING) ? FLUX_BADGE_RUN
                           : (ag.state == CAWD_STATE_DONE)      ? FLUX_BADGE_OK
                           : (ag.state == CAWD_STATE_FAILED ||
                              ag.state == CAWD_STATE_CANCELLED) ? FLUX_BADGE_ERR
                           : (ag.state == CAWD_STATE_WAITING)   ? FLUX_BADGE_WARN
                                                                : FLUX_BADGE_IDLE;
        flux_channel_badge(&bb, FLUX_CH_HITL, bs, badge_w);
        /* Strip trailing newline so we can hbox. */
        {
            char *s = (char *)flux_sb_str(&bb);
            size_t ln = strlen(s);
            if (ln > 0 && s[ln - 1] == '\n') s[ln - 1] = '\0';
        }

        char MB[512]; FluxSB mb; flux_sb_init(&mb, MB, (int)sizeof MB);
        if (model_w > 0) {
            flux_model_badge(&mb, ag.provider ? ag.provider : "—",
                             ag.model ? ag.model : "—",
                             4096, model_w);
            char *s = (char *)flux_sb_str(&mb);
            size_t ln = strlen(s);
            if (ln > 0 && s[ln - 1] == '\n') s[ln - 1] = '\0';
        }

        char CB[512]; FluxSB cb; flux_sb_init(&cb, CB, (int)sizeof cb);
        long used_tok = (long)ag.tokens_in + (long)ag.tokens_out;
        flux_context_window(&cb, used_tok, 200000, ctx_w);
        {
            char *s = (char *)flux_sb_str(&cb);
            size_t ln = strlen(s);
            if (ln > 0 && s[ln - 1] == '\n') s[ln - 1] = '\0';
        }

        if (model_w > 0) {
            const char *panels[3] = {
                flux_sb_str(&bb), flux_sb_str(&mb), flux_sb_str(&cb)
            };
            const int widths[3] = { badge_w, model_w, ctx_w };
            flux_hbox(sb, panels, widths, 3, " ");
        } else {
            const char *panels[2] = { flux_sb_str(&bb), flux_sb_str(&cb) };
            const int widths[2] = { badge_w, ctx_w };
            flux_hbox(sb, panels, widths, 2, " ");
        }
        budget--;
    }

    /* ---- Row 2: spacer. ---------------------------------------- */
    if (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }

    /* ---- Thinking shimmer (if streaming). ---------------------- */
    if (ag.state == CAWD_STATE_STREAMING && budget > 0) {
        cawd__hitl_thinking_row(sb, w);
        budget--;
    }

    /* ---- User prompt bubble. ----------------------------------- */
    /* CawdApp has no user-message log yet, so we surface the agent's
     * system prompt (the operator's intent) as the "user" bubble.   */
    if (budget > 0) {
        static char UBUF[4096]; FluxSB ubb; flux_sb_init(&ubb, UBUF, (int)sizeof UBUF);
        const char *text = ag.system_prompt ? ag.system_prompt
                         : (ag.name ? ag.name : "hello");
        char ts[16];
        uint64_t age = flux_now_ms() - ag.spawned_ms;
        if (age < 60000) snprintf(ts, sizeof ts, "%us", (unsigned)(age / 1000));
        else             snprintf(ts, sizeof ts, "%um", (unsigned)(age / 60000));
        flux_message_bubble(&ubb, FLUX_ROLE_USER, text, ts, w);
        int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&ubb), budget);
        budget -= emitted;
    }

    /* ---- Spacer between bubbles. ------------------------------- */
    if (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }

    /* ---- Assistant streaming bubble. --------------------------- */
    if (budget > 0) {
        /* Compose body = streamed text + optional blinking cursor. */
        static char BODY[CAWD_STREAM_BUF + 8];
        size_t sn = strlen(stream_copy);
        if (sn >= sizeof BODY - 4) sn = sizeof BODY - 4;
        memcpy(BODY, stream_copy, sn);
        BODY[sn] = '\0';
        if (ag.stream_open) {
            const char *g = cawd__hitl_cursor_glyph();
            size_t gl = strlen(g);
            if (sn + gl < sizeof BODY) {
                memcpy(BODY + sn, g, gl);
                BODY[sn + gl] = '\0';
            }
        } else if (sn == 0) {
            snprintf(BODY, sizeof BODY,
                     "(waiting for the assistant to begin\xe2\x80\xa6)");
        }

        static char ABUF[CAWD_STREAM_BUF + 4096];
        FluxSB abb; flux_sb_init(&abb, ABUF, (int)sizeof ABUF);
        char ts[16];
        snprintf(ts, sizeof ts, "%s",
                 ag.state == CAWD_STATE_STREAMING ? "live"
               : ag.state == CAWD_STATE_DONE      ? "done"
               : ag.state == CAWD_STATE_FAILED    ? "fail"
                                                  : "idle");
        flux_message_bubble(&abb, FLUX_ROLE_ASSISTANT, BODY, ts, w);
        int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&abb), budget);
        budget -= emitted;
    }

    /* ---- Op-tree with current tool as a single row. ------------- */
    if (budget > 1 && ag.current_tool) {
        static FluxOpItem items[1];
        static unsigned char expanded[1];
        static FluxOpTree tree;
        items[0].id       = "current";
        items[0].label    = ag.current_tool;
        items[0].detail   = NULL;
        items[0].status   = (ag.state == CAWD_STATE_DONE) ? FLUX_OP_COMPLETED
                          : (ag.state == CAWD_STATE_FAILED) ? FLUX_OP_FAILED
                          : FLUX_OP_RUNNING;
        items[0].depth    = 0;
        items[0].duration_ms = (int)(flux_now_ms() - ag.last_update_ms);
        items[0].has_children = 0;
        expanded[0] = 1;
        flux_op_tree_init(&tree, items, expanded, 1);
        /* Animate spinner via wall clock. */
        tree.spinner_frame = (int)(flux_now_ms() / 80);
        char TBUF[1024]; FluxSB tbb; flux_sb_init(&tbb, TBUF, (int)sizeof TBUF);
        flux_op_tree_render(&tree, &tbb, w);
        int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&tbb), budget);
        budget -= emitted;
    }

    /* ---- Tool-call card (only when there IS a current tool). ---- */
    if (budget > 0 && ag.current_tool) {
        char CBUF[2048]; FluxSB cbb; flux_sb_init(&cbb, CBUF, (int)sizeof CBUF);
        int elapsed_ms = (int)(flux_now_ms() - ag.last_update_ms);
        if (elapsed_ms < 0) elapsed_ms = 0;
        flux_command_block(&cbb, ag.current_tool, "", -1, elapsed_ms, w);
        int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&cbb), budget);
        budget -= emitted;
    }

    /* ---- Pad remaining rows. ----------------------------------- */
    if (budget > 0) cawd__hitl_pad(sb, w, budget);
}

/* ─── Telegram stage (§4.2) ───────────────────────────────────────
 * Telegram-styled remote chat: cyan channel badge header, transcript
 * of left/right aligned message bubbles, inline-keyboard approval
 * preview, footer hints. All state is function-local static so the
 * dispatch signature stays unchanged. Honors width contract — every
 * emitted row is exactly `w` display cells + '\n'. */

typedef struct {
    int   outbound;   /* 0 = inbound (@jane), 1 = outbound (bot)      */
    int   streaming;  /* 1 = render last bubble with shimmer/stream   */
    const char *who;  /* display name, e.g. "@jane" or "@openclaw_bot"*/
    const char *text;
    const char *ts;
} CawdTgMsg;

/* Render a Telegram-styled message bubble spanning up to bubble_w cells,
 * aligned left (inbound) or right (outbound). Emits up to 3 rows:
 * header "╭──── who · ts ────╮" (simplified to a single dimmed line),
 * body line, and a tail. Rows are padded to the full stage width `w`. */
static void cawd__tg_render_bubble(FluxSB *sb, int w, int bubble_w,
                                   const CawdTgMsg *m,
                                   int streaming_cursor_on)
{
    if (bubble_w < 16) bubble_w = 16;
    if (bubble_w > w)  bubble_w = w;

    /* header: who · ts — dimmed, with accent for the name */
    {
        char H[512]; FluxSB h;
        flux_sb_init(&h, H, sizeof H);
        flux_sb_append(&h, m->outbound
            ? FLUX_THEME_BRAND_PURPLE_FG : "\x1b[38;2;125;207;255m"); /* TG cyan */
        flux_sb_append(&h, FLUX_BOLD);
        flux_sb_append(&h, m->who ? m->who : "?");
        flux_sb_append(&h, FLUX_RESET);
        if (m->ts && *m->ts) {
            flux_sb_append(&h, " ");
            flux_sb_append(&h, FLUX_THEME_TEXT_DIM_FG);
            flux_sb_append(&h, "\xc2\xb7 "); /* · */
            flux_sb_append(&h, m->ts);
            flux_sb_append(&h, FLUX_RESET);
        }

        char L[768]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_fit(&l, flux_sb_str(&h), bubble_w, "\xe2\x80\xa6",
                 FLUX_ALIGN_LEFT);
        flux_fit(sb, flux_sb_str(&l), w, NULL,
                 m->outbound ? FLUX_ALIGN_RIGHT : FLUX_ALIGN_LEFT);
        flux_sb_append(sb, "\n");
    }

    /* body row(s): "╭─ text ─╮" style — single-line hard truncate for
     * stub simplicity. Inbound uses light rounded open, outbound uses
     * accented rounded open on the other side. */
    {
        const char *bubble_fg = m->outbound
            ? FLUX_THEME_BRAND_PURPLE_FG : "\x1b[38;2;125;207;255m";
        const char *text_fg   = FLUX_THEME_TEXT_FG;

        char B[2048]; FluxSB bb;
        flux_sb_init(&bb, B, sizeof B);
        /* left ornament for both sides; right ornament flips */
        flux_sb_append(&bb, bubble_fg);
        flux_sb_append(&bb, m->outbound ? "\xe2\x95\xad\xe2\x94\x80 "   /* ╭─ */
                                        : "\xe2\x95\xad\xe2\x94\x80 "); /* ╭─ */
        flux_sb_append(&bb, FLUX_RESET);
        flux_sb_append(&bb, text_fg);
        flux_sb_append(&bb, m->text ? m->text : "");
        if (streaming_cursor_on) flux_sb_append(&bb, "\xe2\x96\x8c"); /* ▌ */
        flux_sb_append(&bb, FLUX_RESET);
        flux_sb_append(&bb, " ");
        flux_sb_append(&bb, bubble_fg);
        flux_sb_append(&bb, "\xe2\x94\x80\xe2\x95\xae"); /* ─╮ */
        flux_sb_append(&bb, FLUX_RESET);

        char L[2560]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_fit(&l, flux_sb_str(&bb), bubble_w, "\xe2\x80\xa6",
                 FLUX_ALIGN_LEFT);
        flux_fit(sb, flux_sb_str(&l), w, NULL,
                 m->outbound ? FLUX_ALIGN_RIGHT : FLUX_ALIGN_LEFT);
        flux_sb_append(sb, "\n");
    }

    /* tail row: "╰──────╯" so bubble looks closed */
    {
        const char *bubble_fg = m->outbound
            ? FLUX_THEME_BRAND_PURPLE_FG : "\x1b[38;2;125;207;255m";
        char B[256]; FluxSB bb;
        flux_sb_init(&bb, B, sizeof B);
        flux_sb_append(&bb, bubble_fg);
        flux_sb_append(&bb, "\xe2\x95\xb0"); /* ╰ */
        int fill = bubble_w - 2;
        if (fill < 1) fill = 1;
        for (int i = 0; i < fill; i++)
            flux_sb_append(&bb, "\xe2\x94\x80"); /* ─ */
        flux_sb_append(&bb, "\xe2\x95\xaf"); /* ╯ */
        flux_sb_append(&bb, FLUX_RESET);

        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_fit(&l, flux_sb_str(&bb), bubble_w, NULL, FLUX_ALIGN_LEFT);
        flux_fit(sb, flux_sb_str(&l), w, NULL,
                 m->outbound ? FLUX_ALIGN_RIGHT : FLUX_ALIGN_LEFT);
        flux_sb_append(sb, "\n");
    }
}

/* Render the inline-keyboard approval preview (3 buttons in a row). */
static void cawd__tg_render_inline_kb(FluxSB *sb, int w) {
    /* Title row */
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l,
            " \xe2\x94\x8c\xe2\x94\x80 Approval "           /* ┌─ Approval  */
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 pending remote decision");
        flux_sb_append(&l, FLUX_RESET);
        flux_fit(sb, flux_sb_str(&l), w, "\xe2\x80\xa6", FLUX_ALIGN_LEFT);
        flux_sb_append(sb, "\n");
    }

    /* Buttons row via flux_button_render into scratch panels, then flux_hbox */
    FluxButton b_approve = {
        "\xe2\x9c\x93 Approve",                 /* ✓ Approve */
        FLUX_THEME_OK_FG, "\x1b[48;2;32;48;32m", FLUX_THEME_OK_FG
    };
    FluxButton b_always = {
        "\xe2\x9c\x93 Always",
        FLUX_THEME_TEXT_FG, "\x1b[48;2;48;48;64m", "\x1b[38;2;125;207;255m"
    };
    FluxButton b_reject = {
        "\xe2\x9c\x97 Reject",                  /* ✗ Reject */
        FLUX_THEME_ERR_FG, "\x1b[48;2;48;32;32m", FLUX_THEME_ERR_FG
    };

    char BA[256]; FluxSB sa; flux_sb_init(&sa, BA, sizeof BA);
    char BL[256]; FluxSB sl; flux_sb_init(&sl, BL, sizeof BL);
    char BR[256]; FluxSB sr; flux_sb_init(&sr, BR, sizeof BR);
    flux_button_render(&sa, &b_approve, 1);
    flux_button_render(&sl, &b_always,  0);
    flux_button_render(&sr, &b_reject,  0);

    int wa = flux_strwidth(b_approve.label) + 2;
    int wl = flux_strwidth(b_always.label)  + 2;
    int wr = flux_strwidth(b_reject.label)  + 2;

    /* Compose "│ [btn] [btn] [btn]" manually into a scratch SB so we
     * keep the vertical bar on both ends and let flux_fit pad the rest. */
    char ROW[2048]; FluxSB row;
    flux_sb_init(&row, ROW, sizeof ROW);
    flux_sb_append(&row, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&row, " \xe2\x94\x82 ");   /* " │ " */
    flux_sb_append(&row, FLUX_RESET);
    flux_sb_append(&row, flux_sb_str(&sa));
    flux_sb_append(&row, " ");
    flux_sb_append(&row, flux_sb_str(&sl));
    flux_sb_append(&row, " ");
    flux_sb_append(&row, flux_sb_str(&sr));
    flux_sb_append(&row, " ");
    flux_sb_append(&row, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&row, "\xe2\x94\x82");     /* │ */
    flux_sb_append(&row, FLUX_RESET);
    (void)wa; (void)wl; (void)wr;

    flux_fit(sb, flux_sb_str(&row), w, "\xe2\x80\xa6", FLUX_ALIGN_LEFT);
    flux_sb_append(sb, "\n");

    /* Bottom rule */
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l,
            " \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 tap [a] / [r] to simulate");
        flux_sb_append(&l, FLUX_RESET);
        flux_fit(sb, flux_sb_str(&l), w, "\xe2\x80\xa6", FLUX_ALIGN_LEFT);
        flux_sb_append(sb, "\n");
    }
}

/* ================================================================
 * Telegram observability tab — sessions + tunnels + transcripts.
 * Built on the public cawd_tg_* API in cawd_tui.h's public block.
 * ================================================================ */

#define CAWD__TG_MAX_SESSIONS  32
#define CAWD__TG_MAX_EVENTS    64

typedef enum {
    CAWD__TG_EV_USER_MSG = 0,
    CAWD__TG_EV_AGENT_MSG,
    CAWD__TG_EV_TOOL_CALL,
    CAWD__TG_EV_THINKING,
    CAWD__TG_EV_POLICY_GATE,
    CAWD__TG_EV_STATE,
} CawdTgEvKind;

typedef struct {
    uint64_t      ts_ms;
    CawdTgEvKind  kind;
    char          actor[32];
    char          body[256];
    char          tool[48];
    int           duration_ms;
    int           ok;
    int           tokens;
    double        cost_usd;
} CawdTgEvent;

typedef struct {
    int                 used;
    char                chat_id[40];
    char                handle[40];
    int                 is_group;
    char                model[40];
    int                 max_tokens;
    char                system_prompt_sha[16];
    int                 turns;
    int                 tokens_in;
    int                 tokens_out;
    double              cost_usd;
    uint64_t            started_ms;
    uint64_t            last_user_msg_ms;
    uint64_t            last_event_ms;
    CawdTgSessionState  state;
    char                current_tool[48];
    int                 pending_approval;
    CawdTgEvent         events[CAWD__TG_MAX_EVENTS];
    int                 n_events;
} CawdTgSession;

typedef struct {
    int                 kind;            /* = CAWD_UI_SLOT_TELEGRAM */
    CawdTgConnState     conn;
    char                bot_handle[64];
    int                 quota_per_sec;
    int                 long_poll_sec;
    int                 msgs_today;
    int                 sessions_today;
    double              cost_today;
    uint64_t            started_ms;

    CawdTgSession       sessions[CAWD__TG_MAX_SESSIONS];
    int                 cursor;
    int                 detail_open;       /* deep-dive expanded?      */
    int                 filter_active;
    char                filter_text[64];
    int                 filter_len;
    FluxCursorList      cl;
    pthread_mutex_t     mu;
    int                 mu_inited;
} CawdTgTabState;

static CawdTgTabState CAWD__TG_TAB;

/* ---- locking ---------------------------------------------------- */
static void cawd__tg_lock(CawdTgTabState *s) {
    if (!s->mu_inited) { pthread_mutex_init(&s->mu, NULL); s->mu_inited = 1; }
    pthread_mutex_lock(&s->mu);
}
static void cawd__tg_unlock(CawdTgTabState *s) { pthread_mutex_unlock(&s->mu); }

/* ---- session lookup --------------------------------------------- */
static CawdTgSession *cawd__tg_find(CawdTgTabState *s, const char *chat_id) {
    if (!chat_id) return NULL;
    for (int i = 0; i < CAWD__TG_MAX_SESSIONS; i++) {
        if (s->sessions[i].used && strcmp(s->sessions[i].chat_id, chat_id) == 0)
            return &s->sessions[i];
    }
    return NULL;
}
static CawdTgSession *cawd__tg_alloc_or_get(CawdTgTabState *s, const char *chat_id) {
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) return e;
    for (int i = 0; i < CAWD__TG_MAX_SESSIONS; i++) {
        if (!s->sessions[i].used) {
            memset(&s->sessions[i], 0, sizeof s->sessions[i]);
            s->sessions[i].used = 1;
            strncpy(s->sessions[i].chat_id, chat_id, sizeof s->sessions[i].chat_id - 1);
            s->sessions[i].started_ms = flux_now_ms();
            s->sessions[i].state = CAWD_TG_STATE_IDLE;
            return &s->sessions[i];
        }
    }
    return NULL;
}

/* ---- append event (ring buffer) --------------------------------- */
static void cawd__tg_push_event(CawdTgSession *e, const CawdTgEvent *ev) {
    if (!e) return;
    if (e->n_events < CAWD__TG_MAX_EVENTS) {
        e->events[e->n_events++] = *ev;
    } else {
        memmove(&e->events[0], &e->events[1],
                (size_t)((CAWD__TG_MAX_EVENTS - 1) * sizeof(CawdTgEvent)));
        e->events[CAWD__TG_MAX_EVENTS - 1] = *ev;
    }
    e->last_event_ms = ev->ts_ms;
}

/* ---- public API impl ------------------------------------------- */
void cawd_tg_set_conn(CawdApp *app, CawdTgConnState st, const char *bot_handle) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    s->conn = st;
    if (bot_handle) {
        strncpy(s->bot_handle, bot_handle, sizeof s->bot_handle - 1);
        s->bot_handle[sizeof s->bot_handle - 1] = 0;
    }
    if (s->started_ms == 0) s->started_ms = flux_now_ms();
    cawd__tg_unlock(s);
}

void cawd_tg_set_runtime(CawdApp *app, CawdTgRunCfg cfg) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    s->quota_per_sec = cfg.request_quota_per_sec > 0 ? cfg.request_quota_per_sec : 30;
    s->long_poll_sec = cfg.long_poll_interval_sec > 0 ? cfg.long_poll_interval_sec : 30;
    cawd__tg_unlock(s);
}

void cawd_tg_session_open(CawdApp *app, const char *chat_id, const char *handle,
                          int is_group, const char *model, int max_tokens,
                          const char *system_prompt) {
    (void)app;
    if (!chat_id) return;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    int existed = (cawd__tg_find(s, chat_id) != NULL);
    CawdTgSession *e = cawd__tg_alloc_or_get(s, chat_id);
    if (e) {
        if (handle) {
            strncpy(e->handle, handle, sizeof e->handle - 1);
            e->handle[sizeof e->handle - 1] = 0;
        }
        e->is_group = is_group ? 1 : 0;
        if (model) {
            strncpy(e->model, model, sizeof e->model - 1);
            e->model[sizeof e->model - 1] = 0;
        }
        if (max_tokens > 0) e->max_tokens = max_tokens;
        if (system_prompt) {
            unsigned long h = 5381;
            for (const char *p = system_prompt; *p; p++) h = h * 33 + (unsigned char)*p;
            snprintf(e->system_prompt_sha, sizeof e->system_prompt_sha, "%08lx", h & 0xffffffff);
        }
        if (!existed) s->sessions_today++;
    }
    cawd__tg_unlock(s);
}

void cawd_tg_user_msg(CawdApp *app, const char *chat_id, const char *body) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_alloc_or_get(s, chat_id);
    if (e) {
        e->turns++;
        e->last_user_msg_ms = flux_now_ms();
        e->state = CAWD_TG_STATE_RUNNING;
        CawdTgEvent ev = {0};
        ev.ts_ms = e->last_user_msg_ms;
        ev.kind  = CAWD__TG_EV_USER_MSG;
        const char *who = e->handle[0] ? e->handle : chat_id;
        size_t wl = strlen(who);
        if (wl >= sizeof ev.actor) wl = sizeof ev.actor - 1;
        memcpy(ev.actor, who, wl); ev.actor[wl] = 0;
        if (body) snprintf(ev.body, sizeof ev.body, "%s", body);
        cawd__tg_push_event(e, &ev);
        s->msgs_today++;
    }
    cawd__tg_unlock(s);
}

void cawd_tg_agent_msg(CawdApp *app, const char *chat_id, const char *body,
                       int tokens_in, int tokens_out, double cost_usd) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) {
        e->tokens_in  += tokens_in;
        e->tokens_out += tokens_out;
        e->cost_usd   += cost_usd;
        e->state       = CAWD_TG_STATE_DONE;
        CawdTgEvent ev = {0};
        ev.ts_ms = flux_now_ms();
        ev.kind  = CAWD__TG_EV_AGENT_MSG;
        snprintf(ev.actor, sizeof ev.actor, "agent");
        if (body) snprintf(ev.body, sizeof ev.body, "%s", body);
        ev.tokens   = tokens_out;
        ev.cost_usd = cost_usd;
        cawd__tg_push_event(e, &ev);
        s->msgs_today++;
        s->cost_today += cost_usd;
    }
    cawd__tg_unlock(s);
}

void cawd_tg_thinking(CawdApp *app, const char *chat_id, int tokens) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) {
        e->state = CAWD_TG_STATE_RUNNING;
        snprintf(e->current_tool, sizeof e->current_tool, "thinking");
        CawdTgEvent ev = {0};
        ev.ts_ms = flux_now_ms();
        ev.kind  = CAWD__TG_EV_THINKING;
        snprintf(ev.actor, sizeof ev.actor, "agent");
        snprintf(ev.body, sizeof ev.body, "thinking… %d tok", tokens);
        ev.tokens = tokens;
        cawd__tg_push_event(e, &ev);
    }
    cawd__tg_unlock(s);
}

void cawd_tg_tool_call(CawdApp *app, const char *chat_id, const char *tool,
                       const char *args_short, int duration_ms, int ok) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) {
        if (tool) snprintf(e->current_tool, sizeof e->current_tool, "%s", tool);
        CawdTgEvent ev = {0};
        ev.ts_ms = flux_now_ms();
        ev.kind  = CAWD__TG_EV_TOOL_CALL;
        snprintf(ev.actor, sizeof ev.actor, "agent");
        if (tool) snprintf(ev.tool, sizeof ev.tool, "%s", tool);
        if (args_short) snprintf(ev.body, sizeof ev.body, "%s", args_short);
        ev.duration_ms = duration_ms;
        ev.ok          = ok;
        cawd__tg_push_event(e, &ev);
    }
    cawd__tg_unlock(s);
}

void cawd_tg_policy_gate(CawdApp *app, const char *chat_id, const char *summary) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) {
        e->state = CAWD_TG_STATE_WAITING_APPROVAL;
        e->pending_approval = 1;
        CawdTgEvent ev = {0};
        ev.ts_ms = flux_now_ms();
        ev.kind  = CAWD__TG_EV_POLICY_GATE;
        snprintf(ev.actor, sizeof ev.actor, "policy");
        if (summary) snprintf(ev.body, sizeof ev.body, "%s", summary);
        cawd__tg_push_event(e, &ev);
    }
    cawd__tg_unlock(s);
}

void cawd_tg_state(CawdApp *app, const char *chat_id, CawdTgSessionState st) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) e->state = st;
    cawd__tg_unlock(s);
}

void cawd_tg_terminate(CawdApp *app, const char *chat_id) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e) { e->state = CAWD_TG_STATE_DONE; e->used = 0; }
    cawd__tg_unlock(s);
}

void cawd_tg_set_session_model(CawdApp *app, const char *chat_id, const char *model) {
    (void)app;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);
    CawdTgSession *e = cawd__tg_find(s, chat_id);
    if (e && model) {
        strncpy(e->model, model, sizeof e->model - 1);
        e->model[sizeof e->model - 1] = 0;
    }
    cawd__tg_unlock(s);
}

/* ---- demo seed (so the tab has data even before real bot wired) - */
static void cawd__tg_demo_seed(CawdApp *app) {
    cawd_tg_set_conn(app, CAWD_TG_CONN_CONNECTED, "@openclaw_bot");
    cawd_tg_set_runtime(app, (CawdTgRunCfg){
        .request_quota_per_sec = 30, .long_poll_interval_sec = 30});
    cawd_tg_session_open(app, "1001", "@jane", 0,
                         "claude-opus-4-7", 200000, "you are a helpful coding agent");
    cawd_tg_user_msg(app, "1001", "review PR #1248");
    cawd_tg_tool_call(app, "1001", "github.read_pr",  "#1248",        180, 1);
    cawd_tg_tool_call(app, "1001", "github.read_diff","#1248",        240, 1);
    cawd_tg_thinking(app, "1001", 412);
    cawd_tg_agent_msg(app, "1001", "I see 3 issues. Drafting comments…",  412, 1102, 0.0143);
    cawd_tg_tool_call(app, "1001", "github.post_review_comment","#1248",  320, 1);
    cawd_tg_agent_msg(app, "1001", "Posted review with 3 suggestions.",   124, 412,  0.0050);
    cawd_tg_user_msg(app, "1001", "approve");
    cawd_tg_policy_gate(app, "1001", "github.merge_pr requires HITL approval");

    cawd_tg_session_open(app, "1002", "@carlos", 0,
                         "claude-haiku-4-5", 100000, "be terse");
    cawd_tg_user_msg(app, "1002", "stats?");
    cawd_tg_thinking(app, "1002", 84);
    cawd_tg_agent_msg(app, "1002", "today: 47 tasks · $1.42 · 99.2% ok", 22, 84, 0.0008);
    cawd_tg_user_msg(app, "1002", "kill agent-docs");
    cawd_tg_policy_gate(app, "1002", "agent.kill needs role:admin");

    cawd_tg_session_open(app, "1003", "#eng", 1,
                         "claude-opus-4-7", 200000, "team channel triage");
    cawd_tg_user_msg(app, "1003", "/claw daily");
    cawd_tg_tool_call(app, "1003", "claw.daily_summary", "", 90, 1);
    cawd_tg_thinking(app, "1003", 220);

    cawd_tg_session_open(app, "1004", "@mika", 0,
                         "claude-sonnet-4-6", 150000, "concise UX critique");
    cawd_tg_user_msg(app, "1004", "review the new dashboard");
    cawd_tg_agent_msg(app, "1004", "Looks good — cursor follow, clear footer hints.", 88, 220, 0.0035);
    cawd_tg_state(app, "1004", CAWD_TG_STATE_IDLE);

    cawd_tg_session_open(app, "1005", "@dev", 0,
                         "claude-haiku-4-5", 100000, NULL);
    cawd_tg_user_msg(app, "1005", "ping");
    cawd_tg_agent_msg(app, "1005", "pong", 4, 4, 0.00006);
}

static void cawd__tg_tab_init(CawdApp *app) {
    CawdTgTabState *s = &CAWD__TG_TAB;
    if (s->kind == CAWD_UI_SLOT_TELEGRAM) return;
    memset(s, 0, sizeof *s);
    s->kind          = CAWD_UI_SLOT_TELEGRAM;
    s->conn          = CAWD_TG_CONN_DISCONNECTED;
    s->quota_per_sec = 30;
    s->long_poll_sec = 30;
    flux_cursor_list_init(&s->cl);
    cawd__tg_demo_seed(app);
    app->ui.tabs_state[CAWD_UI_SLOT_TELEGRAM] = (struct CawdTabState *)s;
}

static void cawd__tg_tab_tick(CawdApp *app, uint64_t now_ms) {
    (void)app; (void)now_ms;
}

/* Live count of used sessions */
static int cawd__tg_count(CawdTgTabState *s) {
    int n = 0;
    for (int i = 0; i < CAWD__TG_MAX_SESSIONS; i++) if (s->sessions[i].used) n++;
    return n;
}
/* Compact filtered index list (visible sessions). */
static int cawd__tg_visible(CawdTgTabState *s, int *idxs, int max) {
    int n = 0;
    for (int i = 0; i < CAWD__TG_MAX_SESSIONS && n < max; i++) {
        CawdTgSession *e = &s->sessions[i];
        if (!e->used) continue;
        if (s->filter_len > 0) {
            int hit = 0;
            if (e->handle[0]      && strstr(e->handle,    s->filter_text)) hit = 1;
            if (e->model[0]       && strstr(e->model,     s->filter_text)) hit = 1;
            if (!hit) continue;
        }
        idxs[n++] = i;
    }
    return n;
}

static int cawd__tg_tab_update(CawdApp *app, FluxMsg msg) {
    if (app->ui.tabs.active != CAWD_UI_SLOT_TELEGRAM) return 0;
    if (msg.type != MSG_KEY) return 0;
    CawdTgTabState *s = &CAWD__TG_TAB;
    cawd__tg_lock(s);

    int idxs[CAWD__TG_MAX_SESSIONS];
    int nv = cawd__tg_visible(s, idxs, CAWD__TG_MAX_SESSIONS);

    int handled = 0;
    if (s->filter_active) {
        if (flux_key_is(msg, "esc"))   { s->filter_active = 0; s->filter_len = 0; s->filter_text[0]=0; handled = 1; }
        else if (flux_key_is(msg, "enter")) { s->filter_active = 0; handled = 1; }
        else if (flux_key_is(msg, "backspace")) {
            if (s->filter_len > 0) s->filter_text[--s->filter_len] = 0;
            handled = 1;
        } else if (msg.u.key.rune >= 0x20 && msg.u.key.rune < 0x7f &&
                   s->filter_len < (int)sizeof s->filter_text - 1) {
            s->filter_text[s->filter_len++] = (char)msg.u.key.rune;
            s->filter_text[s->filter_len] = 0;
            handled = 1;
        } else handled = 1;
        cawd__tg_unlock(s);
        return handled;
    }
    if (flux_key_is(msg, "down") || flux_key_is(msg, "j")) {
        if (nv > 0) s->cursor = (s->cursor + 1) % nv;
        handled = 1;
    } else if (flux_key_is(msg, "up") || flux_key_is(msg, "k")) {
        if (nv > 0) s->cursor = (s->cursor - 1 + nv) % nv;
        handled = 1;
    } else if (flux_key_is(msg, "enter")) {
        s->detail_open = !s->detail_open;
        handled = 1;
    } else if (flux_key_is(msg, "/")) {
        s->filter_active = 1;
        handled = 1;
    } else if (flux_key_is(msg, "x")) {
        if (nv > 0 && s->cursor < nv) {
            s->sessions[idxs[s->cursor]].used = 0;
            cawd_toast(app, CAWD_KIND_WARN, "Terminated", "session ended");
        }
        handled = 1;
    } else if (flux_key_is(msg, "a")) {
        if (nv > 0 && s->cursor < nv) {
            CawdTgSession *e = &s->sessions[idxs[s->cursor]];
            if (e->pending_approval) {
                e->pending_approval = 0;
                e->state = CAWD_TG_STATE_RUNNING;
                cawd_toast(app, CAWD_KIND_SUCCESS, "Approved", e->handle);
            }
        }
        handled = 1;
    } else if (flux_key_is(msg, "r")) {
        if (nv > 0 && s->cursor < nv) {
            CawdTgSession *e = &s->sessions[idxs[s->cursor]];
            if (e->pending_approval) {
                e->pending_approval = 0;
                e->state = CAWD_TG_STATE_DONE;
                cawd_toast(app, CAWD_KIND_WARN, "Rejected", e->handle);
            }
        }
        handled = 1;
    } else if (flux_key_is(msg, "m")) {
        if (nv > 0 && s->cursor < nv) {
            CawdTgSession *e = &s->sessions[idxs[s->cursor]];
            const char *next =
                strcmp(e->model, "claude-opus-4-7")  == 0 ? "claude-sonnet-4-6" :
                strcmp(e->model, "claude-sonnet-4-6")== 0 ? "claude-haiku-4-5"  :
                "claude-opus-4-7";
            strncpy(e->model, next, sizeof e->model - 1);
            cawd_toast(app, CAWD_KIND_INFO, "Model switched", e->model);
        }
        handled = 1;
    }
    cawd__tg_unlock(s);
    return handled;
}

/* ---- render helpers ----- */
static const char *cawd__tg_state_glyph(CawdTgSessionState st, int *fg) {
    switch (st) {
    case CAWD_TG_STATE_RUNNING:          *fg = 0; return "\xe2\x97\x8f"; /* ● */
    case CAWD_TG_STATE_WAITING_APPROVAL: *fg = 1; return "\xe2\x97\x90"; /* ◐ */
    case CAWD_TG_STATE_IDLE:             *fg = 2; return "\xe2\x97\x8b"; /* ○ */
    case CAWD_TG_STATE_DONE:             *fg = 3; return "\xe2\x9c\x93"; /* ✓ */
    case CAWD_TG_STATE_FAILED:           *fg = 4; return "\xe2\x9c\x97"; /* ✗ */
    }
    *fg = 0; return "\xe2\x97\x8b";
}
static const char *cawd__tg_state_color(int code) {
    switch (code) {
    case 0: return FLUX_THEME_OK_FG;
    case 1: return FLUX_THEME_WARN_FG;
    case 2: return FLUX_THEME_TEXT_DIM_FG;
    case 3: return FLUX_THEME_BRAND_PURPLE_FG;
    case 4: return FLUX_THEME_ERR_FG;
    }
    return FLUX_THEME_TEXT_FG;
}

static void cawd__tg_render_header(CawdTgTabState *s, FluxSB *sb, int w) {
    char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    /* tunnel state */
    int conn_fg = (s->conn == CAWD_TG_CONN_CONNECTED) ? 0 :
                  (s->conn == CAWD_TG_CONN_RECONNECTING) ? 1 : 4;
    flux_sb_append(&l, cawd__tg_state_color(conn_fg));
    flux_sb_append(&l, "\xe2\x97\x8f ");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, FLUX_BOLD); flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, s->bot_handle[0] ? s->bot_handle : "@bot");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    const char *cstr = (s->conn == CAWD_TG_CONN_CONNECTED) ? "connected" :
                       (s->conn == CAWD_TG_CONN_RECONNECTING) ? "reconnecting" : "disconnected";
    flux_sb_append(&l, cstr);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    if (w >= 80) {
        char tmp[128];
        snprintf(tmp, sizeof tmp, "long-poll %ds  rate %d/s  %d msgs  %d sessions  $%.2f",
                 s->long_poll_sec, s->quota_per_sec, s->msgs_today,
                 s->sessions_today, s->cost_today);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, tmp);
        flux_sb_append(&l, FLUX_RESET);
    }
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__tg_render_filter(CawdTgTabState *s, FluxSB *sb, int w) {
    char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, "filter: /");
    flux_sb_append(&l, s->filter_text);
    flux_sb_append(&l, "\xe2\x96\x88");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__tg_render_session_row(FluxSB *sb, int w,
                                        const CawdTgSession *e, int selected) {
    char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, selected ? FLUX_THEME_ACCENT_FG : FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, selected ? " \xe2\x96\xb6 " : "   ");
    flux_sb_append(&l, FLUX_RESET);
    int fg_code = 0;
    const char *gl = cawd__tg_state_glyph(e->state, &fg_code);
    flux_sb_append(&l, cawd__tg_state_color(fg_code));
    flux_sb_append(&l, gl);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    char handle_pad[20];
    snprintf(handle_pad, sizeof handle_pad, "%-16s", e->handle[0] ? e->handle : e->chat_id);
    flux_sb_append(&l, handle_pad);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, e->is_group ? " group  " : " DM     ");
    flux_sb_append(&l, FLUX_RESET);
    char tail[128];
    snprintf(tail, sizeof tail, "%-22s %3d turns  $%6.4f",
             e->model[0] ? e->model : "—", e->turns, e->cost_usd);
    flux_sb_append(&l, FLUX_THEME_TEXT2_FG);
    flux_sb_append(&l, tail);
    flux_sb_append(&l, FLUX_RESET);
    if (w >= 100) {
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        if (e->state == CAWD_TG_STATE_WAITING_APPROVAL) flux_sb_append(&l, "awaiting approval");
        else if (e->current_tool[0]) {
            flux_sb_append(&l, "tool: ");
            flux_sb_append(&l, e->current_tool);
        } else if (e->state == CAWD_TG_STATE_RUNNING) flux_sb_append(&l, "running");
        else if (e->state == CAWD_TG_STATE_IDLE)    flux_sb_append(&l, "idle");
        else if (e->state == CAWD_TG_STATE_DONE)    flux_sb_append(&l, "done");
        flux_sb_append(&l, FLUX_RESET);
    }
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__tg_render_event(FluxSB *sb, int w, const CawdTgEvent *ev) {
    char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    char ts[16];
    uint64_t s = (ev->ts_ms / 1000) % 86400;
    snprintf(ts, sizeof ts, "%02llu:%02llu:%02llu",
             (unsigned long long)((s/3600)%24),
             (unsigned long long)((s/60)%60),
             (unsigned long long)(s%60));
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, ts);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    const char *arrow = " ";
    const char *fg    = FLUX_THEME_TEXT_FG;
    switch (ev->kind) {
    case CAWD__TG_EV_USER_MSG:    arrow = "\xe2\x86\x92"; fg = FLUX_THEME_ACCENT_FG; break;
    case CAWD__TG_EV_AGENT_MSG:   arrow = "\xe2\x86\x90"; fg = FLUX_THEME_BRAND_PURPLE_FG; break;
    case CAWD__TG_EV_TOOL_CALL:   arrow = "\xe2\x80\xa2"; fg = ev->ok ? FLUX_THEME_OK_FG : FLUX_THEME_ERR_FG; break;
    case CAWD__TG_EV_THINKING:    arrow = "\xe2\x99\xb2"; fg = FLUX_THEME_TEXT_DIM_FG; break;
    case CAWD__TG_EV_POLICY_GATE: arrow = "\xe2\x97\x90"; fg = FLUX_THEME_WARN_FG; break;
    case CAWD__TG_EV_STATE:       arrow = "\xe2\x97\x8b"; fg = FLUX_THEME_TEXT_DIM_FG; break;
    }
    flux_sb_append(&l, fg);  flux_sb_append(&l, arrow); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    char actor_pad[16];
    snprintf(actor_pad, sizeof actor_pad, "%-12s", ev->actor[0] ? ev->actor : "—");
    flux_sb_append(&l, actor_pad);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    if (ev->kind == CAWD__TG_EV_TOOL_CALL) {
        char tc[256];
        snprintf(tc, sizeof tc, "tool: %s(%s)  %dms  %s",
                 ev->tool, ev->body, ev->duration_ms, ev->ok ? "\xe2\x9c\x93" : "\xe2\x9c\x97");
        flux_sb_append(&l, fg);
        flux_sb_append(&l, tc);
        flux_sb_append(&l, FLUX_RESET);
    } else {
        flux_sb_append(&l, fg);
        flux_sb_append(&l, ev->body);
        flux_sb_append(&l, FLUX_RESET);
    }
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__tg_render_detail(CawdTgTabState *s, FluxSB *sb, int w,
                                    const CawdTgSession *e) {
    (void)s;
    /* divider + header */
    {
        char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_BOLD);
        flux_sb_append(&l, FLUX_THEME_BRAND_PURPLE_FG);
        flux_sb_append(&l, e->handle[0] ? e->handle : e->chat_id);
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        char tmp[256];
        snprintf(tmp, sizeof tmp,
                 "%s  %d turns  $%.4f  in %d / out %d  context %s",
                 e->model[0] ? e->model : "—", e->turns, e->cost_usd,
                 e->tokens_in, e->tokens_out,
                 e->system_prompt_sha[0] ? e->system_prompt_sha : "—");
        flux_sb_append(&l, tmp);
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    cawd__ui_row_blank(sb, w);
    int budget = e->n_events;
    if (budget > 30) budget = 30;
    int start = e->n_events - budget;
    if (start < 0) start = 0;
    for (int i = start; i < e->n_events; i++) {
        cawd__tg_render_event(sb, w, &e->events[i]);
    }
}

static void cawd__tg_render_footer(CawdTgTabState *s, FluxSB *sb, int w) {
    char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    if (w >= 80) {
        flux_sb_append(&l, "\xe2\x86\x91\xe2\x86\x93 nav  Enter ");
        flux_sb_append(&l, s->detail_open ? "collapse" : "expand");
        flux_sb_append(&l, "  m model  a approve  r reject  x stop  / filter");
    } else if (w >= 50) {
        flux_sb_append(&l, "\xe2\x86\x91\xe2\x86\x93  Enter  m  a/r  x  /");
    } else {
        flux_sb_append(&l, "\xe2\x86\x91\xe2\x86\x93 a/r");
    }
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__tg_tab_render(CawdApp *app, FluxSB *sb, int w, int h) {
    (void)h;
    CawdTgTabState *s = &CAWD__TG_TAB;
    if (s->kind != CAWD_UI_SLOT_TELEGRAM) cawd__tg_tab_init(app);
    if (!sb || w <= 0) return;

    cawd__tg_lock(s);
    cawd__tg_render_header(s, sb, w);
    if (s->filter_active) cawd__tg_render_filter(s, sb, w);
    cawd__ui_row_blank(sb, w);

    int idxs[CAWD__TG_MAX_SESSIONS];
    int nv = cawd__tg_visible(s, idxs, CAWD__TG_MAX_SESSIONS);
    if (s->cursor >= nv) s->cursor = nv > 0 ? nv - 1 : 0;

    if (nv == 0) {
        char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "no sessions \xe2\x80\x94 waiting for first user message");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
        cawd__ui_row_blank(sb, w);
        cawd__tg_render_footer(s, sb, w);
        cawd__tg_unlock(s);
        return;
    }

    /* Sessions list */
    flux_cursor_list_begin(&s->cl, &app->ui.scroll[CAWD_UI_SLOT_TELEGRAM],
                           s->cursor, nv, h, /*margin*/2);
    s->cl.running_row = 2 + (s->filter_active ? 1 : 0);
    for (int k = 0; k < nv; k++) {
        int row_off;
        flux_cursor_list_item(&s->cl, k, 1, &row_off);
        cawd__tg_render_session_row(sb, w, &s->sessions[idxs[k]], k == s->cursor);
    }
    flux_cursor_list_end(&s->cl);

    if (s->detail_open && nv > 0) {
        cawd__ui_row_blank(sb, w);
        {
            char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
            flux_sb_append(&l, "  ");
            flux_sb_append(&l, FLUX_THEME_DIVIDER_FG);
            for (int i = 0; i < w - 4; i++) flux_sb_append(&l, "\xe2\x94\x80");
            flux_sb_append(&l, FLUX_RESET);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l));
        }
        cawd__tg_render_detail(s, sb, w, &s->sessions[idxs[s->cursor]]);
    }

    cawd__ui_row_blank(sb, w);
    cawd__tg_render_footer(s, sb, w);

    cawd__tg_unlock(s);
}

/* ---- GitHub channel (slot 3) ------------------------------------
 *
 * PR / issue event board. Renders a header row (repo + live counts),
 * a list of 5 static PR / issue cards (author, status, agent action,
 * optional inline review diff for in-review PRs), and a footer hint row.
 *
 * Everything is width-honest: every emitted row is exactly `w` cells +
 * one '\n' byte. Scrolling is handled by the outer UI `FluxScroll`, so
 * we render all cards sequentially.
 *
 * Namespace: cawd__gh_*. All state is function-local static (the card
 * roster never changes at runtime in this wave).
 * ---------------------------------------------------------------- */

/* Forward decls — these live further down in this file. */
static void cawd__ui_row_blank(FluxSB *sb, int w);
static void cawd__ui_row_fit  (FluxSB *sb, int w, const char *s);

typedef enum {
    CAWD__GH_AUTO_MERGED = 0,  /* PR auto-merged by agent        */
    CAWD__GH_REVIEWING,        /* PR currently under agent review */
    CAWD__GH_ASSIGNED,         /* Issue assigned to an agent      */
    CAWD__GH_APPROVED,         /* PR approved                     */
    CAWD__GH_BLOCKED           /* PR blocked / requires HITL      */
} CawdGhStatus;

typedef enum {
    CAWD__GH_KIND_PR = 0,
    CAWD__GH_KIND_ISSUE
} CawdGhKind;

typedef struct {
    CawdGhKind      kind;
    int             number;
    const char     *title;
    const char     *author;
    const char     *ci;          /* e.g. "✓", "⋯", "—", or NULL */
    CawdGhStatus    status;
    const char     *action;      /* agent-action summary line   */
    /* Optional inline review diff (only shown for REVIEWING).  */
    const FluxDiffLine *diff;
    int             diff_n;
    const char     *diff_file;
} CawdGhCard;

static const char *cawd__gh_status_label(CawdGhStatus s) {
    switch (s) {
        case CAWD__GH_AUTO_MERGED: return " AUTO-MERGED ";
        case CAWD__GH_REVIEWING:   return " REVIEWING ";
        case CAWD__GH_ASSIGNED:    return " ASSIGNED ";
        case CAWD__GH_APPROVED:    return " APPROVED ";
        case CAWD__GH_BLOCKED:     return " BLOCKED ";
    }
    return " ? ";
}

static const char *cawd__gh_status_fg(CawdGhStatus s) {
    switch (s) {
        case CAWD__GH_AUTO_MERGED: return FLUX_THEME_OK_FG;
        case CAWD__GH_REVIEWING:   return FLUX_THEME_WARN_FG;
        case CAWD__GH_ASSIGNED:    return "\x1b[38;2;125;207;255m"; /* cyan */
        case CAWD__GH_APPROVED:    return FLUX_THEME_BRAND_PURPLE_FG;
        case CAWD__GH_BLOCKED:     return FLUX_THEME_ERR_FG;
    }
    return FLUX_THEME_TEXT_DIM_FG;
}

/* Emit one card border row (top or bottom) fit to `w` cells. */
static void cawd__gh_border_row(FluxSB *sb, int w, int inner_w,
                                int is_top, const char *accent_fg) {
    const FluxBorder *B = &FLUX_BORDER_ROUNDED;
    char L[1024]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, accent_fg);
    flux_sb_append(&l, is_top ? B->tl : B->bl);
    for (int i = 0; i < inner_w + 2; i++) flux_sb_append(&l, B->h);
    flux_sb_append(&l, is_top ? B->tr : B->br);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Wrap a pre-formatted content line (already colour-marked, inner_w cells
 * wide from flux_fit) between " │ <content> │ " and fit the whole thing
 * to `w` cells. */
static void cawd__gh_content_row(FluxSB *sb, int w, int inner_w,
                                 const char *accent_fg,
                                 const char *content_line) {
    (void)inner_w;
    const FluxBorder *B = &FLUX_BORDER_ROUNDED;
    char L[4096]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, accent_fg);
    flux_sb_append(&l, B->v);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    /* content_line is already exactly inner_w cells (from flux_fit). */
    if (content_line) flux_sb_append(&l, content_line);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, accent_fg);
    flux_sb_append(&l, B->v);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Helper: build one content line of exactly `inner_w` display cells into
 * a caller-provided char buffer. Takes a source SB (arbitrary ANSI) and
 * passes it through flux_fit. */
static void cawd__gh_fit_into(char *buf, int bufsz,
                              const char *src, int inner_w) {
    FluxSB tmp; flux_sb_init(&tmp, buf, bufsz);
    flux_fit(&tmp, src ? src : "", inner_w, NULL, FLUX_ALIGN_LEFT);
}

/* Render line 1 of a card: "PR #1248  title" (bold). */
static void cawd__gh_line_head(FluxSB *sb, int w, int inner_w,
                               const char *accent_fg,
                               const CawdGhCard *c) {
    char src[1024]; FluxSB s; flux_sb_init(&s, src, sizeof src);
    flux_sb_append(&s, FLUX_BOLD);
    flux_sb_append(&s, accent_fg);
    char head[32];
    snprintf(head, sizeof head, "%s #%d  ",
             c->kind == CAWD__GH_KIND_PR ? "PR" : "Issue", c->number);
    flux_sb_append(&s, head);
    flux_sb_append(&s, FLUX_RESET);
    flux_sb_append(&s, FLUX_THEME_TEXT_FG);
    flux_sb_append(&s, c->title ? c->title : "");
    flux_sb_append(&s, FLUX_RESET);
    char fit[1536];
    cawd__gh_fit_into(fit, sizeof fit, flux_sb_str(&s), inner_w);
    cawd__gh_content_row(sb, w, inner_w, accent_fg, fit);
}

/* Render line 2: "@author   [GH]   [STATUS]". GH pill and status pill
 * are drawn inline (not via flux_channel_badge which appends '\n'). */
static void cawd__gh_line_meta(FluxSB *sb, int w, int inner_w,
                               const char *accent_fg,
                               const CawdGhCard *c) {
    char src[1024]; FluxSB s; flux_sb_init(&s, src, sizeof src);
    flux_sb_append(&s, FLUX_THEME_TEXT2_FG);
    flux_sb_append(&s, c->author ? c->author : "?");
    flux_sb_append(&s, FLUX_RESET);
    flux_sb_append(&s, "  ");
    /* Inline GH channel chip: orange bold " GH ". */
    flux_sb_append(&s, FLUX_BOLD);
    flux_sb_append(&s, accent_fg);
    flux_sb_append(&s, " GH ");
    flux_sb_append(&s, FLUX_RESET);
    flux_sb_append(&s, "  ");
    /* CI indicator. */
    if (c->ci && *c->ci) {
        flux_sb_append(&s, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&s, "CI ");
        const char *ci_fg = FLUX_THEME_TEXT_DIM_FG;
        if (strcmp(c->ci, "✓") == 0) ci_fg = FLUX_THEME_OK_FG;
        else if (strcmp(c->ci, "⋯") == 0) ci_fg = FLUX_THEME_WARN_FG;
        flux_sb_append(&s, ci_fg);
        flux_sb_append(&s, c->ci);
        flux_sb_append(&s, FLUX_RESET);
        flux_sb_append(&s, "  ");
    }
    /* Status pill. */
    flux_sb_append(&s, FLUX_BOLD);
    flux_sb_append(&s, cawd__gh_status_fg(c->status));
    flux_sb_append(&s, cawd__gh_status_label(c->status));
    flux_sb_append(&s, FLUX_RESET);
    char fit[1536];
    cawd__gh_fit_into(fit, sizeof fit, flux_sb_str(&s), inner_w);
    cawd__gh_content_row(sb, w, inner_w, accent_fg, fit);
}

/* Render line 3: agent action summary (dim). */
static void cawd__gh_line_action(FluxSB *sb, int w, int inner_w,
                                 const char *accent_fg,
                                 const CawdGhCard *c) {
    char src[1024]; FluxSB s; flux_sb_init(&s, src, sizeof src);
    flux_sb_append(&s, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&s, "↳ ");
    flux_sb_append(&s, c->action ? c->action : "");
    flux_sb_append(&s, FLUX_RESET);
    char fit[1536];
    cawd__gh_fit_into(fit, sizeof fit, flux_sb_str(&s), inner_w);
    cawd__gh_content_row(sb, w, inner_w, accent_fg, fit);
}

/* Render inline diff preview. Uses flux_diff_block_render into a temp
 * buffer at width inner_w, then re-wraps each resulting row into
 * "│ <row> │" and fits to `w`. */
static void cawd__gh_diff_preview(FluxSB *sb, int w, int inner_w,
                                  const char *accent_fg,
                                  const CawdGhCard *c) {
    if (!c->diff || c->diff_n <= 0) return;
    static char DB[8192];
    FluxSB diff; flux_sb_init(&diff, DB, sizeof DB);
    flux_diff_block_render(&diff, c->diff_file, c->diff, c->diff_n, inner_w);
    /* Walk over the emitted rows; each is exactly inner_w cells + '\n'. */
    const char *p = flux_sb_str(&diff);
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        char row[2048];
        int cp = len < (int)sizeof row - 1 ? len : (int)sizeof row - 1;
        memcpy(row, p, (size_t)cp);
        row[cp] = '\0';
        cawd__gh_content_row(sb, w, inner_w, accent_fg, row);
        if (!nl) break;
        p = nl + 1;
    }
}

/* Render a single card: top border + 3-4 content rows + bottom border. */
static void cawd__gh_card_render(FluxSB *sb, int w,
                                 const char *accent_fg,
                                 const CawdGhCard *c) {
    /* Card layout per row:
     *   " " + "│" + " " + <inner_w cells> + " " + "│"
     *   1  + 1  + 1  + inner_w         + 1  + 1   = inner_w + 5 cells
     * so inner_w = w - 5 to fit total width w exactly. */
    int inner_w = w - 5;
    if (inner_w < 8) inner_w = 8;
    cawd__gh_border_row(sb, w, inner_w, 1, accent_fg);
    cawd__gh_line_head  (sb, w, inner_w, accent_fg, c);
    cawd__gh_line_meta  (sb, w, inner_w, accent_fg, c);
    cawd__gh_line_action(sb, w, inner_w, accent_fg, c);
    if (c->status == CAWD__GH_REVIEWING && c->diff && c->diff_n > 0) {
        cawd__gh_diff_preview(sb, w, inner_w, accent_fg, c);
    }
    cawd__gh_border_row(sb, w, inner_w, 0, accent_fg);
}

/* Top header row: channel badge chip + repo + counts. */
static void cawd__gh_render_header(FluxSB *sb, int w,
                                   const char *accent_fg) {
    char L[1024]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    /* Channel badge chip — inline so no newline. */
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, accent_fg);
    flux_sb_append(&l, " GH ");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, "olealgoritme/flux.h");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "· 3 PRs open · 7 issues · auto-review=");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, FLUX_THEME_OK_FG);
    flux_sb_append(&l, "ON");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Footer hints row. */
static void cawd__gh_render_footer(FluxSB *sb, int w) {
    char L[1024]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "Enter"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "open PR"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "m"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "merge"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "r"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "request changes"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "/"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "filter"); flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* ================================================================
 * GitHub tab — stateful skeleton.
 * ================================================================ */

typedef enum {
    CAWD__GH_VIEW_ALL = 0,
    CAWD__GH_VIEW_PRS,
    CAWD__GH_VIEW_ISSUES,
} CawdGhView;

#define CAWD__GH_MAX_CARDS 64
#define CAWD__GH_FILTER_CAP 64

typedef struct CawdGhTabState {
    int          kind;                 /* = CAWD_UI_SLOT_GITHUB         */
    CawdGhCard   cards[CAWD__GH_MAX_CARDS];
    int          n_cards;
    CawdGhView   view_mode;
    int          auto_review;          /* 1 on, 0 off                   */
    int          cursor;               /* index into filtered list      */
    int          detail_open;          /* 0/1                           */
    int          filter_active;        /* prompt showing?               */
    char         filter_text[CAWD__GH_FILTER_CAP];
    int          filter_len;
    int            spinner_frame;
    FluxCursorList cl;                  /* cursor + auto-scroll */
} CawdGhTabState;

static CawdGhTabState CAWD__GH_TAB;

/* Demo-data seam — replace with real GitHub API calls. Returns a pointer
 * to the seed array + count. */
static void cawd__gh_demo_cards(CawdGhCard **out, int *out_n) {
    static const FluxDiffLine D0[] = {
        { FLUX_DIFF_CONTEXT, "def verify_token(tok):" },
        { FLUX_DIFF_REMOVED, "    return _cache[tok]" },
        { FLUX_DIFF_ADDED,   "    return _cache.get(tok) or _load(tok)" },
    };
    static const FluxDiffLine D1[] = {
        { FLUX_DIFF_CONTEXT, "@app.get(\"/health\")" },
        { FLUX_DIFF_ADDED,   "async def health():" },
        { FLUX_DIFF_ADDED,   "    return {\"ok\": True}" },
    };
    static const FluxDiffLine D2[] = {
        { FLUX_DIFF_CONTEXT, "try:" },
        { FLUX_DIFF_REMOVED, "    return json.load(f)" },
        { FLUX_DIFF_ADDED,   "    return json.load(f) if f.read() else {}" },
    };
    static CawdGhCard arr[] = {
        { CAWD__GH_KIND_PR,    1248, "fix: serialize setUp in auth tests",      "@agent-tester",   "\xe2\x9c\x93",  CAWD__GH_AUTO_MERGED, "reviewed 3 files \xc2\xb7 +24/-11 \xc2\xb7 auto-merged after CI green",       NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1245, "refactor: extract auth token handling",   "@agent-refactor", "\xe2\x8b\xaf",  CAWD__GH_REVIEWING,   "reviewed 7 files \xc2\xb7 added 2 comments \xc2\xb7 awaiting human sign-off", D0,     3, "src/auth/token.py" },
        { CAWD__GH_KIND_ISSUE, 1244, "fix flaky auth test",                     "@jane",           NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-tester \xc2\xb7 repro in progress",                        NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1240, "docs: update readme",                     "@jane",           "\xe2\x9c\x93",  CAWD__GH_APPROVED,    "reviewed 1 file \xc2\xb7 0 comments \xc2\xb7 approved by policy engine",     NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1238, "feat: new policy engine",                 "@agent-policy",   "\xe2\x8b\xaf",  CAWD__GH_BLOCKED,     "reviewed 14 files \xc2\xb7 blocked: touches policy/ \xc2\xb7 requires HITL",  NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1235, "chore: bump dependencies",                "@agent-deps",     "\xe2\x9c\x93",  CAWD__GH_AUTO_MERGED, "reviewed 1 file \xc2\xb7 +142/-89 \xc2\xb7 auto-merged",                     NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1233, "feat: add /health endpoint",              "@agent-api",      "\xe2\x9c\x93",  CAWD__GH_REVIEWING,   "reviewed 2 files \xc2\xb7 1 comment \xc2\xb7 waiting CI",                    D1,     3, "server/routes.py" },
        { CAWD__GH_KIND_ISSUE, 1232, "rate-limit 429 on /v1/chat",              "@carlos",         NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-refactor \xc2\xb7 investigating",                          NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1231, "typo in README.md",                       "@mika",           NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-docs \xc2\xb7 one-liner PR incoming",                      NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1229, "fix: json decode on empty file",          "@agent-tester",   "\xe2\x9c\x93",  CAWD__GH_REVIEWING,   "reviewed 1 file \xc2\xb7 added 1 comment \xc2\xb7 awaiting CI",              D2,     3, "src/config.py" },
        { CAWD__GH_KIND_PR,    1225, "refactor: split auth module",             "@agent-refactor", "\xe2\x8b\xaf",  CAWD__GH_BLOCKED,     "reviewed 18 files \xc2\xb7 blocked: touches auth/ \xc2\xb7 requires HITL",   NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1222, "feat: per-channel cost ledger",           "@agent-policy",   "\xe2\x9c\x93",  CAWD__GH_APPROVED,    "reviewed 4 files \xc2\xb7 0 comments \xc2\xb7 approved",                      NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1220, "Telegram webhook sometimes 502",          "@jane",           NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-ops \xc2\xb7 no repro yet",                                NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1219, "slack threads not rendering emoji",       "@dev",            NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-ui \xc2\xb7 scoped to width calc",                         NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1217, "chore: rename CawdSession->CawdApp",      "@agent-refactor", "\xe2\x9c\x93",  CAWD__GH_AUTO_MERGED, "reviewed 34 files \xc2\xb7 sweeping rename \xc2\xb7 auto-merged",             NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1215, "audit log rotation",                      "@jane",           NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-ops \xc2\xb7 draft proposal up",                           NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1214, "feat: animate orchestra cards",           "@agent-ui",       "\xe2\x8b\xaf",  CAWD__GH_REVIEWING,   "reviewed 2 files \xc2\xb7 added 3 comments",                                 NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1212, "fix: quit-confirm backdrop overlay",      "@agent-ui",       "\xe2\x9c\x93",  CAWD__GH_APPROVED,    "reviewed 1 file \xc2\xb7 approved \xc2\xb7 addresses #1211",                  NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1211, "confirm modal renders below viewport",    "@ole",            NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-ui \xc2\xb7 fix-pr #1212 open",                           NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1208, "feat: per-tab state + vtbl dispatch",     "@agent-refactor", "\xe2\x8b\xaf",  CAWD__GH_REVIEWING,   "reviewed 8 files \xc2\xb7 blocker for #1207",                                NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1207, "tabs have no persistent state",           "@ole",            NULL,             CAWD__GH_ASSIGNED,    "tracked by #1208",                                                           NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1205, "docs: add cawd_tui.h quickstart",         "@agent-docs",     "\xe2\x9c\x93",  CAWD__GH_APPROVED,    "reviewed 1 file \xc2\xb7 approved",                                          NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1203, "orchestra gantt colors blend on light",   "@mika",           NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-ui",                                                       NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1201, "fix: stream_push atomic under contention","@agent-tester",   "\xe2\x9c\x93",  CAWD__GH_AUTO_MERGED, "reviewed 1 file \xc2\xb7 atomic cmpxchg \xc2\xb7 auto-merged",                NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1199, "cost ledger off-by-one on channel=ANY",   "@ole",            NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-refactor",                                                 NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1198, "test: input_fuzz for every widget",       "@agent-tester",   "\xe2\x9c\x93",  CAWD__GH_APPROVED,    "reviewed 1 file \xc2\xb7 0 comments",                                         NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1196, "feat: policy bypass countdown",           "@agent-policy",   "\xe2\x8b\xaf",  CAWD__GH_BLOCKED,     "reviewed 3 files \xc2\xb7 blocked: needs HITL sign-off",                     NULL,   0, NULL },
        { CAWD__GH_KIND_ISSUE, 1195, "policy changes don't persist",            "@jane",           NULL,             CAWD__GH_ASSIGNED,    "assigned to agent-policy \xc2\xb7 ties to #1196",                            NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1193, "fix: audit ring wraparound",              "@agent-refactor", "\xe2\x9c\x93",  CAWD__GH_AUTO_MERGED, "reviewed 1 file \xc2\xb7 auto-merged",                                        NULL,   0, NULL },
        { CAWD__GH_KIND_PR,    1190, "feat: help panel scrolls",                "@agent-ui",       "\xe2\x9c\x93",  CAWD__GH_APPROVED,    "reviewed 2 files \xc2\xb7 approved",                                          NULL,   0, NULL },
    };
    *out = arr;
    *out_n = (int)(sizeof arr / sizeof arr[0]);
}

/* Case-insensitive substring match. */
static int cawd__gh_match(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    size_t ln = strlen(needle), lh = strlen(hay);
    if (ln > lh) return 0;
    for (size_t i = 0; i + ln <= lh; i++) {
        size_t j;
        for (j = 0; j < ln; j++) {
            char a = hay[i+j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == ln) return 1;
    }
    return 0;
}

/* Collect filtered indices into `out_idx`. Returns count. */
static int cawd__gh_filter(CawdGhTabState *s, int *out_idx, int max) {
    int n = 0;
    for (int i = 0; i < s->n_cards && n < max; i++) {
        CawdGhCard *c = &s->cards[i];
        if (s->view_mode == CAWD__GH_VIEW_PRS    && c->kind != CAWD__GH_KIND_PR)    continue;
        if (s->view_mode == CAWD__GH_VIEW_ISSUES && c->kind != CAWD__GH_KIND_ISSUE) continue;
        if (s->filter_len > 0) {
            if (!(cawd__gh_match(c->title, s->filter_text) ||
                  cawd__gh_match(c->author, s->filter_text))) continue;
        }
        out_idx[n++] = i;
    }
    return n;
}

static void cawd__gh_tab_init(CawdApp *app) {
    (void)app;
    CawdGhTabState *s = &CAWD__GH_TAB;
    memset(s, 0, sizeof *s);
    s->kind = CAWD_UI_SLOT_GITHUB;
    s->auto_review = 1;
    s->view_mode = CAWD__GH_VIEW_ALL;
    s->cursor = 0;
    flux_cursor_list_init(&s->cl);
    CawdGhCard *src; int n;
    cawd__gh_demo_cards(&src, &n);
    if (n > CAWD__GH_MAX_CARDS) n = CAWD__GH_MAX_CARDS;
    for (int i = 0; i < n; i++) s->cards[i] = src[i];
    s->n_cards = n;
    app->ui.tabs_state[CAWD_UI_SLOT_GITHUB] = (struct CawdTabState *)s;
}

static void cawd__gh_tab_tick(CawdApp *app, uint64_t now_ms) {
    (void)app;
    CAWD__GH_TAB.spinner_frame = (int)((now_ms / 125ULL) & 3);
}

static int cawd__gh_tab_update(CawdApp *app, FluxMsg msg) {
    if (app->ui.tabs.active != CAWD_UI_SLOT_GITHUB) return 0;
    if (msg.type != MSG_KEY) return 0;
    CawdGhTabState *s = &CAWD__GH_TAB;

    /* Filter prompt captures text input. */
    if (s->filter_active) {
        if (flux_key_is(msg, "esc")) {
            s->filter_active = 0;
            s->filter_len = 0;
            s->filter_text[0] = 0;
            return 1;
        }
        if (flux_key_is(msg, "enter")) { s->filter_active = 0; return 1; }
        if (flux_key_is(msg, "backspace")) {
            if (s->filter_len > 0) { s->filter_text[--s->filter_len] = 0; }
            return 1;
        }
        if (msg.u.key.rune >= 0x20 && msg.u.key.rune < 0x7f &&
            s->filter_len < CAWD__GH_FILTER_CAP - 1) {
            s->filter_text[s->filter_len++] = (char)msg.u.key.rune;
            s->filter_text[s->filter_len] = 0;
            return 1;
        }
        return 1;  /* swallow everything else while filter prompt open */
    }

    /* Detail pane. */
    if (s->detail_open) {
        if (flux_key_is(msg, "esc")) { s->detail_open = 0; return 1; }
        if (flux_key_is(msg, "q"))   { s->detail_open = 0; return 1; }
        return 0;
    }

    int idxs[CAWD__GH_MAX_CARDS];
    int nf = cawd__gh_filter(s, idxs, CAWD__GH_MAX_CARDS);

    if (flux_key_is(msg, "r")) { s->auto_review = !s->auto_review; return 1; }
    if (flux_key_is(msg, "v")) { s->view_mode = (CawdGhView)((s->view_mode + 1) % 3); s->cursor = 0; return 1; }
    if (flux_key_is(msg, "p")) { s->view_mode = CAWD__GH_VIEW_PRS;    s->cursor = 0; return 1; }
    if (flux_key_is(msg, "i")) { s->view_mode = CAWD__GH_VIEW_ISSUES; s->cursor = 0; return 1; }
    if (flux_key_is(msg, "/")) { s->filter_active = 1; return 1; }
    if (flux_key_is(msg, "down") || flux_key_is(msg, "j")) {
        if (nf > 0) s->cursor = (s->cursor + 1) % nf;
        return 1;
    }
    if (flux_key_is(msg, "up") || flux_key_is(msg, "k")) {
        if (nf > 0) s->cursor = (s->cursor - 1 + nf) % nf;
        return 1;
    }
    if (flux_key_is(msg, "enter")) {
        if (nf > 0) s->detail_open = 1;
        return 1;
    }
    if (flux_key_is(msg, "m")) {
        if (nf > 0) {
            CawdGhCard *c = &s->cards[idxs[s->cursor]];
            if (c->kind == CAWD__GH_KIND_PR) {
                c->status = CAWD__GH_AUTO_MERGED;
                c->action = "merged by operator";
                cawd_toast(app, CAWD_KIND_SUCCESS, "Merged", c->title);
            }
        }
        return 1;
    }
    if (flux_key_is(msg, "x")) {
        if (nf > 0) {
            CawdGhCard *c = &s->cards[idxs[s->cursor]];
            c->status = CAWD__GH_BLOCKED;
            c->action = "changes requested by operator";
            cawd_toast(app, CAWD_KIND_WARN, "Changes requested", c->title);
        }
        return 1;
    }
    return 0;
}

/* Header row — adapts to narrow panes. */
static void cawd__gh_live_header(CawdGhTabState *s, FluxSB *sb, int w,
                                 const char *accent_fg) {
    int prs = 0, issues = 0;
    for (int i = 0; i < s->n_cards; i++) {
        if (s->cards[i].kind == CAWD__GH_KIND_PR)    prs++;
        else                                         issues++;
    }
    const char *vm = s->view_mode == CAWD__GH_VIEW_PRS    ? "[PRs]"
                   : s->view_mode == CAWD__GH_VIEW_ISSUES ? "[Issues]"
                                                          : "[All]";
    char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_BOLD); flux_sb_append(&l, accent_fg);
    flux_sb_append(&l, "GH"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    if (w >= 70) {
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        flux_sb_append(&l, "olealgoritme/flux.h");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, "  ");
    }
    if (w >= 90) {
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        char c[128];
        snprintf(c, sizeof c, "%d PRs, %d issues  ", prs, issues);
        flux_sb_append(&l, c);
        flux_sb_append(&l, FLUX_RESET);
    }
    /* auto-review indicator — ALWAYS visible regardless of width */
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "auto-review=");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, s->auto_review ? FLUX_THEME_OK_FG : FLUX_THEME_ERR_FG);
    flux_sb_append(&l, s->auto_review ? "ON" : "OFF");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "view="); flux_sb_append(&l, vm);
    flux_sb_append(&l, FLUX_RESET);
    if (s->filter_len > 0 && w >= 60) {
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, "/"); flux_sb_append(&l, s->filter_text);
        flux_sb_append(&l, FLUX_RESET);
    }
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__gh_live_filter_prompt(CawdGhTabState *s, FluxSB *sb, int w) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, "filter: /");
    flux_sb_append(&l, s->filter_text);
    flux_sb_append(&l, "\xe2\x96\x88");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "  (Enter apply \xc2\xb7 Esc cancel)");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__gh_live_footer(CawdGhTabState *s, FluxSB *sb, int w) {
    char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "\xe2\x86\x91/\xe2\x86\x93"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "move"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "Enter"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "open"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "m"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "merge"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "x"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "request-changes"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "r"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, s->auto_review ? "auto-review:on" : "auto-review:off");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "1/2/3"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "view"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "/"); flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "filter"); flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Draw the focused card detail pane (full-width). */
static void cawd__gh_live_detail(CawdGhTabState *s, FluxSB *sb, int w,
                                 const char *accent_fg, int idx) {
    CawdGhCard *c = &s->cards[idx];
    char hdr[256];
    snprintf(hdr, sizeof hdr, "  %s #%d  %s  \xe2\x80\x94  by %s",
             c->kind == CAWD__GH_KIND_PR ? "PR" : "Issue",
             c->number, c->title ? c->title : "",
             c->author ? c->author : "?");
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, FLUX_BOLD);
        flux_sb_append(&l, accent_fg);
        flux_sb_append(&l, hdr);
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    cawd__ui_row_blank(sb, w);
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  status: ");
        flux_sb_append(&l, cawd__gh_status_fg(c->status));
        flux_sb_append(&l, cawd__gh_status_label(c->status));
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "agent action: ");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, c->action ? c->action : "(none)");
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    cawd__ui_row_blank(sb, w);
    if (c->diff && c->diff_n > 0) {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, c->diff_file ? c->diff_file : "diff");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
        char fnbuf[128]; snprintf(fnbuf, sizeof fnbuf, "%s", c->diff_file ? c->diff_file : "review.diff");
        flux_diff_block_render(sb, fnbuf, c->diff, c->diff_n, w);
    } else {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "(no diff preview) \xe2\x80\x94 Esc to close");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    cawd__ui_row_blank(sb, w);
    {
        char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_kbd(&l, "Esc"); flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "close"); flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
}

static void cawd__gh_tab_render(CawdApp *app, FluxSB *sb, int w, int h) {
    (void)app; (void)h;
    static const char ACCENT_FG[] = "\x1b[38;2;255;158;100m";
    CawdGhTabState *s = &CAWD__GH_TAB;
    if (s->kind != CAWD_UI_SLOT_GITHUB) cawd__gh_tab_init(app);
    if (!sb || w <= 0) return;

    cawd__gh_live_header(s, sb, w, ACCENT_FG);
    if (s->filter_active) cawd__gh_live_filter_prompt(s, sb, w);
    cawd__ui_row_blank(sb, w);

    int idxs[CAWD__GH_MAX_CARDS];
    int nf = cawd__gh_filter(s, idxs, CAWD__GH_MAX_CARDS);
    if (s->cursor >= nf) s->cursor = nf > 0 ? nf - 1 : 0;

    if (s->detail_open && nf > 0) {
        cawd__gh_live_detail(s, sb, w, ACCENT_FG, idxs[s->cursor]);
        cawd__ui_row_blank(sb, w);
        cawd__gh_live_footer(s, sb, w);
        return;
    }

    if (nf == 0) {
        char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "no matches \xe2\x80\x94 Esc to clear filter, 1 to show all");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }

    /* Cursor + auto-scroll via FluxCursorList. */
    flux_cursor_list_begin(&s->cl, &app->ui.scroll[CAWD_UI_SLOT_GITHUB],
                           s->cursor, nf, h, /*margin*/2);
    /* Seed running offset to account for header + optional filter prompt
     * + blank row already emitted before this loop. */
    s->cl.running_row = 2 + (s->filter_active ? 1 : 0);

    for (int k = 0; k < nf; k++) {
        int i = idxs[k];
        int card_rows = 5;
        if (s->cards[i].status == CAWD__GH_REVIEWING
            && s->cards[i].diff && s->cards[i].diff_n > 0) {
            card_rows += s->cards[i].diff_n + (s->cards[i].diff_file ? 1 : 0);
        }
        int row_off;
        flux_cursor_list_item(&s->cl, k, card_rows + 1 /* +blank */, &row_off);
        const char *row_accent = (k == s->cursor) ? FLUX_THEME_ACCENT_FG : ACCENT_FG;
        cawd__gh_card_render(sb, w, row_accent, &s->cards[i]);
        cawd__ui_row_blank(sb, w);
    }
    flux_cursor_list_end(&s->cl);

    cawd__gh_live_footer(s, sb, w);
}

/* ─── Slack stage (channel #5) ──────────────────────────────────────
 * Threaded conversation with #eng channel header, /claw slash-command
 * dropdown preview, ~6 threaded messages with avatar chips + reactions,
 * composer mock and contextual footer hints. Pink (#f7768e) accents all
 * Slack-styled elements; thread replies indent by 4 cells with a pink
 * left-gutter bar. Honors width contract via flux_fit every row. */

/* Slack accent pink (FLUX_CH_SL hue) as an explicit FG SGR. */
#define CAWD__SL_PINK_FG   "\x1b[38;2;247;118;142m"
#define CAWD__SL_PINK_DIM  "\x1b[38;2;163; 78; 94m"
#define CAWD__SL_BG_HOVER  "\x1b[48;2; 36; 38; 54m"

typedef struct {
    const char *initials;   /* avatar chip text, e.g. "JS" */
    const char *handle;     /* "@jane" */
    const char *ts;         /* "9:42" */
    const char *body1;      /* first body line */
    const char *body2;      /* optional second body line (NULL = none) */
    const char *react1;     /* e.g. "👍 3" (NULL = none) */
    const char *react2;     /* e.g. "🎉 1" (NULL = none) */
    int         is_bot;     /* 1 => indent + pink left-gutter for thread reply */
} CawdSlMsg;

/* Width-safe row helpers (local to slack stage). */
static void cawd__sl_row_header(FluxSB *sb, int w) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    /* pink SL badge chip */
    flux_sb_append(&l, " ");
    flux_sb_append(&l, CAWD__SL_PINK_FG);
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, "[ SL ]");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    /* #eng channel + meta */
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, "#eng");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "\xc2\xb7 12 members \xc2\xb7 /claw commands enabled");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__sl_row_slashes(FluxSB *sb, int w) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, "slash \xc2\xb7 ");
    flux_sb_append(&l, FLUX_RESET);
    /* Three /claw chips separated by gap. */
    const char *cmds[3] = { "/claw review", "/claw audit", "/claw status" };
    for (int i = 0; i < 3; i++) {
        flux_sb_append(&l, CAWD__SL_PINK_FG);
        flux_sb_append(&l, "[");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        flux_sb_append(&l, cmds[i]);
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, CAWD__SL_PINK_FG);
        flux_sb_append(&l, "]");
        flux_sb_append(&l, FLUX_RESET);
        if (i < 2) flux_sb_append(&l, "  ");
    }
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Emit one message header row: [gutter] (IN) @handle · ts */
static void cawd__sl_row_msg_head(FluxSB *sb, int w, const CawdSlMsg *m) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    if (m->is_bot) {
        flux_sb_append(&l, "    ");   /* 4-cell indent */
        flux_sb_append(&l, CAWD__SL_PINK_FG);
        flux_sb_append(&l, "\xe2\x94\x82"); /* vertical bar */
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, " ");
    } else {
        flux_sb_append(&l, " ");
    }
    /* avatar chip — bot replies get pink, humans get purple accent. */
    flux_sb_append(&l, m->is_bot ? CAWD__SL_PINK_FG : FLUX_THEME_BRAND_PURPLE_FG);
    flux_sb_append(&l, "(");
    flux_sb_append(&l, m->initials);
    flux_sb_append(&l, ")");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, m->handle);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "\xc2\xb7 ");
    flux_sb_append(&l, m->ts);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Emit one message body row honoring optional indent + gutter. */
static void cawd__sl_row_msg_body(FluxSB *sb, int w, int is_bot, const char *body) {
    if (!body) return;
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    if (is_bot) {
        flux_sb_append(&l, "    ");
        flux_sb_append(&l, CAWD__SL_PINK_FG);
        flux_sb_append(&l, "\xe2\x94\x82");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, "   "); /* align under avatar/name */
    } else {
        flux_sb_append(&l, "     "); /* align under avatar/name */
    }
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, body);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Reactions row: emoji pills via flux_badge-styled chips. */
static void cawd__sl_row_msg_reacts(FluxSB *sb, int w, int is_bot,
                                    const char *r1, const char *r2) {
    if (!r1 && !r2) return;
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    if (is_bot) {
        flux_sb_append(&l, "    ");
        flux_sb_append(&l, CAWD__SL_PINK_FG);
        flux_sb_append(&l, "\xe2\x94\x82");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, "   ");
    } else {
        flux_sb_append(&l, "     ");
    }
    const char *reacts[2] = { r1, r2 };
    for (int i = 0; i < 2; i++) {
        if (!reacts[i]) continue;
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "[ ");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        flux_sb_append(&l, reacts[i]);
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, " ]");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, " ");
    }
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Composer mock row — looks like Slack's compose input. */
static void cawd__sl_row_composer(FluxSB *sb, int w) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, CAWD__SL_PINK_DIM);
    flux_sb_append(&l, "\xe2\x94\x8f"); /* ┏ */
    for (int i = 0; i < 6; i++) flux_sb_append(&l, "\xe2\x94\x81");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "Message #eng");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, "\xe2\x80\xa6"); /* … */
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_sb_append(&l, CAWD__SL_PINK_FG);
    flux_sb_append(&l, "[ send \xe2\x96\xb8 ]"); /* [ send ▸ ] */
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Footer hints row — keybinding chips. */
static void cawd__sl_row_footer(FluxSB *sb, int w) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, "[Enter] send  \xc2\xb7  [r] reply in thread  \xc2\xb7  [:] emoji");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__ui_stage_slack(CawdApp *a, FluxSB *sb, int w, int h) {
    (void)a;
    if (!sb || w <= 0 || h <= 0) return;

    /* Function-local static thread — 6 messages. */
    static const CawdSlMsg MSGS[] = {
        { "JS", "@jane",     "9:42", "/claw review the PR #1248",
          NULL, NULL, NULL, 0 },
        { "CA", "@openclaw", "9:42", "\xe2\x9c\x93 Reviewing PR #1248\xe2\x80\xa6",
          "Running 3 checks in parallel.", "+1 3", "ok 2", 1 },
        { "CA", "@openclaw", "9:43", "Done \xe2\x80\x94 2 minor suggestions. Auto-merged.",
          NULL, "tada 1", NULL, 1 },
        { "DV", "@david",    "9:45", "nice, thanks for the quick turnaround",
          NULL, NULL, NULL, 0 },
        { "JS", "@jane",     "9:47", "/claw audit the auth changes since monday",
          NULL, NULL, NULL, 0 },
        { "CA", "@openclaw", "9:47", "Auditing \xe2\x80\xa6 found 0 policy violations, 2 TODOs.",
          NULL, "eyes 1", NULL, 1 },
    };
    const int n_msgs = (int)(sizeof MSGS / sizeof MSGS[0]);

    int rows_used = 0;

    /* 1) Header. */
    if (rows_used < h) { cawd__sl_row_header(sb, w);  rows_used++; }

    /* 2) Slash-command preview. */
    if (rows_used < h) { cawd__sl_row_slashes(sb, w); rows_used++; }

    /* 3) Spacer. */
    if (rows_used < h) { cawd__ui_row_blank(sb, w);   rows_used++; }

    /* Reserve 3 rows at the bottom: blank, composer, footer.
     * If the pane is very short, drop reservations gracefully. */
    int bottom_reserve = 3;
    if (h - rows_used < bottom_reserve + 2) bottom_reserve = (h - rows_used > 1) ? 1 : 0;
    int msg_budget = h - rows_used - bottom_reserve;
    if (msg_budget < 0) msg_budget = 0;

    /* 4) Threaded message list — each message consumes 1 head + up to
     * 2 body rows + up to 1 reactions row + 1 spacer row between
     * messages. Stop cleanly when we run out of budget. */
    for (int i = 0; i < n_msgs && msg_budget > 0; i++) {
        const CawdSlMsg *m = &MSGS[i];

        cawd__sl_row_msg_head(sb, w, m);       rows_used++; msg_budget--;
        if (msg_budget <= 0) break;

        if (m->body1) {
            cawd__sl_row_msg_body(sb, w, m->is_bot, m->body1);
            rows_used++; msg_budget--;
            if (msg_budget <= 0) break;
        }
        if (m->body2) {
            cawd__sl_row_msg_body(sb, w, m->is_bot, m->body2);
            rows_used++; msg_budget--;
            if (msg_budget <= 0) break;
        }
        if (m->react1 || m->react2) {
            cawd__sl_row_msg_reacts(sb, w, m->is_bot, m->react1, m->react2);
            rows_used++; msg_budget--;
            if (msg_budget <= 0) break;
        }
        /* inter-message spacer (skip after last msg, and only if budget). */
        if (i + 1 < n_msgs && msg_budget > 0) {
            cawd__ui_row_blank(sb, w); rows_used++; msg_budget--;
        }
    }

    /* Fill leftover vertical space before the bottom strip. */
    while (rows_used < h - bottom_reserve) {
        cawd__ui_row_blank(sb, w); rows_used++;
    }

    /* 5) Bottom strip: blank / composer / footer (budget-aware). */
    if (bottom_reserve >= 3 && rows_used < h) {
        cawd__ui_row_blank(sb, w);   rows_used++;
    }
    if (bottom_reserve >= 2 && rows_used < h) {
        cawd__sl_row_composer(sb, w); rows_used++;
    }
    if (bottom_reserve >= 1 && rows_used < h) {
        cawd__sl_row_footer(sb, w);   rows_used++;
    }

    /* Safety: pad any remaining rows. */
    while (rows_used < h) { cawd__ui_row_blank(sb, w); rows_used++; }
}

/* ---- Audit stage (slot 9) ------------------------------------------------
 *
 * Live audit-log viewer. Pulls the most-recent N entries via
 * cawd_audit_tail() into a function-local static buffer, renders a top
 * title strip (N entries + last timestamp), a visual-only filter bar,
 * a scrollable viewport of 1-2 line rows, and a footer hint strip.
 * Empty state falls back to flux_placeholder. Every row is exactly `w`
 * display cells thanks to flux_fit; overlong args are truncated via
 * flux_truncate. Alternating rows carry a subtle bg tint. Namespace:
 * cawd__audit_ui_*.
 * ------------------------------------------------------------------------ */

#define CAWD__AUDIT_UI_MAX_TAIL 40

/* Per-row alternating tint (2 RGB steps lighter than PANEL_BG). */
#define CAWD__AUDIT_UI_ROW_ALT_BG  "\x1b[48;2;30;31;44m"   /* #1e1f2c */

/* Forward decls for chrome helpers defined later in this file. */
static void cawd__ui_row_blank(FluxSB *sb, int w);
static void cawd__ui_row_fit  (FluxSB *sb, int w, const char *s);

static void cawd__audit_ui_fmt_ts(uint64_t ts_ms, char *out, size_t cap) {
    time_t secs = (time_t)(ts_ms / 1000ull);
    unsigned ms  = (unsigned)(ts_ms % 1000ull);
    struct tm tm_local;
#if defined(_WIN32)
    localtime_s(&tm_local, &secs);
#else
    localtime_r(&secs, &tm_local);
#endif
    snprintf(out, cap, "%02d:%02d:%02d.%03u",
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec, ms);
}

static void cawd__audit_ui_fmt_ts_short(uint64_t ts_ms, char *out, size_t cap) {
    time_t secs = (time_t)(ts_ms / 1000ull);
    struct tm tm_local;
#if defined(_WIN32)
    localtime_s(&tm_local, &secs);
#else
    localtime_r(&secs, &tm_local);
#endif
    snprintf(out, cap, "%02d:%02d:%02d",
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
}

static FluxChannelId cawd__audit_ui_channel_to_flux(CawdChannel ch) {
    switch (ch) {
        case CAWD_CH_HITL:       return FLUX_CH_HITL;
        case CAWD_CH_TELEGRAM:   return FLUX_CH_TG;
        case CAWD_CH_AUTONOMOUS: return FLUX_CH_AUTO;
        case CAWD_CH_GITHUB:     return FLUX_CH_GH;
        case CAWD_CH_SLACK:      return FLUX_CH_SL;
        case CAWD_CH_API:        return FLUX_CH_API;
        default:                 return FLUX_CH_HITL;
    }
}

static const char *cawd__audit_ui_verdict_fg(CawdVerdict v) {
    switch (v) {
        case CAWD_ALLOW: return FLUX_THEME_OK_FG;
        case CAWD_DENY:  return FLUX_THEME_ERR_FG;
        case CAWD_ASK:   return FLUX_THEME_WARN_FG;
        default:         return FLUX_THEME_TEXT_DIM_FG;
    }
}

static const char *cawd__audit_ui_verdict_lbl(CawdVerdict v) {
    switch (v) {
        case CAWD_ALLOW: return "ALLOW";
        case CAWD_DENY:  return "DENY ";
        case CAWD_ASK:   return "ASK  ";
        default:         return "?    ";
    }
}

/* Emit the top title strip. */
static void cawd__audit_ui_render_title(FluxSB *sb, int w,
                                        int total, uint64_t last_ts) {
    char L[512]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, "\xe2\x97\x86 AUDIT");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    char meta[96];
    if (total > 0 && last_ts > 0) {
        char ts[16];
        cawd__audit_ui_fmt_ts_short(last_ts, ts, sizeof ts);
        snprintf(meta, sizeof meta,
                 "%s %d entries  %s  last: %s",
                 "\xc2\xb7", total, "\xc2\xb7", ts);
    } else {
        snprintf(meta, sizeof meta, "\xc2\xb7 %d entries", total);
    }
    flux_sb_append(&l, meta);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Visual-only filter bar — chips + query prompt. No interaction yet. */
static void cawd__audit_ui_render_filter(FluxSB *sb, int w) {
    char L[512]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "filter:");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    /* Active chip (all) */
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, "[all]");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    /* Inactive chips */
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "[bash] [reads] [denials]");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, "/query");
    flux_sb_append(&l, FLUX_THEME_BRAND_PURPLE_FG);
    flux_sb_append(&l, "\xe2\x96\x8c"); /* ▌ cursor */
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Render a single audit row (line 1 of 2). Alternates bg tint. */
static void cawd__audit_ui_render_row1(FluxSB *sb, int w,
                                       const CawdAuditEntry *e, int alt) {
    char L[768]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    const char *tint = alt ? CAWD__AUDIT_UI_ROW_ALT_BG : "";
    if (*tint) flux_sb_append(&l, tint);
    flux_sb_append(&l, " ");

    /* timestamp */
    char ts[20];
    cawd__audit_ui_fmt_ts(e->ts_ms, ts, sizeof ts);
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, ts);
    flux_sb_append(&l, FLUX_RESET);
    if (*tint) flux_sb_append(&l, tint);
    flux_sb_append(&l, " ");

    /* channel badge (compact inline) */
    FluxChannelId cid = cawd__audit_ui_channel_to_flux(e->channel);
    FluxRGB crgb = flux_channel_rgb(cid);
    char cesc[32];
    snprintf(cesc, sizeof cesc, "\x1b[38;2;%d;%d;%dm",
             crgb.r, crgb.g, crgb.b);
    flux_sb_append(&l, cesc);
    flux_sb_append(&l, FLUX_BOLD);
    char cbuf[16];
    snprintf(cbuf, sizeof cbuf, "%-4s", flux_channel_label(cid));
    flux_sb_append(&l, cbuf);
    flux_sb_append(&l, FLUX_RESET);
    if (*tint) flux_sb_append(&l, tint);
    flux_sb_append(&l, " ");

    /* verdict badge */
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, cawd__audit_ui_verdict_fg(e->verdict));
    flux_sb_append(&l, cawd__audit_ui_verdict_lbl(e->verdict));
    flux_sb_append(&l, FLUX_RESET);
    if (*tint) flux_sb_append(&l, tint);
    flux_sb_append(&l, " ");

    /* tool name */
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, e->tool ? e->tool : "?");
    flux_sb_append(&l, FLUX_RESET);
    if (*tint) flux_sb_append(&l, tint);
    flux_sb_append(&l, " ");

    /* args truncated to remaining width — rough budget = w - 40. */
    int budget = w - 40;
    if (budget < 8) budget = 8;
    char targs[256];
    flux_truncate(e->args ? e->args : "", budget,
                  "\xe2\x80\xa6", targs, sizeof targs);
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, targs);
    flux_sb_append(&l, FLUX_RESET);

    flux_fit(sb, flux_sb_str(&l), w, NULL, FLUX_ALIGN_LEFT);
    if (*tint) flux_sb_append(sb, FLUX_RESET);
    flux_sb_append(sb, "\n");
}

/* Render the optional line-2 detail (dim). Returns 1 if written. */
static int cawd__audit_ui_render_row2(FluxSB *sb, int w,
                                      const CawdAuditEntry *e, int alt) {
    /* Only emit a detail row if we have any meaningful detail to show. */
    int have = (e->exit_code != 0) ||
               (e->duration_ms > 0) ||
               (e->cost_usd > 0.0) ||
               (e->operator_ && e->operator_[0]);
    if (!have) return 0;

    char L[384]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    const char *tint = alt ? CAWD__AUDIT_UI_ROW_ALT_BG : "";
    if (*tint) flux_sb_append(&l, tint);
    /* Indent to align roughly with the tool column on line 1. */
    flux_sb_append(&l, "             ");  /* 13 sp */
    flux_sb_append(&l, FLUX_DIM);
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "\xe2\x86\xb3 "); /* ↳ */
    char detail[256]; detail[0] = '\0';
    int off = 0;
    off += snprintf(detail + off, sizeof detail - off,
                    "exit=%d  dur=%dms  cost=$%.4f",
                    e->exit_code, e->duration_ms, e->cost_usd);
    if (e->operator_ && e->operator_[0] && off < (int)sizeof detail) {
        snprintf(detail + off, sizeof detail - off,
                 "  op=%s", e->operator_);
    }
    flux_sb_append(&l, detail);
    flux_sb_append(&l, FLUX_RESET);

    flux_fit(sb, flux_sb_str(&l), w, NULL, FLUX_ALIGN_LEFT);
    if (*tint) flux_sb_append(sb, FLUX_RESET);
    flux_sb_append(sb, "\n");
    return 1;
}

/* Footer hint strip. */
static void cawd__audit_ui_render_footer(FluxSB *sb, int w) {
    char L[256]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l,
        "[/] filter  [g/G] top/bottom  [e] export  [Enter] detail");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__ui_stage_audit(CawdApp *a, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0) return;
    if (h < 1) h = 1;

    /* Function-local static buffer — reused across renders, thread-safe
     * because UI thread is single-threaded; cawd_audit_tail() handles
     * its own locking against the sim worker. */
    static CawdAuditEntry BUF[CAWD__AUDIT_UI_MAX_TAIL];
    int n = a ? cawd_audit_tail(a, BUF, CAWD__AUDIT_UI_MAX_TAIL) : 0;

    /* Empty state — placeholder centred over the full stage. */
    if (n <= 0) {
        int content_rows = 3;
        if (h < content_rows) h = content_rows;
        int top    = (h - content_rows) / 2;
        int bottom = h - content_rows - top;
        if (top < 0) top = 0;
        if (bottom < 0) bottom = 0;
        for (int i = 0; i < top; i++) cawd__ui_row_blank(sb, w);
        flux_placeholder(sb,
            "\xe2\x96\xa4",
            "no audit entries yet",
            "tool calls via agents will populate this log. "
            "try typing in the HITL composer to spawn an agent.",
            w);
        for (int i = 0; i < bottom; i++) cawd__ui_row_blank(sb, w);
        return;
    }

    /* ── chrome rows ── */
    cawd__audit_ui_render_title(sb, w, n, BUF[0].ts_ms);
    cawd__audit_ui_render_filter(sb, w);
    cawd__ui_row_blank(sb, w);
    int rows_used = 3;

    /* ── viewport rows ── reserve 1 for footer (h >= 5). */
    int reserve_footer = (h >= 5) ? 1 : 0;
    int viewport_max = h - rows_used - reserve_footer;
    if (viewport_max < 0) viewport_max = 0;

    int i = 0;
    int alt = 0;
    while (i < n && (h - rows_used - reserve_footer) >= 1) {
        cawd__audit_ui_render_row1(sb, w, &BUF[i], alt);
        rows_used++;
        if ((h - rows_used - reserve_footer) >= 1) {
            if (cawd__audit_ui_render_row2(sb, w, &BUF[i], alt)) {
                rows_used++;
            }
        }
        alt ^= 1;
        i++;
    }
    (void)viewport_max;

    /* fill remaining viewport rows with blanks */
    while (rows_used < h - reserve_footer) {
        cawd__ui_row_blank(sb, w);
        rows_used++;
    }

    if (reserve_footer) {
        cawd__audit_ui_render_footer(sb, w);
    }
}
/* ═══════════════════════════════════════════════════════════════════
 *  Autonomous channel — queue-driven runner dashboard (slot 2)
 *
 *  Layout (top→bottom):
 *     [2] header: title + demo-clock pill + pause state / counters
 *     [5] throughput chart (line_chart_multi: resolve/review/cron)
 *     [N] task list (FluxVirtualList) — whatever height remains
 *     [3] alert feed (policy denials, cycling)
 *     [1] footer kbd hints
 *
 *  All state is function-local static; simulation driven by flux_now_ms()
 *  so the UI visibly progresses.  Helpers namespaced `cawd__auto_*`.
 *  No heap allocation — every buffer is static or stack.
 * ═════════════════════════════════════════════════════════════════ */

enum { CAWD__AUTO_SERIES_N    = 3 };
enum { CAWD__AUTO_SAMPLES     = 60 };
enum { CAWD__AUTO_TASK_COUNT  = 20 };
enum { CAWD__AUTO_ALERT_COUNT = 3 };

typedef enum {
    CAWD__AUTO_KIND_RESOLVE = 0,
    CAWD__AUTO_KIND_REVIEW  = 1,
    CAWD__AUTO_KIND_CRON    = 2
} CawdAutoKind;

typedef enum {
    CAWD__AUTO_ST_QUEUED = 0,
    CAWD__AUTO_ST_RUNNING,
    CAWD__AUTO_ST_DONE,
    CAWD__AUTO_ST_FAILED,
    CAWD__AUTO_ST_DENIED
} CawdAutoState;

typedef struct {
    int             id;
    CawdAutoKind    kind;
    const char     *target;     /* borrowed literal */
    const char     *agent;      /* borrowed literal */
    CawdAutoState   state;
    uint64_t        queued_ms;
    uint64_t        start_ms;   /* 0 if not yet started */
    uint64_t        end_ms;     /* 0 if not yet ended   */
    int             run_ms;     /* synthetic planned duration */
    float           cost_usd;
} CawdAutoTask;

static const char *cawd__auto_kind_label(CawdAutoKind k) {
    switch (k) {
        case CAWD__AUTO_KIND_RESOLVE: return "AUTO-RESOLVE";
        case CAWD__AUTO_KIND_REVIEW:  return "AUTO-REVIEW";
        case CAWD__AUTO_KIND_CRON:    return "AUTO-CRON";
    }
    return "AUTO";
}

static const char *cawd__auto_kind_bg(CawdAutoKind k) {
    /* flux_badge synthesises a bg SGR from an fg "\x1b[38;..." value. */
    switch (k) {
        case CAWD__AUTO_KIND_RESOLVE: return FLUX_THEME_OK_FG;
        case CAWD__AUTO_KIND_REVIEW:  return FLUX_THEME_BRAND_PURPLE_FG;
        case CAWD__AUTO_KIND_CRON:    return FLUX_THEME_WARN_FG;
    }
    return FLUX_THEME_ACCENT_FG;
}

/* Deterministic pseudo-random helper — hash(seed, salt) → [0,1). */
static float cawd__auto_rand01(uint32_t seed, uint32_t salt) {
    uint32_t x = seed * 2654435761u ^ (salt + 0x9e3779b9u);
    x ^= x >> 16; x *= 0x85ebca6bu;
    x ^= x >> 13; x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffu) / (float)0x01000000u;
}

/* Lazy-init the static task table (fixed size, never grows). */
static CawdAutoTask *cawd__auto_task_table(void) {
    static CawdAutoTask tasks[CAWD__AUTO_TASK_COUNT];
    static int inited = 0;
    if (inited) return tasks;
    static const char *resolve_targets[] = {
        "issue #1244", "issue #1247", "issue #1251", "issue #1260",
        "issue #1274", "issue #1288", "issue #1301", "issue #1318"
    };
    static const char *review_targets[] = {
        "PR #88",  "PR #91",  "PR #94",  "PR #97",
        "PR #102", "PR #108", "PR #113"
    };
    static const char *cron_targets[] = {
        "nightly.sweep", "deps.audit", "backup.snapshot",
        "health.probe",  "license.scan"
    };
    static const char *agents[] = {
        "Cartographer", "Refactor",    "Tester",
        "Researcher",   "Bench",       "Guardian"
    };
    for (int i = 0; i < CAWD__AUTO_TASK_COUNT; i++) {
        CawdAutoTask *t = &tasks[i];
        t->id = 9000 + i;
        float r = cawd__auto_rand01((uint32_t)i, 0xA17u);
        if (r < 0.50f) {
            t->kind   = CAWD__AUTO_KIND_RESOLVE;
            t->target = resolve_targets[i % (int)(sizeof resolve_targets / sizeof resolve_targets[0])];
        } else if (r < 0.85f) {
            t->kind   = CAWD__AUTO_KIND_REVIEW;
            t->target = review_targets[i % (int)(sizeof review_targets / sizeof review_targets[0])];
        } else {
            t->kind   = CAWD__AUTO_KIND_CRON;
            t->target = cron_targets[i % (int)(sizeof cron_targets / sizeof cron_targets[0])];
        }
        t->agent     = agents[i % (int)(sizeof agents / sizeof agents[0])];
        t->state     = CAWD__AUTO_ST_QUEUED;
        t->queued_ms = 0;
        t->start_ms  = 0;
        t->end_ms    = 0;
        t->run_ms    = 3000 + (int)(cawd__auto_rand01((uint32_t)i, 0xB01u) * 12000.0f);
        t->cost_usd  = 0.0025f + cawd__auto_rand01((uint32_t)i, 0xC41u) * 0.0480f;
    }
    inited = 1;
    return tasks;
}

/* Drive state transitions based on the virtual 10× demo-clock. Every
 * task cycles queued → running → done over its run_ms window; a small
 * scripted minority are marked failed / denied instead. */
static void cawd__auto_simulate(CawdAutoTask *tasks, uint64_t now_ms,
                                int *out_queued, int *out_running,
                                int *out_done_today) {
    int q = 0, r = 0, d = 0;
    for (int i = 0; i < CAWD__AUTO_TASK_COUNT; i++) {
        CawdAutoTask *t = &tasks[i];
        uint64_t span   = (uint64_t)t->run_ms + 6000ULL; /* queue window */
        uint64_t offset = (uint64_t)(i * 1700);
        uint64_t phase  = ((now_ms * 10ULL) + offset) % (span * 3ULL);
        if (i % 7 == 6) {
            t->state    = CAWD__AUTO_ST_DENIED;
            t->start_ms = (phase < now_ms) ? (now_ms - phase) : 0;
            t->end_ms   = t->start_ms + 120;
        } else if (i % 11 == 10) {
            t->state    = CAWD__AUTO_ST_FAILED;
            t->start_ms = (phase < now_ms) ? (now_ms - phase) : 0;
            t->end_ms   = t->start_ms + (uint64_t)t->run_ms / 2;
        } else if (phase < span) {
            t->state    = CAWD__AUTO_ST_QUEUED;
            t->start_ms = 0;
            t->end_ms   = 0;
            q++;
        } else if (phase < span + (uint64_t)t->run_ms) {
            t->state    = CAWD__AUTO_ST_RUNNING;
            t->start_ms = now_ms - (phase - span);
            t->end_ms   = 0;
            r++;
        } else {
            t->state    = CAWD__AUTO_ST_DONE;
            t->start_ms = now_ms - (phase - span);
            t->end_ms   = t->start_ms + (uint64_t)t->run_ms;
            d++;
        }
    }
    if (out_queued)     *out_queued     = q;
    if (out_running)    *out_running    = r;
    if (out_done_today) *out_done_today = 312 + d + (int)((now_ms / 1800ULL) % 93ULL);
}

/* Ring-buffer throughput step — shift left, append a new sine+noise
 * sample. Throttled to ~4 Hz so we don't rewrite samples every tick. */
static void cawd__auto_step_throughput(float *a, float *b, float *c,
                                       uint64_t now_ms, uint64_t *last_step) {
    if (*last_step != 0 && (now_ms - *last_step) < 250ULL) return;
    *last_step = now_ms;
    for (int i = 0; i < CAWD__AUTO_SAMPLES - 1; i++) {
        a[i] = a[i + 1];
        b[i] = b[i + 1];
        c[i] = c[i + 1];
    }
    double t = (double)now_ms * 0.001;
    float na = 3.0f + 2.4f * (float)sin(t * 0.85)
               + (cawd__auto_rand01((uint32_t)now_ms, 0x11u) - 0.5f) * 1.2f;
    float nb = 2.0f + 1.6f * (float)sin(t * 0.55 + 1.1)
               + (cawd__auto_rand01((uint32_t)now_ms, 0x22u) - 0.5f) * 0.9f;
    float nc = 1.2f + 0.9f * (float)sin(t * 0.30 + 2.3)
               + (cawd__auto_rand01((uint32_t)now_ms, 0x33u) - 0.5f) * 0.5f;
    if (na < 0) na = 0;
    if (nb < 0) nb = 0;
    if (nc < 0) nc = 0;
    a[CAWD__AUTO_SAMPLES - 1] = na;
    b[CAWD__AUTO_SAMPLES - 1] = nb;
    c[CAWD__AUTO_SAMPLES - 1] = nc;
}

/* Context passed into the virtual-list render callback. */
typedef struct {
    const CawdAutoTask *tasks;
    uint64_t            now_ms;
    int                 frame;
} CawdAutoListCtx;

static void cawd__auto_fmt_elapsed(char *buf, int bufsz, uint64_t ms) {
    if (ms < 1000ULL) {
        snprintf(buf, (size_t)bufsz, "%llums", (unsigned long long)ms);
    } else if (ms < 60000ULL) {
        snprintf(buf, (size_t)bufsz, "%.1fs", (double)ms / 1000.0);
    } else {
        snprintf(buf, (size_t)bufsz, "%llum%02llus",
                 (unsigned long long)(ms / 60000ULL),
                 (unsigned long long)((ms / 1000ULL) % 60ULL));
    }
}

/* Virtual-list item renderer — must emit exactly `width` cells, no '\n'
 * (flux_vlist_render appends the newline itself). */
static void cawd__auto_list_item(FluxSB *sb, int index, int selected,
                                 int width, void *ctx_v) {
    CawdAutoListCtx *ctx = (CawdAutoListCtx *)ctx_v;
    if (!ctx) { flux_fit(sb, "", width, NULL, FLUX_ALIGN_LEFT); return; }
    const CawdAutoTask *t = &ctx->tasks[index];

    /* Compose into a local SB so we can flux_fit the whole row once. */
    char L[1024]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);

    /* Selection gutter. */
    flux_sb_append(&l, selected ? FLUX_THEME_ACCENT_FG : FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, selected ? " \xe2\x96\xb6 " : "   "); /* ▶ or 3-sp */
    flux_sb_append(&l, FLUX_RESET);

    /* Status glyph/dot — blink for running, solid for terminal states. */
    const char *dot_clr, *dot_glyph;
    switch (t->state) {
        case CAWD__AUTO_ST_DONE:
            dot_clr   = FLUX_THEME_OK_FG;
            dot_glyph = "\xe2\x9c\x93"; /* ✓ */
            break;
        case CAWD__AUTO_ST_RUNNING:
            dot_clr   = FLUX_THEME_WARN_FG;
            dot_glyph = (ctx->frame & 1) ? "\xe2\x97\x8f" : "\xe2\x97\x8b"; /* ●/○ */
            break;
        case CAWD__AUTO_ST_DENIED:
            dot_clr   = FLUX_THEME_ERR_FG;
            dot_glyph = "\xe2\x8a\x98"; /* ⊘ */
            break;
        case CAWD__AUTO_ST_FAILED:
            dot_clr   = FLUX_THEME_ERR_FG;
            dot_glyph = "\xe2\x9c\x97"; /* ✗ */
            break;
        case CAWD__AUTO_ST_QUEUED:
        default:
            dot_clr   = FLUX_THEME_TEXT_DIM_FG;
            dot_glyph = "\xe2\x97\x87"; /* ◇ */
            break;
    }
    flux_sb_append(&l, dot_clr);
    flux_sb_append(&l, dot_glyph);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");

    /* Kind badge padded to a fixed 12-char inner width. */
    char kind_pad[16];
    snprintf(kind_pad, sizeof kind_pad, "%-12s", cawd__auto_kind_label(t->kind));
    flux_badge(&l, kind_pad, FLUX_THEME_TEXT_INV_FG, cawd__auto_kind_bg(t->kind));
    flux_sb_append(&l, " ");

    /* Target. */
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    char target[48];
    snprintf(target, sizeof target, "%s", t->target ? t->target : "?");
    flux_fit(&l, target, 14, "\xe2\x80\xa6", FLUX_ALIGN_LEFT); /* … */
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");

    /* Agent. */
    flux_sb_append(&l, FLUX_THEME_TEXT2_FG);
    char agent[32];
    snprintf(agent, sizeof agent, "%s", t->agent ? t->agent : "?");
    flux_fit(&l, agent, 12, "\xe2\x80\xa6", FLUX_ALIGN_LEFT);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");

    /* Elapsed + cost tail. */
    uint64_t elapsed_ms = 0;
    if (t->state == CAWD__AUTO_ST_RUNNING && t->start_ms && ctx->now_ms > t->start_ms) {
        elapsed_ms = ctx->now_ms - t->start_ms;
    } else if (t->end_ms && t->start_ms && t->end_ms > t->start_ms) {
        elapsed_ms = t->end_ms - t->start_ms;
    }
    char el[24]; cawd__auto_fmt_elapsed(el, (int)sizeof el, elapsed_ms);
    char tail[64];
    snprintf(tail, sizeof tail, "%s  $%.4f", el, (double)t->cost_usd);
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, tail);
    flux_sb_append(&l, FLUX_RESET);

    /* Final fit into `width` cells, no trailing newline. */
    flux_fit(sb, flux_sb_str(&l), width, "\xe2\x80\xa6", FLUX_ALIGN_LEFT);
}

/* Header strip (2 rows). */
static void cawd__auto_render_header(FluxSB *sb, int w, uint64_t virt_s,
                                     int paused, int q, int r, int done) {
    /* Row 1: title + demo-clock pill + pause state. */
    {
        char L[1024]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_OK_FG);
        flux_sb_append(&l, FLUX_BOLD);
        flux_sb_append(&l, "AUTONOMOUS");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "queue-driven runner");
        flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, "  ");
        char pill[96];
        snprintf(pill, sizeof pill,
                 "demo-clock 10\xc3\x97 \xc2\xb7 %02llu:%02llu:%02llu (virtual)",
                 (unsigned long long)((virt_s / 3600ULL) % 24ULL),
                 (unsigned long long)((virt_s /   60ULL) % 60ULL),
                 (unsigned long long)( virt_s           % 60ULL));
        flux_badge(&l, pill, FLUX_THEME_TEXT_INV_FG, FLUX_THEME_OK_FG);
        flux_sb_append(&l, "  ");
        if (paused) flux_badge(&l, "PAUSED",  FLUX_THEME_TEXT_INV_FG, FLUX_THEME_ERR_FG);
        else        flux_badge(&l, "RUNNING", FLUX_THEME_TEXT_INV_FG, FLUX_THEME_ACCENT_FG);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    /* Row 2: live counters. */
    {
        char L[1024]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
        char n[32];
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "Queue: ");
        flux_sb_append(&l, FLUX_RESET);
        snprintf(n, sizeof n, "%d", q);
        flux_sb_append(&l, FLUX_THEME_ACCENT_FG); flux_sb_append(&l, n); flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, " pending \xc2\xb7 ");
        flux_sb_append(&l, FLUX_RESET);
        snprintf(n, sizeof n, "%d", r);
        flux_sb_append(&l, FLUX_THEME_WARN_FG); flux_sb_append(&l, n); flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, " running \xc2\xb7 ");
        flux_sb_append(&l, FLUX_RESET);
        snprintf(n, sizeof n, "%d", done);
        flux_sb_append(&l, FLUX_THEME_OK_FG); flux_sb_append(&l, n); flux_sb_append(&l, FLUX_RESET);
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, " done today");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
}

/* Single-row policy-denial feed entry (width-exact). Cycles 3 messages. */
static void cawd__auto_render_alert_row(FluxSB *sb, int w, int slot,
                                        uint64_t now_ms) {
    static const struct {
        const char *title;
        const char *body;
        FluxKind    kind;
    } feed[] = {
        { "policy.deny", "rm -rf /var/log (AUTO-CRON \xc2\xb7 nightly.sweep) blocked by rule shell.destructive", FLUX_KIND_ERROR   },
        { "policy.deny", "write .env (AUTO-RESOLVE \xc2\xb7 issue #1244) blocked by rule secret.write",          FLUX_KIND_ERROR   },
        { "policy.warn", "rate-limit: AUTO-REVIEW 12/min exceeds soft cap 10/min \xe2\x80\x94 throttling",       FLUX_KIND_WARNING }
    };
    int feed_n = (int)(sizeof feed / sizeof feed[0]);
    int cyc = (int)(((now_ms / 3500ULL) + (uint64_t)slot) % (uint64_t)feed_n);
    const char *icon_clr = (feed[cyc].kind == FLUX_KIND_ERROR)
        ? FLUX_THEME_ERR_FG : FLUX_THEME_WARN_FG;
    const char *icon = (feed[cyc].kind == FLUX_KIND_ERROR)
        ? "\xe2\x9c\x97" : "\xe2\x9a\xa0";

    char L[1024]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, icon_clr);
    flux_sb_append(&l, icon);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_append(&l, feed[cyc].title);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT2_FG);
    flux_sb_append(&l, feed[cyc].body);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__ui_stage_autonomous(CawdApp *a, FluxSB *sb, int w, int h) {
    (void)a;
    if (!sb || w <= 0 || h <= 0) return;

    /* ---- Function-local static state (persists across frames) ---- */
    static float    series_a[CAWD__AUTO_SAMPLES]; /* auto-resolve */
    static float    series_b[CAWD__AUTO_SAMPLES]; /* auto-review  */
    static float    series_c[CAWD__AUTO_SAMPLES]; /* auto-cron    */
    static int      series_seeded = 0;
    static uint64_t last_step_ms  = 0;
    static FluxVirtualList vl;
    static int      vl_ready = 0;
    static int      frame    = 0;
    static int      paused   = 0; /* toggled externally in future waves */

    if (!series_seeded) {
        for (int i = 0; i < CAWD__AUTO_SAMPLES; i++) {
            double t = (double)i * 0.2;
            series_a[i] = 3.0f + 2.2f * (float)sin(t * 0.85);
            series_b[i] = 2.0f + 1.4f * (float)sin(t * 0.55 + 1.1);
            series_c[i] = 1.1f + 0.7f * (float)sin(t * 0.30 + 2.3);
            if (series_a[i] < 0) series_a[i] = 0;
            if (series_b[i] < 0) series_b[i] = 0;
            if (series_c[i] < 0) series_c[i] = 0;
        }
        series_seeded = 1;
    }

    uint64_t now_ms = flux_now_ms();
    frame++;

    cawd__auto_step_throughput(series_a, series_b, series_c, now_ms, &last_step_ms);

    CawdAutoTask *tasks = cawd__auto_task_table();
    int queued_n = 0, running_n = 0, done_today = 0;
    cawd__auto_simulate(tasks, now_ms, &queued_n, &running_n, &done_today);

    /* Row budget: header 2 + chart (up to 5) + alerts 3 + footer 1. */
    const int row_header = 2;
    const int row_alerts = 3;
    const int row_footer = 1;
    int row_chart = 5;
    int row_list  = h - row_header - row_chart - row_alerts - row_footer;
    if (row_list < 3) {
        row_chart = (row_chart > 3) ? 3 : row_chart;
        row_list  = h - row_header - row_chart - row_alerts - row_footer;
    }
    if (row_list < 1 || row_chart < 3 || w < 40) {
        cawd__ui_stub_render(sb, w, h, 2);
        return;
    }

    /* Virtual 10× demo-clock wrapped to a 24 h HH:MM:SS. */
    uint64_t virt_s = (now_ms / 100ULL) % (24ULL * 3600ULL);

    /* 1. Header. */
    cawd__auto_render_header(sb, w, virt_s, paused, queued_n, running_n, done_today);

    /* 2. Throughput chart. */
    {
        FluxSeries s[CAWD__AUTO_SERIES_N] = {
            { series_a, CAWD__AUTO_SAMPLES, FLUX_THEME_OK_FG,           "resolve" },
            { series_b, CAWD__AUTO_SAMPLES, FLUX_THEME_BRAND_PURPLE_FG, "review"  },
            { series_c, CAWD__AUTO_SAMPLES, FLUX_THEME_WARN_FG,         "cron"    }
        };
        FluxLineChartOpts opts; memset(&opts, 0, sizeof opts);
        opts.show_axes = 0;
        opts.title = NULL;
        flux_line_chart_multi(sb, s, CAWD__AUTO_SERIES_N, w, row_chart, &opts);
    }

    /* 3. Task list. */
    if (!vl_ready) {
        flux_vlist_init(&vl, CAWD__AUTO_TASK_COUNT, row_list, cawd__auto_list_item);
        vl_ready = 1;
    }
    vl.visible = row_list;
    flux_vlist_set_count(&vl, CAWD__AUTO_TASK_COUNT);

    CawdAutoListCtx ctx;
    ctx.tasks  = tasks;
    ctx.now_ms = now_ms;
    ctx.frame  = frame;
    flux_vlist_render(&vl, sb, w, &ctx);

    /* 4. Alert feed. */
    for (int i = 0; i < CAWD__AUTO_ALERT_COUNT; i++) {
        cawd__auto_render_alert_row(sb, w, i, now_ms);
    }

    /* 5. Footer kbd hints. */
    {
        char L[1024]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
        flux_sb_append(&l, "  ");
        flux_kbd(&l, "space"); flux_sb_append(&l, " pause  ");
        flux_kbd(&l, "h");     flux_sb_append(&l, " promote to HITL  ");
        flux_kbd(&l, "x");     flux_sb_append(&l, " kill  ");
        flux_kbd(&l, "/");     flux_sb_append(&l, " filter");
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
}

static void cawd__ui_stage_api       (CawdApp *a, FluxSB *sb, int w, int h) { cawd__ui_stage_api_impl(a, sb, w, h); }
/* ---- Orchestra helpers (Wave 3) --------------------------------- */

/* Forward declarations for chrome helpers defined further down in this
 * file (sidebar / dock / stage use them too). The orchestra stage is the
 * first caller textually. */
static void cawd__ui_row_blank(FluxSB *sb, int w);
static void cawd__ui_row_fit  (FluxSB *sb, int w, const char *s);

/* Channel → FluxChannelBadge id (UI-only mapping). */
static FluxChannelId cawd__orch_ch_to_badge(CawdChannel ch) {
    switch (ch) {
    case CAWD_CH_HITL:       return FLUX_CH_HITL;
    case CAWD_CH_TELEGRAM:   return FLUX_CH_TG;
    case CAWD_CH_AUTONOMOUS: return FLUX_CH_AUTO;
    case CAWD_CH_GITHUB:     return FLUX_CH_GH;
    case CAWD_CH_SLACK:      return FLUX_CH_SL;
    case CAWD_CH_API:        return FLUX_CH_API;
    default:                 return FLUX_CH_HITL;
    }
}

/* Agent state → FluxDotState (used by agent-card widget). */
static FluxDotState cawd__orch_state_to_dot(CawdAgentState s) {
    switch (s) {
    case CAWD_STATE_PENDING:   return FLUX_DOT_PENDING;
    case CAWD_STATE_PLANNING:  return FLUX_DOT_PENDING;
    case CAWD_STATE_RUNNING:   return FLUX_DOT_RUNNING;
    case CAWD_STATE_STREAMING: return FLUX_DOT_STREAMING;
    case CAWD_STATE_WAITING:   return FLUX_DOT_PENDING;
    case CAWD_STATE_PAUSED:    return FLUX_DOT_PENDING;
    case CAWD_STATE_DONE:      return FLUX_DOT_COMPLETED;
    case CAWD_STATE_FAILED:    return FLUX_DOT_FAILED;
    case CAWD_STATE_CANCELLED: return FLUX_DOT_CANCELLED;
    default:                   return FLUX_DOT_PENDING;
    }
}

/* Short state label used in the header summary line. */
static int cawd__orch_state_is_running(CawdAgentState s) {
    return s == CAWD_STATE_RUNNING || s == CAWD_STATE_STREAMING ||
           s == CAWD_STATE_PLANNING;
}

/* Snapshot of one orchestra slot (detached copy so we can unlock the
 * agent registry before rendering). String fields point into the
 * registry but those are immutable once set, so this is safe. */
typedef struct {
    int            alive;
    int            used;               /* slot index in registry */
    const char    *name;
    CawdChannel    ch;
    CawdAgentState state;
    const char    *tool;
    int            tokens_out;
    double         cost_usd;
    uint64_t       spawned_ms;
    uint64_t       last_update_ms;
} CawdOrchSnap;

#define CAWD_ORCH_CARDS 6

/* Per-card persistent UI state — sparkline ring + rate gate. Kept
 * function-local static so the stage function remains self-contained. */
typedef struct {
    FluxRate rate;                 /* per-card FPS gate (different Hz each) */
    float    ring[32];             /* token-rate sparkline ring */
    int      ring_head;
    int      last_tokens_out;      /* snapshot from previous tick */
    uint64_t last_sample_ms;
    float    pulse_t;
} CawdOrchCard;

/* Channel accent RGB (matches flux_channel_rgb but keeps gantt rows
 * decoupled from the badge widget implementation). */
static FluxRGB cawd__orch_ch_rgb(CawdChannel ch) {
    return flux_channel_rgb(cawd__orch_ch_to_badge(ch));
}

/* Snapshot the first N alive agents. Locks the registry briefly, copies
 * scalar fields + pointers (strings are stable after spawn). Returns the
 * count of alive agents actually filled. */
static int cawd__orch_snapshot(CawdApp *app, CawdOrchSnap *out, int max) {
    int filled = 0;
    pthread_mutex_lock(&app->agents.mu);
    for (int i = 0; i < CAWD_MAX_AGENTS && filled < max; i++) {
        CawdAgent *ag = &app->agents.slots[i];
        if (!ag->used) continue;
        CawdOrchSnap *s = &out[filled];
        s->alive          = 1;
        s->used           = i;
        s->name           = ag->name;
        s->ch             = ag->current_channel;
        s->state          = ag->state;
        s->tool           = ag->current_tool;
        s->tokens_out     = ag->tokens_out;
        s->cost_usd       = ag->cost_usd;
        s->spawned_ms     = ag->spawned_ms;
        s->last_update_ms = ag->last_update_ms;
        filled++;
    }
    pthread_mutex_unlock(&app->agents.mu);
    /* Zero-initialise the remaining slots so the caller always sees a
     * well-defined alive=0 for empty entries. */
    for (int i = filled; i < max; i++) {
        memset(&out[i], 0, sizeof out[i]);
    }
    return filled;
}

/* Compute the shared timeline window end (now_ms) and start (now - 60s).
 * Also used to seed the "elapsed" field on cards so everything is driven
 * by the same clock. */
#define CAWD_ORCH_WINDOW_MS 60000ULL

/* Render the header strip: "ORCHESTRA — N agents · M running · $X.XX".
 * Emits exactly one row of `w` cells + '\n'. */
static void cawd__orch_header(FluxSB *sb, int w, int alive, int running,
                              double total_cost) {
    char L[384]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, "ORCHESTRA");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "— ");
    char body[192];
    snprintf(body, sizeof body,
             "%d agents · %d running · $%.2f total",
             alive, running, total_cost);
    flux_sb_append(&l, body);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Render the footer: kbd hints. Exactly one row of `w` cells + '\n'. */
static void cawd__orch_footer(FluxSB *sb, int w) {
    char L[512]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, " ");
    flux_kbd(&l, "e"); flux_sb_append(&l, " inspect  ");
    flux_kbd(&l, "x"); flux_sb_append(&l, " kill  ");
    flux_kbd(&l, "p"); flux_sb_append(&l, " pause  ");
    flux_kbd(&l, "f"); flux_sb_append(&l, " fork  ");
    flux_kbd(&l, "/"); flux_sb_append(&l, " filter");
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Render one agent-card into a self-owned string buffer. If `snap` is
 * NULL / not alive, renders a dim "empty slot" placeholder card with the
 * same height so the grid rows still line up. `card_w` must be >= 8;
 * emits exactly FLUX_AGENT_CARD_H rows. */
static void cawd__orch_render_card(char *out, int out_sz,
                                   const CawdOrchSnap *snap,
                                   const CawdOrchCard *card_ui,
                                   int card_w, uint64_t now_ms) {
    FluxSB sb; flux_sb_init(&sb, out, out_sz);
    if (card_w < 12) {
        /* Too narrow for a proper card — emit blank rows of card_w. */
        for (int r = 0; r < FLUX_AGENT_CARD_H; r++) {
            flux_fit(&sb, "", card_w > 0 ? card_w : 1, NULL, FLUX_ALIGN_LEFT);
            flux_sb_nl(&sb);
        }
        return;
    }

    if (!snap || !snap->alive) {
        /* Empty placeholder card — same height, neutral border. */
        FluxAgentCard c;
        memset(&c, 0, sizeof c);
        c.name         = "—";
        c.channel      = FLUX_CH_HITL;       /* neutral */
        c.state        = FLUX_DOT_PENDING;
        c.current_tool = "(empty slot)";
        c.tokens_total = 0;
        c.cost_usd     = 0.0f;
        c.elapsed_ms   = 0;
        c.focused      = 0;
        c.pulse_t      = 0.0f;
        flux_agent_card_render(&c, &sb, card_w);
        return;
    }

    FluxAgentCard c;
    memset(&c, 0, sizeof c);
    c.name         = snap->name ? snap->name : "agent";
    c.channel      = cawd__orch_ch_to_badge(snap->ch);
    c.state        = cawd__orch_state_to_dot(snap->state);
    c.current_tool = snap->tool ? snap->tool : "(idle)";
    c.tokens_total = (long)snap->tokens_out;
    c.cost_usd     = (float)snap->cost_usd;
    c.focused      = 0;
    c.pulse_t      = card_ui ? card_ui->pulse_t : 0.0f;

    uint64_t elapsed = (now_ms > snap->spawned_ms) ? (now_ms - snap->spawned_ms) : 0;
    if (snap->state == CAWD_STATE_DONE ||
        snap->state == CAWD_STATE_FAILED ||
        snap->state == CAWD_STATE_CANCELLED) {
        /* Freeze elapsed at last_update_ms for terminal states. */
        elapsed = (snap->last_update_ms > snap->spawned_ms)
                    ? (snap->last_update_ms - snap->spawned_ms) : 0;
    }
    if (elapsed > (uint64_t)0x7fffffff) elapsed = 0x7fffffff;
    c.elapsed_ms   = (int)elapsed;

    /* Copy sparkline ring. */
    if (card_ui) {
        for (int i = 0; i < 32; i++) c.token_rate_ring[i] = card_ui->ring[i];
        c.token_rate_head = card_ui->ring_head;
    }
    flux_agent_card_render(&c, &sb, card_w);
}

/* -------- the stage renderer -------- */

static void cawd__ui_stage_orchestra(CawdApp *app, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0 || h <= 0) return;

    /* Per-card UI state — owned by the stage across frames. Six static
     * FluxRate gates at 30/20/10/5/2/1 Hz so each spinner visibly ticks
     * at a different cadence (the per-widget-FPS principle made
     * visible). */
    static CawdOrchCard cards[CAWD_ORCH_CARDS];
    static int          cards_inited;
    if (!cards_inited) {
        static const int fps_ladder[CAWD_ORCH_CARDS] = { 30, 20, 10, 5, 2, 1 };
        for (int i = 0; i < CAWD_ORCH_CARDS; i++) {
            flux_rate_init(&cards[i].rate, FLUX_FPS_TO_MS(fps_ladder[i]));
            cards[i].ring_head       = 0;
            cards[i].last_tokens_out = 0;
            cards[i].last_sample_ms  = 0;
            cards[i].pulse_t         = 0.0f;
            for (int j = 0; j < 32; j++) cards[i].ring[j] = 0.0f;
        }
        cards_inited = 1;
    }

    uint64_t now = flux_now_ms();

    /* Snapshot agents from the registry. */
    CawdOrchSnap snaps[CAWD_ORCH_CARDS];
    int alive = cawd__orch_snapshot(app, snaps, CAWD_ORCH_CARDS);

    /* Tick per-card rates & update sparkline rings. We sample regardless
     * of the rate gate but only _advance_ the ring when the gate fires,
     * giving each card a visibly different cadence while keeping the
     * rolling token-rate estimate fresh. */
    int running = 0;
    for (int i = 0; i < CAWD_ORCH_CARDS; i++) {
        CawdOrchCard *c = &cards[i];
        if (snaps[i].alive && cawd__orch_state_is_running(snaps[i].state))
            running++;
        if (flux_rate_due(&c->rate, now)) {
            int delta = 0;
            if (snaps[i].alive) {
                delta = snaps[i].tokens_out - c->last_tokens_out;
                if (delta < 0) delta = 0;
                c->last_tokens_out = snaps[i].tokens_out;
            } else {
                c->last_tokens_out = 0;
            }
            /* Add a tiny sine flutter so the sparkline "breathes" when
             * tokens haven't moved — the token rate is derived from real
             * counters, the flutter merely prevents a flat line. */
            float bump = (float)(((now / 97ULL) + (uint64_t)(i * 11)) % 7);
            float sample = (float)delta + bump * 0.35f;
            c->ring[c->ring_head % 32] = sample;
            c->ring_head = (c->ring_head + 1) % 32;
            c->last_sample_ms = now;
            /* Pulse easing — a slow triangle in [0,1]. */
            uint64_t period = 2000 + (uint64_t)(i * 250);
            float ph = (float)((now + i * 137ULL) % period) / (float)period;
            c->pulse_t = ph < 0.5f ? (ph * 2.0f) : (2.0f - ph * 2.0f);
        }
    }

    /* Aggregate cost from the ledger (sum across channels). */
    CawdCost cost = cawd_cost_snapshot(app, CAWD_CH_ANY);

    int rows_used = 0;

    /* 1. Header ------------------------------------------------------ */
    cawd__orch_header(sb, w, alive, running, cost.usd);
    rows_used++;
    /* spacer */
    cawd__ui_row_blank(sb, w);
    rows_used++;

    /* 2. Decide grid layout based on remaining height. */
    int body_h = h - rows_used - 1; /* reserve 1 row for footer */
    if (body_h < 0) body_h = 0;

    int grid_cols = 3, grid_rows = 2;
    /* Need grid_rows * (FLUX_AGENT_CARD_H + 1 spacer) + gantt(6) + spacer. */
    int need_3x2 = 2 * FLUX_AGENT_CARD_H + 1 /*spacer*/ + 6 /*gantt*/ + 1 /*spacer*/;
    int need_2x2 = 2 * FLUX_AGENT_CARD_H + 1 + 4 + 1;
    int need_1col_all = CAWD_ORCH_CARDS * FLUX_AGENT_CARD_H + 1 + 6 + 1;
    if (body_h < need_3x2) { grid_cols = 2; grid_rows = 2; }
    if (grid_cols == 2 && body_h < need_2x2) { grid_cols = 1; }
    /* For 1-col mode, try to show as many cards as fit vertically. */
    if (grid_cols == 1) {
        int max_cards = (body_h - 2) / FLUX_AGENT_CARD_H;
        if (max_cards < 1) max_cards = 1;
        if (max_cards > CAWD_ORCH_CARDS) max_cards = CAWD_ORCH_CARDS;
        grid_rows = max_cards;
    }
    (void)need_1col_all;
    (void)need_2x2;

    /* 3. Grid of cards --------------------------------------------- */
    int card_gap = 2;
    int card_w   = (w - card_gap * (grid_cols - 1)) / grid_cols;
    if (card_w < 12) card_w = 12;
    if (card_w * grid_cols + card_gap * (grid_cols - 1) > w) {
        /* Last-resort: shrink to fit. */
        card_w = w / grid_cols;
        if (card_w < 8) card_w = 8;
    }
    int total_grid_w = card_w * grid_cols + card_gap * (grid_cols - 1);
    int lpad = (w - total_grid_w) / 2;
    int rpad = w - total_grid_w - lpad;
    if (lpad < 0) lpad = 0;
    if (rpad < 0) rpad = 0;

    char gap_buf[8];
    {
        int i = 0;
        while (i < card_gap && i < (int)(sizeof gap_buf - 1)) { gap_buf[i++] = ' '; }
        gap_buf[i] = 0;
    }

    /* Large scratch buffers for up to 3 panels rendered side-by-side. */
    static char panel_bufs[3][4096];
    const char *panels[3];
    int         panel_widths[3];

    for (int r = 0; r < grid_rows && rows_used < h - 1; r++) {
        for (int c = 0; c < grid_cols; c++) {
            int idx = r * grid_cols + c;
            const CawdOrchSnap *sn = (idx < CAWD_ORCH_CARDS) ? &snaps[idx] : NULL;
            const CawdOrchCard *cu = (idx < CAWD_ORCH_CARDS) ? &cards[idx] : NULL;
            cawd__orch_render_card(panel_bufs[c], sizeof panel_bufs[c],
                                   sn, cu, card_w, now);
            panels[c] = panel_bufs[c];
            panel_widths[c] = card_w;
        }
        /* Left pad before the row, then flux_hbox, then right pad */
        /* We need to emit exactly `w` cells per row. flux_hbox emits
         * card_w * grid_cols + gap * (grid_cols-1) cells per line and
         * FLUX_AGENT_CARD_H lines. We wrap with manual pre/post fill.
         *
         * Strategy: render hbox into a scratch SB, then split by '\n'
         * and pad left+right so each row becomes exactly `w` cells. */
        static char hb_buf[16 * 1024];
        FluxSB hb; flux_sb_init(&hb, hb_buf, sizeof hb_buf);
        flux_hbox(&hb, panels, panel_widths, grid_cols, gap_buf);

        /* Split by newline, pad each line with lpad + rpad spaces. */
        const char *p = flux_sb_str(&hb);
        int emitted = 0;
        while (*p && emitted < FLUX_AGENT_CARD_H && rows_used < h - 1) {
            const char *nl = strchr(p, '\n');
            int len = nl ? (int)(nl - p) : (int)strlen(p);
            for (int i = 0; i < lpad; i++) flux_sb_append(sb, " ");
            /* Emit the hbox row verbatim — flux_hbox already guaranteed
             * total_grid_w display cells. */
            char line_buf[8 * 1024];
            if (len >= (int)sizeof line_buf) len = sizeof line_buf - 1;
            memcpy(line_buf, p, (size_t)len);
            line_buf[len] = 0;
            flux_sb_append(sb, line_buf);
            for (int i = 0; i < rpad; i++) flux_sb_append(sb, " ");
            flux_sb_nl(sb);
            rows_used++;
            emitted++;
            if (!nl) break;
            p = nl + 1;
        }
        /* Inter-row spacer between grid rows (if there's a next row). */
        if (r + 1 < grid_rows && rows_used < h - 1) {
            cawd__ui_row_blank(sb, w);
            rows_used++;
        }
    }

    /* 4. Gantt strip ----------------------------------------------- */
    if (rows_used < h - 1) {
        cawd__ui_row_blank(sb, w);
        rows_used++;
    }

    int gantt_rows = alive > 0 ? alive : 0;
    if (gantt_rows > CAWD_ORCH_CARDS) gantt_rows = CAWD_ORCH_CARDS;
    if (rows_used + gantt_rows > h - 1) {
        gantt_rows = h - 1 - rows_used;
        if (gantt_rows < 0) gantt_rows = 0;
    }

    if (gantt_rows > 0) {
        /* Build the task array inline. Use the snapshot we already have. */
        FluxGanttTask tasks[CAWD_ORCH_CARDS];
        int nt = 0;
        for (int i = 0; i < CAWD_ORCH_CARDS && nt < gantt_rows; i++) {
            if (!snaps[i].alive) continue;
            FluxRGB col = cawd__orch_ch_rgb(snaps[i].ch);
            tasks[nt].label    = snaps[i].name ? snaps[i].name : "agent";
            tasks[nt].color    = col;
            tasks[nt].start_ms = snaps[i].spawned_ms;
            tasks[nt].end_ms   =
                (snaps[i].state == CAWD_STATE_DONE ||
                 snaps[i].state == CAWD_STATE_FAILED ||
                 snaps[i].state == CAWD_STATE_CANCELLED)
                    ? snaps[i].last_update_ms
                    : 0;
            nt++;
        }

        /* flux_gantt_row emits exactly `width` cells per row. Wrap to
         * the full w (no padding needed since width is the canvas). */
        if (nt > 0) {
            flux_gantt_row(sb, tasks, nt, CAWD_ORCH_WINDOW_MS, now, w);
            rows_used += nt;
        } else {
            /* Filler row so the gantt area isn't blank emptiness. */
            char L[256]; FluxSB l; flux_sb_init(&l, L, sizeof L);
            flux_sb_append(&l, " ");
            flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
            flux_sb_append(&l, "(no active spans in last 60s)");
            flux_sb_append(&l, FLUX_RESET);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l));
            rows_used++;
        }
    }

    /* 5. Pad to leave exactly 1 footer row at the bottom. */
    while (rows_used < h - 1) {
        cawd__ui_row_blank(sb, w);
        rows_used++;
    }

    /* 6. Footer keybinds. */
    if (rows_used < h) {
        cawd__orch_footer(sb, w);
        rows_used++;
    }
}
/* -------------------------------------------------------------------------
 * Inspector stage (slot 7) — deep-dive on a single agent (plan §5.2).
 *
 * Three-column workstation modal:
 *   ┌ left 28 ┐ ┌ center flex ┐ ┌ right 36 ┐
 *   │ OP TREE │ │ TRANSCRIPT  │ │  DETAILS │
 *   └─────────┘ └─────────────┘ └──────────┘
 *   [Esc close] [x kill] [f fork] [p pause] [a/r approve/reject]
 *
 * Left   — synthesized FluxOpTree (plan done / current_tool running /
 *          finalize pending) rooted at the agent name.
 * Center — user bubble (seeded from system_prompt as a placeholder since
 *          CawdMessage history isn't retained yet) + assistant bubble
 *          reading stream_buf under stream_mu, with a blinking cursor
 *          glyph appended while stream_open. Fallback flux_placeholder
 *          "awaiting response" when there's no stream yet.
 * Right  — flux_dl with Model / Provider / State / Current tool /
 *          Elapsed / Tokens in / Tokens out / Cost ($) / Origin channel
 *          / Current channel. Cost is pulled from the channel ledger via
 *          cawd_cost_snapshot(); a FluxChannelBadge for origin sits above
 *          the dl.
 *
 * Empty state: no agent used=1 → centered flux_placeholder hint.
 *
 * Narrow terminals (w < 100): fall back to vertical stack of the same
 * three sections so the view never collapses unreadably. Every row
 * emitted honours the width contract (exactly `w` cells + '\n'); all
 * helpers are namespaced cawd__insp_* so they cannot clash with other
 * stages in this same TU.
 * ------------------------------------------------------------------------- */

#define CAWD__INSP_LEFT_W  28
#define CAWD__INSP_RIGHT_W 36
#define CAWD__INSP_GUTTER   2   /* matches default flux_hbox "  " gap   */
#define CAWD__INSP_MIN_HBOX_W 100

/* Pick the first slot whose used==1. Returns index or -1 if none.
 * Registry lock held only during the scan; we copy the index out. */
static int cawd__insp_pick_slot(CawdApp *app) {
    int pick = -1;
    pthread_mutex_lock(&app->agents.mu);
    for (int i = 0; i < CAWD_MAX_AGENTS; i++) {
        if (app->agents.slots[i].used) { pick = i; break; }
    }
    pthread_mutex_unlock(&app->agents.mu);
    return pick;
}

/* Human-readable state label (matches plan terminology). */
static const char *cawd__insp_state_label(CawdAgentState s) {
    switch (s) {
        case CAWD_STATE_PENDING:   return "pending";
        case CAWD_STATE_PLANNING:  return "planning";
        case CAWD_STATE_RUNNING:   return "running";
        case CAWD_STATE_STREAMING: return "streaming";
        case CAWD_STATE_WAITING:   return "waiting approval";
        case CAWD_STATE_PAUSED:    return "paused";
        case CAWD_STATE_DONE:      return "done";
        case CAWD_STATE_FAILED:    return "failed";
        case CAWD_STATE_CANCELLED: return "cancelled";
        default:                   return "?";
    }
}

/* Short channel label (kept local so inspector has no forward-decl
 * dependency on the policy-view helper defined further below). */
static const char *cawd__insp_ch_label(CawdChannel c) {
    switch (c) {
        case CAWD_CH_HITL:       return "HITL";
        case CAWD_CH_TELEGRAM:   return "TG";
        case CAWD_CH_AUTONOMOUS: return "AUTO";
        case CAWD_CH_GITHUB:     return "GH";
        case CAWD_CH_SLACK:      return "SL";
        case CAWD_CH_API:        return "API";
        case CAWD_CH_ANY:        return "ANY";
        default:                 return "?";
    }
}

static FluxBadgeStatus cawd__insp_state_to_badge(CawdAgentState s) {
    switch (s) {
        case CAWD_STATE_STREAMING: return FLUX_BADGE_RUN;
        case CAWD_STATE_DONE:      return FLUX_BADGE_OK;
        case CAWD_STATE_FAILED:
        case CAWD_STATE_CANCELLED: return FLUX_BADGE_ERR;
        case CAWD_STATE_WAITING:   return FLUX_BADGE_WARN;
        default:                   return FLUX_BADGE_IDLE;
    }
}

/* Emit a section header " ◆ LABEL" in exactly `w` cells + '\n'. */
static void cawd__insp_header_row(FluxSB *sb, int w, const char *label) {
    char L[256]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, "\xe2\x97\x86 "); /* ◆ */
    flux_sb_append(&l, label ? label : "");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

/* Render the synthesized op-tree into `sb`. Emits up to `max_rows`
 * rows, each exactly `w` cells + '\n' (header + tree + padding). */
static void cawd__insp_render_optree(FluxSB *sb, const CawdAgent *ag,
                                     int w, int max_rows) {
    if (max_rows <= 0 || w <= 0) return;
    int budget = max_rows;
    cawd__insp_header_row(sb, w, "OP TREE");
    budget--;
    if (budget <= 0) return;

    /* Synthesize four rows: root (agent name) + plan/current/finalize. */
    static FluxOpItem items[4];
    static unsigned char expanded[4];
    static FluxOpTree tree;

    const char *name = (ag && ag->name) ? ag->name : "agent";
    const char *tool = (ag && ag->current_tool) ? ag->current_tool
                                                : "(no tool yet)";
    CawdAgentState st = ag ? ag->state : CAWD_STATE_PENDING;

    items[0].id = "root";  items[0].label = name;
    items[0].detail = NULL; items[0].depth = 0;
    items[0].has_children = 1; items[0].duration_ms = 0;
    items[0].status = (st == CAWD_STATE_DONE)      ? FLUX_OP_COMPLETED
                    : (st == CAWD_STATE_FAILED ||
                       st == CAWD_STATE_CANCELLED) ? FLUX_OP_FAILED
                    : FLUX_OP_RUNNING;

    items[1].id = "plan";  items[1].label = "plan";
    items[1].detail = NULL; items[1].depth = 1;
    items[1].has_children = 0; items[1].duration_ms = 0;
    items[1].status = FLUX_OP_COMPLETED;

    items[2].id = "current"; items[2].label = tool;
    items[2].detail = NULL; items[2].depth = 1;
    items[2].has_children = 0;
    items[2].duration_ms = (ag && ag->last_update_ms)
        ? (int)(flux_now_ms() - ag->last_update_ms) : 0;
    if (items[2].duration_ms < 0) items[2].duration_ms = 0;
    items[2].status = (st == CAWD_STATE_DONE)      ? FLUX_OP_COMPLETED
                    : (st == CAWD_STATE_FAILED)    ? FLUX_OP_FAILED
                    : (st == CAWD_STATE_CANCELLED) ? FLUX_OP_CANCELLED
                    : (st == CAWD_STATE_PAUSED)    ? FLUX_OP_PENDING
                                                   : FLUX_OP_RUNNING;

    items[3].id = "finalize"; items[3].label = "finalize";
    items[3].detail = NULL; items[3].depth = 1;
    items[3].has_children = 0; items[3].duration_ms = 0;
    items[3].status = (st == CAWD_STATE_DONE)   ? FLUX_OP_COMPLETED
                    : (st == CAWD_STATE_FAILED) ? FLUX_OP_FAILED
                                                : FLUX_OP_PENDING;

    flux_op_tree_init(&tree, items, expanded, 4);
    tree.spinner_frame = (int)(flux_now_ms() / 80);

    char TB[2048]; FluxSB tb; flux_sb_init(&tb, TB, (int)sizeof TB);
    flux_op_tree_render(&tree, &tb, w);
    int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&tb), budget);
    budget -= emitted;

    while (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }
}

/* Emit a user-bubble + assistant-bubble transcript into `sb`. Reads
 * stream_copy (already snapshotted under stream_mu by caller) and
 * appends a blinking cursor glyph while stream_open. Falls back to a
 * centered flux_placeholder when stream is empty and not open. Every
 * row is exactly `w` cells + '\n'; emits at most `max_rows`. */
static void cawd__insp_render_transcript(FluxSB *sb, const CawdAgent *ag,
                                         const char *stream_copy,
                                         int w, int max_rows) {
    if (max_rows <= 0 || w <= 0) return;
    int budget = max_rows;
    cawd__insp_header_row(sb, w, "TRANSCRIPT");
    budget--;
    if (budget <= 0) return;

    /* ---- User bubble (seeded from system_prompt). ---- */
    if (budget > 0) {
        const char *text = (ag && ag->system_prompt)
            ? ag->system_prompt
            : (ag && ag->name ? ag->name : "(no prompt)");
        char ts[16];
        uint64_t age = ag ? (flux_now_ms() - ag->spawned_ms) : 0;
        if (age < 60000) snprintf(ts, sizeof ts, "%us", (unsigned)(age / 1000));
        else             snprintf(ts, sizeof ts, "%um", (unsigned)(age / 60000));
        static char UBUF[2048];
        FluxSB ubb; flux_sb_init(&ubb, UBUF, (int)sizeof UBUF);
        flux_message_bubble(&ubb, FLUX_ROLE_USER, text, ts, w);
        int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&ubb), budget);
        budget -= emitted;
    }

    if (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }

    /* ---- Assistant bubble (stream + cursor), or placeholder. ---- */
    int stream_open = ag ? ag->stream_open : 0;
    size_t sn = stream_copy ? strlen(stream_copy) : 0;

    if (sn == 0 && !stream_open) {
        if (budget >= 3) {
            flux_placeholder(sb,
                             "\xe2\x80\xa6", /* … */
                             "awaiting response",
                             "no tokens streamed yet",
                             w);
            budget -= 3;
        } else {
            char L[128]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
            flux_sb_append(&l, "  ");
            flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
            flux_sb_append(&l, "awaiting response\xe2\x80\xa6");
            flux_sb_append(&l, FLUX_RESET);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l));
            budget--;
        }
    } else {
        static char BODY[CAWD_STREAM_BUF + 8];
        size_t cap = sizeof BODY;
        size_t cut = sn;
        if (cut >= cap - 4) cut = cap - 4;
        if (cut > 0 && stream_copy) memcpy(BODY, stream_copy, cut);
        BODY[cut] = '\0';
        if (stream_open) {
            const char *g = cawd__hitl_cursor_glyph();
            size_t gl = strlen(g);
            if (cut + gl < cap) {
                memcpy(BODY + cut, g, gl);
                BODY[cut + gl] = '\0';
            }
        }

        static char ABUF[CAWD_STREAM_BUF + 2048];
        FluxSB abb; flux_sb_init(&abb, ABUF, (int)sizeof ABUF);
        CawdAgentState st = ag ? ag->state : CAWD_STATE_PENDING;
        const char *ts = (st == CAWD_STATE_STREAMING) ? "live"
                       : (st == CAWD_STATE_DONE)      ? "done"
                       : (st == CAWD_STATE_FAILED)    ? "fail"
                                                      : "idle";
        flux_message_bubble(&abb, FLUX_ROLE_ASSISTANT, BODY, ts, w);
        int emitted = cawd__hitl_emit_block(sb, flux_sb_str(&abb), budget);
        budget -= emitted;
    }

    while (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }
}

/* Emit the right-hand details column. */
static void cawd__insp_render_details(FluxSB *sb, CawdApp *app,
                                      const CawdAgent *ag,
                                      int w, int max_rows) {
    if (max_rows <= 0 || w <= 0) return;
    int budget = max_rows;
    cawd__insp_header_row(sb, w, "DETAILS");
    budget--;
    if (budget <= 0) return;

    /* Origin channel badge — gives the column a hue hit. */
    if (budget > 0) {
        FluxChannelId cid = cawd__orch_ch_to_badge(ag ? ag->origin : CAWD_CH_HITL);
        FluxBadgeStatus bs = cawd__insp_state_to_badge(ag ? ag->state
                                                          : CAWD_STATE_PENDING);
        int badge_w = w - 2;
        if (badge_w < 8) badge_w = 8;
        char BB[512]; FluxSB bb; flux_sb_init(&bb, BB, (int)sizeof BB);
        flux_sb_append(&bb, " ");
        {
            char BADGE[256]; FluxSB bdg;
            flux_sb_init(&bdg, BADGE, (int)sizeof BADGE);
            flux_channel_badge(&bdg, cid, bs, badge_w);
            char *s = (char *)flux_sb_str(&bdg);
            size_t ln = strlen(s);
            if (ln > 0 && s[ln - 1] == '\n') s[ln - 1] = '\0';
            flux_sb_append(&bb, s);
        }
        cawd__ui_row_fit(sb, w, flux_sb_str(&bb));
        budget--;
    }

    if (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }

    /* Snapshot cost for the agent's origin channel. */
    CawdCost cost = {0};
    if (app && ag) cost = cawd_cost_snapshot(app, ag->origin);
    (void)cost; /* aggregated cost not surfaced — we print per-agent. */

    /* Formatted fields. All buffers are stack-local per field. */
    char f_model[96], f_prov[96], f_state[64], f_tool[128];
    char f_elapsed[32], f_tin[32], f_tout[32], f_cost[32];
    char f_origin[32], f_current[32];

    snprintf(f_model,   sizeof f_model,   "%s",
             (ag && ag->model)    ? ag->model    : "—");
    snprintf(f_prov,    sizeof f_prov,    "%s",
             (ag && ag->provider) ? ag->provider : "—");
    snprintf(f_state,   sizeof f_state,   "%s",
             cawd__insp_state_label(ag ? ag->state : CAWD_STATE_PENDING));
    snprintf(f_tool,    sizeof f_tool,    "%s",
             (ag && ag->current_tool) ? ag->current_tool : "—");

    if (ag && ag->spawned_ms) {
        uint64_t now = flux_now_ms();
        int ms = (int)(now - ag->spawned_ms);
        if (ms < 0) ms = 0;
        flux_activity_format_duration(ms, f_elapsed, (int)sizeof f_elapsed);
        if (f_elapsed[0] == '\0') snprintf(f_elapsed, sizeof f_elapsed, "0ms");
    } else {
        snprintf(f_elapsed, sizeof f_elapsed, "—");
    }

    snprintf(f_tin,     sizeof f_tin,     "%d", ag ? ag->tokens_in  : 0);
    snprintf(f_tout,    sizeof f_tout,    "%d", ag ? ag->tokens_out : 0);
    snprintf(f_cost,    sizeof f_cost,    "$%.4f",
             ag ? ag->cost_usd : 0.0);
    snprintf(f_origin,  sizeof f_origin,  "%s",
             cawd__insp_ch_label(ag ? ag->origin : CAWD_CH_HITL));
    snprintf(f_current, sizeof f_current, "%s",
             cawd__insp_ch_label(ag ? ag->current_channel : CAWD_CH_HITL));

    FluxDLItem items[10] = {
        { "model",    f_model   },
        { "provider", f_prov    },
        { "state",    f_state   },
        { "tool",     f_tool    },
        { "elapsed",  f_elapsed },
        { "tokens in",  f_tin   },
        { "tokens out", f_tout  },
        { "cost",     f_cost    },
        { "origin",   f_origin  },
        { "channel",  f_current },
    };
    int n = 10;

    FluxDLOpts opts;
    memset(&opts, 0, sizeof opts);
    opts.term_color     = FLUX_THEME_TEXT_OFF_FG;
    opts.layout         = 0;
    opts.separator_line = 0;

    if (budget >= n) {
        flux_dl(sb, items, n, w, &opts);
        budget -= n;
    } else {
        /* Degrade gracefully: one-liner rows we control the height of. */
        for (int k = 0; k < n && budget > 0; k++) {
            char LB[256]; FluxSB l; flux_sb_init(&l, LB, (int)sizeof LB);
            flux_sb_appendf(&l, " %s%-10s%s %s",
                            FLUX_THEME_TEXT_OFF_FG, items[k].term,
                            FLUX_RESET, items[k].def);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l));
            budget--;
        }
    }

    while (budget > 0) { cawd__ui_row_blank(sb, w); budget--; }
}

/* Footer keybind row. Emits exactly one row of `w` cells + '\n'. */
static void cawd__insp_footer(FluxSB *sb, int w) {
    char L[512]; FluxSB l; flux_sb_init(&l, L, (int)sizeof L);
    flux_sb_append(&l, " ");
    flux_kbd(&l, "Esc");   flux_sb_append(&l, " close  ");
    flux_kbd(&l, "x");     flux_sb_append(&l, " kill  ");
    flux_kbd(&l, "f");     flux_sb_append(&l, " fork  ");
    flux_kbd(&l, "p");     flux_sb_append(&l, " pause  ");
    flux_kbd(&l, "a/r");   flux_sb_append(&l, " approve/reject");
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__ui_stage_inspector (CawdApp *a, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0 || h <= 0) return;

    /* ---- Empty state. ---- */
    int slot = cawd__insp_pick_slot(a);
    if (slot < 0) {
        int content = 3;
        int top    = (h - content) / 2; if (top    < 0) top    = 0;
        int bottom = h - content - top; if (bottom < 0) bottom = 0;
        for (int i = 0; i < top; i++) cawd__ui_row_blank(sb, w);
        if (h >= content) {
            flux_placeholder(sb,
                             CAWD__UI_ICONS[7],
                             "no agents",
                             "spawn one by typing in the HITL composer",
                             w);
        } else {
            for (int i = 0; i < h; i++) cawd__ui_row_blank(sb, w);
            return;
        }
        for (int i = 0; i < bottom; i++) cawd__ui_row_blank(sb, w);
        return;
    }

    /* ---- Snapshot the agent + stream under stream_mu. ---- */
    CawdAgent ag;
    static char stream_copy[CAWD_STREAM_BUF + 1];
    if (cawd__hitl_snapshot(a, slot, &ag, stream_copy,
                            sizeof stream_copy) < 0) {
        for (int i = 0; i < h; i++) cawd__ui_row_blank(sb, w);
        return;
    }

    int rows_used = 0;
    int footer_reserve = 1;
    int body_rows = h - footer_reserve;
    if (body_rows < 1) body_rows = h; /* too short for a footer */

    /* ---- Wide layout: three-column hbox. ---- */
    if (w >= CAWD__INSP_MIN_HBOX_W) {
        int left_w  = CAWD__INSP_LEFT_W;
        int right_w = CAWD__INSP_RIGHT_W;
        int gutter  = CAWD__INSP_GUTTER;
        int center_w = w - left_w - right_w - gutter * 2;
        if (center_w < 30) {
            /* Shrink columns proportionally. */
            left_w  = (w - 30 - gutter * 2) * 4 / 10;
            right_w = (w - 30 - gutter * 2) - left_w;
            if (left_w  < 20) left_w  = 20;
            if (right_w < 24) right_w = 24;
            center_w = w - left_w - right_w - gutter * 2;
        }
        if (center_w < 20) {
            /* Really tight — fall through to vertical. */
            goto vertical;
        }

        static char LBUF[6144];
        static char CBUF[6144];
        static char RBUF[6144];
        FluxSB lb, cb, rb;
        flux_sb_init(&lb, LBUF, (int)sizeof LBUF);
        flux_sb_init(&cb, CBUF, (int)sizeof CBUF);
        flux_sb_init(&rb, RBUF, (int)sizeof RBUF);

        cawd__insp_render_optree   (&lb,    &ag,          left_w,   body_rows);
        cawd__insp_render_transcript(&cb,   &ag, stream_copy, center_w, body_rows);
        cawd__insp_render_details  (&rb, a, &ag,          right_w,  body_rows);

        const char *panels[3] = {
            flux_sb_str(&lb), flux_sb_str(&cb), flux_sb_str(&rb)
        };
        int widths[3] = { left_w, center_w, right_w };
        flux_hbox(sb, panels, widths, 3, "  ");
        rows_used += body_rows;
    } else {
vertical: {
        /* ---- Narrow: stack vertically, op-tree / transcript / details. ---- */
        int rem = body_rows;
        int r_opt = rem / 4;                 if (r_opt < 5) r_opt = (rem < 5 ? rem : 5);
        int r_det = rem / 3;                 if (r_det < 6) r_det = (rem < 6 ? rem : 6);
        if (r_opt + r_det >= rem - 3) {
            r_opt = rem / 4; if (r_opt < 3) r_opt = 3;
            r_det = rem / 3; if (r_det < 4) r_det = 4;
        }
        int r_tr  = rem - r_opt - r_det;
        if (r_tr < 3) { r_tr = 3; r_det = rem - r_opt - r_tr; }
        if (r_det < 0) r_det = 0;

        cawd__insp_render_optree   (sb,    &ag,             w, r_opt);
        rows_used += r_opt;
        cawd__insp_render_transcript(sb,   &ag, stream_copy, w, r_tr);
        rows_used += r_tr;
        cawd__insp_render_details  (sb, a, &ag,             w, r_det);
        rows_used += r_det;
    }
    }

    /* ---- Pad to h-1 then footer. ---- */
    while (rows_used < h - 1) { cawd__ui_row_blank(sb, w); rows_used++; }
    if (rows_used < h) { cawd__insp_footer(sb, w); rows_used++; }
}

/* -------------------------------------------------------------------------
 * Policies stage (slot 8) — live consent-rule editor / tree.
 * Helpers are namespaced cawd__pol_ui_* so they do not collide with the
 * internal policy engine helpers (cawd__pol_*). All render helpers honour
 * the width contract: they emit exactly `w` display cells per row.
 * Read-only best-effort scan of the rules array (no manual locking).
 * ------------------------------------------------------------------------- */

static FluxChannelId cawd__pol_ui_fluxch(CawdChannel c) {
    switch (c) {
        case CAWD_CH_HITL:       return FLUX_CH_HITL;
        case CAWD_CH_TELEGRAM:   return FLUX_CH_TG;
        case CAWD_CH_AUTONOMOUS: return FLUX_CH_AUTO;
        case CAWD_CH_GITHUB:     return FLUX_CH_GH;
        case CAWD_CH_SLACK:      return FLUX_CH_SL;
        case CAWD_CH_API:        return FLUX_CH_API;
        default:                 return FLUX_CH_HITL;
    }
}

static const char *cawd__pol_ui_scope_label(CawdChannel c) {
    switch (c) {
        case CAWD_CH_HITL:       return "HITL";
        case CAWD_CH_TELEGRAM:   return "TG";
        case CAWD_CH_AUTONOMOUS: return "AUTO";
        case CAWD_CH_GITHUB:     return "GH";
        case CAWD_CH_SLACK:      return "SL";
        case CAWD_CH_API:        return "API";
        case CAWD_CH_ANY:        return "ANY";
        default:                 return "?";
    }
}

static const char *cawd__pol_ui_verdict_label(CawdVerdict v) {
    switch (v) {
        case CAWD_ALLOW: return "ALLOW";
        case CAWD_DENY:  return "DENY";
        case CAWD_ASK:   return "ASK";
    }
    return "?";
}

static const char *cawd__pol_ui_verdict_color(CawdVerdict v) {
    switch (v) {
        case CAWD_ALLOW: return FLUX_THEME_OK_FG;
        case CAWD_DENY:  return FLUX_THEME_ERR_FG;
        case CAWD_ASK:   return FLUX_THEME_WARN_FG;
    }
    return FLUX_THEME_TEXT_DIM_FG;
}

static void cawd__pol_ui_verdict_badge_inline(FluxSB *l, CawdVerdict v) {
    flux_sb_append(l, cawd__pol_ui_verdict_color(v));
    flux_sb_append(l, FLUX_BOLD);
    flux_sb_append(l, cawd__pol_ui_verdict_label(v));
    flux_sb_append(l, FLUX_RESET);
}

static void cawd__pol_ui_scope_chip_inline(FluxSB *l, CawdChannel ch) {
    const char *label = cawd__pol_ui_scope_label(ch);
    if (ch == CAWD_CH_ANY) {
        flux_sb_append(l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_appendf(l, "[%s]", label);
        flux_sb_append(l, FLUX_RESET);
        return;
    }
    FluxRGB c = flux_channel_rgb(cawd__pol_ui_fluxch(ch));
    flux_sb_appendf(l, "\x1b[38;2;%u;%u;%um[%s]\x1b[0m",
                    (unsigned)c.r, (unsigned)c.g, (unsigned)c.b, label);
}

static void cawd__pol_ui_trunc(char *out, int out_sz,
                                const char *s, int max_chars) {
    if (!out || out_sz <= 0) return;
    if (!s) { out[0] = 0; return; }
    int n = (int)strlen(s);
    if (n <= max_chars) { snprintf(out, out_sz, "%s", s); return; }
    if (max_chars <= 1) { snprintf(out, out_sz, "%.*s", max_chars, s); return; }
    snprintf(out, out_sz, "%.*s\xe2\x80\xa6", max_chars - 1, s);  /* … */
}

static void cawd__pol_ui_rule_row(FluxSB *sb, int w,
                                   const CawdPolicyRule *r, int selected) {
    char LB[512]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
    flux_sb_append(&l, " ");
    if (selected) {
        flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, "\xe2\x96\xb6 ");    /* ▶ */
        flux_sb_append(&l, FLUX_RESET);
    } else {
        flux_sb_append(&l, "  ");
    }
    cawd__pol_ui_verdict_badge_inline(&l, r->verdict);
    flux_sb_append(&l, " ");
    cawd__pol_ui_scope_chip_inline(&l, r->scope);
    flux_sb_append(&l, " ");
    int pat_room = w - 24;
    if (pat_room < 8)  pat_room = 8;
    if (pat_room > 48) pat_room = 48;
    char trunc[160];
    cawd__pol_ui_trunc(trunc, sizeof trunc, r->match, pat_room);
    flux_sb_append(&l, FLUX_THEME_TEXT_FG);
    flux_sb_appendf(&l, "`%s`", trunc);
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_appendf(&l, "%dhit%s", r->hits, r->hits == 1 ? "" : "s");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__pol_ui_section_header(FluxSB *sb, int w, const char *t) {
    char LB[256]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
    flux_sb_append(&l, FLUX_BOLD);
    flux_sb_append(&l, "\xe2\x97\x86 ");        /* ◆ */
    flux_sb_append(&l, t);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__pol_ui_render_left(CawdApp *app, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0 || h <= 0) return;
    int rows = 0;

    int n_active = 0;
    for (int i = 0; i < CAWD_MAX_POLICY_RULES; i++)
        if (app->policy.rules[i].used) n_active++;

    char hdr[96];
    snprintf(hdr, sizeof hdr, "RULES (%d active)", n_active);
    cawd__pol_ui_section_header(sb, w, hdr); rows++;

    uint64_t now = cawd__now_ms();
    if (rows < h && app->policy.bypass_until_ms > now) {
        uint64_t rem = (app->policy.bypass_until_ms - now) / 1000ull;
        char msg[96];
        snprintf(msg, sizeof msg,
                 "BYPASS ACTIVE for %llus more",
                 (unsigned long long)rem);
        if (rows + 3 <= h) {
            flux_alert(sb, FLUX_KIND_WARNING, "bypass-all", msg, w);
            rows += 3;
        } else {
            char LB[256]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
            flux_sb_append(&l, " ");
            flux_sb_append(&l, FLUX_THEME_WARN_FG);
            flux_sb_append(&l, "\xe2\x9a\xa0 ");     /* ⚠ */
            flux_sb_append(&l, msg);
            flux_sb_append(&l, FLUX_RESET);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
        }
    }

    if (rows < h) { cawd__ui_row_blank(sb, w); rows++; }

    if (n_active == 0 && rows < h) {
        char LB[256]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        flux_sb_append(&l, "(no rules yet \xe2\x80\x94 press [+] to add one)");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
    }

    static const CawdChannel ch_order[] = {
        CAWD_CH_HITL, CAWD_CH_TELEGRAM, CAWD_CH_AUTONOMOUS,
        CAWD_CH_GITHUB, CAWD_CH_SLACK, CAWD_CH_API, CAWD_CH_ANY
    };

    int selected_slot = -1;
    for (int i = 0; i < CAWD_MAX_POLICY_RULES && selected_slot < 0; i++)
        if (app->policy.rules[i].used) selected_slot = i;

    int printed_any = 0;
    for (size_t g = 0;
         g < sizeof ch_order / sizeof ch_order[0] && rows < h; g++) {
        CawdChannel ch = ch_order[g];
        int group_count = 0;
        for (int i = 0; i < CAWD_MAX_POLICY_RULES; i++)
            if (app->policy.rules[i].used &&
                app->policy.rules[i].scope == ch) group_count++;
        if (group_count == 0) continue;

        if (printed_any && rows < h) { cawd__ui_row_blank(sb, w); rows++; }
        if (rows < h) {
            char LB[160]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
            flux_sb_append(&l, "  ");
            flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
            flux_sb_append(&l, "scope ");
            flux_sb_append(&l, FLUX_RESET);
            cawd__pol_ui_scope_chip_inline(&l, ch);
            flux_sb_appendf(&l, " %s(%d)%s",
                            FLUX_THEME_TEXT_DIM_FG, group_count, FLUX_RESET);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
        }
        for (int i = 0; i < CAWD_MAX_POLICY_RULES && rows < h; i++) {
            if (!app->policy.rules[i].used) continue;
            if (app->policy.rules[i].scope != ch) continue;
            cawd__pol_ui_rule_row(sb, w, &app->policy.rules[i],
                                  i == selected_slot);
            rows++;
        }
        printed_any = 1;
    }

    while (rows < h) { cawd__ui_row_blank(sb, w); rows++; }
}

static void cawd__pol_ui_render_right(CawdApp *app, FluxSB *sb, int w, int h) {
    if (!sb || w <= 0 || h <= 0) return;
    int rows = 0;

    cawd__pol_ui_section_header(sb, w, "RULE EDITOR"); rows++;
    if (rows < h) { cawd__ui_row_blank(sb, w); rows++; }

    const CawdPolicyRule *sel = NULL;
    for (int i = 0; i < CAWD_MAX_POLICY_RULES; i++) {
        if (app->policy.rules[i].used) { sel = &app->policy.rules[i]; break; }
    }

    if (!sel) {
        if (rows < h) {
            char LB[160]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
            flux_sb_append(&l, "  ");
            flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
            flux_sb_append(&l, "no rule selected");
            flux_sb_append(&l, FLUX_RESET);
            cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
        }
    } else {
        char pat_buf[200];
        snprintf(pat_buf, sizeof pat_buf, "%s`%s`%s",
                 FLUX_THEME_TEXT_FG, sel->match, FLUX_RESET);

        char verdict_buf[128];
        snprintf(verdict_buf, sizeof verdict_buf, "%s%s%s%s",
                 cawd__pol_ui_verdict_color(sel->verdict), FLUX_BOLD,
                 cawd__pol_ui_verdict_label(sel->verdict), FLUX_RESET);

        char scope_buf[128];
        {
            char LB[128]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
            cawd__pol_ui_scope_chip_inline(&l, sel->scope);
            snprintf(scope_buf, sizeof scope_buf, "%s", flux_sb_str(&l));
        }

        char hits_buf[64];
        snprintf(hits_buf, sizeof hits_buf, "%d", sel->hits);

        const char *type_str = sel->once ? "once (one-shot)"
                              : sel->session_until_ms ? "session"
                              : "permanent";

        FluxDLItem items[] = {
            { "pattern", pat_buf },
            { "verdict", verdict_buf },
            { "scope",   scope_buf },
            { "hits",    hits_buf },
            { "type",    type_str },
        };
        int n = (int)(sizeof items / sizeof items[0]);
        FluxDLOpts opts;
        memset(&opts, 0, sizeof opts);
        opts.term_color     = FLUX_THEME_TEXT_OFF_FG;
        opts.layout         = 0;
        opts.separator_line = 0;
        if (rows + n <= h) {
            flux_dl(sb, items, n, w, &opts);
            rows += n;
        } else {
            for (int k = 0; k < n && rows < h; k++) {
                char LB[256]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
                flux_sb_appendf(&l, " %s%-8s%s %s",
                                FLUX_THEME_TEXT_OFF_FG, items[k].term,
                                FLUX_RESET, items[k].def);
                cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
            }
        }
    }

    if (rows < h) { cawd__ui_row_blank(sb, w); rows++; }

    if (rows < h) {
        char LB[512]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
        flux_sb_append(&l, "  ");
        flux_kbd(&l, "a"); flux_sb_append(&l, " allow  ");
        flux_kbd(&l, "d"); flux_sb_append(&l, " deny  ");
        flux_kbd(&l, "?"); flux_sb_append(&l, " ask  ");
        flux_kbd(&l, "x"); flux_sb_append(&l, " del  ");
        flux_kbd(&l, "o"); flux_sb_append(&l, " once  ");
        flux_kbd(&l, "s"); flux_sb_append(&l, " session  ");
        flux_kbd(&l, "b"); flux_sb_append(&l, " bypass");
        cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
    }

    if (rows < h) { cawd__ui_row_blank(sb, w); rows++; }

    if (rows < h) {
        char LB[160]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, "preview \xe2\x80\x94 sample call");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
    }
    if (rows < h) {
        char LB[256]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
        flux_sb_append(&l, "    ");
        flux_sb_append(&l, FLUX_THEME_TEXT_FG);
        flux_sb_append(&l, "bash:git push origin main");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
    }
    if (rows < h) {
        CawdToolCall tc;
        memset(&tc, 0, sizeof tc);
        tc.origin    = CAWD_CH_AUTONOMOUS;
        tc.tool      = "bash";
        tc.args_json = "git push origin main";
        CawdVerdict v = cawd_policy_check(app, tc);
        char LB[256]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
        flux_sb_append(&l, "    ");
        flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
        flux_sb_append(&l, "AUTO \xe2\x86\x92 ");    /* → */
        flux_sb_append(&l, FLUX_RESET);
        cawd__pol_ui_verdict_badge_inline(&l, v);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l)); rows++;
    }

    while (rows < h) { cawd__ui_row_blank(sb, w); rows++; }
}

static void cawd__pol_ui_render_footer(FluxSB *sb, int w) {
    char LB[512]; FluxSB l; flux_sb_init(&l, LB, sizeof LB);
    flux_sb_append(&l, " ");
    flux_kbd(&l, "+");   flux_sb_append(&l, " add rule  ");
    flux_kbd(&l, "Tab"); flux_sb_append(&l, " switch pane  ");
    flux_kbd(&l, "Esc"); flux_sb_append(&l, " back");
    cawd__ui_row_fit(sb, w, flux_sb_str(&l));
}

static void cawd__ui_stage_policies(CawdApp *a, FluxSB *sb, int w, int h) {
    if (!a || !sb || w <= 0 || h <= 0) {
        cawd__ui_stub_render(sb, w, h, 8);
        return;
    }

    int have_footer = (h > 1);
    int body_h = have_footer ? (h - 1) : h;

    if (w < 100) {
        /* Narrow: stack vertically — left top, right below. */
        int left_h  = body_h / 2;
        int right_h = body_h - left_h;
        if (left_h  < 1) left_h  = 1;
        if (right_h < 0) right_h = 0;
        cawd__pol_ui_render_left (a, sb, w, left_h);
        cawd__pol_ui_render_right(a, sb, w, right_h);
    } else {
        /* Wide: left 40% / right 60%, 2-cell gap. */
        int gap = 2;
        int avail = w - gap;
        if (avail < 20) avail = 20;
        int left_w  = (avail * 40) / 100;
        if (left_w < 16) left_w = 16;
        int right_w = avail - left_w;
        if (right_w < 20) {
            right_w = 20;
            left_w  = avail - right_w;
            if (left_w < 16) left_w = 16;
        }
        char LB[16384]; FluxSB lb; flux_sb_init(&lb, LB, sizeof LB);
        char RB[16384]; FluxSB rb; flux_sb_init(&rb, RB, sizeof RB);
        cawd__pol_ui_render_left (a, &lb, left_w,  body_h);
        cawd__pol_ui_render_right(a, &rb, right_w, body_h);

        const char *panels[2];
        int widths[2];
        panels[0] = flux_sb_str(&lb); widths[0] = left_w;
        panels[1] = flux_sb_str(&rb); widths[1] = right_w;
        flux_hbox(sb, panels, widths, 2, "  ");
    }

    if (have_footer) cawd__pol_ui_render_footer(sb, w);
}
static void cawd__ui_stage_analytics (CawdApp *a, FluxSB *sb, int w, int h) { (void)a; cawd__ui_stub_render(sb, w, h, 10); }
static void cawd__ui_stage_settings  (CawdApp *a, FluxSB *sb, int w, int h) { (void)a; cawd__ui_stub_render(sb, w, h, 11); }
static void cawd__ui_stage_help      (CawdApp *a, FluxSB *sb, int w, int h) { (void)a; cawd__ui_stub_render(sb, w, h, 12); }

static void cawd__shim_init  (CawdApp *a) { (void)a; }
static void cawd__shim_tick  (CawdApp *a, uint64_t n) { (void)a; (void)n; }
static int  cawd__shim_update(CawdApp *a, FluxMsg m) { (void)a; (void)m; return 0; }

/* Minimal toast-emitting update handlers per tab — every footer-advertised
 * key fires an observable state change. Real state mutation on these tabs
 * ships in subsequent iterations; today the skeleton demonstrates the
 * full input → handler → feedback loop. */
static int cawd__auto_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_AUTONOMOUS || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, " ") || flux_key_is(m, "space")) { cawd_toast(a, CAWD_KIND_INFO, "Autonomous", "paused (press space to resume)"); return 1; }
    if (flux_key_is(m, "h"))     { cawd_toast(a, CAWD_KIND_INFO, "Promoted", "selected task moved to HITL"); return 1; }
    if (flux_key_is(m, "x"))     { cawd_toast(a, CAWD_KIND_WARN, "Killed",   "selected task cancelled"); return 1; }
    return 0;
}
static int cawd__orch_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_ORCHESTRA || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, "e"))     { cawd_toast(a, CAWD_KIND_INFO, "Inspect", "deep-dive open"); a->ui.tabs.active = CAWD_UI_SLOT_INSPECTOR; return 1; }
    if (flux_key_is(m, "x"))     { cawd_toast(a, CAWD_KIND_WARN, "Killed",  "selected agent cancelled"); return 1; }
    if (flux_key_is(m, "p"))     { cawd_toast(a, CAWD_KIND_INFO, "Paused",  "selected agent paused"); return 1; }
    if (flux_key_is(m, "f"))     { cawd_toast(a, CAWD_KIND_INFO, "Forked",  "agent duplicated"); return 1; }
    return 0;
}
static int cawd__api_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_API || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, "enter")) { cawd_toast(a, CAWD_KIND_INFO, "Request", "opened in detail pane"); return 1; }
    if (flux_key_is(m, "r"))     { cawd_toast(a, CAWD_KIND_INFO, "API log", "cleared"); return 1; }
    return 0;
}
static int cawd__slack_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_SLACK || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, "r"))     { cawd_toast(a, CAWD_KIND_INFO, "Reply", "replying in thread"); return 1; }
    return 0;
}
static int cawd__audit_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_AUDIT || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, "e"))     { cawd_toast(a, CAWD_KIND_INFO, "Export", "audit log exported to /tmp/audit.jsonl"); return 1; }
    return 0;
}
static int cawd__pol_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_POLICIES || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, "a")) { cawd_toast(a, CAWD_KIND_SUCCESS, "Allow", "rule verdict set to ALLOW"); return 1; }
    if (flux_key_is(m, "d")) { cawd_toast(a, CAWD_KIND_ERROR,   "Deny",  "rule verdict set to DENY"); return 1; }
    if (flux_key_is(m, "b")) { cawd_policy_bypass_all(a, 60); cawd_toast(a, CAWD_KIND_WARN, "Bypass", "all policies bypassed for 60s"); return 1; }
    if (flux_key_is(m, "o")) { cawd_toast(a, CAWD_KIND_INFO, "Override", "once-override set"); return 1; }
    if (flux_key_is(m, "s")) { cawd_toast(a, CAWD_KIND_INFO, "Session override", "rule pinned for this session"); return 1; }
    if (flux_key_is(m, "x")) { cawd_toast(a, CAWD_KIND_WARN, "Deleted", "rule removed"); return 1; }
    return 0;
}
static int cawd__settings_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_SETTINGS || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, " ") || flux_key_is(m, "space")) { cawd_toast(a, CAWD_KIND_INFO, "Toggled", "channel enable flipped"); return 1; }
    if (flux_key_is(m, "enter")) { cawd_toast(a, CAWD_KIND_INFO, "Edit",    "opening provider editor"); return 1; }
    return 0;
}
static int cawd__insp_tab_update(CawdApp *a, FluxMsg m) {
    if (a->ui.tabs.active != CAWD_UI_SLOT_INSPECTOR || m.type != MSG_KEY) return 0;
    if (flux_key_is(m, "x")) { cawd_toast(a, CAWD_KIND_WARN, "Killed", "agent cancelled"); return 1; }
    if (flux_key_is(m, "f")) { cawd_toast(a, CAWD_KIND_INFO, "Forked", "agent duplicated"); return 1; }
    if (flux_key_is(m, "p")) { cawd_toast(a, CAWD_KIND_INFO, "Paused", "agent paused"); return 1; }
    return 0;
}

static const CawdTabVtbl CAWD__UI_VTBL[CAWD_UI_SLOT_COUNT] = {
    { cawd__shim_init, cawd__shim_tick, cawd__shim_update, cawd__ui_stage_hitl       },
    { cawd__tg_tab_init, cawd__tg_tab_tick, cawd__tg_tab_update, cawd__tg_tab_render },
    { cawd__shim_init, cawd__shim_tick, cawd__auto_tab_update, cawd__ui_stage_autonomous },
    { cawd__gh_tab_init, cawd__gh_tab_tick, cawd__gh_tab_update, cawd__gh_tab_render },
    { cawd__shim_init, cawd__shim_tick, cawd__slack_tab_update, cawd__ui_stage_slack },
    { cawd__shim_init, cawd__shim_tick, cawd__api_tab_update, cawd__ui_stage_api     },
    { cawd__shim_init, cawd__shim_tick, cawd__orch_tab_update, cawd__ui_stage_orchestra },
    { cawd__shim_init, cawd__shim_tick, cawd__insp_tab_update, cawd__ui_stage_inspector },
    { cawd__shim_init, cawd__shim_tick, cawd__pol_tab_update, cawd__ui_stage_policies },
    { cawd__shim_init, cawd__shim_tick, cawd__audit_tab_update, cawd__ui_stage_audit },
    { cawd__shim_init, cawd__shim_tick, cawd__shim_update, cawd__ui_stage_analytics  },
    { cawd__shim_init, cawd__shim_tick, cawd__settings_tab_update, cawd__ui_stage_settings },
    { cawd__shim_init, cawd__shim_tick, cawd__shim_update, cawd__ui_stage_help       },
};
static void cawd__ui_kill_dead_code(void) {
    (void)cawd__gh_render_header; (void)cawd__gh_render_footer;
    (void)cawd__tg_render_bubble; (void)cawd__tg_render_inline_kb;
    (void)cawd__tg_count;
}

/* ---- Command palette items ----- */

static FluxCmdItem CAWD__PALETTE_ITEMS[] = {
    { "jump.hitl",       "Jump to HITL",       "interactive chat",         "Channels", "1",      0 },
    { "jump.telegram",   "Jump to Telegram",   "remote user via bot",      "Channels", "2",      0 },
    { "jump.autonomous", "Jump to Autonomous", "queue / cron / webhooks",  "Channels", "3",      0 },
    { "jump.github",     "Jump to GitHub",     "PR / issue events",        "Channels", "4",      0 },
    { "jump.slack",      "Jump to Slack",      "slash / mentions",         "Channels", "5",      0 },
    { "jump.api",        "Jump to API",        "inbound HTTP requests",    "Channels", "6",      0 },
    { "jump.orchestra",  "Jump to Orchestra",  "cross-channel agent grid", "Views",    "7",      0 },
    { "jump.inspector",  "Jump to Inspector",  "deep-dive agent modal",    "Views",    "8",      0 },
    { "jump.policies",   "Jump to Policies",   "consent rule editor",      "Views",    "9",      0 },
    { "jump.audit",      "Jump to Audit",      "immutable event log",      "Views",    "0",      0 },
    { "jump.analytics",  "Jump to Analytics",  "cross-channel stats",      "Views",    NULL,     0 },
    { "jump.settings",   "Jump to Settings",   "providers / API keys",     "Views",    NULL,     0 },
    { "jump.help",       "Jump to Help",       "keybindings & credits",    "Views",    "?",      0 },
    { "app.quit",        "Quit",               "confirm before exiting",   "App",      "Ctrl+C", 0 },
};
#define CAWD__PALETTE_N (int)(sizeof CAWD__PALETTE_ITEMS / sizeof CAWD__PALETTE_ITEMS[0])

static int cawd__palette_id_to_slot(const char *id) {
    if (!id) return -1;
    if (strcmp(id, "jump.hitl")       == 0) return CAWD_UI_SLOT_HITL;
    if (strcmp(id, "jump.telegram")   == 0) return CAWD_UI_SLOT_TELEGRAM;
    if (strcmp(id, "jump.autonomous") == 0) return CAWD_UI_SLOT_AUTONOMOUS;
    if (strcmp(id, "jump.github")     == 0) return CAWD_UI_SLOT_GITHUB;
    if (strcmp(id, "jump.slack")      == 0) return CAWD_UI_SLOT_SLACK;
    if (strcmp(id, "jump.api")        == 0) return CAWD_UI_SLOT_API;
    if (strcmp(id, "jump.orchestra")  == 0) return CAWD_UI_SLOT_ORCHESTRA;
    if (strcmp(id, "jump.inspector")  == 0) return CAWD_UI_SLOT_INSPECTOR;
    if (strcmp(id, "jump.policies")   == 0) return CAWD_UI_SLOT_POLICIES;
    if (strcmp(id, "jump.audit")      == 0) return CAWD_UI_SLOT_AUDIT;
    if (strcmp(id, "jump.analytics")  == 0) return CAWD_UI_SLOT_ANALYTICS;
    if (strcmp(id, "jump.settings")   == 0) return CAWD_UI_SLOT_SETTINGS;
    if (strcmp(id, "jump.help")       == 0) return CAWD_UI_SLOT_HELP;
    return -1;
}

/* ---- Chrome helpers ----- */

static int cawd__ui_inner_w_for_terminal(int cols) {
    int w = cols - 8;
    if (w > 200) w = 200;
    if (w <  40) w =  40;
    return w;
}

static void cawd__ui_row_blank(FluxSB *sb, int w) {
    for (int i = 0; i < w; i++) flux_sb_append(sb, " ");
    flux_sb_append(sb, "\n");
}

static void cawd__ui_row_fit(FluxSB *sb, int w, const char *s) {
    flux_fit(sb, s ? s : "", w, NULL, FLUX_ALIGN_LEFT);
    flux_sb_append(sb, "\n");
}

static void cawd__ui_desktop_row(FluxSB *sb, int cols) {
    flux_sb_append(sb, FLUX_THEME_WINDOW_BG);
    for (int j = 0; j < cols; j++) flux_sb_append(sb, " ");
    flux_sb_append(sb, FLUX_RESET);
    flux_sb_append(sb, "\n");
}

static void cawd__ui_render_ticker(CawdApp *app, FluxSB *sb, int inner_w) {
    (void)app;
    char L[512]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, "ticker ·");
    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
    flux_sb_append(&l, "cross-channel events will stream here");
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, inner_w, flux_sb_str(&l));
}

static void cawd__ui_render_sidebar(CawdApp *app, FluxSB *sb, int w, int h, int active) {
    (void)app;
    if (w <= 0 || h <= 0) return;
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
        flux_sb_append(&l, "OPENCLAW");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    cawd__ui_row_blank(sb, w);

    int rows_used = 2;
    for (int i = 0; i < CAWD_UI_SLOT_COUNT && rows_used < h; i++) {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        if (i == active) {
            flux_sb_append(&l, FLUX_THEME_ACCENT_FG);
            flux_sb_append(&l, "\xe2\x97\x86 ");
        } else {
            flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
            flux_sb_append(&l, "  ");
        }
        flux_sb_append(&l, CAWD__UI_LABELS[i]);
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
        rows_used++;
    }
    while (rows_used < h) { cawd__ui_row_blank(sb, w); rows_used++; }
}

static void cawd__ui_render_dock(CawdApp *app, FluxSB *sb, int w, int h) {
    if (w <= 0 || h <= 0) return;
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_BRAND_PURPLE_FG);
        flux_sb_append(&l, "TELEMETRY");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    cawd__ui_row_blank(sb, w);
    {
        char L[512]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, " ");
        flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG);
        CawdCost c = cawd_cost_snapshot(app, CAWD_CH_ANY);
        char summary[128];
        snprintf(summary, sizeof summary,
                 "%.0f in · %.0f out · $%.4f · %d req",
                 c.tokens_in, c.tokens_out, c.usd, c.requests);
        flux_sb_append(&l, summary);
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(sb, w, flux_sb_str(&l));
    }
    int rows_used = 3;
    while (rows_used < h) { cawd__ui_row_blank(sb, w); rows_used++; }
}

static void cawd__ui_render_main(CawdApp *app, FluxSB *sb, int w, int h, int active) {
    if (active < 0 || active >= CAWD_UI_SLOT_COUNT) {
        cawd__ui_stub_render(sb, w, h, 0);
        return;
    }
    FluxSB content;
    flux_sb_init(&content, app->ui.tab_content, (int)sizeof app->ui.tab_content);
    CAWD__UI_VTBL[active].render(app, &content, w, h);
    flux_scrollview_render(&app->ui.scroll[active], sb,
                           flux_sb_str(&content), w, h);
    (void)cawd__ui_kill_dead_code;
}

static void cawd__ui_render_three_region(CawdApp *app, FluxSB *sb,
                                         int inner_w, int viewport_h, int active) {
    int left_w = 28, right_w = 36;
    if (inner_w < 120) { left_w = 20; right_w = 24; }
    if (inner_w <  90) { left_w = 16; right_w = 18; }
    int main_w = inner_w - left_w - right_w - 4;
    if (main_w < 20) {
        left_w = 14; right_w = 0;
        main_w = inner_w - left_w - 2;
        if (main_w < 10) main_w = 10;
    }
    static char LB[64 * 1024];  FluxSB lb; flux_sb_init(&lb, LB, sizeof LB);
    static char MB[128 * 1024]; FluxSB mb; flux_sb_init(&mb, MB, sizeof MB);
    static char RB[64 * 1024];  FluxSB rb; flux_sb_init(&rb, RB, sizeof RB);

    cawd__ui_render_sidebar(app, &lb, left_w, viewport_h, active);
    cawd__ui_render_main   (app, &mb, main_w, viewport_h, active);
    if (right_w > 0) cawd__ui_render_dock(app, &rb, right_w, viewport_h);

    if (right_w > 0) {
        const char *panels[3] = {
            flux_sb_str(&lb), flux_sb_str(&mb), flux_sb_str(&rb)
        };
        const int   widths[3] = { left_w, main_w, right_w };
        flux_hbox(sb, panels, widths, 3, " ");
    } else {
        const char *panels[2] = { flux_sb_str(&lb), flux_sb_str(&mb) };
        const int   widths[2] = { left_w, main_w };
        flux_hbox(sb, panels, widths, 2, " ");
    }
}

static void cawd__ui_render_composer(CawdApp *app, FluxSB *sb, int inner_w) {
    flux_composer_layout(&app->ui.composer, inner_w);
    flux_composer_render(&app->ui.composer, sb, inner_w);
}

static void cawd__ui_render_footer(CawdApp *app, FluxSB *sb, int inner_w, int active) {
    (void)app;
    char L[2048]; FluxSB l;
    flux_sb_init(&l, L, sizeof L);
    flux_sb_append(&l, "  ");
    flux_kbd(&l, "Ctrl+K");   flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG); flux_sb_append(&l, "palette"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "1-6");      flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG); flux_sb_append(&l, "channel"); flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "7-0");      flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG); flux_sb_append(&l, "view");    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "Tab");      flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG); flux_sb_append(&l, "cycle");   flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "?");        flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG); flux_sb_append(&l, "help");    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_kbd(&l, "Ctrl+C");   flux_sb_append(&l, " ");
    flux_sb_append(&l, FLUX_THEME_TEXT_DIM_FG); flux_sb_append(&l, "quit");    flux_sb_append(&l, FLUX_RESET);
    flux_sb_append(&l, "   ");
    flux_sb_append(&l, FLUX_THEME_TEXT_OFF_FG);
    flux_sb_append(&l, "· ");
    flux_sb_append(&l, CAWD__UI_LABELS[active]);
    flux_sb_append(&l, FLUX_RESET);
    cawd__ui_row_fit(sb, inner_w, flux_sb_str(&l));
}

/* ---- FluxModel adapter ----- */

static FluxCmd cawd__ui_model_init(FluxModel *m) {
    CawdApp *app = (CawdApp *)m->state;
    app->ui.start_ms = cawd__now_ms();
    /* Pump the cawd step machinery alongside the UI tick. */
    return FLUX_TICK(8);
}

static FluxCmd cawd__ui_model_update(FluxModel *m, FluxMsg msg) {
    CawdApp *app = (CawdApp *)m->state;

    /* Tick: drain any pending cawd events, route to every slot, re-arm. */
    if (msg.type == MSG_TICK) {
        /* Drain events so handlers get dispatched to UI / workers.     */
        CawdEvent ev;
        while (cawd__ring_pop(&app->ring, &ev) == 0) {
            switch (ev.kind) {
            case CAWD_EV_MESSAGE:          cawd__dispatch_message(app, &ev); break;
            case CAWD_EV_TOOL_CALL:        cawd__dispatch_tool   (app, &ev); break;
            case CAWD_EV_APPROVAL_REQUEST: cawd__dispatch_approval(app, &ev); break;
            case CAWD_EV_APPROVAL_RESPONSE: {
                uint32_t t = ev.u.approval_response.ticket;
                for (int i = 0; i < 32; i++) {
                    pthread_mutex_lock(&app->pending_approvals[i].mu);
                    if (app->pending_approvals[i].used && app->pending_approvals[i].ticket == t) {
                        app->pending_approvals[i].decision = ev.u.approval_response.decision;
                        app->pending_approvals[i].used = 0;
                        pthread_cond_signal(&app->pending_approvals[i].cv);
                    }
                    pthread_mutex_unlock(&app->pending_approvals[i].mu);
                }
            } break;
            case CAWD_EV_TOKEN:       cawd__dispatch_token(app, &ev); break;
            case CAWD_EV_AGENT_STATE: cawd__dispatch_state(app, &ev); break;
            case CAWD_EV_ERROR:       cawd__dispatch_error(app, &ev); break;
            case CAWD_EV_QUIT:        app->running = 0; return FLUX_CMD_QUIT;
            case CAWD_EV_TICKER:      /* UI ticker widget ready later */ break;
            case CAWD_EV_TOAST: {
                FluxToastKind fk = FLUX_TOAST_INFO;
                switch (ev.u.toast.kind) {
                    case CAWD_KIND_SUCCESS: fk = FLUX_TOAST_OK;   break;
                    case CAWD_KIND_WARN:    fk = FLUX_TOAST_WARN; break;
                    case CAWD_KIND_ERROR:   fk = FLUX_TOAST_ERR;  break;
                    default:                fk = FLUX_TOAST_INFO; break;
                }
                flux_toast_center_push(&app->ui.toasts, fk,
                                       ev.u.toast.title ? ev.u.toast.title : "",
                                       ev.u.toast.body  ? ev.u.toast.body  : "",
                                       3000);
            } break;
            case CAWD_EV_FOCUS_CHANNEL:
                if ((int)ev.channel >= 0 && (int)ev.channel < CAWD_CH__COUNT)
                    app->ui.tabs.active = (int)ev.channel;
                break;
            default: break;
            }
        }
        cawd__ring_drain_wake(&app->ring);
        app->ui.tick_total++;
        uint64_t tnow = cawd__now_ms();
        flux_toast_center_tick(&app->ui.toasts, tnow);
        for (int ti = 0; ti < CAWD_UI_SLOT_COUNT; ti++) {
            CAWD__UI_VTBL[ti].tick(app, tnow);
        }
        return FLUX_TICK(8);
    }

    /* Helper: open the quit dialog. */
    #define CAWD__OPEN_QUIT_DIALOG()                                       \
        flux_dialog_open(&app->ui.quit_dialog, (FluxDialogCfg){            \
            .title = "Quit?",                                              \
            .body  = "All channels will stop. Unsaved drafts are kept.",  \
            .n_buttons = 2,                                                \
            .buttons[0] = { "Quit", FLUX_DIALOG_BTN_DANGER,  "q" },       \
            .buttons[1] = { "Stay", FLUX_DIALOG_BTN_DEFAULT, "s" },       \
            .default_idx = 1,                                              \
            .dim_backdrop = 1,                                             \
        })

    /* Quit dialog consumes all keys while open. */
    if (app->ui.quit_dialog.open) {
        flux_dialog_update(&app->ui.quit_dialog, msg);
        if (app->ui.quit_dialog.answered) {
            int r = app->ui.quit_dialog.result;
            app->ui.quit_dialog.answered = 0;
            if (r == 0) { app->running = 0; return FLUX_CMD_QUIT; }
            /* Stay or Esc — just close the dialog. */
        }
        return FLUX_CMD_NONE;
    }

    /* Command palette consumes keys while open. */
    if (app->ui.palette.is_open && msg.type == MSG_KEY) {
        if (flux_key_is(msg, "ctrl+c") || flux_key_is(msg, "C-c")) {
            app->ui.palette.is_open = 0;
            CAWD__OPEN_QUIT_DIALOG();
            return FLUX_CMD_NONE;
        }
        flux_command_palette_update(&app->ui.palette, msg);
        if (app->ui.palette.activated) {
            int slot = cawd__palette_id_to_slot(app->ui.palette.selected_id);
            if (slot >= 0 && slot < CAWD_UI_SLOT_COUNT) app->ui.tabs.active = slot;
            if (app->ui.palette.selected_id &&
                strcmp(app->ui.palette.selected_id, "app.quit") == 0) {
                CAWD__OPEN_QUIT_DIALOG();
            }
            app->ui.palette.activated   = 0;
            app->ui.palette.selected_id = NULL;
        }
        return FLUX_CMD_NONE;
    }

    if (msg.type == MSG_KEY) {
        if (flux_key_is(msg, "ctrl+c") || flux_key_is(msg, "C-c")) {
            CAWD__OPEN_QUIT_DIALOG();
            return FLUX_CMD_NONE;
        }
        if (flux_key_is(msg, "ctrl+k")) {
            app->ui.palette.is_open = 1;
            app->ui.palette.activated = 0;
            app->ui.palette.selected_id = NULL;
            return FLUX_CMD_NONE;
        }
        if (flux_key_is(msg, "?")) { app->ui.tabs.active = CAWD_UI_SLOT_HELP; return FLUX_CMD_NONE; }

        /* Tab navigation (digits + Tab) always wins — tabs own all other keys. */
        const char *k = msg.u.key.key;
        if (k[0] >= '1' && k[0] <= '9' && k[1] == '\0') {
            int idx = k[0] - '1';
            if (idx < CAWD_UI_SLOT_COUNT) app->ui.tabs.active = idx;
            return FLUX_CMD_NONE;
        }
        if (k[0] == '0' && k[1] == '\0') {
            if (app->ui.tabs.active < CAWD_UI_SLOT_AUDIT ||
                app->ui.tabs.active > CAWD_UI_SLOT_HELP) {
                app->ui.tabs.active = CAWD_UI_SLOT_AUDIT;
            } else {
                app->ui.tabs.active++;
                if (app->ui.tabs.active > CAWD_UI_SLOT_HELP)
                    app->ui.tabs.active = CAWD_UI_SLOT_AUDIT;
            }
            return FLUX_CMD_NONE;
        }
        if (flux_key_is(msg, "tab")) {
            app->ui.tabs.active = (app->ui.tabs.active + 1) % CAWD_UI_SLOT_COUNT;
            return FLUX_CMD_NONE;
        }
        if (flux_key_is(msg, "shift+tab") || flux_key_is(msg, "S-tab") ||
            flux_key_is(msg, "btab")) {
            app->ui.tabs.active = (app->ui.tabs.active - 1 + CAWD_UI_SLOT_COUNT) % CAWD_UI_SLOT_COUNT;
            return FLUX_CMD_NONE;
        }
    }

    int active = app->ui.tabs.active;

    /* AppBar:
     *  - direct shortcuts (item.shortcut) match anywhere → fires action
     *  - focused mode (Down handoff): bar owns ←/→/Enter/Up/Esc until
     *    user returns. Composer focus is synced when bar releases focus.
     */
    if (msg.type == MSG_KEY) {
        int appbar_was_focused = app->ui.appbar.has_focus;
        if (flux_appbar_update(&app->ui.appbar, msg)) {
            if (appbar_was_focused && !app->ui.appbar.has_focus) {
                app->ui.composer_focused = 1;
                flux_composer_focus(&app->ui.composer, 1);
            }
            return FLUX_CMD_NONE;
        }
    }

    /* Composer pre-empts ONLY edit keys (Left/Right/Backspace/Home/End,
     * Enter when there's content) so caret movement and submit work
     * even on tabs that use Up/Down for cursor list. Letter keys keep
     * going to the active tab so per-tab shortcuts still work. */
    flux_composer_focus(&app->ui.composer, app->ui.composer_focused);
    if (msg.type == MSG_PASTE) {
        flux_composer_update(&app->ui.composer, msg);
        return FLUX_CMD_NONE;
    }
    if (msg.type == MSG_KEY && app->ui.composer_focused) {
        int composer_edit =
            flux_key_is(msg, "left")      ||
            flux_key_is(msg, "right")     ||
            flux_key_is(msg, "backspace") ||
            flux_key_is(msg, "home")      ||
            flux_key_is(msg, "end")       ||
            flux_key_is(msg, "down");          /* descend handoff */
        /* Enter only when the composer has content — otherwise it's
         * usable by the tab. */
        if (flux_key_is(msg, "enter") &&
            (app->ui.composer.text_len > 0 || app->ui.composer.nsegs > 0))
            composer_edit = 1;
        if (composer_edit) {
            flux_composer_update(&app->ui.composer, msg);
            if (flux_composer_descend_pending(&app->ui.composer)) {
                flux_composer_clear_descend(&app->ui.composer);
                app->ui.composer_focused = 0;
                flux_composer_focus(&app->ui.composer, 0);
                flux_appbar_set_focus(&app->ui.appbar, 1);
                return FLUX_CMD_NONE;
            }
            const char *sent = flux_composer_consume(&app->ui.composer);
            if (sent && *sent) {
                CawdEvent ev = {0};
                ev.kind = CAWD_EV_MESSAGE;
                ev.channel = CAWD_CH_HITL;
                ev.ts_ms = cawd__now_ms();
                ev.u.msg.ch = CAWD_CH_HITL;
                ev.u.msg.text = cawd__strdup(&app->strs, sent);
                ev.u.msg.ts_ms = ev.ts_ms;
                cawd__ring_push(&app->ring, &ev);
            }
            return FLUX_CMD_NONE;
        }
    }

    /* Tab module gets letter keys + arrows it cares about. */
    if (msg.type == MSG_KEY && active >= 0 && active < CAWD_UI_SLOT_COUNT &&
        CAWD__UI_VTBL[active].update(app, msg)) {
        return FLUX_CMD_NONE;
    }

    /* Composer fallback: anything the tab didn't consume — printable
     * characters end up here so typing-into-the-composer always works
     * even on tabs without text-input handlers. */
    if (msg.type == MSG_KEY && app->ui.composer_focused &&
        msg.u.key.rune >= 0x20 && msg.u.key.rune < 0x7f) {
        flux_composer_update(&app->ui.composer, msg);
        const char *sent = flux_composer_consume(&app->ui.composer);
        if (sent && *sent) {
            CawdEvent ev = {0};
            ev.kind = CAWD_EV_MESSAGE;
            ev.channel = CAWD_CH_HITL;
            ev.ts_ms = cawd__now_ms();
            ev.u.msg.ch = CAWD_CH_HITL;
            ev.u.msg.text = cawd__strdup(&app->strs, sent);
            ev.u.msg.ts_ms = ev.ts_ms;
            cawd__ring_push(&app->ring, &ev);
        }
        return FLUX_CMD_NONE;
    }

    flux_tabbar_update(&app->ui.tabs, msg);
    flux_scroll_update(&app->ui.scroll[active], msg);

    const char *sent = flux_composer_consume(&app->ui.composer);
    if (sent && *sent) {
        CawdEvent ev = {0};
        ev.kind = CAWD_EV_MESSAGE;
        ev.channel = CAWD_CH_HITL;
        ev.ts_ms = cawd__now_ms();
        ev.u.msg.ch = CAWD_CH_HITL;
        ev.u.msg.text = cawd__strdup(&app->strs, sent);
        ev.u.msg.ts_ms = ev.ts_ms;
        cawd__ring_push(&app->ring, &ev);
    }
    return FLUX_CMD_NONE;
}

static void cawd__ui_model_view(FluxModel *m, char *buf, int sz) {
    CawdApp *app = (CawdApp *)m->state;
    int W = flux_cols(), H = flux_rows();

    FluxSB out; flux_sb_init(&out, buf, sz);

    enum { CAWD_UI_MIN_COLS = 60, CAWD_UI_MIN_ROWS = 20 };
    if (W < CAWD_UI_MIN_COLS || H < CAWD_UI_MIN_ROWS) {
        char msg[96];
        snprintf(msg, sizeof msg,
                 "terminal too small: %dx%d (need %dx%d) — resize to continue",
                 W, H, (int)CAWD_UI_MIN_COLS, (int)CAWD_UI_MIN_ROWS);
        int mw = (int)strlen(msg);
        int pad_top = (H - 1) / 2;
        for (int r = 0; r < H; r++) {
            if (r == pad_top) {
                int lpad = W > mw ? (W - mw) / 2 : 0;
                for (int i = 0; i < lpad; i++) flux_sb_append(&out, " ");
                flux_sb_append(&out, FLUX_THEME_WARN_FG);
                int mp = 0;
                for (const char *p = msg; *p && lpad + mp < W; p++, mp++) {
                    char c[2] = { *p, 0 };
                    flux_sb_append(&out, c);
                }
                flux_sb_append(&out, FLUX_RESET);
                int rpad = W - lpad - mp;
                for (int i = 0; i < rpad; i++) flux_sb_append(&out, " ");
            } else {
                for (int i = 0; i < W; i++) flux_sb_append(&out, " ");
            }
            if (r < H - 1) flux_sb_append(&out, "\n");
        }
        return;
    }

    /* Palette is the only legacy full-screen overlay still using its
     * own paint code; quit dialog now uses FluxDialog (cell-diff). */
    if (app->ui.palette.is_open) {
        flux_command_palette_render(&app->ui.palette, &out, W, H);
        return;
    }

    int inner_w  = cawd__ui_inner_w_for_terminal(W);
    int chrome_w = inner_w + 2;
    int left_pad = (W - chrome_w) / 2;
    if (left_pad < 0) left_pad = 0;
    int right_pad = W - left_pad - chrome_w;
    if (right_pad < 0) right_pad = 0;

    /* Pre-layout the composer so we know its requested rows and can
     * reserve them in the chrome — the composer auto-grows 1..max_rows
     * as the user types or pastes. */
    flux_composer_layout(&app->ui.composer, inner_w);
    int composer_rows = app->ui.composer.visible_rows;
    if (composer_rows < 1) composer_rows = 1;

    /* fixed inner rows: tabs (1) + divider (1) + ticker (1) + spacer (1)
     * + composer (variable) + appbar-divider (1) + appbar (1) +
     * footer (1) + scroll-indicator (1) + bottom (1) */
    int fixed_inner_rows = 9 + composer_rows;
    int viewport_h = H - (4 + fixed_inner_rows + 4);
    if (viewport_h < 6)  viewport_h = 6;
    if (viewport_h > H - 4) viewport_h = H - 4;
    int active = app->ui.tabs.active;

    int chrome_rows_total = 4 + fixed_inner_rows + viewport_h;
    int top_pad = (H - chrome_rows_total) / 2;
    if (top_pad < 1) top_pad = 1;
    int tab_screen_y = top_pad + 3 + 1;
    int tab_screen_x_origin = left_pad + 2;

    static char ibuf[262144];
    FluxSB inner; flux_sb_init(&inner, ibuf, sizeof ibuf);

    flux_tabbar_render(&app->ui.tabs, &inner, inner_w,
                       tab_screen_x_origin, tab_screen_y);

    {
        char L[2048]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_DIVIDER_FG);
        for (int i = 0; i < inner_w - 4; i++) flux_sb_append(&l, "\xe2\x94\x80");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(&inner, inner_w, flux_sb_str(&l));
    }

    cawd__ui_render_ticker(app, &inner, inner_w);
    cawd__ui_row_blank(&inner, inner_w);

    cawd__ui_render_three_region(app, &inner, inner_w, viewport_h, active);

    {
        char L[2048]; FluxSB l;
        flux_sb_init(&l, L, sizeof L);
        flux_sb_append(&l, "  ");
        flux_sb_append(&l, FLUX_THEME_DIVIDER_FG);
        for (int i = 0; i < inner_w - 4; i++) flux_sb_append(&l, "\xe2\x94\x80");
        flux_sb_append(&l, FLUX_RESET);
        cawd__ui_row_fit(&inner, inner_w, flux_sb_str(&l));
    }

    cawd__ui_render_composer(app, &inner, inner_w);
    /* AppBar — Claude-Code-style status row directly below composer. */
    {
        char L[2048]; FluxSB l; flux_sb_init(&l, L, sizeof L);
        flux_appbar_render(&app->ui.appbar, &l, inner_w, cawd__now_ms());
        cawd__ui_row_fit(&inner, inner_w, flux_sb_str(&l));
    }
    cawd__ui_render_footer(app, &inner, inner_w, active);
    flux_scroll_indicator(&app->ui.scroll[active], &inner, inner_w);

    static char framebuf[262144];
    flux_window_chrome(framebuf, sizeof framebuf,
                       flux_sb_str(&inner),
                       CAWD__UI_TITLES[active],
                       inner_w, NULL);

    int chrome_rows = 0;
    for (const char *p = framebuf; *p; p++) if (*p == '\n') chrome_rows++;
    top_pad = (H - chrome_rows) / 2;
    if (top_pad < 1) top_pad = 1;
    app->ui.tabs.y = top_pad + 3 + 1;

    for (int i = 0; i < top_pad; i++) cawd__ui_desktop_row(&out, W);

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
    for (int i = 0; i < bottom_pad - 1; i++) cawd__ui_desktop_row(&out, W);

    /* Overlay invariant: the cell-diff renderer splits the frame buffer
     * into one entry per "\n" and only emits clines[0..H-1]. Anything
     * appended AFTER the final newline lands in clines[N] where N == #
     * of newlines in the buffer. If chrome+padding already produced H
     * newlines (small terminals where bottom_pad <= 0), CUP-positioned
     * overlay bytes (toasts/dialogs) fall into clines[H] and get
     * dropped. Trim the trailing newline so subsequent overlay output
     * gets folded into the last visible line. */
    if (out.len > 0 && out.buf[out.len - 1] == '\n') {
        int nl = 0;
        for (int i = 0; i < out.len; i++) if (out.buf[i] == '\n') nl++;
        if (nl >= H) {
            out.len--;
            out.buf[out.len] = 0;
        }
    }

    /* Adapt toast position to current terminal size every frame —
     * tight terminals get smaller safe-area + narrower toast so toasts
     * stay visible instead of being squeezed out. Resize-friendly. */
    {
        FluxToastConfig tcfg;
        memset(&tcfg, 0, sizeof tcfg);
        tcfg.anchor      = FLUX_TOAST_POS_BOTTOM_RIGHT;
        tcfg.max_stack   = 4;
        if (H < 24) {
            /* Tight: hover toast directly over composer/footer briefly. */
            tcfg.safe_bottom = 1;
            tcfg.safe_top    = 1;
            tcfg.safe_left   = 2;
            tcfg.safe_right  = 2;
            tcfg.width_cells = W > 30 ? W - 6 : W - 4;
            if (tcfg.width_cells > 50) tcfg.width_cells = 50;
            tcfg.max_stack   = 1;
        } else if (H < 32) {
            tcfg.safe_bottom = 3;
            tcfg.safe_top    = 2;
            tcfg.safe_left   = 3;
            tcfg.safe_right  = 3;
            tcfg.width_pct   = 0.40f;
            tcfg.max_stack   = 2;
        } else {
            tcfg.safe_bottom = 5;
            tcfg.safe_top    = 3;
            tcfg.safe_left   = 4;
            tcfg.safe_right  = 4;
            tcfg.width_pct   = 0.30f;
        }
        flux_toast_center_configure(&app->ui.toasts, tcfg);
    }
    flux_toast_center_render(&app->ui.toasts, &out, W, H);
    /* Quit dialog as a cell-diff overlay (it'll self-clear when closed). */
    flux_dialog_render(&app->ui.quit_dialog, &out, W, H);
    /* palette handled earlier via full-screen takeover */
}

/* ---- AppBar callback shims ----- */
static int  cawd__appbar_bypass_active(void *ctx) {
    CawdApp *app = (CawdApp *)ctx;
    if (!app) return 0;
    pthread_mutex_lock(&app->policy.mu);
    int on = app->policy.bypass_until_ms > cawd__now_ms();
    pthread_mutex_unlock(&app->policy.mu);
    return on;
}
static void cawd__appbar_toggle_bypass(void *ctx) {
    CawdApp *app = (CawdApp *)ctx;
    if (!app) return;
    pthread_mutex_lock(&app->policy.mu);
    int on = app->policy.bypass_until_ms > cawd__now_ms();
    pthread_mutex_unlock(&app->policy.mu);
    if (on) {
        pthread_mutex_lock(&app->policy.mu);
        app->policy.bypass_until_ms = 0;
        pthread_mutex_unlock(&app->policy.mu);
        cawd_toast(app, CAWD_KIND_INFO, "Bypass off", "policy enforcement resumed");
    } else {
        cawd_policy_bypass_all(app, 60);
        cawd_toast(app, CAWD_KIND_WARN, "Bypass on", "all policies bypassed for 60s");
    }
}
static void cawd__appbar_agent_count(void *ctx, char *out, int out_sz) {
    CawdApp *app = (CawdApp *)ctx;
    int n = 0;
    if (app) {
        for (int i = 0; i < CAWD_MAX_AGENTS; i++) {
            CawdAgent *a = &app->agents.slots[i];
            if (a->used && a->state != CAWD_STATE_DONE &&
                a->state != CAWD_STATE_FAILED &&
                a->state != CAWD_STATE_CANCELLED) n++;
        }
    }
    snprintf(out, (size_t)out_sz, "%d", n);
}
static void cawd__appbar_open_agents(void *ctx) {
    CawdApp *app = (CawdApp *)ctx;
    if (!app) return;
    app->ui.tabs.active = CAWD_UI_SLOT_ORCHESTRA;
    cawd_toast(app, CAWD_KIND_INFO, "Agents", "opened orchestra");
}
static void cawd__appbar_stop_agents(void *ctx) {
    CawdApp *app = (CawdApp *)ctx;
    if (!app) return;
    int killed = 0;
    pthread_mutex_lock(&app->agents.mu);
    for (int i = 0; i < CAWD_MAX_AGENTS; i++) {
        CawdAgent *a = &app->agents.slots[i];
        if (a->used && a->state != CAWD_STATE_DONE &&
            a->state != CAWD_STATE_FAILED &&
            a->state != CAWD_STATE_CANCELLED) {
            a->state = CAWD_STATE_CANCELLED;
            killed++;
        }
    }
    pthread_mutex_unlock(&app->agents.mu);
    char body[64]; snprintf(body, sizeof body, "%d agent%s cancelled",
                            killed, killed == 1 ? "" : "s");
    cawd_toast(app, killed ? CAWD_KIND_WARN : CAWD_KIND_INFO,
               "Stop", body);
}

static int cawd__run_with_ui(CawdApp *app) {
    /* Initialize UI widgets. */
    flux_tabbar_init(&app->ui.tabs, CAWD__UI_LABELS, CAWD_UI_SLOT_COUNT);
    for (int i = 0; i < CAWD_UI_SLOT_COUNT; i++)
        flux_scroll_init(&app->ui.scroll[i]);
    flux_composer_init(&app->ui.composer);
    flux_composer_configure(&app->ui.composer, (FluxComposerCfg){
        .min_rows             = 1,
        .max_rows             = 6,
        .history_enabled      = 1,
        .paste_collapse       = 1,
        .paste_collapse_lines = 3,
        .paste_collapse_chars = 200,
        .placeholder          = "Message OpenClaw…",
    });
    flux_composer_focus(&app->ui.composer, 1);
    app->ui.composer_focused = 1;

    /* AppBar — Claude-Code-style status strip below the composer. */
    flux_appbar_init(&app->ui.appbar);
    flux_appbar_add(&app->ui.appbar, (FluxAppBarItem){
        .id          = "bypass",
        .kind        = FLUX_APPBAR_KIND_TOGGLE,
        .icon        = "\xe2\x8f\xb5\xe2\x8f\xb5",   /* ⏵⏵ */
        .label       = "bypass permissions",
        .bool_fn     = cawd__appbar_bypass_active,
        .on_activate = cawd__appbar_toggle_bypass,
        .ctx         = app,
        .interactive = 1,
        .priority    = 100,
    });
    flux_appbar_add(&app->ui.appbar, (FluxAppBarItem){
        .id          = "agents",
        .kind        = FLUX_APPBAR_KIND_VALUE,
        .icon        = "\xe2\x97\x86",                /* ◆ */
        .label       = "local agents",
        .value_fn    = cawd__appbar_agent_count,
        .on_activate = cawd__appbar_open_agents,
        .ctx         = app,
        .interactive = 1,
        .priority    = 90,
    });
    flux_appbar_add(&app->ui.appbar, (FluxAppBarItem){
        .id          = "stop_agents",
        .kind        = FLUX_APPBAR_KIND_HINT,
        .label       = "stop agents",
        .shortcut    = "ctrl+x",
        .on_activate = cawd__appbar_stop_agents,
        .ctx         = app,
        .interactive = 0,
        .priority    = 50,
    });
    flux_appbar_add(&app->ui.appbar, (FluxAppBarItem){
        .id          = "manage",
        .kind        = FLUX_APPBAR_KIND_HINT,
        .icon        = "\xe2\x86\x93",   /* ↓ glyph as hint, not bound */
        .label       = "manage",
        .on_activate = cawd__appbar_open_agents,
        .ctx         = app,
        .interactive = 0,
        .priority    = 40,
    });
    flux_command_palette_init(&app->ui.palette,
                              CAWD__PALETTE_ITEMS, CAWD__PALETTE_N);
    flux_dialog_init(&app->ui.quit_dialog);
    flux_toast_center_init(&app->ui.toasts);
    /* Toast position: configured per frame in view(); see comment there. */
    flux_ticker_init(&app->ui.ticker);
    flux_particle_burst_init(&app->ui.particles);
    for (int i = 0; i < CAWD_UI_SLOT_COUNT; i++) {
        CAWD__UI_VTBL[i].init(app);
    }
    app->ui.enabled = 1;

    FluxModel model = {
        .state  = app,
        .init   = cawd__ui_model_init,
        .update = cawd__ui_model_update,
        .view   = cawd__ui_model_view,
        .free   = NULL,
    };
    FluxProgram prog = {
        .model      = model,
        .alt_screen = app->cfg.alt_screen != 0,
        .mouse      = app->cfg.mouse     != 0,
        .fps        = app->cfg.fps > 0 ? app->cfg.fps : 120,
    };
    flux_run(&prog);
    return 0;
}

#endif /* CAWD_TUI_IMPL */

#endif /* CAWD_TUI_H */
