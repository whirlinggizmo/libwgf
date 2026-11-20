#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_platform_input_priv.h"
#include "wgf_input.h"
#include "wgf_mouse.h"
#include "wgf_touch.h"
#include "wgf_keyboard.h"
#include "wgf_window.h"
#include "wgf_time.h"
#include "wgf_core_priv.h"

/* Touch, through input's private calls, frame by frame as the loop drives them:
 * fingers listed oldest first with ids that stay put, edges per frame, the mouse
 * following the first finger and cancelled by a second, the gesture's pan, pinch,
 * and twist, the edges ticks see, and pixels in logical pixels. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(wgf_vec2_t v, float x, float y)
{
    return fabsf(v.x - x) < 1e-3f && fabsf(v.y - y) < 1e-3f;
}

/* One touch event: `count` fingers, given as system id, x, y, changed. */
static void touches(sapp_event_type type, int count, const float *fingers, float dpi_scale)
{
    sapp_event event;
    int i;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.num_touches = count;
    for (i = 0; i < count; i++) {
        event.touches[i].identifier = (uintptr_t)fingers[i * 4];
        event.touches[i].pos_x = fingers[i * 4 + 1];
        event.touches[i].pos_y = fingers[i * 4 + 2];
        event.touches[i].changed = fingers[i * 4 + 3] != 0.0f;
    }
    wgf_platform_priv_input_handle_event(&event, dpi_scale);
}

static void next_frame(void)
{
    wgf_platform_priv_input_end_tick(); /* a tick ran, and read nothing */
    wgf_platform_priv_input_end_frame();
}

static void fingers_and_mouse(void)
{
    const float one_down[] = {101, 10, 20, 1};
    const float one_moved[] = {101, 14, 23, 1};
    const float two_down[] = {101, 14, 23, 0, 202, 50, 60, 1};
    const float one_up[] = {101, 14, 23, 1, 202, 50, 60, 0};
    const float two_up[] = {202, 50, 60, 1};
    int first, second;

    wgf_platform_priv_input_reset();
    expect(wgf_touch_get_count() == 0 && wgf_touch_get_id(0) == -1, "no fingers");
    expect(wgf_touch_is_mouse_emulated(), "the first finger drives the mouse by default");

    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, one_down, 1.0f);
    first = wgf_touch_get_id(0);
    expect(wgf_touch_get_count() == 1 && first >= 0 && first < 8, "one finger");
    expect(wgf_touch_get_state(first) == WGF_INPUT_STATE_PRESSED, "pressed");
    expect(near(wgf_touch_get_position(first), 10, 20), "where it touched");
    expect(wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT) && near(wgf_mouse_get_position(), 10, 20),
           "the mouse follows it, its left button pressed");
    next_frame();

    touches(SAPP_EVENTTYPE_TOUCHES_MOVED, 1, one_moved, 1.0f);
    expect(wgf_touch_get_state(first) == WGF_INPUT_STATE_DOWN, "then held");
    expect(near(wgf_touch_get_delta(first), 4, 3), "its movement");
    expect(near(wgf_mouse_get_position(), 14, 23) && wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT), "the mouse drags along");
    next_frame();

    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 2, two_down, 1.0f);
    expect(wgf_touch_get_count() == 2 && wgf_touch_get_id(0) == first, "two fingers, the first listed first");
    second = wgf_touch_get_id(1);
    expect(second != first && wgf_touch_get_state(second) == WGF_INPUT_STATE_PRESSED, "the second pressed");
    expect(wgf_mouse_is_released(WGF_MOUSE_BUTTON_LEFT) && near(wgf_mouse_get_position(), -1, -1),
           "a second finger lets the mouse go off the window");
    next_frame();

    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 2, one_up, 1.0f);
    expect(wgf_touch_get_state(first) == WGF_INPUT_STATE_RELEASED && wgf_touch_get_count() == 2,
           "a lifted finger is listed the frame it lifts");
    expect(wgf_touch_get_id(1) == second, "the other keeps its id");
    expect(!wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT) && !wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT),
           "and the mouse stays up");
    next_frame();
    expect(wgf_touch_get_count() == 1 && wgf_touch_get_id(0) == second, "then only the one down");
    expect(wgf_touch_get_state(first) == WGF_INPUT_STATE_UP, "a lifted finger's id is up");

    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, two_up, 1.0f);
    next_frame();
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, one_down, 1.0f);
    expect(wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT), "once every finger lifted, the next drives the mouse again");
    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, one_down, 1.0f);
    expect(wgf_touch_get_state(wgf_touch_get_id(0)) == WGF_INPUT_STATE_PRESSED, "a tap within a frame is pressed");
    next_frame();

    wgf_touch_set_mouse_emulated(false);
    expect(!wgf_touch_is_mouse_emulated(), "the mouse left alone");
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, one_moved, 1.0f);
    expect(!wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT) && near(wgf_mouse_get_position(), 10, 20),
           "touch no longer moves or presses it");
    wgf_touch_set_mouse_emulated(true);
    expect(wgf_touch_get_state(-1) == WGF_INPUT_STATE_UP && wgf_touch_get_state(99) == WGF_INPUT_STATE_UP &&
               near(wgf_touch_get_position(99), 0, 0),
           "an id that isn't one");
}

static void gesture(void)
{
    const float down[] = {1, 100, 100, 1, 2, 200, 100, 1};
    /* both move right 10; then apart, to 4 times as far, and turned a quarter */
    const float panned[] = {1, 110, 100, 1, 2, 210, 100, 1};
    const float turned[] = {1, 160, -100, 1, 2, 160, 300, 1};
    wgf_platform_priv_input_reset();

    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 2, down, 2.0f); /* pixels at 2 to a logical pixel */
    expect(wgf_touch_is_gesture(), "two fingers: a gesture");
    expect(near(wgf_touch_get_gesture_center(), 75, 50), "its center, in logical pixels");
    expect(fabsf(wgf_touch_get_gesture_scale() - 1.0f) < 1e-4f && wgf_touch_get_gesture_rotation() == 0.0f,
           "no pinch or twist on touching down");
    next_frame();

    touches(SAPP_EVENTTYPE_TOUCHES_MOVED, 2, panned, 2.0f);
    expect(near(wgf_touch_get_gesture_pan(), 5, 0), "panned");
    touches(SAPP_EVENTTYPE_TOUCHES_MOVED, 2, turned, 2.0f);
    expect(fabsf(wgf_touch_get_gesture_scale() - 4.0f) < 1e-3f, "pinched out to 4 times as far");
    expect(fabsf(wgf_touch_get_gesture_rotation() - 1.5707963f) < 1e-3f, "turned a quarter, clockwise");
    expect(near(wgf_touch_get_gesture_pan(), 5, 0), "the pan summed over the frame: turning kept the center");

    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    expect(fabsf(wgf_touch_get_gesture_scale() - 4.0f) < 1e-3f, "a tick sees the same since the previous tick");
    wgf_platform_priv_input_end_tick();
    expect(fabsf(wgf_touch_get_gesture_scale() - 1.0f) < 1e-4f, "and the next tick none");
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    next_frame();
    expect(fabsf(wgf_touch_get_gesture_scale() - 1.0f) < 1e-4f && near(wgf_touch_get_gesture_pan(), 0, 0),
           "nothing since the frame before");
    wgf_platform_priv_input_reset();
    expect(!wgf_touch_is_gesture(), "not active with no fingers");
}

static void too_many(void)
{
    float fingers[9 * 4];
    int i;
    wgf_platform_priv_input_reset();
    for (i = 0; i < 9; i++) {
        fingers[i * 4] = (float)(i + 1);
        fingers[i * 4 + 1] = (float)i;
        fingers[i * 4 + 2] = 0;
        fingers[i * 4 + 3] = 1;
    }
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 8, fingers, 1.0f); /* an event holds 8 */
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, fingers + 8 * 4, 1.0f);
    expect(wgf_touch_get_count() == 8, "at most 8 fingers");
    wgf_platform_priv_input_reset();
}

/* One finger alone: a tap, a second (a double), a swipe, a hold; and two fingers, no tap. */
static void one_finger(void)
{
    const float at[] = {7, 100, 100, 1}, near_it[] = {7, 104, 102, 1}, far_off[] = {7, 300, 110, 1};
    const float pair[] = {7, 100, 100, 0, 8, 200, 200, 1}, pair_up[] = {7, 100, 100, 1, 8, 200, 200, 1};
    double start;
    wgf_platform_priv_input_reset();
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, at, 1.0f);
    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, near_it, 1.0f);
    expect(wgf_touch_is_tapped() && !wgf_touch_is_double_tapped() && near(wgf_touch_get_tap_position(), 104, 102),
           "a quick still touch: a tap, where");
    next_frame();
    expect(!wgf_touch_is_tapped(), "for the frame it was in");
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, at, 1.0f);
    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, at, 1.0f);
    expect(wgf_touch_is_tapped() && wgf_touch_is_double_tapped(), "a second soon after, near: a double tap");
    next_frame();
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, at, 1.0f);
    touches(SAPP_EVENTTYPE_TOUCHES_MOVED, 1, far_off, 1.0f);
    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, far_off, 1.0f);
    expect(!wgf_touch_is_tapped() && near(wgf_touch_get_swipe(), 200, 10), "a quick long move: a swipe, how far");
    next_frame();
    expect(near(wgf_touch_get_swipe(), 0, 0), "none after");
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, at, 1.0f);
    expect(!wgf_touch_is_holding(), "not held yet");
    start = wgf_time_get_seconds();
    while (wgf_time_get_seconds() - start < 0.55 && wgf_time_get_seconds() > 0.0) {
    }
    expect(wgf_touch_is_holding(), "held still half a second: a hold");
    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 1, at, 1.0f);
    expect(!wgf_touch_is_holding() && !wgf_touch_is_tapped(), "lifted: no hold, and too slow for a tap");
    next_frame();
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 1, at, 1.0f);
    touches(SAPP_EVENTTYPE_TOUCHES_BEGAN, 2, pair, 1.0f);
    touches(SAPP_EVENTTYPE_TOUCHES_ENDED, 2, pair_up, 1.0f);
    expect(!wgf_touch_is_tapped(), "two fingers: no tap");
    wgf_platform_priv_input_reset();
}

/* Keys: a repeat, and the last pressed. */
static void keys(void)
{
    sapp_event event;
    wgf_platform_priv_input_reset();
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_KEY_DOWN;
    event.key_code = SAPP_KEYCODE_LEFT;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
    event.key_code = SAPP_KEYCODE_A;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
    expect(wgf_keyboard_is_repeated(WGF_KEY_LEFT) && wgf_keyboard_get_last_pressed() == WGF_KEY_A, "pressed: repeated, the last");
    next_frame();
    expect(!wgf_keyboard_is_repeated(WGF_KEY_LEFT) && wgf_keyboard_get_last_pressed() == WGF_KEY_NONE, "held, no repeat yet: none");
    event.key_code = SAPP_KEYCODE_LEFT;
    event.key_repeat = true;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
    expect(wgf_keyboard_is_repeated(WGF_KEY_LEFT) && !wgf_keyboard_is_pressed(WGF_KEY_LEFT) &&
               wgf_keyboard_get_last_pressed() == WGF_KEY_NONE,
           "the system's repeat: repeated, not pressed, not the last pressed");
    wgf_platform_priv_input_reset();
}

/* The cursor, the clipboard, a URL, and the device, headless. */
static void system_calls(void)
{
    expect(wgf_mouse_get_cursor() == WGF_MOUSE_CURSOR_DEFAULT && wgf_mouse_set_cursor(WGF_MOUSE_CURSOR_HAND) &&
               wgf_mouse_get_cursor() == WGF_MOUSE_CURSOR_HAND && !wgf_mouse_set_cursor((wgf_mouse_cursor_t)11),
           "a cursor's shape; one that isn't refused");
    expect(wgf_input_set_clipboard("copied") && strcmp(wgf_input_get_clipboard(), "copied") == 0 && !wgf_input_set_clipboard(NULL),
           "the clipboard's text");
    expect(wgf_window_open_url("https://example.com") && !wgf_window_open_url("") && !wgf_window_open_url(NULL), "a URL");
#if defined(__EMSCRIPTEN__)
    expect(wgf_window_is_web(), "in a page: the web");
#else
    expect(!wgf_window_is_web(), "natively: not the web");
#endif
    expect(!wgf_window_has_touch() && wgf_window_has_fine_pointer() && wgf_window_get_os() != WGF_WINDOW_OS_OTHER,
           "a headless run's device: a desktop's");
}

int main(void)
{
    system_calls();
    wgf_core_priv_init(); /* the clock, for a hold */
    one_finger();
    keys();
    fingers_and_mouse();
    gesture();
    too_many();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
