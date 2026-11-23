#include <math.h>
#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_cubemap.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* A cubemap (wgf_cubemap.h) sampled by shaders of your own, raylib's skybox example and
 * more: one image of six faces laid out as a 4 by 3 cross (examples/assets/textures/
 * skybox.png, drawn by tools/gen_skybox.py), its layout found by its shape.
 *
 *   - the sky: a box around the camera, drawn from inside by examples/shaders/skybox.glsl,
 *     the cubemap along the direction from the camera
 *   - in it, a chrome ball, a tinted glass torus turning about it, and a gold cube, each
 *     drawn by examples/shaders/mirror.glsl, the cubemap reflected about its surface
 *
 * The camera circles the middle, looking at it.
 *
 *   Space    stop or start the camera
 *   Esc      quit, where quitting means anything */

#define SKYBOX_PATH "textures/skybox.png"

static struct {
    wgf_actor_t stage, camera, sky, torus, cube;
    float time, angle;
    bool circling;
} g = {.circling = true};

/* A material drawn by the shader at `path`, sampling `cubemap` as its `sky`. */
static wgf_material_t custom(const char *path, wgf_cubemap_t cubemap)
{
    const wgf_shader_t shader = wgf_shader_create(path);
    const wgf_material_t material = wgf_material_create_custom(shader);
    wgf_resource_release(shader); /* the material holds its own reference */
    wgf_material_set_cubemap(material, "sky", cubemap); /* white until it loads */
    return material;
}

/* A mirror of `mesh` at (x, y, z), its reflection seen through `tint`. */
static wgf_actor_t mirror(wgf_mesh_t mesh, wgf_cubemap_t cubemap, float x, float y, float z, wgf_color_t tint,
                          float reflectance)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    const wgf_material_t material = custom("shaders/mirror.wgfshader", cubemap);
    wgf_resource_release(mesh); /* the model holds it */
    wgf_material_set_color(material, "tint", tint);
    wgf_material_set_float(material, "reflectance", reflectance);
    wgf_model_set_material(model, 0, material);
    wgf_resource_release(material);
    wgf_actor_set_position(model, x, y, z);
    wgf_actor_set_parent(model, g.stage);
    return model;
}

static void init(void *user)
{
    wgf_cubemap_t cubemap;
    wgf_material_t material;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));
    g.stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(g.stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f); /* the sky's own colors */
    g.camera = wgf_camera3d_create();
    wgf_camera3d_set_clip(g.camera, 0.1f, 500.0f);
    wgf_stage3d_set_camera(g.stage, g.camera);

    cubemap = wgf_cubemap_create(SKYBOX_PATH, WGF_CUBEMAP_LAYOUT_AUTO);

    /* the sky: a box far around the camera, going where it goes, seen from inside */
    g.sky = wgf_model_create(wgf_mesh_create_cube(200.0f, 200.0f, 200.0f));
    material = custom("shaders/skybox.wgfshader", cubemap);
    wgf_material_set_double_sided(material, true);
    wgf_model_set_material(g.sky, 0, material);
    wgf_resource_release(material);
    wgf_actor_set_parent(g.sky, g.stage);

    mirror(wgf_mesh_create_sphere(1.0f, 48, 96), cubemap, 0, 0, 0, WGF_COLOR_WHITE, 0.6f); /* chrome */
    g.torus = mirror(wgf_mesh_create_torus(2.2f, 0.25f, 32, 96), cubemap, 0, 0, 0, wgf_color_make(120, 200, 255, 255),
                     0.1f); /* blue glass */
    g.cube = mirror(wgf_mesh_create_cube(0.9f, 0.9f, 0.9f), cubemap, 0, 2.2f, 0, wgf_color_make(255, 190, 80, 255),
                    0.3f); /* gold */
    wgf_resource_release(cubemap); /* the materials hold their own references */
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    float x, y, z;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) g.circling = !g.circling;

    g.time += dt;
    if (g.circling) g.angle += dt * 0.25f;
    x = sinf(g.angle) * 7.0f;
    y = 1.5f + sinf(g.angle * 0.7f) * 1.5f;
    z = cosf(g.angle) * 7.0f;
    wgf_actor_set_position(g.camera, x, y, z);
    wgf_actor_look_at(g.camera, 0, 0.5f, 0, 0, 1, 0);
    wgf_actor_set_position(g.sky, x, y, z); /* the sky goes with the camera: it is never nearer */
    wgf_actor_set_rotation(g.torus, 0.6f + sinf(g.time * 0.5f) * 0.3f, g.time * 0.6f, 0.0f);
    wgf_actor_set_rotation(g.cube, g.time * 0.4f, g.time * 0.7f, 0.0f);

    wgf_stage3d_draw(g.stage);
    wgf_draw_text(0, "libwgf cubemaps: a skybox and reflections", 12, 36, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_app_can_quit() ? "Space stop or start the camera, Esc quit" : "Space stop or start the camera",
                  12, 64, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf cubemaps");
    wgf_window_set_size(960, 540);
    wgf_window_set_flags(WGF_WINDOW_FLAG_MSAA);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
