#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_gamepad.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_window.h"

/* The pads through the loop's hook, headless: none joined to the loop until a pad is first
 * reached mid-run (here a test pad, as a program's first wgf_gamepad call would), read at
 * once, then at each frame's start, its press an edge for one frame, as from the first frame. */

static int failures, frame;
static wgf_platform_priv_gamepad_t pad;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void on_frame(void *user)
{
    (void)user;
    switch (++frame) {
        case 3: /* a pad reached for the first time, the window long open */
            memset(&pad, 0, sizeof(pad));
            pad.connected = true;
            snprintf(pad.name, sizeof(pad.name), "test pad");
            wgf_platform_priv_gamepad_set_test_pad(0, &pad);
            expect(wgf_gamepad_is_connected(0) && strcmp(wgf_gamepad_get_name(0), "test pad") == 0,
                   "joined to the loop: read at once, by the call that reached it");
            break;
        case 4:
            expect(wgf_gamepad_is_connected(0), "and at each frame's start after");
            pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH] = true;
            wgf_platform_priv_gamepad_set_test_pad(0, &pad);
            break;
        case 5:
            expect(wgf_gamepad_is_pressed(0, WGF_GAMEPAD_BUTTON_SOUTH), "a press, the frame after");
            break;
        case 6:
            expect(!wgf_gamepad_is_pressed(0, WGF_GAMEPAD_BUTTON_SOUTH) && wgf_gamepad_is_down(0, WGF_GAMEPAD_BUTTON_SOUTH),
                   "then held: its edges ended with the frame");
            wgf_app_quit();
            break;
        default: break;
    }
}

int main(void)
{
    wgf_window_set_size(64, 64);
    expect(wgf_app_run(NULL, NULL, on_frame, NULL, NULL), "runs");
    expect(frame == 6, "every frame ran");
    if (failures == 0) printf("wgf_app_pads_test: passed\n");
    return failures == 0 ? 0 : 1;
}
