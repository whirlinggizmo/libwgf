#include "wgf_platform_tilt_priv.h"

/* No tilt to read: natively (sokol_app reads no sensors), and headless. */

wgf_tilt_status_t wgf_platform_priv_tilt_start(void)
{
    return WGF_TILT_STATUS_UNAVAILABLE;
}

void wgf_platform_priv_tilt_stop(void)
{
}

wgf_tilt_status_t wgf_platform_priv_tilt_take(bool *fresh, double reading[3])
{
    (void)reading;
    *fresh = false;
    return WGF_TILT_STATUS_UNAVAILABLE;
}
