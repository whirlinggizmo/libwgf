#include <stdio.h>
#include <stdlib.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Custom shaders, in a browser (tools/run_in_browser.py) on a WebGL2 canvas, their pixels
 * read back: a unit plane facing the camera in the middle of a 64 by 32 canvas,
 * drawn by the tests' flat shader (libwgf/gfx/tests/shaders/flat.glsl: `color` times its
 * texture, unlit, its vertex hook moving it `shift` along x). Nothing while the shader
 * loads; then its color set before it loaded, its tint (the placement's record), a render
 * target cleared yellow as its texture, and moved by its vertex hook's parameter. */

static int failures;
static wgf_actor_t stage, plane;
static wgf_material_t flat;
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

/* One frame of the stage, the frame's pixels left to read. */
static void draw(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_render_begin_target(target); /* cleared, and drawn before the screen */
    wgf_render_end_target();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
}

static int frames;

static void on_frame(void *user)
{
    (void)user;
    frames++;
    if (wgf_resource_get_status(wgf_material_get_shader(flat)) == WGF_RESOURCE_STATUS_PENDING && frames < 600) {
        draw();
        expect_pixel(32, 16, WGF_COLOR_BLACK, 0, "nothing while its shader loads");
        return;
    }
    expect(wgf_resource_get_status(wgf_material_get_shader(flat)) == WGF_RESOURCE_STATUS_READY, "loaded");

    draw();
    expect_pixel(32, 16, wgf_color_make(255, 0, 0, 255), 1, "its color, set before it loaded");
    expect_pixel(4, 16, WGF_COLOR_BLACK, 0, "nothing beside it");

    wgf_model_set_tint(plane, wgf_color_make(0, 255, 255, 255));
    wgf_material_set_color(flat, "color", WGF_COLOR_WHITE);
    draw();
    expect_pixel(32, 16, wgf_color_make(0, 255, 255, 255), 1, "its tint, from the placement's record");

    wgf_model_set_tint(plane, WGF_COLOR_WHITE);
    wgf_material_set_texture(flat, "tex", target);
    draw();
    expect_pixel(32, 16, WGF_COLOR_YELLOW, 1, "its texture: a target cleared yellow");

    wgf_material_set_float(flat, "shift", 2.0f);
    draw();
    expect_pixel(32, 16, WGF_COLOR_BLACK, 0, "moved by its vertex hook: gone from the middle");
    expect_pixel(45, 16, WGF_COLOR_YELLOW, 1, "and 2 units right");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

/* What loads from a file is made once core runs. */
static void init(void *user)
{
    wgf_shader_t shader;
    (void)user;
    target = wgf_texture_create_target(4, 4);
    wgf_render_set_target_clear_color(target, WGF_COLOR_YELLOW);
    shader = wgf_shader_create("shaders/flat.wgfshader");
    flat = wgf_material_create_custom(shader);
    wgf_resource_release(shader); /* the material holds it */
    wgf_material_set_vec4(flat, "color", 1.0f, 0.0f, 0.0f, 1.0f); /* while it loads: kept */
    wgf_model_set_material(plane, 0, flat);
}

int main(void)
{
    wgf_actor_t camera;

    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f); /* 32 pixels tall: about 6.6 a unit at the plane */
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_stage3d_set_camera(stage, camera);

    plane = wgf_model_create(wgf_mesh_create_plane(1.0f, 1.0f, 0));
    wgf_actor_set_rotation(plane, 1.5707963f, 0.0f, 0.0f); /* facing the camera */
    wgf_actor_set_parent(plane, stage);

    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
