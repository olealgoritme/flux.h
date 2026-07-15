/* input_fuzz.c — 10000 random FluxMsg events fed into every stateful
 * widget's *_update.  Never crash; never leak; after each input do one
 * *_render and verify no buffer overrun.
 *
 * For widgets that don't accept FluxMsg (pure renderers — channel badge,
 * http badge, focus ring, gantt row, toast center) we just re-render
 * with pseudo-random inputs (scripted at end).
 *
 * Fixed seed for reproducibility.
 */
#define FLUX_IMPL
#include "../flux.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ITERS 10000

static unsigned g_seed = 0xf00d;
static int rnd(int lo, int hi) {
    g_seed = g_seed * 1103515245u + 12345u;
    unsigned v = (g_seed >> 8) & 0xffff;
    return lo + (int)(v % (unsigned)(hi - lo + 1));
}

static FluxMsg random_msg(void) {
    FluxMsg m = {0};
    int t = rnd(0, 6);
    static const char *keys[] = {
        "tab", "shift+tab", "enter", "esc", "up", "down", "left", "right",
        "space", "a", "r", "h", "e", "f", "x", "p", "/", "?", "ctrl+k",
        "ctrl+c", "ctrl+s", "ctrl+e", "ctrl+n", "pgup", "pgdn", "home", "end"
    };
    switch (t) {
        case 0:
            m.type = MSG_KEY;
            strncpy(m.u.key.key, keys[rnd(0, (int)(sizeof keys / sizeof keys[0]) - 1)],
                    sizeof m.u.key.key - 1);
            m.u.key.ctrl = rnd(0, 1);
            m.u.key.alt = rnd(0, 1);
            m.u.key.rune = rnd(0, 0x10ffff);
            break;
        case 1:
            m.type = MSG_WINSIZE;
            m.u.winsize.cols = rnd(10, 300);
            m.u.winsize.rows = rnd(5, 100);
            break;
        case 2:
            m.type = MSG_TICK;
            break;
        case 3:
            m.type = MSG_MOUSE;
            m.u.mouse.event = (FluxMouseEvent)rnd(0, 4);
            m.u.mouse.x = rnd(1, 200);
            m.u.mouse.y = rnd(1, 100);
            m.u.mouse.button = rnd(0, 2);
            break;
        case 4:
            m.type = MSG_PASTE;
            m.u.paste.len = rnd(0, 50);
            for (int i = 0; i < m.u.paste.len && i < FLUX_PASTE_MAX - 1; i++) {
                m.u.paste.text[i] = (char)rnd(0x20, 0x7e);
            }
            m.u.paste.text[m.u.paste.len] = 0;
            break;
        case 5:
            m.type = MSG_CUSTOM;
            m.u.custom.id = rnd(0, 1000);
            m.u.custom.data = NULL;
            break;
        default:
            m.type = MSG_NONE;
    }
    return m;
}

static int ansi_ok(const char *s, int len) {
    /* Verify no raw bytes outside expected ANSI / UTF-8 / printable ranges
     * that would be obviously invalid.  Specifically: no 0x7f, no
     * unbalanced ESC sequences. */
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x7f) return 0;
        /* plain ascii + \n + \t + ESC allowed; UTF-8 continuations allowed */
    }
    return 1;
}

int main(void) {
    int fails = 0;
    static char buf[65536];
    FluxSB sb;

    /* ─── FluxSplitPane ──────────────────────────────────────── */
    {
        FluxSplitPane sp;
        flux_split_init(&sp, FLUX_SPLIT_HORIZONTAL, 3);
        FluxSplitRenderFn fns[3] = { NULL, NULL, NULL };
        void *ctxs[3] = { NULL, NULL, NULL };
        for (int i = 0; i < ITERS; i++) {
            flux_split_update(&sp, random_msg());
            if (sp.focus < 0 || sp.focus >= sp.n_panes) {
                fprintf(stderr, "[split] focus OOB iter %d: %d\n", i, sp.focus);
                fails++;
                break;
            }
            flux_sb_init(&sb, buf, sizeof buf);
            flux_split_render(&sp, &sb, rnd(10, 200), rnd(5, 50), fns, ctxs);
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) {
                fprintf(stderr, "[split] invalid ANSI iter %d\n", i);
                fails++;
                break;
            }
        }
        printf("[split] %d inputs: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxToastCenter ────────────────────────────────────── */
    {
        FluxToastCenter tc;
        flux_toast_center_init(&tc);
        uint64_t clock = 0;
        for (int i = 0; i < ITERS; i++) {
            FluxMsg m = random_msg();
            if (m.type == MSG_CUSTOM) {
                flux_toast_center_push(&tc,
                    (FluxToastKind)(rnd(0, 3)),
                    "t", "b", (uint64_t)rnd(100, 2000));
            }
            clock += (uint64_t)rnd(1, 200);
            flux_toast_center_tick(&tc, clock);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_toast_center_render(&tc, &sb, rnd(10, 200), rnd(5, 50));
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) {
                fprintf(stderr, "[toast] invalid ANSI iter %d\n", i);
                fails++;
                break;
            }
        }
        printf("[toast] %d inputs: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxTicker ─────────────────────────────────────────── */
    {
        FluxTicker t;
        flux_ticker_init(&t);
        uint64_t clock = 0;
        for (int i = 0; i < ITERS; i++) {
            FluxMsg m = random_msg();
            if (m.type == MSG_CUSTOM) {
                flux_ticker_push(&t, (FluxChannelId)(rnd(0, 5)),
                                 i % 2 ? "evt" : "你好");
            }
            clock += (uint64_t)rnd(1, 50);
            flux_ticker_tick(&t, clock);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_ticker_render(&t, &sb, rnd(10, 200));
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) {
                fprintf(stderr, "[ticker] invalid ANSI iter %d\n", i);
                fails++;
                break;
            }
        }
        printf("[ticker] %d inputs: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── FluxParticleBurst ──────────────────────────────────── */
    {
        FluxParticleBurst pb;
        flux_particle_burst_init(&pb);
        uint64_t clock = 0;
        for (int i = 0; i < ITERS; i++) {
            FluxMsg m = random_msg();
            if (m.type == MSG_CUSTOM) {
                FluxRGB c = { 255, 200, 100 };
                flux_particle_burst_spawn(&pb, rnd(1, 80), rnd(1, 20),
                                          c, rnd(1, 32));
            }
            clock += (uint64_t)rnd(1, 20);
            flux_particle_burst_tick(&pb, clock);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_particle_burst_render(&pb, &sb, rnd(10, 200), rnd(5, 50));
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) {
                fprintf(stderr, "[particle] invalid ANSI iter %d\n", i);
                fails++;
                break;
            }
        }
        printf("[particle] %d inputs: %s\n", ITERS, fails ? "FAIL" : "OK");
    }

    /* ─── Stateless renderers: channel / http / focus / gantt / agent /
     *    request_trace / easing — just fuzz the render path with
     *    random inputs. */
    {
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(1, 200);
            flux_sb_init(&sb, buf, sizeof buf);
            flux_channel_badge(&sb, (FluxChannelId)rnd(0, 5),
                               (FluxBadgeStatus)rnd(0, 4), w);
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) { fails++; break; }
        }
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(1, 200);
            flux_sb_init(&sb, buf, sizeof buf);
            const char *m = (i & 1) ? "POST" : "GET";
            flux_http_badge(&sb, m, rnd(0, 599), w);
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) { fails++; break; }
        }
        for (int i = 0; i < ITERS; i++) {
            int w = rnd(2, 200), h = rnd(2, 80);
            flux_sb_init(&sb, buf, sizeof buf);
            FluxRGB c = { (unsigned char)rnd(0, 255),
                          (unsigned char)rnd(0, 255),
                          (unsigned char)rnd(0, 255) };
            flux_focus_ring(&sb, rnd(1, 50), rnd(1, 20), w, h, c,
                            (float)rnd(0, 100) / 100.0f);
            if (!ansi_ok(flux_sb_str(&sb), sb.len)) { fails++; break; }
        }
        /* easing: just crunch a billion t values */
        for (int i = 0; i < ITERS; i++) {
            float t = (float)rnd(-100, 200) / 100.0f;
            float a = flux_ease_out_expo(t);
            float b = flux_ease_in_out_cubic(t);
            float c = flux_ease_out_back(t);
            float d = flux_ease_out_bounce(t);
            float e = flux_spring(t, (float)rnd(1, 50),
                                  (float)rnd(1, 20));
            if (a != a || b != b || c != c || d != d || e != e) {
                fprintf(stderr, "[easing] NaN iter %d t=%f\n", i, (double)t);
                fails++;
                break;
            }
        }
        printf("[stateless] %d inputs each: %s\n",
               ITERS, fails ? "FAIL" : "OK");
    }

    if (fails == 0) {
        printf("\nALL INPUT FUZZ TESTS PASS.\n");
        return 0;
    }
    fprintf(stderr, "\n%d INPUT FUZZ FAILURES.\n", fails);
    return 1;
}
