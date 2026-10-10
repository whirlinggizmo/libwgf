#include "wgf_platform_gamepad_priv.h"

/* No gamepads: macOS until its GameController framework is used, and a headless build,
 * whose tests feed pads of their own (wgf_platform_priv_gamepad_set_test_pad). */

void wgf_platform_priv_gamepad_platform_open(void)
{
}

void wgf_platform_priv_gamepad_platform_close(void)
{
}

void wgf_platform_priv_gamepad_platform_poll(wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    (void)pads;
}
