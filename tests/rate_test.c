/* tests/rate_test.c — verify FluxRate behavior */

#define FLUX_IMPL
#include "../flux.h"

#include <stdio.h>
#include <assert.h>

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); fails++; } \
} while (0)

int main(void) {
    /* T1: first call always fires + arms. */
    {
        FluxRate r;
        flux_rate_init(&r, 100);
        CHECK(flux_rate_due(&r, 1000) == 1, "first call should fire");
        CHECK(r.last_ms == 1000,             "first call should arm last_ms");
        CHECK(flux_rate_due(&r, 1050) == 0,  "50ms later (period=100) should NOT fire");
        CHECK(flux_rate_due(&r, 1099) == 0,  "99ms later should NOT fire");
        CHECK(flux_rate_due(&r, 1100) == 1,  "100ms later should fire");
        CHECK(r.last_ms == 1100,             "fire should advance last_ms by exactly one period");
        CHECK(flux_rate_due(&r, 1199) == 0,  "next 99ms should NOT fire again");
        CHECK(flux_rate_due(&r, 1200) == 1,  "200ms later should fire");
        printf("T1 basic period gate: %s\n", fails == 0 ? "PASS" : "FAIL");
    }

    /* T2: catch-up — backlog of 5 periods should fire ONCE per call (not 5x). */
    {
        FluxRate r;
        int saved_fails = fails;
        flux_rate_init(&r, 100);
        CHECK(flux_rate_due(&r, 1) == 1,       "first call arms at t=1");
        CHECK(flux_rate_due(&r, 501) == 1,     "fires after 500ms backlog");
        CHECK(r.last_ms == 501,                "advances by full backlog (no drift)");
        CHECK(flux_rate_due(&r, 501) == 0,     "second call same time should not refire");
        CHECK(flux_rate_due(&r, 601) == 1,     "100ms later fires again");
        printf("T2 catch-up: %s\n", fails == saved_fails ? "PASS" : "FAIL");
    }

    /* T3: period 0 = always fires (no rate gating). */
    {
        FluxRate r;
        int saved = fails;
        flux_rate_init(&r, 0);
        CHECK(flux_rate_due(&r, 100) == 1, "period 0 always fires (call 1)");
        CHECK(flux_rate_due(&r, 100) == 1, "period 0 always fires (same time)");
        CHECK(flux_rate_due(&r, 200) == 1, "period 0 always fires (later)");
        printf("T3 period 0 (always due): %s\n", fails == saved ? "PASS" : "FAIL");
    }

    /* T4: flux_rate_set_fps converts correctly. */
    {
        FluxRate r;
        int saved = fails;
        flux_rate_set_fps(&r, 30);   CHECK(r.period_ms == 33, "30fps → 33ms");
        flux_rate_set_fps(&r, 60);   CHECK(r.period_ms == 16, "60fps → 16ms");
        flux_rate_set_fps(&r, 120);  CHECK(r.period_ms == 8,  "120fps → 8ms");
        flux_rate_set_fps(&r, 1);    CHECK(r.period_ms == 1000, "1fps → 1000ms");
        flux_rate_set_fps(&r, 0);    CHECK(r.period_ms == 0, "0fps → 0ms (always)");
        flux_rate_set_fps(&r, -10);  CHECK(r.period_ms == 0, "neg fps → 0ms");
        CHECK(FLUX_FPS_TO_MS(10) == 100, "macro: 10fps → 100ms");
        CHECK(FLUX_FPS_TO_MS(0)  == 0,   "macro: 0fps → 0ms");
        printf("T4 set_fps: %s\n", fails == saved ? "PASS" : "FAIL");
    }

    /* T5: NULL-safe. */
    {
        int saved = fails;
        flux_rate_init(NULL, 100);                  /* must not crash */
        CHECK(flux_rate_due(NULL, 100) == 0, "NULL rate → 0");
        flux_rate_set_fps(NULL, 60);                /* must not crash */
        flux_rate_reset(NULL);                      /* must not crash */
        printf("T5 NULL-safe: %s\n", fails == saved ? "PASS" : "FAIL");
    }

    /* T6: reset re-arms (next due returns 1). */
    {
        FluxRate r;
        int saved = fails;
        flux_rate_init(&r, 100);
        (void)flux_rate_due(&r, 0);            /* arm */
        CHECK(flux_rate_due(&r, 50) == 0, "50ms later not due");
        flux_rate_reset(&r);
        CHECK(flux_rate_due(&r, 50) == 1, "after reset, fires immediately");
        printf("T6 reset: %s\n", fails == saved ? "PASS" : "FAIL");
    }

    /* T7: flux_now_ms is monotonic. */
    {
        int saved = fails;
        uint64_t t0 = flux_now_ms();
        struct timespec slp = { 0, 10000000 };  /* 10ms */
        nanosleep(&slp, NULL);
        uint64_t t1 = flux_now_ms();
        CHECK(t1 >= t0,        "now_ms is monotonic non-decreasing");
        CHECK(t1 - t0 >= 5,    "now_ms advances at least 5ms after 10ms sleep");
        CHECK(t1 - t0 < 1000,  "now_ms doesn't jump wildly");
        printf("T7 flux_now_ms: %s (delta=%llums)\n",
               fails == saved ? "PASS" : "FAIL",
               (unsigned long long)(t1 - t0));
    }

    /* T8: realistic scenario — 5 widgets at 1/2/5/10/30 Hz over 1 second.
     * At a 120Hz outer loop (8ms tick) starting at t=8 and running through
     * t=1008 (126 calls), each rate's first call arms+fires immediately,
     * then subsequent fires happen on every period boundary. Expected
     * approximate counts (including the initial arm-fire):
     *   1Hz  → ~2  (arm + 1 period fire at t≈1008)
     *   2Hz  → ~3  (arm + 2 period fires)
     *   5Hz  → ~6  (arm + 5)
     *   10Hz → ~11 (arm + 10)
     *   30Hz → ~31 (arm + 30)
     */
    {
        FluxRate r1, r2, r5, r10, r30;
        flux_rate_init(&r1,  1000);
        flux_rate_init(&r2,  500);
        flux_rate_init(&r5,  200);
        flux_rate_init(&r10, 100);
        flux_rate_init(&r30, 33);

        int c1=0, c2=0, c5=0, c10=0, c30=0;
        for (int t = 8; t <= 1008; t += 8) {
            if (flux_rate_due(&r1,  (uint64_t)t)) c1++;
            if (flux_rate_due(&r2,  (uint64_t)t)) c2++;
            if (flux_rate_due(&r5,  (uint64_t)t)) c5++;
            if (flux_rate_due(&r10, (uint64_t)t)) c10++;
            if (flux_rate_due(&r30, (uint64_t)t)) c30++;
        }
        printf("T8 1s mix @120Hz tick: 1Hz=%d 2Hz=%d 5Hz=%d 10Hz=%d 30Hz=%d\n",
               c1, c2, c5, c10, c30);
        int saved = fails;
        CHECK(c1  >= 1 && c1  <= 3,    "1Hz fires 1-3 in 1s");
        CHECK(c2  >= 2 && c2  <= 4,    "2Hz fires 2-4 in 1s");
        CHECK(c5  >= 5 && c5  <= 7,    "5Hz fires 5-7 in 1s");
        CHECK(c10 >= 10 && c10 <= 12,  "10Hz fires 10-12 in 1s");
        CHECK(c30 >= 30 && c30 <= 34,  "30Hz fires 30-34 in 1s");
        printf("T8 rate accuracy: %s\n", fails == saved ? "PASS" : "FAIL");
    }

    if (fails == 0) {
        printf("\nALL FLUXRATE TESTS PASS\n");
        return 0;
    }
    printf("\n%d FAILURES\n", fails);
    return 1;
}
