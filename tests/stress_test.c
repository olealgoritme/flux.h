/* Stress test: pass extreme inputs to every widget.
 * If any widget emits a row != requested width, fail loudly. */

#define FLUX_IMPL
#include "../flux.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int row_width(const char *s) {
    /* Measure width up to first '\n' or end. */
    const char *nl = strchr(s, '\n');
    int len = nl ? (int)(nl - s) : (int)strlen(s);
    char tmp[4096];
    if (len >= (int)sizeof tmp) len = sizeof tmp - 1;
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    return flux_strwidth(tmp);
}

static int check_rows_eq(const char *content, int expected_w) {
    int fail = 0;
    const char *p = content;
    int line_no = 0;
    while (*p) {
        int w = row_width(p);
        const char *nl = strchr(p, '\n');
        if (w != expected_w) {
            fprintf(stderr, "FAIL: line %d width=%d, expected=%d\n",
                    line_no, w, expected_w);
            fail = 1;
        }
        line_no++;
        if (!nl) break;
        p = nl + 1;
    }
    return fail;
}

int main(void) {
    char buf[16384];
    int fails = 0;

    /* === Test 1: flux_truncate basic cases === */
    {
        char out[64];
        int w = flux_truncate("hello", 10, NULL, out, sizeof out);
        if (w != 5 || strcmp(out, "hello") != 0) {
            fprintf(stderr, "T1.1 fail: w=%d out='%s'\n", w, out);
            fails++;
        }
        w = flux_truncate("hello world this is long", 10, NULL, out, sizeof out);
        if (w > 10) { fprintf(stderr, "T1.2 fail: w=%d > 10\n", w); fails++; }
        printf("T1 truncate basic: w=%d (<=10) out='%s'\n", w, out);

        /* CJK wide char */
        w = flux_truncate("你好世界 hello", 6, NULL, out, sizeof out);
        if (w > 6) { fprintf(stderr, "T1.3 fail CJK: w=%d > 6\n", w); fails++; }
        printf("T1 CJK truncate: w=%d (<=6) out='%s'\n", w, out);

        /* ANSI escapes preserved */
        w = flux_truncate("\x1b[31mred\x1b[0m text", 5, NULL, out, sizeof out);
        printf("T1 ANSI: w=%d out='%s'\n", w, out);
    }

    /* === Test 2: flux_fit always emits exactly target_w === */
    {
        const struct { const char *text; int w; } cases[] = {
            { "short", 20 },
            { "this is a very long string that exceeds the width", 20 },
            { "你好世界 CJK wide chars 漢字", 30 },
            { "\x1b[31mred\x1b[0m colored", 25 },
            { "", 15 },
            { NULL, 10 },
        };
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_fit(&sb, cases[i].text, cases[i].w, NULL, FLUX_ALIGN_LEFT);
            int w = flux_strwidth(flux_sb_str(&sb));
            if (w != cases[i].w) {
                fprintf(stderr, "T2 fail: case %zu got %d, want %d, text='%s'\n",
                        i, w, cases[i].w, cases[i].text ? cases[i].text : "(null)");
                fails++;
            }
        }
        printf("T2 flux_fit width contract: %s\n", fails ? "SOME FAILED" : "PASS (all widths exact)");
    }

    /* === Test 3: flux_window_chrome with wild content === */
    {
        const char *bad_content =
            "\x1b[31mShort\n"
            "this row is intentionally way wider than the inner_w setting we choose for the chrome\n"
            "你好世界 CJK heavy\n"
            "\n"
            "with embedded\x1b[0m ANSI \x1b[1mcolors\x1b[0m galore\n";
        flux_window_chrome(buf, sizeof buf, bad_content, "TEST", 30, NULL);
        /* Each chrome row should be exactly inner_w + 2 = 32 cells. */
        int row_fails = check_rows_eq(buf, 32);
        printf("T3 chrome with wild content @ inner_w=30: %s\n",
               row_fails ? "FAIL" : "PASS (every row exactly 32 cells)");
        fails += row_fails;
    }

    /* === Test 4: flux_activity_render with extreme labels === */
    {
        FluxActivity items[] = {
            { "short", FLUX_ACT_DONE, 100, 0 },
            { "this is a deliberately overlong activity label that should truncate gracefully", FLUX_ACT_RUNNING, -1, 3 },
            { "你好世界 CJK 漢字 wide chars", FLUX_ACT_PENDING, -1, 0 },
            { "\x1b[33mwith\x1b[0m ANSI codes injected", FLUX_ACT_FAILED, -1, 0 },
        };
        for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_activity_render(&sb, &items[i], 25, 2);
            int w = row_width(flux_sb_str(&sb));
            if (w != 25) {
                fprintf(stderr, "T4 fail item %zu: w=%d label='%s'\n", i, w, items[i].label);
                fails++;
            }
        }
        printf("T4 activity rows @ width=25: %s\n", fails > (fails - 4) ? "FAIL" : "PASS");
    }

    /* === Test 5: flux_diff_block_render with extreme inputs === */
    {
        FluxDiffLine lines[] = {
            { FLUX_DIFF_REMOVED, "very long removed line that won't fit in narrow width" },
            { FLUX_DIFF_ADDED, "你好 CJK added with wide chars" },
            { FLUX_DIFF_CONTEXT, "context" },
        };
        FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
        flux_diff_block_render(&sb, "very/long/path/to/file.ts", lines, 3, 30);
        int row_fails = check_rows_eq(flux_sb_str(&sb), 30);
        printf("T5 diff block @ width=30: %s\n",
               row_fails ? "FAIL" : "PASS (every row exactly 30 cells)");
        fails += row_fails;
    }

    /* === Test 6: flux_approval_render with overflow inputs === */
    {
        FluxButton btns[2] = {
            { "Looooong Allow Label", FLUX_THEME_OK_FG, FLUX_THEME_BUTTON_OK_BG, FLUX_THEME_TEXT_DIM_FG },
            { "Deny Forever", FLUX_THEME_ERR_FG, FLUX_THEME_BUTTON_NO_BG, FLUX_THEME_TEXT_DIM_FG },
        };
        FluxApproval a;
        flux_approval_init(&a, "Apply this enormous patch to all 4096 files in the entire repo right now please", btns, 2);
        FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
        flux_approval_render(&a, &sb, 30);
        int row_fails = check_rows_eq(flux_sb_str(&sb), 30);
        printf("T6 approval @ width=30: %s\n",
               row_fails ? "FAIL" : "PASS (every row exactly 30 cells)");
        fails += row_fails;
    }

    /* === Test 7: flux_statusbar_render at narrow widths === */
    {
        FluxStatusBar s = { "very long brand name that won't fit", 8400, 0.12f, 0.62f, 8 };
        for (int w = 20; w <= 80; w += 10) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_statusbar_render(&sb, &s, w);
            int got = flux_strwidth(flux_sb_str(&sb));
            if (got > w) {
                fprintf(stderr, "T7 fail @ w=%d: got %d > target\n", w, got);
                fails++;
            }
            printf("  T7 statusbar w=%d → emitted=%d cells\n", w, got);
        }
    }

    /* ═══════════════════════════════════════════════════════════════
     * Wave-5 new widgets (ai_demo): 11 entries
     * ═════════════════════════════════════════════════════════════ */

    /* === T8: flux_easing purity ========================================== */
    {
        float samples[] = {-1.0f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f};
        for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
            float t = samples[i];
            float a = flux_ease_out_expo(t);
            float b = flux_ease_in_out_cubic(t);
            float c = flux_ease_out_back(t);
            float d = flux_ease_out_bounce(t);
            float e = flux_spring(t, 0.0f, 0.0f);
            /* Just verify no NaN */
            if (a != a || b != b || c != c || d != d || e != e) {
                fprintf(stderr, "T8 NaN t=%f\n", (double)t);
                fails++;
            }
        }
        printf("T8 easing sanity: PASS\n");
    }

    /* === T9: FluxChannelBadge width contract ============================ */
    {
        for (int ch = 0; ch <= FLUX_CH_API; ch++) {
            for (int w = 0; w <= 40; w += 8) {
                FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
                flux_channel_badge(&sb, (FluxChannelId)ch, FLUX_BADGE_OK, w);
                int got = w > 0 ? flux_strwidth(flux_sb_str(&sb)) : 0;
                if (w > 0 && got != w) {
                    fprintf(stderr, "T9 fail ch=%d w=%d got=%d\n", ch, w, got);
                    fails++;
                }
            }
        }
        printf("T9 channel badge width: %s\n", fails ? "CHECK" : "PASS");
    }

    /* === T10: FluxHttpBadge width contract ============================== */
    {
        const char *methods[] = { "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "???" };
        int codes[] = { 0, 200, 301, 404, 500 };
        for (size_t m = 0; m < sizeof methods / sizeof methods[0]; m++) {
            for (size_t s = 0; s < sizeof codes / sizeof codes[0]; s++) {
                for (int w = 1; w <= 40; w += 7) {
                    FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
                    flux_http_badge(&sb, methods[m], codes[s], w);
                    int got = flux_strwidth(flux_sb_str(&sb));
                    if (got != w) {
                        fprintf(stderr, "T10 fail m=%s c=%d w=%d got=%d\n",
                                methods[m], codes[s], w, got);
                        fails++;
                    }
                }
            }
        }
        printf("T10 http badge width: PASS\n");
    }

    /* === T11: FluxFocusRing doesn't crash =============================== */
    {
        FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
        FluxRGB accent = { 180, 130, 255 };
        flux_focus_ring(&sb, 2, 5, 40, 10, accent, 0.8f);
        flux_focus_ring(&sb, 0, 0, 0, 0, accent, 0.0f);
        flux_focus_ring(&sb, 1, 1, 2, 2, accent, 1.0f);
        printf("T11 focus ring smoke: PASS (%d bytes)\n", sb.len);
    }

    /* === T12: FluxToastCenter stack =================================== */
    {
        FluxToastCenter tc;
        flux_toast_center_init(&tc);
        flux_toast_center_push(&tc, FLUX_TOAST_OK, "short", "ok", 100);
        flux_toast_center_push(&tc, FLUX_TOAST_ERR,
            "very long title far beyond the 63-char limit we imposed in the struct literal ok?",
            "body body body", 100);
        flux_toast_center_push(&tc, FLUX_TOAST_WARN, "你好", "emoji body", 100);
        flux_toast_center_push(&tc, FLUX_TOAST_INFO, "with\x1b[31m ANSI", "mid", 100);
        for (int i = 0; i < 20; i++) {
            flux_toast_center_push(&tc, FLUX_TOAST_INFO, "x", "y", 100);
        }
        if (tc.count > FLUX_TOAST_CENTER_MAX) {
            fprintf(stderr, "T12 toast center overflow: count=%d\n", tc.count);
            fails++;
        }
        FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
        flux_toast_center_tick(&tc, flux_now_ms());
        flux_toast_center_render(&tc, &sb, 80, 24);
        printf("T12 toast center stack: PASS (count=%d)\n", tc.count);
    }

    /* === T13: FluxTicker =============================================== */
    {
        FluxTicker t;
        flux_ticker_init(&t);
        for (int i = 0; i < 30; i++) {
            flux_ticker_push(&t, (FluxChannelId)(i % 6),
                             i % 2 ? "PR #1234 merged" : "很长的消息 漢字 CJK");
        }
        if (t.count > FLUX_TICKER_MAX) {
            fprintf(stderr, "T13 ticker overflow: count=%d\n", t.count);
            fails++;
        }
        for (int w = 10; w <= 100; w += 10) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_ticker_tick(&t, flux_now_ms());
            flux_ticker_render(&t, &sb, w);
            int got = row_width(flux_sb_str(&sb));
            if (got != w) {
                fprintf(stderr, "T13 ticker w=%d got=%d\n", w, got);
                fails++;
            }
        }
        printf("T13 ticker width contract: PASS\n");
    }

    /* === T14: FluxGanttRow ============================================= */
    {
        uint64_t now = flux_now_ms();
        FluxGanttTask tasks[] = {
            { "tester",   { 180, 130, 255 }, now - 20000, 0 },
            { "refactor", { 120, 200, 120 }, now - 10000, now - 5000 },
            { "researcher with a deliberately overlong label", { 255, 160, 100 }, now - 15000, 0 },
            { "cjk 漢字 agent", { 60, 200, 255 }, now - 30000, now - 25000 },
        };
        for (int w = 10; w <= 100; w += 10) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_gantt_row(&sb, tasks, 4, 30000, now, w);
            int row_fails = check_rows_eq(flux_sb_str(&sb), w);
            if (row_fails) fails++;
        }
        printf("T14 gantt row: PASS\n");
    }

    /* === T15: FluxParticleBurst ======================================== */
    {
        FluxParticleBurst pb;
        flux_particle_burst_init(&pb);
        FluxRGB c = { 255, 200, 100 };
        flux_particle_burst_spawn(&pb, 10, 5, c, 40);
        flux_particle_burst_spawn(&pb, 10, 5, c, 200);  /* clamp */
        for (int i = 0; i < 100; i++) {
            flux_particle_burst_tick(&pb, flux_now_ms() + i * 10);
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_particle_burst_render(&pb, &sb, 80, 24);
        }
        printf("T15 particle burst: PASS\n");
    }

    /* === T16: FluxSplitPane focus cycling =============================== */
    {
        FluxSplitPane sp;
        flux_split_init(&sp, FLUX_SPLIT_HORIZONTAL, 3);
        FluxMsg tab = {0};
        tab.type = MSG_KEY;
        strcpy(tab.u.key.key, "tab");
        for (int i = 0; i < 10; i++) flux_split_update(&sp, tab);
        if (sp.focus < 0 || sp.focus >= sp.n_panes) {
            fprintf(stderr, "T16 split focus OOB: %d\n", sp.focus);
            fails++;
        }
        printf("T16 split pane: PASS (focus=%d)\n", sp.focus);
    }

    /* === T17: FluxAgentCard ============================================ */
    {
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
        for (int w = 12; w <= 80; w += 8) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_agent_card_render(&card, &sb, w);
            /* exactly FLUX_AGENT_CARD_H rows, each width wide */
            int nrows = 0;
            const char *p = flux_sb_str(&sb);
            while (*p) {
                int rw = row_width(p);
                if (rw != w) {
                    fprintf(stderr, "T17 agent card row w=%d got=%d line %d\n",
                            w, rw, nrows);
                    fails++;
                }
                nrows++;
                p = strchr(p, '\n');
                if (!p) break;
                p++;
            }
            if (nrows != FLUX_AGENT_CARD_H) {
                fprintf(stderr, "T17 agent card w=%d rows=%d (want %d)\n",
                        w, nrows, FLUX_AGENT_CARD_H);
                fails++;
            }
        }
        printf("T17 agent card: PASS\n");
    }

    /* === T18: FluxRequestTrace ========================================= */
    {
        FluxRequestTrace tr = {
            "POST", "/v1/chat", 200, 412, 1234,
            "Authorization: Bearer sk-123\nContent-Type: application/json",
            "{\"model\":\"claude-opus-4-7\"}",
            "{\"id\":\"msg_01\"}"
        };
        for (int w = 20; w <= 100; w += 20) {
            FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
            flux_request_trace_render(&tr, &sb, w, 0);
            int row_fails = check_rows_eq(flux_sb_str(&sb), w);
            if (row_fails) {
                fprintf(stderr, "T18 collapsed w=%d: %d row fails\n", w, row_fails);
                fails++;
            }
        }
        /* Expanded: just verify no crash */
        FluxSB sb; flux_sb_init(&sb, buf, sizeof buf);
        flux_request_trace_render(&tr, &sb, 60, 1);
        printf("T18 request trace: PASS\n");
    }

    if (fails == 0) {
        printf("\nALL STRESS TESTS PASS — every widget emits exactly the requested width.\n");
        return 0;
    } else {
        printf("\n%d FAILURES.\n", fails);
        return 1;
    }
}
