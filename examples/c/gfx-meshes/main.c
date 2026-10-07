#include <math.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_actor.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Generated meshes: the seven shapes (plane, cube, sphere, cylinder, cone, capsule,
 * torus) in a row on a generated floor, each with its own color and the same
 * normal-mapped tile material, so their texture coordinates and tangents show: the tiles
 * should sit flat on every surface and catch the light the same way. The camera turns
 * around them; O stops it, and Escape quits where quitting means anything.
 *
 * wgrender-c's meshes example done 1:1 for the size table (libwgt has none). The
 * differences:
 *   - the stage is an actor: the models are its children, and the sun is a light actor
 *     aimed with wgf_actor_look_at, where wgrender's is given a direction;
 *   - the camera is placed and aimed each frame with the actor calls, where wgrender's
 *     camera3d_set_view takes the eye, target, and up at once;
 *   - the readout is libwgf's overlay (wgf_debug_show_fps), as wgrender's is its
 *     wgr_debug_enable_fps, at the same place. */

#define NORMAL_MAP_PATH "textures/tiles_normal.png"

enum { SHAPE_COUNT = 7 };

static struct {
    wgf_actor_t stage, camera;
    bool orbit;
    float angle;
} g = {.orbit = true};

static void init(void *user)
{
    /* each shape, how high its center sits (so it rests on the floor), and its color */
    const struct {
        wgf_mesh_t mesh;
        float y;
        float r, gr, b;
    } shapes[SHAPE_COUNT] = {
        {wgf_mesh_create_plane(1.0f, 1.0f, 4), 0.01f, 0.85f, 0.85f, 0.85f},
        {wgf_mesh_create_cube(0.9f, 0.9f, 0.9f), 0.45f, 0.9f, 0.3f, 0.2f},
        {wgf_mesh_create_sphere(0.5f, 24, 48), 0.5f, 0.2f, 0.55f, 0.9f},
        {wgf_mesh_create_cylinder(0.45f, 1.0f, 40), 0.5f, 0.3f, 0.8f, 0.35f},
        {wgf_mesh_create_cone(0.5f, 1.1f, 40), 0.55f, 0.95f, 0.75f, 0.2f},
        {wgf_mesh_create_capsule(0.35f, 1.2f, 16, 40), 0.6f, 0.7f, 0.35f, 0.85f},
        {wgf_mesh_create_torus(0.4f, 0.15f, 48, 24), 0.15f, 0.9f, 0.5f, 0.6f},
    };
    wgf_mesh_t plane;
    wgf_actor_t sun, floor;
    wgf_material_t ground;
    wgf_texture_t normal_map;
    int i;
    (void)user;
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));

    g.camera = wgf_camera3d_create();
    g.stage = wgf_stage3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, WGF_COLOR_WHITE, 0.25f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.4f, 0, 1, 0); /* from the origin, shining that way */
    wgf_light_set_intensity(sun, 3.0f);
    wgf_actor_set_parent(sun, g.stage);

    plane = wgf_mesh_create_plane(12.0f, 12.0f, 0);
    floor = wgf_model_create(plane);
    wgf_resource_release(plane); /* the model holds its own reference */
    ground = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(ground, "base_color", 0.06f, 0.06f, 0.07f, 1.0f);
    wgf_material_set_float(ground, "metallic", 0.0f);
    wgf_material_set_float(ground, "roughness", 0.9f);
    wgf_model_set_material(floor, -1, ground);
    wgf_resource_release(ground);
    wgf_actor_set_parent(floor, g.stage);

    normal_map = wgf_texture_create(NORMAL_MAP_PATH); /* a flat normal until it loads */
    for (i = 0; i < SHAPE_COUNT; i++) {
        const float x = ((float)i - (SHAPE_COUNT - 1) * 0.5f) * 1.4f;
        const wgf_actor_t model = wgf_model_create(shapes[i].mesh);
        const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
        wgf_resource_release(shapes[i].mesh);
        wgf_actor_set_transform(model, x, shapes[i].y, 0, 0, 0, 0, 1, 1, 1);
        wgf_material_set_vec4(material, "base_color", shapes[i].r, shapes[i].gr, shapes[i].b, 1.0f);
        wgf_material_set_float(material, "metallic", 0.0f);
        wgf_material_set_float(material, "roughness", 0.45f);
        wgf_material_set_vec2(material, "normal_texture_scale", 2.0f, 2.0f); /* tiles repeat */
        wgf_material_set_texture(material, "normal_texture", normal_map);
        wgf_model_set_material(model, 0, material);
        wgf_resource_release(material); /* the model keeps it alive */
        wgf_actor_set_parent(model, g.stage);
    }
    wgf_resource_release(normal_map); /* the materials hold their own references */
}

static void frame(void *user)
{
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (g.orbit) g.angle += wgf_loop_get_frame_delta() * 0.2f;
    wgf_actor_set_position(g.camera, 9.0f * sinf(g.angle), 3.5f, 9.0f * cosf(g.angle));
    wgf_actor_look_at(g.camera, 0, 0.4f, 0, 0, 1, 0);

    wgf_stage3d_draw(g.stage);
    wgf_draw_text(0, "libwgf generated meshes: plane, cube, sphere, cylinder, cone, capsule, torus", 12, 36, 20,
                  WGF_COLOR_RAYWHITE);
    if (wgf_app_can_quit()) { /* the quit key's hint, where there is one */
        wgf_draw_text(0, g.orbit ? "O: stop the camera   ESC: quit" : "O: turn the camera   ESC: quit", 12, 64, 16,
                      WGF_COLOR_LIGHTGRAY);
    } else {
        wgf_draw_text(0, g.orbit ? "O: stop the camera" : "O: turn the camera", 12, 64, 16, WGF_COLOR_LIGHTGRAY);
    }
}

int main(void)
{
    wgf_window_set_title("libwgf meshes");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* wgrender's enable_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
