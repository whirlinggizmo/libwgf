#include "wgf_app_tick_clock_priv.h"
#include <math.h>
#include <stdio.h>

/* The runtime's tick clock, ported from wgrender's tick_test. */

static int failures;

static void check(int ok, const char *what, int line)
{
    if (!ok) {
        printf("FAIL: line %d: %s\n", line, what);
        failures++;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_NEAR(actual, expected, eps) check(fabs((double)(actual) - (double)(expected)) <= (eps), #actual, __LINE__)

#define EPS 1e-6

static void test_tick_clock_rate(void)
{
    wgf_app_priv_tick_clock_t clock;

    wgf_app_priv_tick_clock_set_rate(&clock, 0);
    CHECK(!wgf_app_priv_tick_clock_enabled(&clock));
    CHECK(wgf_app_priv_tick_clock_advance(&clock, 1.0, WGF_APP_PRIV_MAX_TICKS_PER_FRAME) == 0);
    CHECK_NEAR(wgf_app_priv_tick_clock_fraction(&clock), 0, EPS);

    /* frames exactly one step apart tick exactly once each, despite rounding */
    wgf_app_priv_tick_clock_set_rate(&clock, 60);
    int total = 0, per_frame_ok = 1;
    for (int i = 0; i < 600; i++) {
        int n = wgf_app_priv_tick_clock_advance(&clock, 1.0 / 60.0, WGF_APP_PRIV_MAX_TICKS_PER_FRAME);
        per_frame_ok &= (n == 1);
        total += n;
    }
    CHECK(per_frame_ok);
    CHECK(total == 600);

    /* 30 Hz ticks under 144 Hz frames: 30 ticks per second, at most one per frame,
     * fraction always in [0, 1) */
    wgf_app_priv_tick_clock_set_rate(&clock, 30);
    total = 0;
    int max_per_frame = 0, fraction_ok = 1;
    for (int i = 0; i < 144; i++) {
        int n = wgf_app_priv_tick_clock_advance(&clock, 1.0 / 144.0, WGF_APP_PRIV_MAX_TICKS_PER_FRAME);
        float f = wgf_app_priv_tick_clock_fraction(&clock);
        total += n;
        max_per_frame = n > max_per_frame ? n : max_per_frame;
        fraction_ok &= (f >= 0.0f && f < 1.0f);
    }
    CHECK(total >= 29 && total <= 30);
    CHECK(max_per_frame == 1);
    CHECK(fraction_ok);

    /* negative elapsed is ignored */
    wgf_app_priv_tick_clock_set_rate(&clock, 10);
    CHECK(wgf_app_priv_tick_clock_advance(&clock, -5.0, WGF_APP_PRIV_MAX_TICKS_PER_FRAME) == 0);
    CHECK_NEAR(wgf_app_priv_tick_clock_fraction(&clock), 0, EPS);
}

static void test_tick_clock_stall(void)
{
    wgf_app_priv_tick_clock_t clock;
    wgf_app_priv_tick_clock_set_rate(&clock, 10); /* step 0.1 s */

    /* partial progress shows up as the fraction */
    CHECK(wgf_app_priv_tick_clock_advance(&clock, 0.025, WGF_APP_PRIV_MAX_TICKS_PER_FRAME) == 0);
    CHECK_NEAR(wgf_app_priv_tick_clock_fraction(&clock), 0.25, 1e-4);

    /* a 2.35 s stall (23 steps due, 0.025 already accumulated): run the maximum of
     * 5, drop the backlog, keep the phase (2.375 s total -> 0.075 into a step) */
    CHECK(wgf_app_priv_tick_clock_advance(&clock, 2.35, WGF_APP_PRIV_MAX_TICKS_PER_FRAME) == 5);
    CHECK_NEAR(wgf_app_priv_tick_clock_fraction(&clock), 0.75, 1e-4);

    /* and normal ticking resumes */
    CHECK(wgf_app_priv_tick_clock_advance(&clock, 0.1, WGF_APP_PRIV_MAX_TICKS_PER_FRAME) == 1);

    /* changing the rate restarts the accumulator */
    wgf_app_priv_tick_clock_set_rate(&clock, 20);
    CHECK_NEAR(wgf_app_priv_tick_clock_fraction(&clock), 0, EPS);
}

int main(void)
{
    test_tick_clock_rate();
    test_tick_clock_stall();
    return failures == 0 ? 0 : 1;
}
