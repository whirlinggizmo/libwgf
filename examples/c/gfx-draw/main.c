#include <math.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Immediate mode's shapes beyond the basics: a ring, a sector, and an arc sweeping round, an
 * ellipse and its outline, a rectangle and a circle in gradients, a texture turning about
 * its middle, a texture filling as a bar and as a clockwise sweep (a cooldown); and in 3D,
 * a triangle, a cylinder, and wire spheres, cylinders, and capsules, round a turning camera.
 * Escape quits where quitting means anything. */

static wgf_texture_t tiles;
static wgf_actor_t eye;
static float time_passed;

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(16, 18, 26, 255));
    tiles = wgf_texture_create("textures/tiles.png");
    eye = wgf_camera3d_create();
}

static void frame(void *user)
{
    const float t = time_passed, sweep = fmodf(t * 0.5f, 1.0f);
    (void)user;
    time_passed += wgf_loop_get_frame_delta();
    if (wgf_keyboard_is_pressed(WGF_KEY_ESCAPE) && wgf_app_can_quit()) wgf_app_quit();

    wgf_draw_ring(90, 90, 40, 60, 0, 6.2831853f, wgf_color_make(60, 70, 110, 255));
    wgf_draw_ring(90, 90, 40, 60, -1.5707963f, -1.5707963f + 6.2831853f * sweep, wgf_color_make(120, 200, 255, 255));
    wgf_draw_ring(230, 90, 0, 60, t, t + 1.2f, wgf_color_make(255, 190, 80, 255));
    wgf_draw_arc(370, 90, 50, -t, -t + 4.0f, 6, wgf_color_make(140, 255, 160, 255));
    wgf_draw_ellipse(510, 90, 70, 35, wgf_color_make(200, 100, 220, 255));
    wgf_draw_ellipse_lines(650, 90, 50, 70, 3, wgf_color_make(255, 255, 255, 255));
    wgf_draw_rectangle_gradient(30, 180, 200, 100, wgf_color_make(255, 60, 60, 255), wgf_color_make(60, 255, 60, 255),
                                wgf_color_make(60, 60, 255, 255), wgf_color_make(255, 255, 60, 255));
    wgf_draw_circle_gradient(320, 230, 55, wgf_color_make(255, 255, 255, 255), wgf_color_make(255, 80, 0, 0));
    wgf_draw_texture_rotated(tiles, 0, 0, 16, 16, 460, 230, 80, 80, 0.5f, 0.5f, t, wgf_color_make(255, 255, 255, 255));
    wgf_draw_texture_fill(tiles, 560, 190, 160, 24, sweep, WGF_DRAW_FILL_LEFT_TO_RIGHT, wgf_color_make(255, 255, 255, 255));
    wgf_draw_texture_fill(tiles, 620, 220, 70, 70, sweep, WGF_DRAW_FILL_CLOCKWISE, wgf_color_make(255, 255, 255, 255));

    wgf_actor_set_position(eye, sinf(t * 0.3f) * 9.0f, 4.0f, cosf(t * 0.3f) * 9.0f);
    wgf_actor_look_at(eye, 0, 0.5f, 0, 0, 1, 0);
    if (wgf_draw_begin_3d(eye)) {
        wgf_draw_grid(10, 1.0f, wgf_color_make(70, 70, 90, 255));
        wgf_draw_triangle_3d(-3, 0, -2, -1, 0, -2, -2, 2, -2, wgf_color_make(255, 120, 120, 255));
        wgf_draw_cylinder(2, 1, -2, 0.6f, 2, wgf_color_make(120, 160, 255, 255));
        wgf_draw_cylinder_wires(2, 1, 2, 0.6f, 2, wgf_color_make(255, 255, 255, 255));
        wgf_draw_sphere_wires(-2, 1, 2, 1, wgf_color_make(160, 255, 160, 255));
        wgf_draw_capsule_wires(0, 1, 0, 0.5f, 2, wgf_color_make(255, 220, 120, 255));
        wgf_draw_end_3d();
    }
}

int main(void)
{
    wgf_window_set_title("libwgf: drawing");
    wgf_window_set_size(800, 600);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
