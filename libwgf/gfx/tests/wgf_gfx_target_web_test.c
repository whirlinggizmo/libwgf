#include <stdio.h>
#include <stdlib.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Render targets, in a browser (tools/run_in_browser.py) on a WebGL2 canvas, their
 * pixels read back through the screen they are drawn on: a 32 by 32 target with a stage
 * (an unlit red plane above the middle, one-sided, facing the camera) and a yellow square
 * at its top-left, drawn at the screen's left, the right half blue: the square at the top,
 * as drawn (a GL target is drawn flipped, so stored top-down), and the plane seen (its
 * winding turned with the flip, else culled). A target begun twice in a frame keeps the
 * first pass's drawing; a clip inside a target clips there, and the screen's doesn't; a
 * target drawn into itself draws nothing; and the stage through another camera (looking
 * away) shows none of the plane, its own camera left as it was. */

static int failures;
static wgf_actor_t stage, away;
static wgf_texture_t target;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The pixel at (x, y) from the top-left of the 32-pixel-high canvas, its channels
 * within `slack` of the color's. */
static void expect_pixel(int x, int y, wgf_color_t color, int slack, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, 31 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (abs(rgba[0] - (int)wgf_color_get_red(color)) > slack ||
        abs(rgba[1] - (int)wgf_color_get_green(color)) > slack ||
        abs(rgba[2] - (int)wgf_color_get_blue(color)) > slack) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static void begin_frame(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
}

/* The target at the screen's left, the right half blue, and the frame drawn. */
static void show_target(void)
{
    wgf_draw_texture(target, 0, 0, 32, 32, WGF_COLOR_WHITE);
    wgf_draw_rectangle(32, 0, 32, 32, WGF_COLOR_BLUE);
    wgf_gfx_priv_end_frame();
}

static int frames;

static void on_frame(void *user)
{
    (void)user;
    if (++frames != 2) return;

    begin_frame();
    wgf_stage3d_draw(stage); /* on the screen, unflipped: its face toward the camera */
    wgf_gfx_priv_end_frame();
    expect_pixel(32, 6, WGF_COLOR_RED, 0, "the plane on the screen");

    begin_frame();
    expect(wgf_render_begin_target(target), "begun");
    wgf_stage3d_draw(stage);
    wgf_draw_rectangle(0, 0, 8, 8, WGF_COLOR_YELLOW);
    wgf_render_end_target();
    show_target();
    expect_pixel(2, 2, WGF_COLOR_YELLOW, 0, "the square at the target's top-left, as drawn");
    expect_pixel(16, 6, WGF_COLOR_RED, 0, "the plane above the middle, its face seen");
    expect_pixel(16, 28, WGF_COLOR_DARKGRAY, 0, "the target's clear color below it");
    expect_pixel(48, 16, WGF_COLOR_BLUE, 0, "the screen drawn after the target");

    begin_frame();
    wgf_render_begin_target(target);
    wgf_draw_rectangle(0, 0, 8, 8, WGF_COLOR_YELLOW);
    wgf_render_end_target();
    wgf_render_begin_target(target);
    wgf_draw_rectangle(24, 24, 8, 8, WGF_COLOR_GREEN);
    wgf_draw_texture(target, 0, 0, 32, 32, WGF_COLOR_WHITE); /* itself: nothing */
    wgf_render_end_target();
    show_target();
    expect_pixel(2, 2, WGF_COLOR_YELLOW, 0, "begun again: the first pass's drawing kept");
    expect_pixel(28, 28, WGF_COLOR_GREEN, 0, "and the second's over it");
    expect_pixel(16, 16, WGF_COLOR_DARKGRAY, 0, "a target drawn into itself draws nothing");

    begin_frame();
    wgf_render_push_clip(40, 0, 4, 4); /* the screen's, which the target's pass doesn't see */
    wgf_render_begin_target(target);
    wgf_render_push_clip(0, 0, 8, 32);
    wgf_draw_rectangle(0, 0, 32, 32, WGF_COLOR_GREEN);
    wgf_render_pop_clip();
    wgf_draw_rectangle(24, 0, 8, 8, WGF_COLOR_YELLOW);
    wgf_render_end_target();
    wgf_render_pop_clip();
    show_target();
    expect_pixel(2, 16, WGF_COLOR_GREEN, 0, "a clip in a target: inside it");
    expect_pixel(16, 16, WGF_COLOR_DARKGRAY, 0, "a clip in a target: outside it");
    expect_pixel(28, 2, WGF_COLOR_YELLOW, 0, "the screen's clip doesn't reach into the target");

    begin_frame();
    wgf_render_begin_target(target);
    wgf_stage3d_draw_with(stage, away);
    wgf_render_end_target();
    show_target();
    expect_pixel(16, 6, WGF_COLOR_DARKGRAY, 0, "through a camera looking away: none of the plane");
    expect(wgf_stage3d_get_camera(stage) != away, "the stage's own camera left as it was");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void init(void *user)
{
    (void)user;
    target = wgf_texture_create_target(32, 32);
    wgf_render_set_target_clear_color(target, WGF_COLOR_DARKGRAY);
}

int main(void)
{
    wgf_material_t red;
    wgf_actor_t camera, plane;

    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_stage3d_set_camera(stage, camera);
    away = wgf_camera3d_create();
    wgf_actor_set_position(away, 0.0f, 0.0f, 5.0f);
    wgf_actor_set_rotation(away, 0.0f, 3.14159265f, 0.0f);

    red = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    wgf_material_set_color(red, "base_color", WGF_COLOR_RED);
    plane = wgf_model_create(wgf_mesh_create_plane(4.0f, 1.2f, 0));
    wgf_model_set_material(plane, 0, red);
    wgf_actor_set_rotation(plane, 1.5707963f, 0.0f, 0.0f); /* facing the camera */
    wgf_actor_set_position(plane, 0.0f, 0.9f, 0.0f);
    wgf_actor_set_parent(plane, stage);

    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
