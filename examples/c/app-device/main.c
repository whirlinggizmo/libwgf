#include <stdio.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_gamepad.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* What the device does around the game: a pad's rumble, the safe area, and the screen's
 * orientation (helix-raylib's SetGamepadVibration, GetWindowSafeArea, and
 * SetScreenOrientationAsync).
 *
 * Rumble: on any pad, SOUTH rumbles the low-frequency motor, EAST the high-frequency one,
 * NORTH both, each for half a second; WEST stops it. The safe area (a phone's notch,
 * rounded corners, home indicator) is drawn as a frame inset by it, the window's edge
 * outside it; on a desktop it is the whole window. Orientation: the window's shape, and the
 * lock held; L asks for landscape, P for portrait, A lets go, F asks for fullscreen (a
 * browser locks only there, and only on a phone). Escape quits where quitting means
 * anything. */

static const char *orientation_name(wgf_window_orientation_t orientation)
{
    switch (orientation) {
        case WGF_WINDOW_ORIENTATION_LANDSCAPE: return "landscape";
        case WGF_WINDOW_ORIENTATION_PORTRAIT: return "portrait";
        default: return "any";
    }
}

static const char *asked = "nothing asked yet";

static void ask_orientation(wgf_window_orientation_t orientation)
{
    if (wgf_window_request_orientation(orientation)) {
        asked = orientation == WGF_WINDOW_ORIENTATION_LANDSCAPE  ? "asked for landscape"
                : orientation == WGF_WINDOW_ORIENTATION_PORTRAIT ? "asked for portrait"
                                                                 : "let go";
    } else {
        asked = "refused: nothing here locks the screen";
    }
}

static void draw_pad(int pad, float x, float y)
{
    char line[128];
    const wgf_color_t color = wgf_gamepad_is_rumbling(pad) ? WGF_COLOR_GOLD : WGF_COLOR_LIGHTGRAY;
    if (!wgf_gamepad_is_connected(pad)) {
        snprintf(line, sizeof(line), "pad %d: not connected", pad);
        wgf_draw_text(0, line, x, y, 16, WGF_COLOR_GRAY);
        return;
    }
    snprintf(line, sizeof(line), "pad %d: %.32s, %s%s", pad, wgf_gamepad_get_name(pad),
             wgf_gamepad_can_rumble(pad) ? "can rumble" : "no motors",
             wgf_gamepad_is_rumbling(pad) ? ", rumbling" : "");
    wgf_draw_text(0, line, x, y, 16, color);
}

static void frame(void *user)
{
    const int width = wgf_window_get_width(), height = wgf_window_get_height();
    const int top = wgf_window_get_safe_inset_top(), right = wgf_window_get_safe_inset_right();
    const int bottom = wgf_window_get_safe_inset_bottom(), left = wgf_window_get_safe_inset_left();
    const float x = (float)left + 16.0f;
    float y = (float)top + 16.0f;
    char line[128];
    int pad;
    (void)user;

    for (pad = 0; pad < 4; pad++) {
        if (wgf_gamepad_is_pressed(pad, WGF_GAMEPAD_BUTTON_SOUTH)) wgf_gamepad_rumble(pad, 1.0f, 0.0f, 0.5f);
        if (wgf_gamepad_is_pressed(pad, WGF_GAMEPAD_BUTTON_EAST)) wgf_gamepad_rumble(pad, 0.0f, 1.0f, 0.5f);
        if (wgf_gamepad_is_pressed(pad, WGF_GAMEPAD_BUTTON_NORTH)) wgf_gamepad_rumble(pad, 1.0f, 1.0f, 0.5f);
        if (wgf_gamepad_is_pressed(pad, WGF_GAMEPAD_BUTTON_WEST)) wgf_gamepad_stop_rumble(pad);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_L)) ask_orientation(WGF_WINDOW_ORIENTATION_LANDSCAPE);
    if (wgf_keyboard_is_pressed(WGF_KEY_P)) ask_orientation(WGF_WINDOW_ORIENTATION_PORTRAIT);
    if (wgf_keyboard_is_pressed(WGF_KEY_A)) ask_orientation(WGF_WINDOW_ORIENTATION_ANY);
    if (wgf_keyboard_is_pressed(WGF_KEY_F)) wgf_window_request_fullscreen(!wgf_window_is_fullscreen());
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    /* the safe area: what is outside it shaded, its edge a frame */
    wgf_draw_rectangle(0.0f, 0.0f, (float)width, (float)top, wgf_color_make(60, 30, 30, 255));
    wgf_draw_rectangle(0.0f, (float)(height - bottom), (float)width, (float)bottom, wgf_color_make(60, 30, 30, 255));
    wgf_draw_rectangle(0.0f, (float)top, (float)left, (float)(height - top - bottom), wgf_color_make(60, 30, 30, 255));
    wgf_draw_rectangle((float)(width - right), (float)top, (float)right, (float)(height - top - bottom),
                       wgf_color_make(60, 30, 30, 255));
    wgf_draw_rectangle_lines((float)left + 2.0f, (float)top + 2.0f, (float)(width - left - right) - 4.0f,
                             (float)(height - top - bottom) - 4.0f, 4.0f, WGF_COLOR_LIME);

    wgf_draw_text(0, "libwgf device", x, y, 24, WGF_COLOR_RAYWHITE);
    y += 36.0f;
    snprintf(line, sizeof(line), "safe area insets: top %d, right %d, bottom %d, left %d", top, right, bottom, left);
    wgf_draw_text(0, line, x, y, 16, WGF_COLOR_LIGHTGRAY);
    y += 24.0f;
    snprintf(line, sizeof(line), "window %d by %d: %s", width, height, orientation_name(wgf_window_get_orientation()));
    wgf_draw_text(0, line, x, y, 16, WGF_COLOR_LIGHTGRAY);
    y += 24.0f;
    snprintf(line, sizeof(line), "lock held: %s; %s; %s", orientation_name(wgf_window_get_orientation_lock()),
             wgf_window_can_lock_orientation() ? "a lock can be asked for" : "nothing here locks", asked);
    wgf_draw_text(0, line, x, y, 16, WGF_COLOR_LIGHTGRAY);
    y += 24.0f;
    wgf_draw_text(0, "L landscape, P portrait, A let go, F fullscreen", x, y, 16, WGF_COLOR_GRAY);
    y += 36.0f;
    wgf_draw_text(0, "pad: SOUTH low motor, EAST high, NORTH both, WEST stops", x, y, 16, WGF_COLOR_GRAY);
    y += 24.0f;
    for (pad = 0; pad < 4; pad++) {
        draw_pad(pad, x, y);
        y += 22.0f;
    }
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(20, 22, 30, 255));
}

int main(void)
{
    wgf_window_set_title("libwgf device");
    wgf_window_set_size(720, 400);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
