#include <math.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* A camera circling depth-tested 3D shapes drawn immediately -- a grid, the axes, a
 * cube with its edges, and two spheres -- under a 2D overlay. Escape quits where
 * quitting means anything.
 *
 * libwgt's gfx-hello3d (wgrender's hello3d) done 1:1 for the size table. The
 * differences:
 *   - the title and the big text say libwgf where libwgt's say libwgt;
 *   - wgf_node_look_at takes the target and up direction as numbers, libwgf's public
 *     calls taking no vectors;
 *   - the readout is libwgf's overlay (wgf_debug_show_fps), as libwgt's is its
 *     wgt_loop_draw_fps, at the same place. */

static wgf_node_t camera;

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(28, 28, 38, 255));
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 3.14159265f / 4.0f);
}

static void frame(void *user)
{
    const float t = (float)wgf_time_get_seconds();
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    /* circle the origin, looking at it */
    wgf_node_set_position(camera, cosf(t * 0.4f) * 16.0f, 9.0f, sinf(t * 0.4f) * 16.0f);
    wgf_node_look_at(camera, 0, 1, 0, 0, 1, 0);

    /* ---- 3D ---- */
    wgf_draw_begin_3d(camera);
    wgf_draw_grid(20, 1.0f, WGF_COLOR_DARKGRAY);
    /* axes */
    wgf_draw_line_3d(0, 0, 0, 5, 0, 0, WGF_COLOR_RED);
    wgf_draw_line_3d(0, 0, 0, 0, 5, 0, WGF_COLOR_GREEN);
    wgf_draw_line_3d(0, 0, 0, 0, 0, 5, WGF_COLOR_BLUE);
    wgf_draw_cube(0.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f, WGF_COLOR_SKYBLUE);
    wgf_draw_cube_wires(0.0f, 1.0f, 0.0f, 2.02f, 2.02f, 2.02f, WGF_COLOR_DARKBLUE);
    wgf_draw_sphere(5.0f, 1.5f, 0.0f, 1.5f, WGF_COLOR_GOLD);
    wgf_draw_sphere(-5.0f, 1.5f, 0.0f, 1.5f, WGF_COLOR_MAROON);
    wgf_draw_end_3d();

    /* ---- 2D overlay ---- */
    wgf_draw_text(0, "libwgf 3D", 12, 36, 24, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, "a circling camera over depth-tested 3D shapes", 12, 70, 16, WGF_COLOR_LIGHTGRAY);
    if (wgf_app_can_quit()) wgf_draw_text(0, "press Esc to quit", 12, 94, 16, WGF_COLOR_GRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf hello3d");
    wgf_window_set_size(900, 700);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
