#ifndef WGF_TILT_H
#define WGF_TILT_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The device's tilt, from level and turned with the screen, for a game steered by tilting a
 * phone or a tablet: on the web, the page's device orientation; natively, UNAVAILABLE (as
 * helix's Tilt service, whose behavior this is). Ask with wgf_tilt_request, then read each
 * frame:
 *
 *   x += speed * wgf_tilt_get_x() * dt;
 *
 * A page may need permission, asked at the player's next tap (iOS, and Chrome on Android),
 * so a game asks before the tap that starts play. Level is measured once the device is held
 * within 3 degrees for half a second (as the tilt starts, after the screen turns, and after
 * wgf_tilt_calibrate); the tilt reads 0 meanwhile. Readings are taken as each frame ends, once
 * asked for: a program that never asks links none of it. */

typedef enum wgf_tilt_status_t {
    WGF_TILT_STATUS_OFF = 0,             /* not asked for, or released */
    WGF_TILT_STATUS_WAITING_FOR_TAP = 1, /* the browser's permission is asked at the next tap */
    WGF_TILT_STATUS_STARTING = 2,        /* asked, no reading yet */
    WGF_TILT_STATUS_ON = 3,
    WGF_TILT_STATUS_REFUSED = 4,     /* the player refused the permission */
    WGF_TILT_STATUS_UNAVAILABLE = 5, /* no sensor: natively, or no reading 1.5 s after the page has its focus
                                        (a reading after still turns it ON) */
    WGF_TILT_STATUS_NEEDS_HTTPS = 6  /* the page isn't secure, so the browser reads no orientation */
} wgf_tilt_status_t;

/* Ask for the tilt. False where it can't be had at once (UNAVAILABLE, NEEDS_HTTPS); true when
 * asked, the status then saying how it goes. Asking again while asked does nothing. */
WGF_API bool wgf_tilt_request(void);
/* Stop the readings, OFF, reading 0 from then. A permission granted stays granted. */
WGF_API void wgf_tilt_release(void);
WGF_API wgf_tilt_status_t wgf_tilt_get_status(void);
/* Whether level is being measured, reading 0 until it is. */
WGF_API bool wgf_tilt_is_calibrating(void);
/* ON, and level measured: the tilt steers. */
WGF_API bool wgf_tilt_is_ready(void);
/* Measure level again from how the device is held next, as a game's "Level" button does. */
WGF_API void wgf_tilt_calibrate(void);
/* How far the device is tilted from level, -1 to 1, `range` degrees reading 1: x across the
 * screen, right positive; y up and down it, down positive. 0 while calibrating, or not ON. */
WGF_API float wgf_tilt_get_x(void);
WGF_API float wgf_tilt_get_y(void);
/* The degrees of tilt that read 1, at least 1; default 30. */
WGF_API void wgf_tilt_set_range(float degrees);
WGF_API float wgf_tilt_get_range(void);

#ifdef __cplusplus
}
#endif

#endif
