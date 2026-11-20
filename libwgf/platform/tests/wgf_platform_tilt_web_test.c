#include <emscripten.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_tilt.h"
#include "wgf_time.h"

/* The tilt in a browser (tools/run_in_browser.py), the page's events dispatched as a device
 * and a player would: asked for; where the browser asks permission (Chrome has
 * requestPermission), WAITING_FOR_TAP, asked at a tap (the browser's prompt stood in for by
 * one that grants, and the tap's user activation by one that is active, as a script's click
 * has none); STARTING; ON at the first reading, level measured as it is held, then a tilt
 * read from level; released, OFF, and events after read nothing. wgf_platform_tilt_test is
 * the logic's. */

static int failures;
static int phase;
static double since;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

EM_JS(void, orient, (double beta, double gamma), {
    window.dispatchEvent(new DeviceOrientationEvent('deviceorientation', {alpha : 0, beta : beta, gamma : gamma}));
})

EM_JS(void, tap_granting, (void), {
    DeviceOrientationEvent.requestPermission = () => Promise.resolve('granted');
    Object.defineProperty(navigator, 'userActivation', {value : {isActive : true}, configurable : true});
    window.dispatchEvent(new MouseEvent('click'));
})

static void done(void)
{
    if (failures == 0) printf("wgf_platform_tilt_web_test: passed\n");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    const double now = wgf_time_get_seconds();
    const wgf_tilt_status_t status = wgf_tilt_get_status();
    (void)user;
    if (phase > 0 && now - since > 5.0) {
        printf("FAIL: phase %d timed out, status %d\n", phase, (int)status);
        failures++;
        done();
    }
    switch (phase) {
        case 0:
            expect(wgf_tilt_request(), "asked");
            expect(wgf_tilt_get_status() == WGF_TILT_STATUS_WAITING_FOR_TAP || wgf_tilt_get_status() == WGF_TILT_STATUS_STARTING,
                   "WAITING_FOR_TAP where the browser asks, else STARTING");
            if (wgf_tilt_get_status() == WGF_TILT_STATUS_WAITING_FOR_TAP) tap_granting();
            phase = 1;
            since = now;
            break;
        case 1: /* the permission's answer comes in a promise */
            if (status == WGF_TILT_STATUS_STARTING) {
                phase = 2;
                since = now;
                orient(10.0, 4.0);
            }
            break;
        case 2: /* the first reading: ON, measuring level */
            expect(status == WGF_TILT_STATUS_ON && wgf_tilt_is_calibrating() && wgf_tilt_get_x() == 0.0f,
                   "ON at the first reading, level being measured");
            phase = 3;
            orient(10.0, 4.0);
            break;
        case 3: /* held there */
            if (wgf_tilt_is_ready()) {
                expect(wgf_tilt_get_x() == 0.0f && now - since >= 0.5, "level measured, held half a second");
                phase = 4;
                orient(10.0 + 6.0, 4.0 + 15.0);
            } else {
                orient(10.0, 4.0);
            }
            break;
        default:
            expect(wgf_tilt_get_x() > 0.49f && wgf_tilt_get_x() < 0.51f && wgf_tilt_get_y() > 0.19f &&
                       wgf_tilt_get_y() < 0.21f,
                   "a tilt from level: across 15 of 30, forward 6 of 30");
            wgf_tilt_release();
            orient(30.0, 30.0);
            expect(wgf_tilt_get_status() == WGF_TILT_STATUS_OFF && wgf_tilt_get_x() == 0.0f, "released: OFF, reading 0");
            done();
            break;
    }
}

int main(void)
{
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
