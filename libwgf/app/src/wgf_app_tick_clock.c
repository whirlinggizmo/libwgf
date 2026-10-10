#include "wgf_app_tick_clock_priv.h"

#include <math.h>
#include <stddef.h>

/* Ported from wgrender's wgr_tick_clock. */

void wgf_app_priv_tick_clock_set_rate(wgf_app_priv_tick_clock_t *clock, int hz)
{
    if (clock == NULL) {
        return;
    }
    clock->step = hz > 0 ? 1.0 / (double)hz : 0.0;
    clock->accumulator = 0.0;
}

bool wgf_app_priv_tick_clock_enabled(const wgf_app_priv_tick_clock_t *clock)
{
    return clock != NULL && clock->step > 0.0;
}

int wgf_app_priv_tick_clock_advance(wgf_app_priv_tick_clock_t *clock, double elapsed, int max_ticks)
{
    int ticks = 0;
    double due;

    if (!wgf_app_priv_tick_clock_enabled(clock)) {
        return 0;
    }
    if (elapsed > 0.0) {
        clock->accumulator += elapsed;
    }
    /* tolerate rounding: frames exactly one step apart must tick every frame, not
     * miss one and run two the next */
    due = clock->step * (1.0 - 1e-6);
    while (clock->accumulator >= due && ticks < max_ticks) {
        clock->accumulator -= clock->step;
        ticks++;
    }
    if (clock->accumulator < 0.0) {
        clock->accumulator = 0.0;
    }
    if (clock->accumulator >= due) {
        clock->accumulator = fmod(clock->accumulator, clock->step); /* stalled: drop the backlog */
    }
    return ticks;
}

float wgf_app_priv_tick_clock_fraction(const wgf_app_priv_tick_clock_t *clock)
{
    if (!wgf_app_priv_tick_clock_enabled(clock)) {
        return 0.0f;
    }
    return (float)(clock->accumulator / clock->step);
}
