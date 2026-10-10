#ifndef WGF_APP_TICK_CLOCK_PRIV_H
#define WGF_APP_TICK_CLOCK_PRIV_H

#include <stdbool.h>

/* Fixed-rate tick scheduling for the runtime's ticks, ported from wgrender's
 * wgr_tick_clock. Pure logic (no clock, no callbacks) so it can be unit tested;
 * wgf_app.c feeds it real elapsed time and runs the ticks it asks for. */

#define WGF_APP_PRIV_MAX_TICKS_PER_FRAME 5

typedef struct wgf_app_priv_tick_clock_t {
    double step;        /* seconds per tick; 0 = no tick */
    double accumulator; /* elapsed time not yet consumed by ticks */
} wgf_app_priv_tick_clock_t;

/* hz <= 0 disables ticking. Resets the accumulator. */
void wgf_app_priv_tick_clock_set_rate(wgf_app_priv_tick_clock_t *clock, int hz);
bool wgf_app_priv_tick_clock_enabled(const wgf_app_priv_tick_clock_t *clock);

/* Add `elapsed` seconds and return how many ticks to run now (at most
 * `max_ticks`). If still a full step or more behind after that, the backlog is
 * dropped (keeping the phase) so a stall can't snowball. */
int wgf_app_priv_tick_clock_advance(wgf_app_priv_tick_clock_t *clock, double elapsed, int max_ticks);

/* How far into the next tick we are, 0..1 (0 when no tick is set). */
float wgf_app_priv_tick_clock_fraction(const wgf_app_priv_tick_clock_t *clock);

#endif
