#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"
#include "wgf_gamepad.h"
#include "wgf.h"

/* Gamepads with test pads in place of the platform's, read frame by frame as the loop
 * reads them: connecting, slots, edges per frame and per tick, the triggers as
 * buttons, a pad unplugged while a button is held, the round dead zone, and values
 * clamped. */

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

static wgf_platform_priv_gamepad_t pad_state(const char *name)
{
    wgf_platform_priv_gamepad_t pad;
    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    snprintf(pad.name, sizeof(pad.name), "%s", name);
    return pad;
}

/* One frame: the pads read, then (as the loop does after the frame) edges cleared. */
static void next_frame(void)
{
    wgf_platform_priv_gamepad_end_tick();
    wgf_platform_priv_gamepad_end_frame();
    wgf_platform_priv_gamepad_begin_frame();
}

static void buttons(void)
{
    wgf_platform_priv_gamepad_t one = pad_state("test pad");

    expect(!wgf_gamepad_is_connected(0) && strcmp(wgf_gamepad_get_name(0), "") == 0, "no pads");
    wgf_platform_priv_gamepad_set_test_pad(1, &one);
    next_frame();
    expect(!wgf_gamepad_is_connected(0) && wgf_gamepad_is_connected(1), "a pad in slot 1");
    expect(strcmp(wgf_gamepad_get_name(1), "test pad") == 0, "its name");

    one.buttons[WGF_GAMEPAD_BUTTON_SOUTH] = true;
    wgf_platform_priv_gamepad_set_test_pad(1, &one);
    next_frame();
    expect(wgf_gamepad_get_button_state(1, WGF_GAMEPAD_BUTTON_SOUTH) == WGF_INPUT_STATE_PRESSED, "pressed");
    expect(wgf_gamepad_is_down(1, WGF_GAMEPAD_BUTTON_SOUTH), "and down");

    /* a tick reads its own edges: one tick sees the press, the next doesn't */
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    expect(wgf_gamepad_is_pressed(1, WGF_GAMEPAD_BUTTON_SOUTH), "the tick sees the press");
    wgf_platform_priv_gamepad_end_tick();
    expect(!wgf_gamepad_is_pressed(1, WGF_GAMEPAD_BUTTON_SOUTH) && wgf_gamepad_is_down(1, WGF_GAMEPAD_BUTTON_SOUTH),
           "the next tick sees it held");
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    expect(wgf_gamepad_is_pressed(1, WGF_GAMEPAD_BUTTON_SOUTH), "while the frame still sees the press");

    next_frame();
    expect(wgf_gamepad_get_button_state(1, WGF_GAMEPAD_BUTTON_SOUTH) == WGF_INPUT_STATE_DOWN, "then held");

    /* a trigger pulled past halfway is a button too */
    one.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER] = 0.4f;
    wgf_platform_priv_gamepad_set_test_pad(1, &one);
    next_frame();
    expect(near(wgf_gamepad_get_trigger(1, WGF_GAMEPAD_TRIGGER_RIGHT), 0.4f), "a trigger partway");
    expect(!wgf_gamepad_is_down(1, WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER), "isn't a press below halfway");
    one.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER] = 0.8f;
    wgf_platform_priv_gamepad_set_test_pad(1, &one);
    next_frame();
    expect(wgf_gamepad_is_pressed(1, WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER), "past halfway it is");

    /* unplugged while held: its buttons are released */
    wgf_platform_priv_gamepad_set_test_pad(1, NULL);
    next_frame();
    expect(!wgf_gamepad_is_connected(1), "unplugged");
    expect(wgf_gamepad_is_released(1, WGF_GAMEPAD_BUTTON_SOUTH) &&
               wgf_gamepad_is_released(1, WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER),
           "what it held is released");
    next_frame();
    expect(wgf_gamepad_get_button_state(1, WGF_GAMEPAD_BUTTON_SOUTH) == WGF_INPUT_STATE_UP, "then up");

    expect(wgf_gamepad_get_button_state(7, WGF_GAMEPAD_BUTTON_SOUTH) == WGF_INPUT_STATE_UP &&
               wgf_gamepad_get_button_state(0, (wgf_gamepad_button_t)99) == WGF_INPUT_STATE_UP &&
               !wgf_gamepad_is_connected(-1),
           "a pad or button that isn't one");
}

static void sticks(void)
{
    wgf_platform_priv_gamepad_t pad = pad_state("sticks");
    wgf_vec2_t v;

    expect(near(wgf_gamepad_get_deadzone(), 0.15f), "a dead zone of 0.15 by default");
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = 0.1f;
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y] = 0.1f; /* 0.14 from the middle */
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X] = 1.0f;
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y] = 1.0f; /* a square stick's corner, past 1 */
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
    next_frame();
    v = wgf_gamepad_get_stick(0, WGF_GAMEPAD_STICK_LEFT);
    expect(v.x == 0.0f && v.y == 0.0f, "inside the dead zone: the middle");
    v = wgf_gamepad_get_stick(0, WGF_GAMEPAD_STICK_RIGHT);
    expect(near(sqrtf(v.x * v.x + v.y * v.y), 1.0f) && near(v.x, v.y), "a corner stops at a length of 1");

    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = 0.0f;
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y] = -0.575f; /* halfway from the dead zone to the edge, up */
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
    next_frame();
    v = wgf_gamepad_get_stick(0, WGF_GAMEPAD_STICK_LEFT);
    expect(near(v.x, 0.0f) && near(v.y, -0.5f), "rescaled past the dead zone, y down");

    wgf_gamepad_set_deadzone(0.0f);
    v = wgf_gamepad_get_stick(0, WGF_GAMEPAD_STICK_LEFT);
    expect(near(v.y, -0.575f), "no dead zone: as it is");
    wgf_gamepad_set_deadzone(2.0f);
    expect(near(wgf_gamepad_get_deadzone(), 0.9f), "the dead zone is at most 0.9");
    wgf_gamepad_set_deadzone(-1.0f);
    expect(wgf_gamepad_get_deadzone() == 0.0f, "and at least 0");
    wgf_gamepad_set_deadzone(0.15f);

    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER] = 3.0f;
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = -5.0f;
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
    next_frame();
    expect(near(wgf_gamepad_get_trigger(0, WGF_GAMEPAD_TRIGGER_LEFT), 1.0f), "a trigger is clamped to 1");
    v = wgf_gamepad_get_stick(0, WGF_GAMEPAD_STICK_LEFT);
    expect(v.x >= -1.0f && v.x < 0.0f, "a stick axis is clamped to -1");
    expect(wgf_gamepad_get_trigger(0, (wgf_gamepad_trigger_t)5) == 0.0f &&
               wgf_gamepad_get_stick(3, WGF_GAMEPAD_STICK_LEFT).x == 0.0f,
           "a trigger or pad that isn't one");
}

int main(void)
{
    wgf_core_priv_init();
    wgf_platform_priv_gamepad_set_test_pad(0, NULL); /* test pads, never the platform's */
    wgf_platform_priv_gamepad_open();
    buttons();
    sticks();
    wgf_platform_priv_gamepad_close();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
