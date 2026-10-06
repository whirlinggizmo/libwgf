#include <stdio.h>
#include <string.h>

#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_app.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_log.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_ui.h"
#include "wgf_ui_priv.h"
#include "wgf_window.h"

/* The UI headless, frame by frame through the runtime, with events queued as the window
 * would deliver them and a test pad in place of the platform's: a menu laid out and
 * drawn; the pointer hovering, pressing, and releasing a button, which activates it and
 * gives it the focus, undrawn; the focus moved by arrow keys, Tab and Shift+Tab, and the
 * D-pad, wrapping, and a button activated by Enter and by the pad; the focus lost with
 * its button and given by the program; the captures; the layout setters and every
 * refusal; and the style. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void push(sapp_event_type type, sapp_keycode key, uint32_t modifiers)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    event.modifiers = modifiers;
    wgf_platform_priv_headless_push_event(&event);
}

static void tap(sapp_keycode key, uint32_t modifiers)
{
    push(SAPP_EVENTTYPE_KEY_DOWN, key, modifiers);
    push(SAPP_EVENTTYPE_KEY_UP, key, modifiers);
}

static void mouse(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    event.mouse_x = x;
    event.mouse_y = y;
    wgf_platform_priv_headless_push_event(&event);
}

/* The center of what `id` was laid out as. */
static void center(const char *id, float *x, float *y)
{
    float left = 0, top = 0, width = 0, height = 0;
    expect(wgf_ui_priv_get_bounds(id, &left, &top, &width, &height), "laid out");
    *x = left + width * 0.5f;
    *y = top + height * 0.5f;
}

static wgf_platform_priv_gamepad_t pad;

static void press_pad(wgf_gamepad_button_t button, bool down)
{
    pad.buttons[button] = down;
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
}

/* The menu: a panel of a title and three buttons; which were activated, as letters. */
static char activated[8];
static bool with_play = true;

static void menu(void)
{
    int n = 0;
    expect(wgf_ui_begin(), "begun");
    expect(wgf_ui_begin_panel("menu"), "a panel");
    wgf_ui_label("ASTEROIDS", 48);
    if (with_play && wgf_ui_button("play", "Play")) activated[n++] = 'p';
    if (wgf_ui_button("options", "Options")) activated[n++] = 'o';
    if (wgf_ui_button("quit", "Quit")) activated[n++] = 'q';
    activated[n] = '\0';
    expect(wgf_ui_end_panel(), "the panel ended");
    expect(wgf_ui_end(), "ended");
}

static int frame_number;

static void on_init(void *user)
{
    (void)user;
    expect(!wgf_ui_begin(), "outside a frame: refused");
    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    snprintf(pad.name, sizeof(pad.name), "%s", "test pad");
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
}

static void on_frame(void *user)
{
    float x, y;
    (void)user;
    switch (frame_number++) {
    case 0: {
        const int before = sgl_num_vertices();
        menu();
        expect(sgl_num_vertices() > before, "drawn");
        expect(activated[0] == '\0' && wgf_ui_get_focus()[0] == '\0', "nothing activated, nothing focused");
        center("play", &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
        break;
    }
    case 1:
        menu();
        expect(wgf_input_is_pointer_captured(), "over a button: the pointer is the UI's");
        center("play", &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x, y);
        break;
    case 2:
        menu();
        expect(activated[0] == '\0', "pressed: not yet activated");
        expect(strcmp(wgf_ui_get_focus(), "play") == 0, "and focused");
        expect(wgf_input_is_keyboard_captured(), "a focus: the keyboard is the UI's");
        center("play", &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_UP, x, y);
        break;
    case 3:
        menu();
        expect(strcmp(activated, "p") == 0, "released over it: activated");
        center("quit", &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x, y);
        break;
    case 4:
        menu();
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 2, 2); /* an up carries no position: moved first */
        mouse(SAPP_EVENTTYPE_MOUSE_UP, 2, 2);
        break;
    case 5:
        menu();
        expect(activated[0] == '\0', "released off it: not activated");
        expect(!wgf_input_is_pointer_captured(), "off the UI: the pointer is the game's");
        wgf_ui_set_focus("play");
        tap(SAPP_KEYCODE_DOWN, 0);
        break;
    case 6:
        menu();
        expect(strcmp(wgf_ui_get_focus(), "options") == 0, "Down: the next");
        tap(SAPP_KEYCODE_ENTER, 0);
        break;
    case 7:
        menu();
        expect(strcmp(activated, "o") == 0, "Enter: the focused one activated");
        tap(SAPP_KEYCODE_TAB, SAPP_MODIFIER_SHIFT);
        push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_LEFT_SHIFT, SAPP_MODIFIER_SHIFT);
        break;
    case 8:
        menu();
        expect(strcmp(wgf_ui_get_focus(), "play") == 0, "Shift+Tab: the one before");
        push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_LEFT_SHIFT, 0);
        tap(SAPP_KEYCODE_UP, 0);
        break;
    case 9:
        menu();
        expect(strcmp(wgf_ui_get_focus(), "quit") == 0, "Up from the first: the last");
        press_pad(WGF_GAMEPAD_BUTTON_DPAD_DOWN, true);
        break;
    case 10:
        menu();
        expect(strcmp(wgf_ui_get_focus(), "play") == 0, "the D-pad from the last: the first");
        press_pad(WGF_GAMEPAD_BUTTON_DPAD_DOWN, false);
        press_pad(WGF_GAMEPAD_BUTTON_SOUTH, true);
        break;
    case 11:
        menu();
        expect(strcmp(activated, "p") == 0, "the pad's south button: activated");
        press_pad(WGF_GAMEPAD_BUTTON_SOUTH, false);
        with_play = false;
        break;
    case 12:
        menu();
        expect(wgf_ui_get_focus()[0] == '\0', "its button gone: the focus with it");
        expect(!wgf_input_is_keyboard_captured(), "no focus: the keyboard is the game's");
        tap(SAPP_KEYCODE_TAB, 0);
        with_play = true;
        break;
    case 13:
        menu();
        expect(strcmp(wgf_ui_get_focus(), "play") == 0, "Tab with no focus: the first");
        center("menu", &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y - 1000);
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
        break;
    case 14:
        menu();
        expect(wgf_input_is_pointer_captured(), "over the panel: the pointer is the UI's");
        break;
    case 15:
        /* no UI this frame */
        break;
    case 16:
        expect(!wgf_input_is_pointer_captured() && !wgf_input_is_keyboard_captured(),
               "a frame with no UI let both go");

        /* the layout setters and the refusals */
        expect(!wgf_ui_label("outside", 0) && !wgf_ui_button("b", "B") && !wgf_ui_end() && !wgf_ui_end_box(),
               "outside a UI: refused");
        expect(wgf_ui_begin() && !wgf_ui_begin(), "a second begin refused");
        expect(!wgf_ui_set_gap(4), "the screen's layout isn't the program's");
        expect(wgf_ui_begin_box("row", WGF_UI_DIRECTION_ROW), "a row");
        expect(wgf_ui_set_width(WGF_UI_SIZING_FIXED, 300) && wgf_ui_set_height(WGF_UI_SIZING_PERCENT, 0.5f) &&
                   wgf_ui_set_padding(4, 2) && wgf_ui_set_gap(6) &&
                   wgf_ui_set_align(WGF_UI_ALIGN_END, WGF_UI_ALIGN_START) && wgf_ui_set_color(0x203040FFu),
               "its layout");
        expect(!wgf_ui_set_width(WGF_UI_SIZING_PERCENT, 2) && !wgf_ui_set_height((wgf_ui_sizing_t)9, 1) &&
                   !wgf_ui_set_padding(-1, 0) && !wgf_ui_set_align((wgf_ui_align_t)3, WGF_UI_ALIGN_START),
               "a sizing, value, or alignment that isn't one: refused");
        expect(wgf_ui_spacer(10) && wgf_ui_label("left", 0) && wgf_ui_spacer(0) && wgf_ui_label("right", 0), "a row's children");
        expect(!wgf_ui_set_gap(2), "after a child: refused");
        expect(!wgf_ui_label("", 0) && !wgf_ui_label(NULL, 0) && !wgf_ui_button("", "x") && !wgf_ui_spacer(-1),
               "no text, no id, or less than no room: refused");
        expect(!wgf_ui_end_panel() && wgf_ui_end_box(), "an end of the wrong kind refused, the right one taken");
        expect(!wgf_ui_end_box(), "nothing open: refused");
        expect(!wgf_ui_begin_box(NULL, (wgf_ui_direction_t)4), "a direction that isn't one: refused");
        wgf_ui_begin_box(NULL, WGF_UI_DIRECTION_COLUMN);
        wgf_log_set_level(WGF_LOG_LEVEL_FATAL); /* the unbalanced end logs, on purpose */
        expect(!wgf_ui_end(), "ended with a box open: closed, and false");
        wgf_log_set_level(WGF_LOG_LEVEL_INFO);
        {
            float bx, by, bw, bh;
            expect(wgf_ui_priv_get_bounds("row", &bx, &by, &bw, &bh) && bw == 300 && bh == 294,
                   "a fixed width; half the screen's height less the gap to its sibling");
        }

        /* the style */
        expect(wgf_ui_get_style_value(WGF_UI_VALUE_TEXT_SIZE) == 20 &&
                   wgf_ui_get_style_color(WGF_UI_COLOR_FOCUS) == 0xFFD040FFu && wgf_ui_get_style_font() == 0,
               "libwgf's style");
        expect(wgf_ui_set_style_value(WGF_UI_VALUE_TEXT_SIZE, 30) && wgf_ui_get_style_value(WGF_UI_VALUE_TEXT_SIZE) == 30 &&
                   wgf_ui_set_style_color(WGF_UI_COLOR_PANEL, 0x11223344u) &&
                   wgf_ui_get_style_color(WGF_UI_COLOR_PANEL) == 0x11223344u,
               "the game's");
        expect(!wgf_ui_set_style_value(WGF_UI_VALUE_GAP, -1) && !wgf_ui_set_style_value((wgf_ui_value_t)7, 1) &&
                   !wgf_ui_set_style_color((wgf_ui_color_t)-1, 0) && wgf_ui_get_style_value((wgf_ui_value_t)7) == 0 &&
                   !wgf_ui_set_style_font(12345),
               "what isn't one: refused");
        wgf_ui_reset_style();
        expect(wgf_ui_get_style_value(WGF_UI_VALUE_TEXT_SIZE) == 20, "reset");
        expect(!wgf_ui_set_focus("0123456789012345678901234567890123456789012345678901234567890123") &&
                   wgf_ui_set_focus(NULL) && wgf_ui_get_focus()[0] == '\0',
               "a focus id of 64 bytes refused; none taken");
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 600);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 17, "every frame ran");
    return failures == 0 ? 0 : 1;
}
