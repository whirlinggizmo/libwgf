#include <math.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_text.h"
#include "wgf_window.h"

/* Text in the world and 3D shapes, libwgt's gfx-text3d (wgrender-c's text3d):
 *   - labels facing the camera above a cube, a sphere, and a rectangle
 *   - a sign: text facing FREE, turned with its actor
 *   - circle outlines on the ground, and a line-strip spiral
 * Differs: libwgf has no picking yet (milestone 2.5), so libwgt's hover highlight and its
 * P key (the cube pickable or not) are left out; "libwgt" reads "libwgf", its scene is a
 * stage3d, and its frame rate libwgf's overlay.
 *
 *   Escape   quit, where quitting means anything */

#define FONT_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define SPIRAL_POINTS 161

static wgf_actor_t stage, camera, cube, sign;
static wgf_font_t font;
static float t; /* seconds of frames */

static wgf_actor_t add_shape(float x, float y, float z, float rx, float ry)
{
    const wgf_actor_t shape = wgf_shape3d_create();
    wgf_actor_set_transform(shape, x, y, z, rx, ry, 0, 1, 1, 1);
    wgf_actor_set_parent(shape, stage);
    return shape;
}

static wgf_actor_t add_label(const char *string, float x, float y, float z)
{
    const wgf_actor_t label = wgf_text_create(font);
    wgf_text_set_string(label, string);
    wgf_text_set_font_size(label, 0.35f);
    wgf_text_set_color(label, WGF_COLOR_RAYWHITE);
    wgf_actor_set_position(label, x, y, z);
    wgf_actor_set_parent(label, stage);
    return label;
}

static void init(void *user)
{
    static float spiral[SPIRAL_POINTS * 3];
    wgf_actor_t shape;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(22, 24, 30, 255));
    font = wgf_font_create(FONT_PATH); /* the text draws once it has loaded */

    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 3.14159265f / 4.0f);
    wgf_actor_set_position(camera, 0, 4.0f, 9.0f);
    wgf_actor_look_at(camera, 0, 0.8f, 0, 0, 1, 0);
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);

    cube = add_shape(-3, 0.6f, 0, 0, 0);
    wgf_shape3d_set_cube(cube, 1.2f, 1.2f, 1.2f);
    wgf_shape3d_set_color(cube, wgf_color_make(230, 180, 60, 255));
    shape = add_shape(0, 0.7f, 0, 0, 0);
    wgf_shape3d_set_sphere(shape, 0.7f);
    wgf_shape3d_set_color(shape, wgf_color_make(220, 90, 120, 255));
    shape = add_shape(3, 0.8f, 0, 0, -0.5f);
    wgf_shape3d_set_rectangle(shape, 1.4f, 1.0f);
    wgf_shape3d_set_color(shape, wgf_color_make(60, 190, 180, 255));
    for (i = 0; i < 3; i++) { /* rings lying on the ground under each */
        shape = add_shape(-3.0f + 3.0f * (float)i, 0.01f, 0, -1.5707963f, 0);
        wgf_shape3d_set_circle(shape, 1.0f);
        wgf_shape3d_set_color(shape, wgf_color_make(120, 130, 160, 255));
    }
    for (i = 0; i < SPIRAL_POINTS; i++) { /* a line strip */
        const float s = (float)i / (float)(SPIRAL_POINTS - 1), a = s * 6.2831853f * 4.0f;
        spiral[i * 3] = cosf(a) * (0.2f + s);
        spiral[i * 3 + 1] = s * 2.5f;
        spiral[i * 3 + 2] = sinf(a) * (0.2f + s);
    }
    shape = add_shape(0, 0, -3, 0, 0);
    wgf_shape3d_set_line_strip(shape, spiral, SPIRAL_POINTS * 3);
    wgf_shape3d_set_color(shape, wgf_color_make(60, 190, 180, 255));

    add_label("cube", -3, 1.7f, 0);
    add_label("sphere", 0, 1.9f, 0);
    add_label("rectangle", 3, 1.8f, 0);
    sign = add_label("libwgf text3d", 0, 3.2f, -3); /* FREE facing: turned by its actor, like a sign */
    wgf_text_set_font_size(sign, 0.6f);
    wgf_text_set_facing(sign, WGF_SPRITE_FACING_FREE);
    wgf_text_set_color(sign, wgf_color_make(230, 180, 60, 255));
    wgf_resource_release(font); /* the texts hold their own references */
}

static void frame(void *user)
{
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    t += wgf_loop_get_frame_delta();
    wgf_actor_set_rotation(cube, 0, t * 0.7f, 0);
    wgf_actor_set_rotation(sign, 0, sinf(t * 0.6f) * 0.6f, 0);

    wgf_draw_begin_3d(camera);
    wgf_draw_grid(16, 1.0f, wgf_color_make(60, 64, 76, 255));
    wgf_draw_end_3d();
    wgf_stage3d_draw(stage);
    wgf_draw_text(wgf_text_get_font(sign), "libwgf text3d: text in 3D, shapes", 12, 36, 20, WGF_COLOR_RAYWHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf text3d");
    wgf_window_set_size(1000, 640);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's wgt_loop_draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
