#include <stdio.h>

#include "wgf_core_priv.h"
#include "wgf_window.h"

/* The window's safe area and orientation where nothing covers or turns it (headless, as
 * natively): every inset 0; the orientation its shape, landscape when as wide as tall; a
 * lock refused, none held, none offered, and a value that isn't one refused. The web's
 * are wgf_platform_device_web_test's. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    wgf_core_priv_init();
    expect(wgf_window_get_safe_inset_top() == 0 && wgf_window_get_safe_inset_right() == 0 &&
               wgf_window_get_safe_inset_bottom() == 0 && wgf_window_get_safe_inset_left() == 0,
           "no safe-area insets");

    expect(wgf_window_get_orientation() == WGF_WINDOW_ORIENTATION_LANDSCAPE, "1024 by 768: landscape");
    expect(wgf_window_set_size(400, 800) && wgf_window_get_orientation() == WGF_WINDOW_ORIENTATION_PORTRAIT,
           "taller than wide: portrait");
    expect(wgf_window_set_size(500, 500) && wgf_window_get_orientation() == WGF_WINDOW_ORIENTATION_LANDSCAPE,
           "square: landscape");

    expect(!wgf_window_can_lock_orientation(), "no lock offered");
    expect(!wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_PORTRAIT) &&
               !wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_LANDSCAPE) &&
               !wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_ANY),
           "a lock, or letting go, refused");
    expect(!wgf_window_request_orientation((wgf_window_orientation_t)7), "a value that isn't one refused");
    expect(wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_ANY, "no lock held");
    expect(wgf_window_get_orientation() == WGF_WINDOW_ORIENTATION_LANDSCAPE, "the shape unchanged");

    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_platform_window_test: passed\n");
    return failures == 0 ? 0 : 1;
}
