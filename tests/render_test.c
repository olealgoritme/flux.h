/* render_test.c — snapshot tests for Agent F2 widgets.
 *
 * For each widget we render with deterministic inputs into a buffer,
 * then byte-compare against `tests/golden/<widget>.ansi`.  On first run
 * (goldens missing) we write the buffer to disk and the test passes
 * with a "CREATED" log.  On subsequent runs any byte-diff fails.
 *
 * Set FLUX_UPDATE_GOLDENS=1 in the environment (or `make update-goldens`)
 * to unconditionally overwrite the goldens.
 */
#define FLUX_IMPL
#include "../flux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOLD_DIR "tests/golden"

static int read_file(const char *path, char *buf, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

static int write_file(const char *path, const char *buf, int len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(buf, 1, (size_t)len, f);
    fclose(f);
    return len;
}

static void _pane_emit_a(FluxSB *s, int w, int h, int f, void *c) {
    (void)c; char line[32];
    snprintf(line, sizeof line, "A w=%d h=%d f=%d\n", w, h, f);
    flux_sb_append(s, line);
}
static void _pane_emit_b(FluxSB *s, int w, int h, int f, void *c) {
    (void)c; char line[32];
    snprintf(line, sizeof line, "B w=%d h=%d f=%d\n", w, h, f);
    flux_sb_append(s, line);
}

static int snapshot(const char *name, const char *data, int len,
                    int update_all) {
    char path[256];
    snprintf(path, sizeof path, "%s/%s.ansi", GOLD_DIR, name);
    if (update_all) {
        write_file(path, data, len);
        printf("[UPD]  %s (%d bytes)\n", name, len);
        return 0;
    }
    char expected[65536];
    int exp_n = read_file(path, expected, sizeof expected);
    if (exp_n < 0) {
        write_file(path, data, len);
        printf("[NEW]  %s (%d bytes) — golden created\n", name, len);
        return 0;
    }
    if (exp_n != len || memcmp(expected, data, (size_t)len) != 0) {
        fprintf(stderr, "[FAIL] %s: golden %d bytes vs actual %d bytes\n",
                name, exp_n, len);
        /* Write .actual alongside for diffing. */
        char actual_path[512];
        snprintf(actual_path, sizeof actual_path, "%s.actual", path);
        write_file(actual_path, data, len);
        return 1;
    }
    printf("[OK]   %s\n", name);
    return 0;
}

int main(void) {
    int update_all = getenv("FLUX_UPDATE_GOLDENS") != NULL;
    int fails = 0;
    char buf[65536];
    FluxSB sb;

    /* ─── 1. easing — numeric values for 11 sample points ─────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        for (int i = 0; i <= 10; i++) {
            float t = (float)i / 10.0f;
            char line[128];
            snprintf(line, sizeof line,
                     "t=%.1f expo=%.4f cubic=%.4f back=%.4f bounce=%.4f spring=%.4f\n",
                     (double)t,
                     (double)flux_ease_out_expo(t),
                     (double)flux_ease_in_out_cubic(t),
                     (double)flux_ease_out_back(t),
                     (double)flux_ease_out_bounce(t),
                     (double)flux_spring(t, 12.0f, 4.0f));
            flux_sb_append(&sb, line);
        }
        fails += snapshot("easing", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 2. channel badge — all 6 channels × 3 statuses ─────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        for (int ch = 0; ch <= FLUX_CH_API; ch++) {
            flux_channel_badge(&sb, (FluxChannelId)ch, FLUX_BADGE_OK,   14);
            flux_channel_badge(&sb, (FluxChannelId)ch, FLUX_BADGE_RUN,  14);
            flux_channel_badge(&sb, (FluxChannelId)ch, FLUX_BADGE_ERR,  14);
        }
        fails += snapshot("channel_badge", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 3. http badge ───────────────────────────────────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        const char *methods[] = { "GET", "POST", "PUT", "DELETE", "PATCH" };
        int codes[] = { 200, 301, 404, 500 };
        for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) {
            for (size_t j = 0; j < sizeof codes / sizeof codes[0]; j++) {
                flux_http_badge(&sb, methods[i], codes[j], 20);
            }
        }
        fails += snapshot("http_badge", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 4. focus ring — fixed rectangle ─────────────────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        FluxRGB accent = { 180, 130, 255 };
        flux_focus_ring(&sb, 2, 3, 20, 5, accent, 0.8f);
        fails += snapshot("focus_ring", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 5. toast center — stacked ───────────────────────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        FluxToastCenter tc;
        flux_toast_center_init(&tc);
        /* Fake clock via the created_ms field. */
        flux_toast_center_push(&tc, FLUX_TOAST_OK,   "PR merged",   "#1248", 3000);
        flux_toast_center_push(&tc, FLUX_TOAST_WARN, "High cost",   "$1.84", 3000);
        flux_toast_center_push(&tc, FLUX_TOAST_ERR,  "Policy deny", "rm -rf", 3000);
        /* Normalize timestamps for determinism. */
        for (int i = 0; i < tc.count; i++) {
            tc.toasts[i].created_ms = 0;
            tc.toasts[i].now_ms     = 500;   /* mid-spring-in */
        }
        flux_toast_center_render(&tc, &sb, 60, 20);
        fails += snapshot("toast_center", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 6. ticker ──────────────────────────────────────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        FluxTicker t;
        flux_ticker_init(&t);
        flux_ticker_push(&t, FLUX_CH_GH,   "PR #1248 merged");
        flux_ticker_push(&t, FLUX_CH_AUTO, "pytest passing");
        flux_ticker_push(&t, FLUX_CH_API,  "POST /v1/chat 200");
        t.now_ms = 1000;
        /* Make sure arrivals older than shimmer window. */
        for (int i = 0; i < t.count; i++) {
            t.events[i].arrived_ms = 0;
        }
        flux_ticker_render(&t, &sb, 70);
        fails += snapshot("ticker", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 7. gantt row ───────────────────────────────────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        uint64_t now = 30000;
        FluxGanttTask tasks[] = {
            { "tester",   { 180, 130, 255 }, 10000, 0 },
            { "refactor", { 120, 200, 120 }, 20000, 25000 },
        };
        flux_gantt_row(&sb, tasks, 2, 30000, now, 60);
        fails += snapshot("gantt_row", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 8. particle burst — skipped (motion, not a snapshot) ───── */
    /* Particle positions depend on RNG + time; covered by resize_fuzz
     * and input_fuzz.  Snapshot test here is impractical. */

    /* ─── 9. split pane — render with static callbacks ───────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        FluxSplitPane sp;
        flux_split_init(&sp, FLUX_SPLIT_HORIZONTAL, 2);
        sp.focus = 1;
        FluxSplitRenderFn fns[2] = { _pane_emit_a, _pane_emit_b };
        void *ctxs[2] = { NULL, NULL };
        flux_split_render(&sp, &sb, 80, 20, fns, ctxs);
        fails += snapshot("split_pane", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 10. agent card ──────────────────────────────────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        FluxAgentCard card = {0};
        card.name = "Tester";
        card.channel = FLUX_CH_AUTO;
        card.state = FLUX_DOT_RUNNING;
        card.current_tool = "pytest -q";
        for (int i = 0; i < 32; i++) card.token_rate_ring[i] = (float)i;
        card.token_rate_head = 0;
        card.tokens_total = 14203;
        card.cost_usd = 0.123f;
        card.elapsed_ms = 8421;
        card.focused = 1;
        card.pulse_t = 0.5f;
        flux_agent_card_render(&card, &sb, 30);
        fails += snapshot("agent_card", flux_sb_str(&sb), sb.len, update_all);
    }

    /* ─── 11. request trace (collapsed + expanded) ───────────────── */
    {
        flux_sb_init(&sb, buf, sizeof buf);
        FluxRequestTrace tr = {
            "POST", "/v1/chat", 200, 412, 1234,
            "Authorization: Bearer sk-*\nContent-Type: application/json",
            "{\"model\":\"claude-opus-4-7\"}",
            "{\"id\":\"msg_01\",\"ok\":true}"
        };
        flux_request_trace_render(&tr, &sb, 60, 0);
        flux_request_trace_render(&tr, &sb, 60, 1);
        fails += snapshot("request_trace", flux_sb_str(&sb), sb.len, update_all);
    }

    if (fails == 0) {
        printf("\nALL SNAPSHOT TESTS PASS.\n");
        return 0;
    }
    fprintf(stderr, "\n%d SNAPSHOT FAILURES.\n", fails);
    return 1;
}
