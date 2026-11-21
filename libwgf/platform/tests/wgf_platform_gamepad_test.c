#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"
#include "wgf_gamepad.h"
#include "wgf.h"
#include "wgf_time.h"

#if defined(__EMSCRIPTEN__)
#define WGF_TEST_PLATFORM "Web"
#elif defined(_WIN32)
#define WGF_TEST_PLATFORM "Windows"
#elif defined(__APPLE__)
#define WGF_TEST_PLATFORM "Mac OS X"
#else
#define WGF_TEST_PLATFORM "Linux"
#endif

/* Gamepads with test pads in place of the platform's, read frame by frame as the loop
 * reads them: connecting, slots, edges per frame and per tick, the triggers as
 * buttons, a pad unplugged while a button is held, the round dead zone, and values
 * clamped; and rumble, what the test pads were asked recorded in place of the platform:
 * refused for a pad without motors, a bad length, or none connected; motors and length
 * clamped; stopped, by a call, once its time is up, and as the pads close; forgotten,
 * nothing sent, for a pad unplugged. And mappings, SDL's lines read and applied to a raw
 * pad: this platform's kept, others' and comments left out, a broken line skipped, a GUID
 * found without its name check or its version, and every kind of source and half. */

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
    expect(wgf_gamepad_get_last_pressed(1) == WGF_GAMEPAD_BUTTON_SOUTH && wgf_gamepad_get_last_pressed(0) == WGF_GAMEPAD_BUTTON_NONE &&
               wgf_gamepad_get_last_pressed(99) == WGF_GAMEPAD_BUTTON_NONE,
           "the last pressed: its button; none on another pad, or one out of range");
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

/* What test pad `pad` was last asked: its count of asks, and low, high, and seconds. */
static int asked(int pad, float *low, float *high, float *seconds)
{
    float out[3] = {0.0f, 0.0f, 0.0f};
    const int count = wgf_platform_priv_gamepad_get_test_rumble(pad, out);
    *low = out[0];
    *high = out[1];
    *seconds = out[2];
    return count;
}

static void rumble(void)
{
    wgf_platform_priv_gamepad_t motors = pad_state("rumbling pad"), still = pad_state("still pad");
    float low = 0.0f, high = 0.0f, seconds = 0.0f;
    double until;
    int count;

    motors.rumble = true;
    wgf_platform_priv_gamepad_set_test_pad(0, &motors);
    wgf_platform_priv_gamepad_set_test_pad(1, &still);
    wgf_platform_priv_gamepad_set_test_pad(2, NULL);
    next_frame();
    expect(wgf_gamepad_can_rumble(0) && !wgf_gamepad_can_rumble(1) && !wgf_gamepad_can_rumble(2) &&
               !wgf_gamepad_can_rumble(-1) && !wgf_gamepad_can_rumble(99),
           "a pad with motors can rumble; one without, none there, or none at all can't");
    expect(!wgf_gamepad_rumble(1, 1.0f, 1.0f, 0.5f) && asked(1, &low, &high, &seconds) == 0 &&
               !wgf_gamepad_is_rumbling(1),
           "a pad without motors is refused, nothing asked");
    expect(!wgf_gamepad_rumble(2, 1.0f, 1.0f, 0.5f) && !wgf_gamepad_rumble(99, 1.0f, 1.0f, 0.5f),
           "no pad there, or out of range: refused");
    expect(!wgf_gamepad_rumble(0, 1.0f, 1.0f, 0.0f) && !wgf_gamepad_rumble(0, 1.0f, 1.0f, -1.0f) &&
               !wgf_gamepad_rumble(0, 1.0f, 1.0f, nanf("")) && asked(0, &low, &high, &seconds) == 0,
           "no length, a negative one, or NaN: refused, nothing asked");
    expect(!wgf_gamepad_is_rumbling(0) && !wgf_gamepad_stop_rumble(0), "not rumbling: nothing to stop");

    expect(wgf_gamepad_rumble(0, -0.5f, 2.0f, 5.0f) && wgf_gamepad_is_rumbling(0), "rumbling");
    count = asked(0, &low, &high, &seconds);
    expect(count == 1 && low == 0.0f && high == 1.0f && seconds == 2.0f,
           "the motors clamped to 0 to 1, the length to 2 seconds");
    next_frame();
    expect(wgf_gamepad_is_rumbling(0) && asked(0, &low, &high, &seconds) == 1, "still rumbling a frame on");
    expect(wgf_gamepad_stop_rumble(0) && !wgf_gamepad_is_rumbling(0), "stopped");
    count = asked(0, &low, &high, &seconds);
    expect(count == 2 && seconds == 0.0f && low == 0.0f && high == 0.0f, "a stop asked");
    expect(!wgf_gamepad_stop_rumble(0) && asked(0, &low, &high, &seconds) == 2, "stopped twice: refused");

    /* its time up: stopped at the next frame's read */
    expect(wgf_gamepad_rumble(0, 0.25f, 0.75f, 0.05f), "a short rumble");
    count = asked(0, &low, &high, &seconds);
    expect(count == 3 && low == 0.25f && high == 0.75f && seconds == 0.05f, "its motors and length asked");
    until = wgf_time_get_seconds() + 0.06;
    while (wgf_time_get_seconds() < until) {
    }
    next_frame();
    count = asked(0, &low, &high, &seconds);
    expect(!wgf_gamepad_is_rumbling(0) && count == 4 && seconds == 0.0f, "stopped once its time was up");

    /* unplugged while rumbling: forgotten, and nothing sent to a pad that isn't there */
    expect(wgf_gamepad_rumble(0, 1.0f, 0.0f, 1.0f) && asked(0, &low, &high, &seconds) == 5, "rumbling again");
    wgf_platform_priv_gamepad_set_test_pad(0, NULL);
    next_frame();
    expect(!wgf_gamepad_is_rumbling(0) && asked(0, &low, &high, &seconds) == 5,
           "unplugged: not rumbling, no stop sent");
    motors.rumble = true;
    wgf_platform_priv_gamepad_set_test_pad(0, &motors);
    next_frame();
    expect(!wgf_gamepad_is_rumbling(0) && wgf_gamepad_can_rumble(0), "plugged in again: still");

    /* the pads closing stop what rumbles */
    expect(wgf_gamepad_rumble(0, 0.5f, 0.5f, 1.0f) && asked(0, &low, &high, &seconds) == 6, "rumbling as they close");
    wgf_platform_priv_gamepad_close();
    expect(asked(0, &low, &high, &seconds) == 7 && seconds == 0.0f, "closing stopped it");
    wgf_platform_priv_gamepad_open();
}

static void mappings(void)
{
    /* bus 3, vendor 0x045e, product 0x028e, version 0x0114 */
    static const unsigned char pad_guid[16] = {0x03, 0, 0xab, 0xcd, 0x5e, 0x04, 0, 0, 0x8e, 0x02, 0, 0, 0x14, 0x01, 0, 0};
    static const unsigned char other_version[16] = {0x03, 0, 0, 0, 0x5e, 0x04, 0, 0, 0x8e, 0x02, 0, 0, 0x99, 0x00, 0, 0};
    static const unsigned char unknown[16] = {0x05, 0, 0, 0, 0x11, 0x11, 0, 0, 0x22, 0x22, 0, 0, 0, 0, 0, 0};
    const char *text =
        "# a comment\n"
        "\n"
        "030000005e0400008e02000014010000,Test Pad,a:b0,b:b1,misc1:b9,dpup:h0.1,dpleft:h0.8,leftx:a0,lefty:a1~,"
        "lefttrigger:a2,righttrigger:+a5,-rightx:b6,+rightx:b7,x:+a3,y:-a3,platform:" WGF_TEST_PLATFORM ",\r\n"
        "030000005e0400008e02000000000000,Any Version,a:b5,platform:" WGF_TEST_PLATFORM ",\n"
        "030000005e0400008e02000014010000,On Elsewhere,a:b3,platform:Nowhere,\n"
        "not a guid,Broken,a:b0,\n"
        "05000000111100002222000000000000,No Platform,a:b2\n";
    const wgf_platform_priv_gamepad_mapping_t *map;
    wgf_platform_priv_gamepad_raw_t raw;
    wgf_platform_priv_gamepad_t pad;
    const unsigned before = wgf_platform_priv_gamepad_mapping_generation();
    expect(wgf_gamepad_add_mappings(text) == 3, "this platform's lines and those with none kept; the rest left out");
    expect(wgf_platform_priv_gamepad_mapping_generation() != before, "the mappings changed");
    expect(wgf_gamepad_add_mappings(NULL) == 0 && wgf_gamepad_add_mappings("") == 0, "nothing to add");
    map = wgf_platform_priv_gamepad_find_mapping(pad_guid);
    expect(map != NULL, "found by its GUID, its name check left out");
    expect(wgf_platform_priv_gamepad_find_mapping(unknown) != NULL, "a line with no platform kept");
    memset(&raw, 0, sizeof(raw));
    raw.button_count = 10;
    raw.axis_count = 6;
    raw.hat_count = 1;
    memset(&pad, 0, sizeof(pad));
    wgf_platform_priv_gamepad_apply_mapping(wgf_platform_priv_gamepad_find_mapping(other_version), &raw, &pad);
    raw.buttons[5] = true;
    wgf_platform_priv_gamepad_apply_mapping(wgf_platform_priv_gamepad_find_mapping(other_version), &raw, &pad);
    expect(pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH], "another version: the line written without one");
    raw.buttons[5] = false;
    raw.axes[2] = -1.0f; /* a trigger at rest, as a whole axis */
    wgf_platform_priv_gamepad_apply_mapping(map, &raw, &pad);
    expect(!pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH] && near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER], 0) &&
               near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X], 0),
           "at rest: nothing held, a whole-axis trigger released");
    raw.buttons[0] = raw.buttons[9] = true;
    raw.hats[0] = 1 | 8;
    raw.axes[0] = 0.25f;
    raw.axes[1] = 0.5f;
    raw.axes[2] = 1.0f;
    raw.axes[3] = 0.8f;
    raw.axes[5] = 0.6f;
    raw.buttons[6] = true;
    wgf_platform_priv_gamepad_apply_mapping(map, &raw, &pad);
    expect(pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH] && !pad.buttons[WGF_GAMEPAD_BUTTON_EAST], "a button");
    expect(pad.buttons[WGF_GAMEPAD_BUTTON_DPAD_UP] && pad.buttons[WGF_GAMEPAD_BUTTON_DPAD_LEFT] &&
               !pad.buttons[WGF_GAMEPAD_BUTTON_DPAD_DOWN],
           "a hat's directions");
    expect(near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X], 0.25f) && near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y], -0.5f),
           "an axis, and one inverted");
    expect(near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER], 1) && near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER], 0.6f),
           "a trigger from a whole axis, pulled, and from an axis's half");
    expect(near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X], -1), "a stick's half from a button");
    expect(pad.buttons[WGF_GAMEPAD_BUTTON_WEST] && !pad.buttons[WGF_GAMEPAD_BUTTON_NORTH], "a button from an axis's half");
    raw.buttons[6] = false;
    raw.buttons[7] = true;
    raw.axes[3] = -0.8f;
    wgf_platform_priv_gamepad_apply_mapping(map, &raw, &pad);
    expect(near(pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X], 1) && pad.buttons[WGF_GAMEPAD_BUTTON_NORTH] &&
               !pad.buttons[WGF_GAMEPAD_BUTTON_WEST],
           "the other halves");
    expect(wgf_gamepad_add_mappings("030000005e0400008e02000014010000,Again,b:b0,platform:" WGF_TEST_PLATFORM ",") == 1,
           "a GUID given again");
    raw.buttons[0] = true;
    wgf_platform_priv_gamepad_apply_mapping(wgf_platform_priv_gamepad_find_mapping(pad_guid), &raw, &pad);
    expect(pad.buttons[WGF_GAMEPAD_BUTTON_EAST] && !pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH], "replaces its mapping");

    /* a pad's GUID and mapping, as the platform reports them */
    pad = pad_state("mapped");
    memcpy(pad.guid, pad_guid, 16);
    pad.mapped = true;
    wgf_platform_priv_gamepad_set_test_pad(2, &pad);
    next_frame();
    expect(strcmp(wgf_gamepad_get_guid(2), "0300abcd5e0400008e02000014010000") == 0 && wgf_gamepad_is_mapped(2),
           "its GUID, SDL's way, and mapped");
    pad.guid[0] = 0;
    memset(pad.guid, 0, 16);
    pad.mapped = false;
    wgf_platform_priv_gamepad_set_test_pad(2, &pad);
    next_frame();
    expect(strcmp(wgf_gamepad_get_guid(2), "") == 0 && !wgf_gamepad_is_mapped(2) && strcmp(wgf_gamepad_get_guid(9), "") == 0,
           "none where the platform makes none, or no such pad");
    wgf_platform_priv_gamepad_set_test_pad(2, NULL);
}

int main(void)
{
    wgf_core_priv_init();
    wgf_platform_priv_gamepad_set_test_pad(0, NULL); /* test pads, never the platform's */
    wgf_platform_priv_gamepad_open();
    buttons();
    sticks();
    rumble();
    mappings();
    wgf_platform_priv_gamepad_close();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
