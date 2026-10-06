#include <stdio.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* Every connected gamepad, live: up to four side by side, each with its name, both
 * sticks (the dot is where the stick points, past the dead zone; the ring is its
 * reach), the triggers as bars, and the buttons laid out as on an Xbox-style pad, lit
 * while held and flashed the frame they're pressed. SOUTH on a pad cycles the dead
 * zone; Escape quits where quitting means anything. In a browser, press a button on
 * the pad first: a page sees gamepads only after that.
 *
 * libwgt's app-gamepad (wgrender's gamepad) done 1:1 for the size table. The
 * differences:
 *   - the title and the heading say libwgf where libwgt's say libwgt;
 *   - outlines take a thickness here, 1 logical pixel: libwgt's are GL lines of one
 *     pixel;
 *   - circles get enough segments for their size, where libwgt's have 36. */

static const float DEADZONES[] = {0.15f, 0.0f, 0.3f};
static int deadzone;

static wgf_color_t button_color(int pad, wgf_gamepad_button_t button)
{
    switch (wgf_gamepad_get_button_state(pad, button)) {
        case WGF_INPUT_STATE_PRESSED: return WGF_COLOR_WHITE;
        case WGF_INPUT_STATE_DOWN: return WGF_COLOR_GOLD;
        default: return wgf_color_make(60, 66, 80, 255);
    }
}

static void draw_button(int pad, wgf_gamepad_button_t button, float x, float y, float r)
{
    wgf_draw_circle(x, y, r, button_color(pad, button));
}

static void draw_stick(int pad, wgf_gamepad_stick_t stick, wgf_gamepad_button_t click, float cx, float cy)
{
    const float reach = 34.0f;
    const wgf_vec2_t at = wgf_gamepad_get_stick(pad, stick);
    if (wgf_gamepad_is_down(pad, click)) wgf_draw_circle(cx, cy, reach, wgf_color_make(60, 66, 80, 255));
    wgf_draw_circle_lines(cx, cy, reach, 1, wgf_color_make(90, 98, 118, 255));
    wgf_draw_circle(cx + at.x * reach, cy + at.y * reach, 9.0f, WGF_COLOR_SKYBLUE);
}

static void draw_trigger(int pad, wgf_gamepad_trigger_t trigger, float x, float y)
{
    const float height = 60.0f, value = wgf_gamepad_get_trigger(pad, trigger);
    wgf_draw_rectangle_lines(x, y, 16.0f, height, 1, wgf_color_make(90, 98, 118, 255));
    wgf_draw_rectangle(x, y + height * (1.0f - value), 16.0f, height * value, WGF_COLOR_ORANGE);
}

static void draw_pad(int pad, float x, float y)
{
    wgf_vec2_t left, right;
    char line[96];
    snprintf(line, sizeof(line), "pad %d", pad);
    wgf_draw_text(0, line, x, y, 20, WGF_COLOR_RAYWHITE);
    if (!wgf_gamepad_is_connected(pad)) {
        wgf_draw_text(0, "not connected", x, y + 26, 16, WGF_COLOR_GRAY);
        return;
    }
    snprintf(line, sizeof(line), "%.40s", wgf_gamepad_get_name(pad));
    wgf_draw_text(0, line, x, y + 26, 14, WGF_COLOR_LIGHTGRAY);

    /* shoulders and triggers */
    draw_trigger(pad, WGF_GAMEPAD_TRIGGER_LEFT, x + 10.0f, y + 56.0f);
    draw_trigger(pad, WGF_GAMEPAD_TRIGGER_RIGHT, x + 274.0f, y + 56.0f);
    wgf_draw_rectangle(x + 34.0f, y + 60.0f, 60.0f, 14.0f, button_color(pad, WGF_GAMEPAD_BUTTON_LEFT_BUMPER));
    wgf_draw_rectangle(x + 206.0f, y + 60.0f, 60.0f, 14.0f, button_color(pad, WGF_GAMEPAD_BUTTON_RIGHT_BUMPER));

    /* sticks, d-pad, face buttons, middle buttons */
    draw_stick(pad, WGF_GAMEPAD_STICK_LEFT, WGF_GAMEPAD_BUTTON_LEFT_STICK, x + 70.0f, y + 130.0f);
    draw_stick(pad, WGF_GAMEPAD_STICK_RIGHT, WGF_GAMEPAD_BUTTON_RIGHT_STICK, x + 190.0f, y + 210.0f);
    wgf_draw_rectangle(x + 102.0f, y + 180.0f, 18.0f, 18.0f, button_color(pad, WGF_GAMEPAD_BUTTON_DPAD_UP));
    wgf_draw_rectangle(x + 102.0f, y + 220.0f, 18.0f, 18.0f, button_color(pad, WGF_GAMEPAD_BUTTON_DPAD_DOWN));
    wgf_draw_rectangle(x + 82.0f, y + 200.0f, 18.0f, 18.0f, button_color(pad, WGF_GAMEPAD_BUTTON_DPAD_LEFT));
    wgf_draw_rectangle(x + 122.0f, y + 200.0f, 18.0f, 18.0f, button_color(pad, WGF_GAMEPAD_BUTTON_DPAD_RIGHT));
    draw_button(pad, WGF_GAMEPAD_BUTTON_NORTH, x + 240.0f, y + 106.0f, 12.0f);
    draw_button(pad, WGF_GAMEPAD_BUTTON_SOUTH, x + 240.0f, y + 154.0f, 12.0f);
    draw_button(pad, WGF_GAMEPAD_BUTTON_WEST, x + 216.0f, y + 130.0f, 12.0f);
    draw_button(pad, WGF_GAMEPAD_BUTTON_EAST, x + 264.0f, y + 130.0f, 12.0f);
    draw_button(pad, WGF_GAMEPAD_BUTTON_BACK, x + 126.0f, y + 130.0f, 8.0f);
    draw_button(pad, WGF_GAMEPAD_BUTTON_GUIDE, x + 150.0f, y + 110.0f, 10.0f);
    draw_button(pad, WGF_GAMEPAD_BUTTON_START, x + 174.0f, y + 130.0f, 8.0f);

    left = wgf_gamepad_get_stick(pad, WGF_GAMEPAD_STICK_LEFT);
    right = wgf_gamepad_get_stick(pad, WGF_GAMEPAD_STICK_RIGHT);
    snprintf(line, sizeof(line), "L %+.2f %+.2f  R %+.2f %+.2f", left.x, left.y, right.x, right.y);
    wgf_draw_text(0, line, x, y + 262, 14, WGF_COLOR_LIGHTGRAY);
}

static void frame(void *user)
{
    char line[96];
    int pad;
    (void)user;

    for (pad = 0; pad < 4; pad++) {
        if (wgf_gamepad_is_pressed(pad, WGF_GAMEPAD_BUTTON_SOUTH)) {
            deadzone = (deadzone + 1) % (int)(sizeof(DEADZONES) / sizeof(DEADZONES[0]));
            wgf_gamepad_set_deadzone(DEADZONES[deadzone]);
        }
    }
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    wgf_draw_text(0, "libwgf gamepads", 12, 36, 24, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "dead zone %.2f (SOUTH changes it)   web: press a pad button first",
             DEADZONES[deadzone]);
    wgf_draw_text(0, line, 12, 70, 16, WGF_COLOR_LIGHTGRAY);
    for (pad = 0; pad < 4; pad++) draw_pad(pad, 20.0f + (float)(pad % 2) * 320.0f, 110.0f + (float)(pad / 2) * 300.0f);
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(20, 22, 30, 255));
}

int main(void)
{
    wgf_window_set_title("libwgf gamepad");
    wgf_window_set_size(680, 720);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
