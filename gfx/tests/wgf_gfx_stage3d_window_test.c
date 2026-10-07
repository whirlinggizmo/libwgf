#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>
#include <stdlib.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_light.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_presentation.h"
#include "wgf_render.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* A stage, in a window (tools/run_in_xvfb.py, OpenGL on a virtual display), its pixels
 * read back: an unlit red cube in front of the camera, its color exact (no tone
 * mapping of an unlit color: NONE), with a green 3D shape (unlit) above it; a lit white
 * cube beside it, black with no light, then lit by a sun facing it; a see-through blue
 * plane in front of the red, blended over it; and 2D drawn before and after the stage,
 * under and over it. Then under a presentation (FIT, 32 by 32 on 64 by 32): the stage
 * fills the design area, between the bars. */

static int failures;
static wgf_actor_t stage, camera, lit, glass, sun;

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

static void draw_frame(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_draw_rectangle(0, 28, 64, 4, WGF_COLOR_YELLOW); /* under the stage, where nothing of it covers */
    wgf_stage3d_draw(stage);
    wgf_draw_rectangle(0, 0, 2, 2, WGF_COLOR_WHITE); /* after it, over it */
    wgf_gfx_priv_end_frame();
}

static int frames;

static void on_frame(void *user)
{
    (void)user;
    if (++frames != 2) return;
    draw_frame();
    expect_pixel(24, 16, WGF_COLOR_RED, 0, "the unlit cube: its color as it is");
    expect_pixel(40, 16, WGF_COLOR_BLACK, 0, "the lit cube, with no light: black");
    expect_pixel(1, 30, WGF_COLOR_YELLOW, 0, "2D drawn before the stage, where it draws nothing");
    expect_pixel(1, 1, WGF_COLOR_WHITE, 0, "2D drawn after it, over it");
    expect_pixel(60, 4, WGF_COLOR_DARKGRAY, 0, "the clear color past the models");
    expect_pixel(32, 4, WGF_COLOR_GREEN, 0, "a 3D shape on the stage");

    wgf_actor_set_parent(sun, stage); /* the sun, from behind the camera */
    draw_frame();
    {
        unsigned char rgba[4];
        glReadPixels(40, 15, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        expect(rgba[0] > 150 && rgba[0] == rgba[1] && rgba[1] == rgba[2], "lit by the sun: bright, and white");
    }

    wgf_actor_set_visible(glass, true); /* half see-through blue, in front of the red */
    draw_frame();
    expect_pixel(24, 15,
                 wgf_color_make((wgf_color_get_red(WGF_COLOR_RED) + wgf_color_get_red(WGF_COLOR_BLUE)) / 2,
                                (wgf_color_get_green(WGF_COLOR_RED) + wgf_color_get_green(WGF_COLOR_BLUE)) / 2,
                                (wgf_color_get_blue(WGF_COLOR_RED) + wgf_color_get_blue(WGF_COLOR_BLUE)) / 2, 255),
                 2, "the glass blended over the red, half and half");

    wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 32, 32);
    wgf_actor_set_visible(glass, false);
    draw_frame();
    expect_pixel(4, 16, WGF_COLOR_BLUE, 0, "fit: the bar, nothing of the stage in it");
    expect_pixel(17, 1, WGF_COLOR_WHITE, 0, "fit: 2D at the design's corner");
    expect_pixel(22, 16, WGF_COLOR_RED, 0, "fit: the unlit cube in the design area");
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    wgf_mesh_t cube;
    wgf_material_t red, blue;
    wgf_actor_t model;

    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    wgf_render_set_bar_color(WGF_COLOR_BLUE);
    stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_stage3d_set_camera(stage, camera);
    cube = wgf_mesh_create_cube(1.5f, 1.5f, 1.5f);

    red = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    wgf_material_set_color(red, "base_color", WGF_COLOR_RED);
    model = wgf_model_create(cube);
    wgf_model_set_material(model, 0, red);
    wgf_actor_set_position(model, -1.5f, 0.0f, 0.0f);
    wgf_actor_set_parent(model, stage);

    lit = wgf_model_create(cube); /* the mesh's own material: white, lit */
    wgf_actor_set_position(lit, 1.5f, 0.0f, 0.0f);
    wgf_actor_set_parent(lit, stage);

    blue = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    wgf_material_set_color(blue, "base_color", WGF_COLOR_BLUE);
    glass = wgf_model_create(wgf_mesh_create_plane(2.0f, 2.0f, 0));
    wgf_model_set_material(glass, 0, blue);
    wgf_model_set_tint(glass, wgf_color_make(255, 255, 255, 128));
    wgf_actor_set_rotation(glass, 1.5707963f, 0.0f, 0.0f); /* facing the camera */
    wgf_actor_set_position(glass, -1.5f, 0.0f, 1.5f);
    wgf_actor_set_visible(glass, false);
    wgf_actor_set_parent(glass, stage);

    {
        const wgf_actor_t marker = wgf_shape3d_create();
        wgf_shape3d_set_cube(marker, 0.8f, 0.6f, 0.8f);
        wgf_shape3d_set_color(marker, WGF_COLOR_GREEN);
        wgf_actor_set_position(marker, 0.0f, 1.6f, 0.0f);
        wgf_actor_set_parent(marker, stage);
    }

    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_intensity(sun, 3.14159265f);
    wgf_actor_set_position(sun, 0.0f, 0.0f, 10.0f);
    wgf_actor_look_at(sun, 0, 0, 0, 0, 1, 0);

    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
