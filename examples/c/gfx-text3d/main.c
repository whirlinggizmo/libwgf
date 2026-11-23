#include <math.h>
#include <stddef.h>
#include <stdio.h>

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
#include "wgf_pointer.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_text.h"
#include "wgf_window.h"

/* Text in the world, 3D shapes, and picking, libwgt's gfx-text3d (wgrender-c's text3d), the
 * same program for the size table:
 *   - labels facing the camera above a cube, a sphere, and a rectangle
 *   - a sign: text facing FREE, turned with its actor
 *   - circle outlines on the ground, and a line-strip spiral
 *   - hover: the stage is interactive, so the pointer finds what is under it (the labels
 *     included) and highlights it
 * Where it isn't libwgt's word for word: "libwgt" reads "libwgf", its scene is a stage3d, the
 * spiral's points are given at once (libwgt adds them one by one), and the frame rate is
 * libwgf's overlay.
 *
 *   P        the cube pickable or not
 *   Escape   quit, where quitting means anything */

#define FONT_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define SPIRAL_POINTS 161

enum { LABEL_COUNT = 3 };

static struct {
    wgf_actor_t stage, camera;
    wgf_font_t font;
    wgf_color_t grey, gold, teal, rose, highlight, ring;
    wgf_actor_t cube, sphere, panel, spiral, sign;
    wgf_actor_t rings[3];
    wgf_actor_t labels[LABEL_COUNT];
    float time;
} g;

static wgf_actor_t add_shape(float x, float y, float z, float rx, float ry)
{
    const wgf_actor_t shape = wgf_shape3d_create();
    wgf_actor_set_transform(shape, x, y, z, rx, ry, 0, 1, 1, 1);
    wgf_actor_set_parent(shape, g.stage);
    return shape;
}

static wgf_actor_t add_label(const char *string, float x, float y, float z)
{
    const wgf_actor_t label = wgf_text_create(g.font);
    wgf_text_set_string(label, string);
    wgf_text_set_font_size(label, 0.35f);
    wgf_text_set_color(label, WGF_COLOR_RAYWHITE);
    wgf_actor_set_position(label, x, y, z);
    wgf_actor_set_parent(label, g.stage);
    return label;
}

static void init(void *user)
{
    static float spiral[SPIRAL_POINTS * 3];
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(22, 24, 30, 255));
    g.grey = wgf_color_make(60, 64, 76, 255);
    g.gold = wgf_color_make(230, 180, 60, 255);
    g.teal = wgf_color_make(60, 190, 180, 255);
    g.rose = wgf_color_make(220, 90, 120, 255);
    g.highlight = wgf_color_make(255, 255, 255, 255);
    g.ring = wgf_color_make(120, 130, 160, 255);
    g.font = wgf_font_create(FONT_PATH); /* the text draws once it has loaded */

    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(g.camera, 3.14159265f / 4.0f);
    wgf_actor_set_position(g.camera, 0, 4.0f, 9.0f);
    wgf_actor_look_at(g.camera, 0, 0.8f, 0, 0, 1, 0);
    wgf_actor_set_parent(g.camera, g.stage);
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_pointer_set_interactive(g.stage, true);

    g.cube = add_shape(-3, 0.6f, 0, 0, 0);
    wgf_shape3d_set_cube(g.cube, 1.2f, 1.2f, 1.2f);
    g.sphere = add_shape(0, 0.7f, 0, 0, 0);
    wgf_shape3d_set_sphere(g.sphere, 0.7f);
    g.panel = add_shape(3, 0.8f, 0, 0, -0.5f);
    wgf_shape3d_set_rectangle(g.panel, 1.4f, 1.0f);
    for (i = 0; i < 3; i++) { /* rings lying on the ground under each */
        g.rings[i] = add_shape(-3.0f + 3.0f * (float)i, 0.01f, 0, -1.5707963f, 0);
        wgf_shape3d_set_circle(g.rings[i], 1.0f);
        wgf_shape3d_set_color(g.rings[i], g.ring);
        wgf_actor_set_pickable(g.rings[i], false);
    }
    for (i = 0; i < SPIRAL_POINTS; i++) { /* a line strip */
        const float s = (float)i / (float)(SPIRAL_POINTS - 1), a = s * 6.2831853f * 4.0f;
        spiral[i * 3] = cosf(a) * (0.2f + s);
        spiral[i * 3 + 1] = s * 2.5f;
        spiral[i * 3 + 2] = sinf(a) * (0.2f + s);
    }
    g.spiral = add_shape(0, 0, -3, 0, 0);
    wgf_shape3d_set_line_strip(g.spiral, spiral, SPIRAL_POINTS * 3);
    wgf_shape3d_set_color(g.spiral, g.teal);

    g.labels[0] = add_label("cube", -3, 1.7f, 0);
    g.labels[1] = add_label("sphere", 0, 1.9f, 0);
    g.labels[2] = add_label("rectangle", 3, 1.8f, 0);
    g.sign = add_label("libwgf text3d", 0, 3.2f, -3); /* FREE facing: turned by its actor, like a sign */
    wgf_text_set_font_size(g.sign, 0.6f);
    wgf_text_set_facing(g.sign, WGF_SPRITE_FACING_FREE);
    wgf_text_set_color(g.sign, g.gold);
    wgf_resource_release(g.font); /* the texts hold their own references */
}

static void frame(void *user)
{
    const wgf_actor_t hovered = wgf_pointer_get_hovered();
    char line[160];
    int i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_P)) wgf_actor_set_pickable(g.cube, !wgf_actor_is_pickable(g.cube));

    g.time += wgf_loop_get_frame_delta();
    wgf_actor_set_rotation(g.cube, 0, g.time * 0.7f, 0);
    wgf_actor_set_rotation(g.sign, 0, sinf(g.time * 0.6f) * 0.6f, 0);

    /* hover: the nearest pickable thing under the pointer */
    wgf_shape3d_set_color(g.cube, hovered == g.cube ? g.highlight : g.gold);
    wgf_shape3d_set_color(g.sphere, hovered == g.sphere ? g.highlight : g.rose);
    wgf_shape3d_set_color(g.panel, hovered == g.panel ? g.highlight : g.teal);
    for (i = 0; i < LABEL_COUNT; i++) {
        wgf_text_set_color(g.labels[i], hovered == g.labels[i] ? g.gold : WGF_COLOR_RAYWHITE);
    }

    wgf_draw_begin_3d(g.camera);
    wgf_draw_grid(16, 1.0f, g.grey);
    wgf_draw_end_3d();
    wgf_stage3d_draw(g.stage);

    wgf_draw_text(wgf_text_get_font(g.sign), "libwgf text3d: text in 3D, shapes, picking", 12, 10, 20,
                  WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "hover: %s   [P] cube pickable: %s",
             hovered == 0          ? "nothing"
             : hovered == g.cube   ? "cube"
             : hovered == g.sphere ? "sphere"
             : hovered == g.panel  ? "rectangle"
             : hovered == g.sign   ? "sign"
                                   : "label",
             wgf_actor_is_pickable(g.cube) ? "yes" : "no");
    wgf_draw_text(wgf_text_get_font(g.sign), line, 12, 36, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf text3d");
    wgf_window_set_size(1000, 640);
    wgf_window_set_flags(WGF_WINDOW_FLAG_MSAA);
    wgf_debug_show_fps(0, 12, 60, 16.0f, WGF_COLOR_LIME); /* libwgt's wgt_loop_draw_fps_ex */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
