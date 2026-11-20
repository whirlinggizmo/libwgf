#ifndef WGF_PLATFORM_TILT_PRIV_H
#define WGF_PLATFORM_TILT_PRIV_H

#include <stdbool.h>

#include "wgf_tilt.h"

/* The platform's tilt (wgf_platform_tilt_web.c, _none.c): start asking or listening, and the
 * status it starts in; stop; and, each frame, the status now and whether a reading came since
 * the last take, the newest in `reading` (beta and gamma, in degrees, and the screen's angle). */
wgf_tilt_status_t wgf_platform_priv_tilt_start(void);
void wgf_platform_priv_tilt_stop(void);
wgf_tilt_status_t wgf_platform_priv_tilt_take(bool *fresh, double reading[3]);

/* Each frame's end (app): the platform's status and newest reading taken in. */
void wgf_platform_priv_tilt_end_frame(void);

/* What a take gives, fed in at `now` (seconds): for the tests, as end_frame does. */
void wgf_platform_priv_tilt_feed(wgf_tilt_status_t status, bool fresh, const double reading[3], double now);

#endif
