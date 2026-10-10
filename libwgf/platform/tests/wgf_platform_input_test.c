#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_platform_input_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_app.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_window.h"

/* Input, through the loop with no window (a headless build) and events queued as
 * the window would deliver them: edges seen for one frame, held state after, a tap
 * within one frame, typing, the mouse, and the focus lost. Then the tick edges,
 * through input's private calls, since how many ticks a frame runs depends on the
 * clock. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void push(sapp_event_type type, sapp_keycode key)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    wgf_platform_priv_headless_push_event(&event);
}

static void push_char(uint32_t code)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_CHAR;
    event.char_code = code;
    wgf_platform_priv_headless_push_event(&event);
}

static void push_mouse(sapp_event_type type, sapp_mousebutton button, float x, float y, float dx, float dy)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.mouse_button = button;
    event.mouse_x = x;
    event.mouse_y = y;
    event.mouse_dx = dx;
    event.mouse_dy = dy;
    wgf_platform_priv_headless_push_event(&event);
}

static void push_scroll(float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
    event.scroll_x = x;
    event.scroll_y = y;
    wgf_platform_priv_headless_push_event(&event);
}

static int near(wgf_vec2_t v, float x, float y)
{
    return fabsf(v.x - x) < 1e-4f && fabsf(v.y - y) < 1e-4f;
}

/* Each frame checks what the events queued by the frame before did, then queues
 * the next. */
static void on_frame(void *user)
{
    int *frame = user;
    switch (++*frame) {
        case 1:
            expect(wgf_keyboard_get_state(WGF_KEY_A) == WGF_INPUT_STATE_UP, "a key starts up");
            expect(strcmp(wgf_input_get_chars(), "") == 0, "nothing typed");
            push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_A);
            push_char('h');
            push_char(0xE9);    /* é: two bytes */
            push_char(0x1F600); /* an emoji: four */
            push_char(8);       /* backspace: not text */
            break;
        case 2:
            expect(wgf_keyboard_get_state(WGF_KEY_A) == WGF_INPUT_STATE_PRESSED, "pressed the frame after");
            expect(wgf_keyboard_is_pressed(WGF_KEY_A) && wgf_keyboard_is_down(WGF_KEY_A), "pressed is down too");
            expect(strcmp(wgf_input_get_chars(), "h\xC3\xA9\xF0\x9F\x98\x80") == 0, "typed as UTF-8");
            {
                sapp_event repeat;
                memset(&repeat, 0, sizeof(repeat));
                repeat.type = SAPP_EVENTTYPE_KEY_DOWN;
                repeat.key_code = SAPP_KEYCODE_A;
                repeat.key_repeat = true;
                wgf_platform_priv_headless_push_event(&repeat);
            }
            break;
        case 3:
            expect(wgf_keyboard_get_state(WGF_KEY_A) == WGF_INPUT_STATE_DOWN, "then held, its repeat no press");
            expect(strcmp(wgf_input_get_chars(), "") == 0, "typing is per frame");
            push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_A);
            break;
        case 4:
            expect(wgf_keyboard_get_state(WGF_KEY_A) == WGF_INPUT_STATE_RELEASED && !wgf_keyboard_is_down(WGF_KEY_A),
                   "released");
            push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE);
            push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_SPACE);
            break;
        case 5:
            expect(wgf_keyboard_get_state(WGF_KEY_A) == WGF_INPUT_STATE_UP, "then up");
            expect(wgf_keyboard_is_pressed(WGF_KEY_SPACE) && wgf_keyboard_is_released(WGF_KEY_SPACE),
                   "a tap within a frame is pressed and released");
            expect(wgf_keyboard_get_state(WGF_KEY_SPACE) == WGF_INPUT_STATE_PRESSED, "and reads as pressed");
            expect(wgf_keyboard_get_state((wgf_keyboard_key_t)9999) == WGF_INPUT_STATE_UP, "a key that isn't one is up");
            push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 10, 20, 3, 4);
            push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 12, 25, 2, 5);
            push_mouse(SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_RIGHT, 12, 25, 0, 0);
            push_scroll(0.5f, -1.0f);
            push_scroll(0.25f, -2.0f);
            break;
        case 6:
            expect(near(wgf_mouse_get_position(), 12, 25), "the pointer where it last moved");
            expect(near(wgf_mouse_get_delta(), 5, 9), "movement summed over the frame");
            expect(near(wgf_mouse_get_wheel(), 0.75f, -3.0f), "scrolling summed over the frame");
            expect(wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_RIGHT) && !wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT),
                   "the right button pressed");
            push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_W);
            break;
        case 7:
            expect(near(wgf_mouse_get_delta(), 0, 0) && near(wgf_mouse_get_wheel(), 0, 0), "no movement since");
            expect(wgf_mouse_get_button_state(WGF_MOUSE_BUTTON_RIGHT) == WGF_INPUT_STATE_DOWN, "held");
            expect(wgf_keyboard_is_down(WGF_KEY_W), "W held");
            push(SAPP_EVENTTYPE_UNFOCUSED, SAPP_KEYCODE_INVALID);
            break;
        case 8:
            expect(wgf_keyboard_is_released(WGF_KEY_W) && !wgf_keyboard_is_down(WGF_KEY_W),
                   "losing the focus releases the keys held");
            wgf_app_quit();
            break;
        default:
            break;
    }
}

/* The tick edges, kept apart from the frame's. */
static void tick_edges(void)
{
    sapp_event event;
    wgf_platform_priv_input_reset();
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_KEY_DOWN;
    event.key_code = SAPP_KEYCODE_J;
    wgf_platform_priv_input_handle_event(&event, 1.0f);

    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    expect(wgf_keyboard_is_pressed(WGF_KEY_J), "the first tick sees the press");
    wgf_platform_priv_input_end_tick();
    expect(!wgf_keyboard_is_pressed(WGF_KEY_J) && wgf_keyboard_is_down(WGF_KEY_J), "the next tick sees it held");
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    expect(wgf_keyboard_is_pressed(WGF_KEY_J), "while the frame still sees the press");
    wgf_platform_priv_input_end_frame();
    expect(!wgf_keyboard_is_pressed(WGF_KEY_J), "until the frame ends");

    /* a frame that runs no tick carries its edges to the next tick */
    event.type = SAPP_EVENTTYPE_KEY_UP;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
    wgf_platform_priv_input_end_frame();
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    expect(wgf_keyboard_is_released(WGF_KEY_J), "a release waits for a tick");
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);

    /* pixels to logical pixels */
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_MOUSE_MOVE;
    event.mouse_x = 200;
    event.mouse_y = 100;
    event.mouse_dx = 8;
    wgf_platform_priv_input_handle_event(&event, 2.0f);
    expect(near(wgf_mouse_get_position(), 100, 50) && near(wgf_mouse_get_delta(), 4, 0),
           "positions in logical pixels");
    wgf_platform_priv_input_reset();
}

int main(void)
{
    int frame = 0;
    wgf_window_set_size(64, 64);
    expect(wgf_app_run(NULL, NULL, on_frame, NULL, &frame), "runs");
    expect(frame == 8, "every frame ran");
    tick_edges();
    return failures == 0 ? 0 : 1;
}
