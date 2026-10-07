#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_action.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"

/* Input actions, headless, with keys and touches as the window's events and a test pad:
 * binding and its refusals, a key, a pair of keys as an axis, a pad axis either side and
 * one side past a threshold, a trigger, a pad button on any pad, a touch region, the
 * furthest binding winning, pressed and released per frame and per tick, a tap within a
 * frame, clearing, and the bindings listed as text. */

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

static void key(sapp_event_type type, sapp_keycode code)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = code;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
}

static void finger(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.num_touches = 1;
    event.touches[0].identifier = 7;
    event.touches[0].pos_x = x;
    event.touches[0].pos_y = y;
    event.touches[0].changed = true;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
}

/* A frame ended (a tick ran in it), as the loop ends one, and the pads read for the next. */
static void next_frame(void)
{
    wgf_platform_priv_input_end_tick();
    wgf_platform_priv_input_end_frame();
    wgf_platform_priv_gamepad_end_tick();
    wgf_platform_priv_gamepad_end_frame();
    wgf_platform_priv_gamepad_begin_frame();
}

static void binding(void)
{
    expect(wgf_action_bind_key("fire", WGF_KEY_SPACE) && wgf_action_bind_pad_button("fire", WGF_GAMEPAD_BUTTON_SOUTH),
           "a key and a button bound");
    expect(!wgf_action_bind_key("", WGF_KEY_SPACE) && !wgf_action_bind_key(NULL, WGF_KEY_SPACE) &&
               !wgf_action_bind_key("0123456789012345678901234567890123", WGF_KEY_SPACE) &&
               !wgf_action_bind_key("fire", (wgf_keyboard_key_t)5) &&
               !wgf_action_bind_pad_button("fire", (wgf_gamepad_button_t)40) &&
               !wgf_action_bind_pad_axis("steer", WGF_GAMEPAD_AXIS_LEFT_X, 2, 0.1f) &&
               !wgf_action_bind_pad_axis("steer", WGF_GAMEPAD_AXIS_LEFT_X, 0, 1.5f) &&
               !wgf_action_bind_touch("fire", 0, 0, 0, 10),
           "a name, key, button, direction, threshold, or region that isn't one: refused");
    expect(wgf_action_get_count() == 1 && strcmp(wgf_action_get_name(0), "fire") == 0 &&
               strcmp(wgf_action_get_name(1), "") == 0,
           "one action made, the refused ones not");
}

static void keys_and_axes(void)
{
    wgf_platform_priv_gamepad_t pad;
    wgf_action_bind_keys("steer", WGF_KEY_LEFT, WGF_KEY_RIGHT);
    wgf_action_bind_pad_axis("steer", WGF_GAMEPAD_AXIS_LEFT_X, 0, 0.0f);
    wgf_action_bind_pad_axis("left", WGF_GAMEPAD_AXIS_LEFT_X, -1, 0.4f);
    wgf_action_bind_pad_axis("throttle", WGF_GAMEPAD_AXIS_RIGHT_TRIGGER, 1, 0.0f);
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    expect(near(wgf_action_get_axis("steer"), 0) && !wgf_action_is_down("steer"), "at rest");
    key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_LEFT);
    expect(near(wgf_action_get_axis("steer"), -1) && near(wgf_action_get_value("steer"), 1) &&
               wgf_action_is_pressed("steer"),
           "the pair's first key: -1, pressed");
    next_frame();
    expect(wgf_action_get_state("steer") == WGF_INPUT_STATE_DOWN, "held the next frame");
    key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_RIGHT);
    expect(near(wgf_action_get_axis("steer"), 0), "both keys: 0");
    key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_LEFT);
    key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_RIGHT);
    next_frame();
    expect(wgf_action_is_released("steer"), "let go: released");
    next_frame();
    expect(wgf_action_get_state("steer") == WGF_INPUT_STATE_UP, "then up");

    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    pad.axes[WGF_GAMEPAD_AXIS_LEFT_X] = -0.6f;
    pad.axes[WGF_GAMEPAD_AXIS_RIGHT_TRIGGER] = 0.25f;
    wgf_platform_priv_gamepad_set_test_pad(2, &pad); /* any pad, not only pad 0 */
    next_frame();
    expect(wgf_action_get_axis("steer") < -0.4f && wgf_action_get_axis("steer") > -0.6f,
           "a stick, past its dead zone, on pad 2");
    expect(wgf_action_is_down("left") && wgf_action_get_axis("left") > 0.4f,
           "one side of an axis, read 0 to 1, down past its threshold");
    expect(near(wgf_action_get_value("throttle"), 0.25f) && !wgf_action_is_down("throttle"), "a trigger, a quarter");
    key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_RIGHT);
    expect(near(wgf_action_get_axis("steer"), 1), "the binding pushed furthest wins");
    key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_RIGHT);
    pad.axes[WGF_GAMEPAD_AXIS_LEFT_X] = 0.3f;
    wgf_platform_priv_gamepad_set_test_pad(2, &pad);
    next_frame();
    expect(wgf_action_is_released("left") && near(wgf_action_get_axis("left"), 0), "the other side: none, released");
    wgf_platform_priv_gamepad_set_test_pad(2, NULL);
    next_frame();
}

static void buttons_taps_and_ticks(void)
{
    wgf_platform_priv_gamepad_t pad;
    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH] = true;
    wgf_platform_priv_gamepad_set_test_pad(1, &pad);
    next_frame();
    expect(wgf_action_is_pressed("fire"), "a pad button");
    wgf_platform_priv_gamepad_set_test_pad(1, NULL);
    next_frame();
    next_frame();

    key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE);
    key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_SPACE);
    expect(wgf_action_is_pressed("fire"), "a tap within a frame is still a press");
    next_frame();
    expect(!wgf_action_is_pressed("fire"), "seen once");

    key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE);
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    expect(wgf_action_is_pressed("fire"), "pressed in the tick");
    wgf_platform_priv_input_end_tick();
    expect(wgf_action_get_state("fire") == WGF_INPUT_STATE_DOWN, "held at the next tick");
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    expect(wgf_action_is_pressed("fire"), "and still pressed in the frame: each reader its own edges");
    key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_SPACE);
    next_frame();
}

static void touch(void)
{
    expect(wgf_action_bind_touch("jump", 600, 400, 200, 200), "a touch region");
    finger(SAPP_EVENTTYPE_TOUCHES_BEGAN, 100, 100);
    expect(!wgf_action_is_down("jump"), "a finger elsewhere");
    finger(SAPP_EVENTTYPE_TOUCHES_ENDED, 100, 100);
    next_frame();
    finger(SAPP_EVENTTYPE_TOUCHES_BEGAN, 700, 500);
    expect(wgf_action_is_pressed("jump"), "a finger in it: pressed");
    finger(SAPP_EVENTTYPE_TOUCHES_ENDED, 700, 500);
    next_frame();
    next_frame();
    expect(!wgf_action_is_down("jump"), "lifted");
}

static void listing(void)
{
    expect(wgf_action_get_binding_count("steer") == 2 &&
               strcmp(wgf_action_get_binding_text("steer", 0), "Left / Right") == 0 &&
               strcmp(wgf_action_get_binding_text("steer", 1), "Left stick X") == 0,
           "bindings as text");
    expect(strcmp(wgf_action_get_binding_text("fire", 0), "Space") == 0 &&
               strcmp(wgf_action_get_binding_text("fire", 1), "Pad south") == 0 &&
               strcmp(wgf_action_get_binding_text("left", 0), "Left stick X -") == 0 &&
               strcmp(wgf_action_get_binding_text("throttle", 0), "Right trigger") == 0 &&
               strcmp(wgf_action_get_binding_text("jump", 0), "Touch") == 0,
           "each kind's text");
    expect(strcmp(wgf_action_get_binding_text("fire", 2), "") == 0 && wgf_action_get_binding_count("none") == 0 &&
               strcmp(wgf_action_get_binding_text("none", 0), "") == 0,
           "past the end, or no such action: none");
    expect(wgf_action_clear("fire") && wgf_action_get_binding_count("fire") == 0 && wgf_action_get_count() == 5 &&
               wgf_action_bind_key("fire", WGF_KEY_F) && strcmp(wgf_action_get_binding_text("fire", 0), "F") == 0,
           "cleared, kept, and bound again");
    expect(!wgf_action_clear("none") && wgf_action_get_state("none") == WGF_INPUT_STATE_UP &&
               near(wgf_action_get_axis("none"), 0),
           "no such action: refused, up, 0");
}

int main(void)
{
    wgf_platform_priv_input_reset();
    wgf_platform_priv_gamepad_set_test_pad(0, NULL); /* test pads from here, none at first */
    binding();
    keys_and_axes();
    buttons_taps_and_ticks();
    touch();
    listing();
    return failures == 0 ? 0 : 1;
}
