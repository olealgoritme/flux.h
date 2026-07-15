/*
 * ai_demo.c — OpenClaw Workstation reference app
 * ==============================================
 *
 * A ~200-line reference showing how to build a full multi-channel AI
 * agent TUI on top of flux.h + cawd_tui.h. You wire 4-6 callbacks,
 * enable the channels you want, add policies, and call cawd_run().
 * The built-in UI renders a 13-slot workstation (6 exec channels
 * + 7 cross-cutting views) and handles all keyboard/mouse input.
 *
 * Build:  make ai_demo
 * Run:    ./ai_demo
 * Quit:   Ctrl+C (confirm)
 *
 * What this file demonstrates:
 *   - The canonical handler shape for a production agent app
 *   - Worker-thread streaming (CAWD_ON_WORKER handlers unblock UI)
 *   - Policy-gated tool calls (allow/deny/ask)
 *   - Channel enable pattern (HITL + API + Autonomous here)
 *   - A small scripted sim that fakes LLM streaming — replace with
 *     your real LLM client (Anthropic SDK, OpenAI, etc.)
 */

#define FLUX_IMPL
#include "flux.h"
#define CAWD_TUI_IMPL
#include "cawd_tui.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* --------------------------------------------------------------------- *
 * Scripted "LLM" — in a real app this is your Anthropic/OpenAI client.   *
 * We stream back tokens chunk-by-chunk with a small delay to simulate    *
 * real latency, then finish.                                             *
 * --------------------------------------------------------------------- */

typedef struct {
    CawdApp    *app;
    char        agent_id[32];
    char        prompt[512];
} SimJob;

static void *sim_worker(void *arg) {
    SimJob *j = (SimJob *)arg;

    /* Fake tool call first — demonstrates tool-call flow + audit log. */
    /* In real code you'd iterate messages + tool calls per the API spec. */

    cawd_agent_set_current_tool(j->app, j->agent_id, "read auth.c");
    cawd_agent_set_state(j->app, j->agent_id, CAWD_STATE_RUNNING);

    struct timespec pause = { 0, 200 * 1000 * 1000L };  /* 200ms */
    nanosleep(&pause, NULL);

    cawd_stream_begin(j->app, j->agent_id);

    const char *script[] = {
        "I'll analyze ", "the authentication module ",
        "and propose a ", "refactor.\n\n",
        "First, I'll read ", "the relevant files ",
        "and identify any ", "complexity hotspots. ",
        "Then I'll draft a ", "minimal diff ",
        "that preserves ", "existing behavior.\n\n",
        "Ready to proceed.",
        NULL
    };
    for (int i = 0; script[i]; i++) {
        cawd_stream_push(j->app, j->agent_id, script[i]);
        pause.tv_nsec = 80 * 1000 * 1000L;  /* 80ms between chunks */
        nanosleep(&pause, NULL);
        cawd_agent_add_tokens(j->app, j->agent_id, 0, (int)strlen(script[i]) / 4);
    }
    cawd_agent_add_tokens(j->app, j->agent_id, 540, 0);  /* prompt tokens */
    cawd_stream_end(j->app, j->agent_id, 1);
    cawd_toast(j->app, CAWD_KIND_SUCCESS, "Done",
               "Assistant finished streaming.");
    free(j);
    return NULL;
}

/* --------------------------------------------------------------------- *
 * Handlers — this is all your app code.                                  *
 * --------------------------------------------------------------------- */

/* Called when the user types something into the composer (HITL channel). */
static void on_hitl_message(CawdApp *app, CawdMessage m) {
    /* Spawn a fresh agent for this message, scripted streamer simulates
     * the real LLM call running on a worker thread. */
    const char *aid = cawd_agent_spawn(app, CAWD_CH_HITL,
        (CawdAgentSpec){
            .name = "main",
            .model = "claude-opus-4-7",
            .provider = "Anthropic",
            .system_prompt = "You are a helpful coding assistant.",
            .max_tokens = 4096
        });
    if (!aid) return;

    SimJob *j = (SimJob *)calloc(1, sizeof *j);
    if (!j) return;
    j->app = app;
    strncpy(j->agent_id, aid, sizeof j->agent_id - 1);
    strncpy(j->prompt, m.text ? m.text : "", sizeof j->prompt - 1);

    pthread_t t;
    if (pthread_create(&t, NULL, sim_worker, j) != 0) { free(j); return; }
    pthread_detach(t);
    cawd_ticker_push(app, CAWD_CH_HITL, "spawned agent main");
}

/* A tool handler — called when an agent requests a tool. Return 0 on
 * success, <0 on error, and fill `out` with the tool's output (plain
 * text or JSON). */
static int on_tool_call(CawdApp *app, CawdToolCall tc, char *out, size_t out_sz) {
    (void)app;
    if (!tc.tool) return -1;
    if (strcmp(tc.tool, "read") == 0) {
        /* In real code: open(tc.args_json->path), read, return contents. */
        snprintf(out, out_sz, "fake file contents for %s", tc.args_json);
        return 0;
    }
    if (strcmp(tc.tool, "bash") == 0) {
        /* In real code: execve in sandbox, capture stdout/err, return.    */
        snprintf(out, out_sz, "fake bash output for: %s", tc.args_json);
        return 0;
    }
    snprintf(out, out_sz, "unknown tool: %s", tc.tool);
    return -1;
}

/* Approval handler — the agent wants to take a risky action. Return
 * non-zero to approve. In this reference we auto-approve LOW risk,
 * ask for MEDIUM+HIGH via cawd_await_approval (blocks the worker
 * thread until the UI posts an answer). */
static int on_approval(CawdApp *app, CawdApproval ap) {
    if (ap.risk == CAWD_RISK_LOW) return 1;
    CawdDecision d = cawd_await_approval(app, ap, 30000);
    return d == CAWD_APPROVE || d == CAWD_APPROVE_ALWAYS;
}

/* Observational hook — printed to audit, can also be forwarded to your
 * own logging system, metrics, etc. */
static void on_agent_state(CawdApp *app, const char *id, CawdAgentState s) {
    (void)app; (void)id; (void)s;
    /* e.g. forward to your metrics: */
    /* statsd_incr("agent.state_change", {"state": s}); */
}

/* --------------------------------------------------------------------- *
 * main                                                                    *
 * --------------------------------------------------------------------- */

int main(void) {
    CawdApp app;
    CawdConfig cfg = {
        .title          = "OpenClaw Workstation",
        .fps            = 120,
        .worker_threads = 4,
        .alt_screen     = 1,
        .mouse          = 1,
        .custom_ui      = 0,   /* built-in UI */
    };
    if (cawd_init(&app, cfg) != 0) {
        fprintf(stderr, "cawd_init failed\n");
        return 1;
    }

    /* Wire handlers with explicit threading choice per handler. */
    cawd_on_message     (&app, CAWD_CH_HITL, on_hitl_message, CAWD_ON_UI);
    cawd_on_tool_call   (&app,               on_tool_call,    CAWD_ON_WORKER);
    cawd_on_approval    (&app,               on_approval,     CAWD_ON_WORKER);
    cawd_on_agent_state (&app,               on_agent_state,  CAWD_ON_UI);

    /* Enable the channels we want live. */
    cawd_channel_enable(&app, CAWD_CH_HITL, NULL);
    cawd_channel_enable(&app, CAWD_CH_AUTONOMOUS,
        &(CawdAutonomousCfg){.interval_sec = 10});
    cawd_channel_enable(&app, CAWD_CH_API,
        &(CawdApiCfg){
            .port = 8080,
            .bearer_token = "demo-token",
            .rate_limit_rpm = 60,
            .ngrok_like_label = "claw-demo.ngrok.io",
        });

    /* Policies: hard-block dangerous shell commands, ask-first for other
     * writes, allow reads freely. */
    cawd_policy_add(&app, "bash:rm -rf*",  CAWD_DENY,  CAWD_CH_ANY);
    cawd_policy_add(&app, "bash:sudo*",    CAWD_DENY,  CAWD_CH_ANY);
    cawd_policy_add(&app, "bash:git push*",CAWD_ASK,   CAWD_CH_ANY);
    cawd_policy_add(&app, "read:*",        CAWD_ALLOW, CAWD_CH_ANY);
    cawd_policy_add(&app, "bash:*",        CAWD_ASK,   CAWD_CH_HITL);
    cawd_policy_add(&app, "bash:*",        CAWD_DENY,  CAWD_CH_AUTONOMOUS);

    /* Off we go. Built-in UI owns the loop. */
    int rc = cawd_run(&app);
    cawd_shutdown(&app);
    return rc;
}
