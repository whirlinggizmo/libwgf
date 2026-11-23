#include <math.h>
#include <stdio.h>

#include "wgf_platform_tilt_priv.h"
#include "wgf_tilt.h"

/* The tilt, its readings fed in at given times as each frame's take gives them: natively
 * UNAVAILABLE; level measured once held within 3 degrees for half a second, 0 until then;
 * readings from level, scaled by the range and clamped, turned with the screen, which levels
 * again as it turns; calibrate; and a status other than ON reading 0.
 * wgf_platform_tilt_web_test is the page's, its events dispatched. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void feed(double beta, double gamma, double angle, double now)
{
    const double reading[3] = {beta, gamma, angle};
    wgf_platform_priv_tilt_feed(WGF_TILT_STATUS_ON, true, reading, now);
}

int main(void)
{
    expect(wgf_tilt_get_status() == WGF_TILT_STATUS_OFF && !wgf_tilt_is_ready() && wgf_tilt_get_range() == 30.0f,
           "OFF to start, a range of 30");
    expect(!wgf_tilt_request() && wgf_tilt_get_status() == WGF_TILT_STATUS_UNAVAILABLE, "natively: UNAVAILABLE");
    wgf_tilt_release();
    expect(wgf_tilt_get_status() == WGF_TILT_STATUS_OFF, "released: OFF");

    wgf_platform_priv_tilt_feed(WGF_TILT_STATUS_STARTING, false, NULL, 0.0);
    expect(wgf_tilt_get_status() == WGF_TILT_STATUS_STARTING && !wgf_tilt_is_ready(), "STARTING");
    feed(10, 5, 0, 1.0);
    expect(wgf_tilt_get_status() == WGF_TILT_STATUS_ON && wgf_tilt_is_calibrating() && !wgf_tilt_is_ready() &&
               wgf_tilt_get_x() == 0.0f,
           "ON, level being measured, reading 0");
    feed(12, 7, 0, 1.3);
    expect(wgf_tilt_is_calibrating(), "held within 3 degrees, not yet half a second");
    feed(20, 5, 0, 1.4);
    feed(21, 6, 0, 1.8);
    expect(wgf_tilt_is_calibrating(), "moved more than 3 degrees: the half second starts again");
    feed(21, 6, 0, 1.95);
    expect(!wgf_tilt_is_calibrating() && wgf_tilt_is_ready() && wgf_tilt_get_x() == 0.0f && wgf_tilt_get_y() == 0.0f,
           "held half a second: that's level, reading 0");
    feed(21 + 15, 6 + 6, 0, 2.0);
    expect(near(wgf_tilt_get_x(), 0.2f) && near(wgf_tilt_get_y(), 0.5f),
           "from level: across 6 of 30, forward 15 of 30");
    feed(21 - 90, 6 + 45, 0, 2.1);
    expect(near(wgf_tilt_get_x(), 1.0f) && near(wgf_tilt_get_y(), -1.0f), "past the range: clamped");
    wgf_tilt_set_range(60.0f);
    feed(21 + 15, 6 + 6, 0, 2.2);
    expect(near(wgf_tilt_get_x(), 0.1f) && near(wgf_tilt_get_y(), 0.25f), "a range of 60");
    wgf_tilt_set_range(0.0f);
    expect(wgf_tilt_get_range() == 1.0f, "a range at least 1");
    wgf_tilt_set_range(30.0f);

    feed(0, 0, 90, 3.0);
    expect(wgf_tilt_is_calibrating() && wgf_tilt_get_x() == 0.0f, "the screen turned a quarter: level again");
    feed(0, 0, 90, 3.6);
    feed(15, 6, 90, 3.7);
    expect(near(wgf_tilt_get_x(), 0.5f) && near(wgf_tilt_get_y(), -0.2f), "turned: forward is across, across is up");
    feed(0, 0, -90, 4.0);
    feed(0, 0, 270, 4.6);
    feed(15, 6, 270, 4.7);
    expect(near(wgf_tilt_get_x(), -0.5f) && near(wgf_tilt_get_y(), 0.2f), "a quarter the other way (-90 is 270)");

    wgf_tilt_calibrate();
    expect(wgf_tilt_is_calibrating() && wgf_tilt_get_x() == 0.0f && wgf_tilt_get_y() == 0.0f, "calibrate: level again");
    feed(15, 6, 270, 5.0);
    feed(15, 6, 270, 5.5);
    expect(wgf_tilt_is_ready() && wgf_tilt_get_x() == 0.0f, "level where it was held");
    feed(15, 21, 270, 5.6);
    expect(!near(wgf_tilt_get_y(), 0.0f), "and reading from there");
    wgf_platform_priv_tilt_feed(WGF_TILT_STATUS_REFUSED, false, NULL, 6.0);
    expect(wgf_tilt_get_status() == WGF_TILT_STATUS_REFUSED && wgf_tilt_get_x() == 0.0f && wgf_tilt_get_y() == 0.0f &&
               !wgf_tilt_is_ready(),
           "not ON: reading 0");
    feed(15, 21, 270, 6.1);
    expect(wgf_tilt_get_status() == WGF_TILT_STATUS_ON, "a reading turns it ON again");
    if (failures == 0) printf("wgf_platform_tilt_test: passed\n");
    return failures == 0 ? 0 : 1;
}
