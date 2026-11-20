#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* A mesh made from numbers, in a window (tools/run_in_xvfb.py, OpenGL on a virtual
 * display), its pixels read back: two squares in one mesh, unlit white, colored by their
 * corners, red and green, each its color exactly (no tone mapping: NONE); then the mesh
 * changed in place, the red square moved to the middle and the green one colored blue:
 * drawn in its new place and color, the clear color where the red one was. */

static int failures;
static wgf_actor_t stage;
static wgf_mesh_t mesh;

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

static void draw_frame(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
}

/* Square `square`'s four corners (counterclockwise from the camera), x from x0 to x0 + 2,
 * y -1 to 1, into `positions`. */
static void put_square(float *positions, int square, float x0)
{
    const float corners[12] = {x0, -1, 0, x0 + 2, -1, 0, x0 + 2, 1, 0, x0, 1, 0};
    int i;
    for (i = 0; i < 12; i++) positions[square * 12 + i] = corners[i];
}

/* Square `square`'s corners colored `color`'s channels, 0..1. */
static void paint_square(float *colors, int square, wgf_color_t color)
{
    int i;
    for (i = 0; i < 4; i++) {
        float *c = &colors[(square * 4 + i) * 4];
        c[0] = (float)wgf_color_get_red(color) / 255.0f;
        c[1] = (float)wgf_color_get_green(color) / 255.0f;
        c[2] = (float)wgf_color_get_blue(color) / 255.0f;
        c[3] = 1.0f;
    }
}

static int frames;

static void on_frame(void *user)
{
    float positions[24], colors[32];
    (void)user;
    if (++frames != 2) return;
    draw_frame();
    expect_pixel(18, 16, WGF_COLOR_RED, 1, "the left square, its corners red");
    expect_pixel(45, 16, WGF_COLOR_GREEN, 1, "the right square, its corners green");
    expect_pixel(32, 16, WGF_COLOR_DARKGRAY, 0, "the clear color between them");

    put_square(positions, 0, -1.0f); /* the red square to the middle */
    put_square(positions, 1, 1.0f);
    paint_square(colors, 0, WGF_COLOR_RED);
    paint_square(colors, 1, WGF_COLOR_BLUE);
    if (!wgf_mesh_set_positions(mesh, positions, 24) || !wgf_mesh_set_colors(mesh, colors, 32)) {
        printf("FAIL: the mesh changed in place\n");
        failures++;
    }
    draw_frame();
    expect_pixel(18, 16, WGF_COLOR_DARKGRAY, 0, "moved: the clear color where the red square was");
    expect_pixel(30, 16, WGF_COLOR_RED, 1, "moved: the red square in the middle");
    expect_pixel(45, 16, WGF_COLOR_BLUE, 1, "recolored: the right square blue");

    put_square(positions, 0, -3.0f); /* and back, a frame on: the buffer that changes, written again */
    wgf_mesh_set_positions(mesh, positions, 24);
    draw_frame();
    expect_pixel(18, 16, WGF_COLOR_RED, 1, "moved back, a frame on");
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    static const int indices[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    float positions[24], colors[32];
    wgf_material_t white;
    wgf_actor_t camera, model;

    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_stage3d_set_camera(stage, camera);

    put_square(positions, 0, -3.0f);
    put_square(positions, 1, 1.0f);
    paint_square(colors, 0, WGF_COLOR_RED);
    paint_square(colors, 1, WGF_COLOR_GREEN);
    mesh = wgf_mesh_create_triangles(positions, 24, NULL, 0, NULL, 0, indices, 12);
    if (mesh == 0 || !wgf_mesh_set_colors(mesh, colors, 32)) {
        printf("FAIL: a mesh made from numbers, colored\n");
        return 1;
    }
    white = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    wgf_material_set_color(white, "base_color", WGF_COLOR_WHITE);
    model = wgf_model_create(mesh);
    wgf_model_set_material(model, 0, white);
    wgf_actor_set_parent(model, stage);

    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
