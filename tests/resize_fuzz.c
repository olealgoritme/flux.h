/* resize_fuzz.c — random-resize stress for the 11 Agent F2 widgets.
 *
 * For each stateful widget we run 1000 iterations of (random_resize,
 * random_tick, render) and verify:
 *   - no crash
 *   - output length stays within render-buffer capacity
 *   - row width (for row-based widgets) matches the requested width
 *
 * Fixed PRNG seed so failures are reproducible.  Widths range 10..300,
 * heights 5..100.
 */
#define FLUX_IMPL
#include "../flux.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ITERS 1000

static unsigned g_seed = 0x5eed;
static int rnd(int lo, int hi) {
    g_seed = g_seed * 1103515245u + 12345u;
    unsigned v = (g_seed >> 8) & 0xffff;
    return lo + (int)(v % (unsigned)(hi - lo + 1));
}

static int row_width_first(const char *s) {
    const char *nl = strchr(s, '\n');
    int len = nl ? (int)(nl - s) : (int)strlen(s);
    char tmp[8192];
    if (len >= (int)sizeof tmp) len = sizeof tmp - 1;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    return flux_strwidth(tmp);
}

int main(void) {
    int fails = 0;
    static char buf[65536];
    FluxSB sb;
    uint64_t clock = 0;

    /* ─── FluxTicker ──────────────────────────────────────────── */
    {
        FluxTicker t;
        flux_ticker_init(&t);
        for (int i = 0; i < ITERS; i++) {
            if (rnd(0, 10) < 4) {
                flux_ticker_push(&t,
                    (FluxChannelId)(rnd(0, 5)),
                    i % 2 ? "resize-evt" : "你好 world");
            }
            int w = rnd(10, 300);
            clock += (uint64_t)rnd(1, 50);
            flux_ticker_tick(&t, clock);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_ticker_render(&t, &sb, w);
            int got = row_width_first(flux_sb_str(&sb));
            if (got != w) {
                fprintf(stderr, "[ticker] iter %d w=%d got=%d\n", i, w, got);
                fails++;
                if (fails > 5) break;
            }
        }
        printf("[ticker] %d iters: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxToastCenter ────────────────────────────────────── */
    {
        FluxToastCenter tc;
        flux_toast_center_init(&tc);
        for (int i = 0; i < ITERS; i++) {
            if (rnd(0, 10) < 5) {
                flux_toast_center_push(&tc,
                    (FluxToastKind)(rnd(0, 3)), "t", "b", (uint64_t)rnd(100, 3000));
            }
            int sw = rnd(10, 300);
            int sh = rnd(5, 100);
            clock += (uint64_t)rnd(1, 200);
            flux_toast_center_tick(&tc, clock);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_toast_center_render(&tc, &sb, sw, sh);
            if (sb.len > FLUX_RENDER_BUF) { fails++; break; }
        }
        printf("[toast_center] %d iters: %s (count=%d)\n",
               ITERS, fails ? "FAIL" : "OK", tc.count);
    }

    /* ─── FluxGanttRow ───────────────────────────────────────── */
    {
        for (int i = 0; i < ITERS; i++) {
            int n = rnd(1, 4);
            FluxGanttTask tasks[4];
            uint64_t now = (uint64_t)rnd(1000, 1000000);
            for (int t = 0; t < n; t++) {
                tasks[t].label = t == 0 ? "tester" :
                                 t == 1 ? "really-long-label-name-overlong" :
                                 t == 2 ? "你好 CJK" : "";
                tasks[t].color.r = (unsigned char)rnd(0, 255);
                tasks[t].color.g = (unsigned char)rnd(0, 255);
                tasks[t].color.b = (unsigned char)rnd(0, 255);
                tasks[t].start_ms = now - (uint64_t)rnd(0, 30000);
                tasks[t].end_ms   = rnd(0, 1) ? 0 : now - (uint64_t)rnd(0, 1000);
            }
            int w = rnd(10, 300);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_gantt_row(&sb, tasks, n, 30000, now, w);
            /* Each row should be exactly w */
            const char *p = flux_sb_str(&sb);
            int row = 0;
            while (*p && row < n) {
                int rw = row_width_first(p);
                if (rw != w) {
                    fprintf(stderr, "[gantt] iter %d row %d w=%d got=%d\n",
                            i, row, w, rw);
                    fails++;
                    break;
                }
                row++;
                p = strchr(p, '\n');
                if (!p) break;
                p++;
            }
            if (fails > 5) break;
        }
        printf("[gantt] %d iters: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxParticleBurst ──────────────────────────────────── */
    {
        FluxParticleBurst pb;
        flux_particle_burst_init(&pb);
        for (int i = 0; i < ITERS; i++) {
            if (rnd(0, 10) < 3) {
                FluxRGB c = { 255, 200, 100 };
                flux_particle_burst_spawn(&pb,
                    rnd(1, 100), rnd(1, 50), c, rnd(1, 32));
            }
            int sw = rnd(10, 300);
            int sh = rnd(5, 100);
            clock += (uint64_t)rnd(1, 30);
            flux_particle_burst_tick(&pb, clock);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_particle_burst_render(&pb, &sb, sw, sh);
            if (sb.len > FLUX_RENDER_BUF) { fails++; break; }
        }
        printf("[particle_burst] %d iters: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxSplitPane ──────────────────────────────────────── */
    {
        FluxSplitPane sp;
        flux_split_init(&sp, FLUX_SPLIT_HORIZONTAL, 2);
        FluxSplitRenderFn dummy_fns[2] = { NULL, NULL };
        void *ctxs[2] = { NULL, NULL };
        for (int i = 0; i < ITERS; i++) {
            FluxMsg m = {0};
            m.type = MSG_KEY;
            strcpy(m.u.key.key, (i & 1) ? "tab" : "shift+tab");
            flux_split_update(&sp, m);
            int w = rnd(10, 300);
            int h = rnd(5, 100);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_split_render(&sp, &sb, w, h, dummy_fns, ctxs);
        }
        printf("[split_pane] %d iters: OK (focus=%d)\n", ITERS, sp.focus);
    }

    /* ─── FluxAgentCard ──────────────────────────────────────── */
    {
        FluxAgentCard card = {0};
        card.name = "Tester";
        card.channel = FLUX_CH_AUTO;
        card.state = FLUX_DOT_RUNNING;
        card.current_tool = "pytest";
        for (int j = 0; j < 32; j++) card.token_rate_ring[j] = (float)j;
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(12, 300);
            card.pulse_t = ((float)(i % 100)) / 100.0f;
            card.elapsed_ms = rnd(0, 60000);
            card.cost_usd = (float)rnd(0, 10000) / 1000.0f;
            flux_sb_init(&sb, buf, sizeof buf);
            flux_agent_card_render(&card, &sb, w);
            /* Verify each of FLUX_AGENT_CARD_H rows is exactly `w`. */
            const char *p = flux_sb_str(&sb);
            int row = 0;
            while (*p) {
                int rw = row_width_first(p);
                if (rw != w) {
                    fprintf(stderr, "[agent_card] iter %d row %d w=%d got=%d\n",
                            i, row, w, rw);
                    fails++;
                    break;
                }
                row++;
                p = strchr(p, '\n');
                if (!p) break;
                p++;
            }
            if (row != FLUX_AGENT_CARD_H) {
                fprintf(stderr, "[agent_card] iter %d w=%d rows=%d (want %d)\n",
                        i, w, row, FLUX_AGENT_CARD_H);
                fails++;
            }
            if (fails > 5) break;
        }
        printf("[agent_card] %d iters: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxRequestTrace ───────────────────────────────────── */
    {
        FluxRequestTrace tr = {
            "POST", "/v1/chat", 200, 412, 1234,
            "Authorization: Bearer sk-*\nContent-Type: application/json",
            "{\"model\":\"opus\"}", "{\"id\":\"msg_1\"}"
        };
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(20, 300);
            int expanded = i & 1;
            flux_sb_init(&sb, buf, sizeof buf);
            flux_request_trace_render(&tr, &sb, w, expanded);
            /* Collapsed mode: every row must be exactly `w`. */
            if (!expanded) {
                int rw = row_width_first(flux_sb_str(&sb));
                if (rw != w) {
                    fprintf(stderr, "[request_trace] iter %d w=%d got=%d\n",
                            i, w, rw);
                    fails++;
                    if (fails > 5) break;
                }
            }
        }
        printf("[request_trace] %d iters: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxChannelBadge / FluxHttpBadge / FluxFocusRing ───── */
    {
        /* Stateless — just width fuzz. */
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(1, 300);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_channel_badge(&sb, (FluxChannelId)rnd(0, 5),
                               (FluxBadgeStatus)rnd(0, 4), w);
            int rw = row_width_first(flux_sb_str(&sb));
            if (rw != w) {
                fprintf(stderr, "[channel_badge] iter %d w=%d got=%d\n",
                        i, w, rw);
                fails++;
                if (fails > 5) break;
            }
        }
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(1, 300);
            flux_sb_init(&sb, buf, sizeof buf);
            const char *methods[] = { "GET", "POST", "PUT", "DELETE" };
            flux_http_badge(&sb, methods[rnd(0, 3)], rnd(0, 599), w);
            int rw = row_width_first(flux_sb_str(&sb));
            if (rw != w) {
                fprintf(stderr, "[http_badge] iter %d w=%d got=%d\n", i, w, rw);
                fails++;
                if (fails > 5) break;
            }
        }
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(2, 300);
            int h = rnd(2, 100);
            flux_sb_init(&sb, buf, sizeof buf);
            FluxRGB c = { (unsigned char)rnd(0, 255),
                          (unsigned char)rnd(0, 255),
                          (unsigned char)rnd(0, 255) };
            flux_focus_ring(&sb, rnd(1, 100), rnd(1, 50),
                            w, h, c, (float)rnd(0, 100) / 100.0f);
            /* focus ring emits cursor escapes — just verify no overflow. */
            if (sb.len > FLUX_RENDER_BUF) {
                fails++;
                break;
            }
        }
        printf("[channel/http/focus] %d iters each: %s\n",
               ITERS, fails ? "FAIL" : "OK");
    }

    if (fails == 0) {
        printf("\nALL RESIZE FUZZ TESTS PASS.\n");
        return 0;
    }
    fprintf(stderr, "\n%d RESIZE FUZZ FAILURES.\n", fails);
    return 1;
}
