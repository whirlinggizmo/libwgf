#include "wgf_tilt.h"

#include <math.h>
#include <stddef.h>

#include "wgf_platform_input_priv.h"
#include "wgf_platform_tilt_priv.h"
#include "wgf_time.h"

/* The tilt, helix's Tilt service in C: the platform's readings (its web or none file) taken
 * as each frame ends once it is asked for (the loop's hook: a program that never asks links
 * none of it), level measured once the device is held steady, and each reading taken
 * from level, turned with the screen, and scaled by the range. */

#define STEADY_DEGREES 3.0
#define STEADY_SECONDS 0.5

static struct {
    bool requested;
    wgf_tilt_status_t status;
    bool calibrating;
    float x, y;
    float range;
    /* the reading measured as level, and the screen's quarter turn when it was; while level is
       being measured, the reading the device is held near, and since when */
    bool level;
    double level_beta, level_gamma;
    int level_quarter;
    bool steady;
    double steady_beta, steady_gamma, steady_since;
} tilt = {.range = 30.0f};

static float clamp_unit(double value)
{
    return value < -1.0 ? -1.0f : value > 1.0 ? 1.0f : (float)value;
}

static void reading(double beta, double gamma, double angle, double now)
{
    const int quarter = (int)lround(fmod(fmod(angle, 360.0) + 360.0, 360.0) / 90.0) % 4;
    double forward, across, x, y;
    if (quarter != tilt.level_quarter) { /* the screen turned: level again */
        tilt.level = false;
        tilt.steady = false;
        tilt.level_quarter = quarter;
    }
    if (!tilt.level) {
        if (!tilt.steady || fabs(beta - tilt.steady_beta) > STEADY_DEGREES || fabs(gamma - tilt.steady_gamma) > STEADY_DEGREES) {
            tilt.steady = true;
            tilt.steady_beta = beta;
            tilt.steady_gamma = gamma;
            tilt.steady_since = now;
        }
        tilt.x = tilt.y = 0.0f;
        if (now - tilt.steady_since < STEADY_SECONDS) {
            tilt.calibrating = true;
            return;
        }
        tilt.level = true;
        tilt.level_beta = beta;
        tilt.level_gamma = gamma;
    }
    tilt.calibrating = false;
    forward = beta - tilt.level_beta;
    across = gamma - tilt.level_gamma;
    /* the screen turned counterclockwise from the device's way up: its across and down in the
       device's terms */
    switch (quarter) {
        case 1: x = forward, y = -across; break;
        case 2: x = -across, y = -forward; break;
        case 3: x = -forward, y = across; break;
        default: x = across, y = forward; break;
    }
    tilt.x = clamp_unit(x / tilt.range);
    tilt.y = clamp_unit(y / tilt.range);
}

void wgf_platform_priv_tilt_feed(wgf_tilt_status_t status, bool fresh, const double reading_now[3], double now)
{
    if (status != tilt.status) {
        tilt.status = status;
        if (status != WGF_TILT_STATUS_ON) {
            tilt.x = tilt.y = 0.0f;
            tilt.calibrating = false;
        }
    }
    if (fresh && status == WGF_TILT_STATUS_ON) reading(reading_now[0], reading_now[1], reading_now[2], now);
}

void wgf_platform_priv_tilt_end_frame(void)
{
    double newest[3] = {0.0, 0.0, 0.0};
    bool fresh = false;
    wgf_tilt_status_t status;
    if (!tilt.requested) return;
    status = wgf_platform_priv_tilt_take(&fresh, newest);
    wgf_platform_priv_tilt_feed(status, fresh, newest, wgf_time_get_seconds());
}

bool wgf_tilt_request(void)
{
    if (!tilt.requested) {
        tilt.requested = true;
        tilt.level = tilt.steady = false;
        wgf_platform_priv_set_tilt_frame(wgf_platform_priv_tilt_end_frame); /* taken in each frame from now */
        wgf_platform_priv_tilt_feed(wgf_platform_priv_tilt_start(), false, NULL, 0.0);
    }
    return tilt.status != WGF_TILT_STATUS_UNAVAILABLE && tilt.status != WGF_TILT_STATUS_NEEDS_HTTPS;
}

void wgf_tilt_release(void)
{
    if (tilt.requested) wgf_platform_priv_tilt_stop();
    tilt.requested = false;
    tilt.level = tilt.steady = false;
    wgf_platform_priv_tilt_feed(WGF_TILT_STATUS_OFF, false, NULL, 0.0);
}

wgf_tilt_status_t wgf_tilt_get_status(void)
{
    return tilt.status;
}

bool wgf_tilt_is_calibrating(void)
{
    return tilt.calibrating;
}

bool wgf_tilt_is_ready(void)
{
    return tilt.status == WGF_TILT_STATUS_ON && !tilt.calibrating;
}

void wgf_tilt_calibrate(void)
{
    tilt.level = tilt.steady = false;
    tilt.x = tilt.y = 0.0f;
    if (tilt.status == WGF_TILT_STATUS_ON) tilt.calibrating = true;
}

float wgf_tilt_get_x(void)
{
    return tilt.x;
}

float wgf_tilt_get_y(void)
{
    return tilt.y;
}

void wgf_tilt_set_range(float degrees)
{
    tilt.range = degrees > 1.0f ? degrees : 1.0f; /* NaN too reads as 1 */
}

float wgf_tilt_get_range(void)
{
    return tilt.range;
}
