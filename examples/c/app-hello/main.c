#include <stddef.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* A window with 2D shapes and input: a marker follows the mouse and grows while the
 * left button is down, and Escape quits where quitting means anything. libwgt's
 * hello, drawn with thick lines. */

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(WGF_COLOR_RAYWHITE);
}

static void frame(void *user)
{
    static const float star[] = {560, 60, 575, 105, 620, 105, 585, 130, 600, 175,
                                 560, 150, 520, 175, 535, 130, 500, 105, 545, 105};
    const wgf_vec2_t mouse = wgf_mouse_get_position();
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    /* filled and outlined rectangles */
    wgf_draw_rectangle(40, 40, 200, 120, WGF_COLOR_SKYBLUE);
    wgf_draw_rectangle_lines(40, 40, 200, 120, 3, WGF_COLOR_DARKBLUE);

    /* lines, a triangle, circles, and a polyline with sharp corners */
    wgf_draw_line(40, 200, 240, 320, 1, WGF_COLOR_RED);
    wgf_draw_line(60, 200, 260, 320, 6, WGF_COLOR_RED);
    wgf_draw_triangle(320, 60, 280, 180, 360, 180, WGF_COLOR_GOLD);
    wgf_draw_circle(440, 120, 60, WGF_COLOR_PURPLE);
    wgf_draw_circle_lines(440, 120, 60, 4, WGF_COLOR_BLACK);
    wgf_draw_polygon(star, 20, WGF_COLOR_ORANGE);
    wgf_draw_polyline(star, 20, true, 3, WGF_COLOR_BROWN);

    /* a marker that follows the mouse */
    wgf_draw_circle(mouse.x, mouse.y, wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT) ? 16.0f : 8.0f, WGF_COLOR_MAROON);
}

int main(void)
{
    wgf_window_set_title("app-hello");
    wgf_window_set_size(800, 450);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
