#ifndef WGF_TILT_H
#define WGF_TILT_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The device's tilt, for a game steered by tilting a phone or a tablet.
 *
 * The tilt is measured from level, and turns with the screen. On the web it comes from the
 * page's device orientation. Natively it is UNAVAILABLE. Ask for it with wgf_tilt_request,
 * then read it each frame:
 *
 *   x += speed * wgf_tilt_get_x() * dt;
 *
 * - A page may need permission, asked at the player's next tap (iOS, and Chrome on Android).
 *   So a game asks before the tap that starts play.
 * - Level is measured once the device is held within 3 degrees for half a second. That happens
 *   as the tilt starts, after the screen turns, and after wgf_tilt_calibrate. The tilt reads 0
 *   meanwhile.
 * - Once asked for, readings are taken as each frame ends. A program that never asks links
 *   none of the tilt's code. */

typedef enum wgf_tilt_status_t {
    WGF_TILT_STATUS_OFF = 0,             /* not asked for, or released */
    WGF_TILT_STATUS_WAITING_FOR_TAP = 1, /* the browser's permission will be asked at the next tap */
    WGF_TILT_STATUS_STARTING = 2,        /* asked for, with no reading yet */
    WGF_TILT_STATUS_ON = 3,
    WGF_TILT_STATUS_REFUSED = 4,     /* the player refused the permission */
    WGF_TILT_STATUS_UNAVAILABLE = 5, /* no sensor: natively, or no reading 1.5 s after the page has focus
                                        (a later reading still turns it ON) */
    WGF_TILT_STATUS_NEEDS_HTTPS = 6  /* the page isn't secure, so the browser reads no orientation */
} wgf_tilt_status_t;

/* Asks for the tilt.
 *
 * Returns false if it can't be had at once (UNAVAILABLE, NEEDS_HTTPS). Returns true once asked,
 * and the status then says how it goes. Asking again while asked does nothing. */
WGF_API bool wgf_tilt_request(void);
/* Stops the readings: the status is OFF, and the tilt reads 0 from then. A permission granted
 * stays granted. */
WGF_API void wgf_tilt_release(void);
WGF_API wgf_tilt_status_t wgf_tilt_get_status(void);
/* Returns whether level is being measured. The tilt reads 0 until it is. */
WGF_API bool wgf_tilt_is_calibrating(void);
/* Returns whether the status is ON and level is measured, so the tilt steers. */
WGF_API bool wgf_tilt_is_ready(void);
/* Measures level again from how the device is held next, as a game's "Level" button does. */
WGF_API void wgf_tilt_calibrate(void);
/* Returns how far the device is tilted from level, -1 to 1. A tilt of the range's degrees
 * reads 1 (see wgf_tilt_set_range).
 *
 * - x is across the screen, right positive.
 * - y is up and down the screen, down positive.
 *
 * Returns 0 while calibrating, or while the status isn't ON. */
WGF_API float wgf_tilt_get_x(void);
WGF_API float wgf_tilt_get_y(void);
/* Sets or gets the tilt, in degrees, that reads 1. It is at least 1 (default: 30). */
WGF_API void wgf_tilt_set_range(float degrees);
WGF_API float wgf_tilt_get_range(void);

#ifdef __cplusplus
}
#endif

#endif
