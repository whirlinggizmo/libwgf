#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_debug.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* A window with 2D shapes, text, and input: a marker follows the mouse, typing shows
 * what was typed (Backspace takes the last character back), and Escape quits where
 * quitting means anything. The frame rate is drawn at the top.
 *
 * libwgt's app-hello (wgrender's hello) done 1:1 for the size table. The differences:
 *   - the title and the big text say libwgf where libwgt's say libwgt;
 *   - outlines and the line take a thickness here, 1 logical pixel: libwgt's are GL
 *     lines of one pixel;
 *   - The readout is libwgf's overlay (wgf_debug_show_fps), as libwgt's is its
 *     wgt_loop_draw_fps: the same place, font, size, and color, with the frame's
 *     cost in milliseconds beside the rate.
 *   - circles get enough segments for their size, where libwgt's have 36. */

static char typed[64];


static void frame(void *user)
{
    const wgf_vec2_t mouse = wgf_mouse_get_position();
    const char *chars = wgf_input_get_chars();
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_BACKSPACE) && typed[0] != '\0') typed[strlen(typed) - 1] = '\0';
    if (strlen(typed) + strlen(chars) < sizeof(typed)) strcat(typed, chars);

    /* filled and outlined rectangles */
    wgf_draw_rectangle(40, 40, 200, 120, WGF_COLOR_SKYBLUE);
    wgf_draw_rectangle_lines(40, 40, 200, 120, 1, WGF_COLOR_DARKBLUE);

    /* a line, a triangle, circles */
    wgf_draw_line(40, 200, 240, 320, 1, WGF_COLOR_RED);
    wgf_draw_triangle(320, 60, 280, 180, 360, 180, WGF_COLOR_GOLD);
    wgf_draw_circle(440, 120, 60, WGF_COLOR_PURPLE);
    wgf_draw_circle_lines(440, 120, 60, 1, WGF_COLOR_BLACK);

    /* a marker that follows the mouse */
    wgf_draw_circle(mouse.x, mouse.y, 8, WGF_COLOR_MAROON);

    /* text */
    wgf_draw_text(0, "libwgf", 40, 360, 32, WGF_COLOR_DARKGRAY);
    wgf_draw_text(0, typed[0] != '\0' ? typed : "type something", 40, 410, 16,
                  typed[0] != '\0' ? WGF_COLOR_DARKBLUE : WGF_COLOR_GRAY);
    if (wgf_app_can_quit()) wgf_draw_text(0, "press Esc to quit", 40, 440, 16, WGF_COLOR_GRAY);
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(WGF_COLOR_RAYWHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf hello");
    wgf_window_set_size(800, 600);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 40, 12, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
