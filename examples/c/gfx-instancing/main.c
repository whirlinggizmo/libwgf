#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* Many models that share a mesh and a material. Nothing here asks for instancing: it is
 * what a stage does when models agree on everything but where they stand and their tint.
 * A field of cubes shares one mesh and one material, so it is one draw; six toy cars share
 * one file, so each of its parts is one draw for all six; a few cubes are see-through, and
 * those keep their back-to-front order. SPACE gives every cube its own material instead,
 * the same picture drawn a cube at a time. The sun casts, so the same batching happens
 * again into its shadow map, and every shadow lands under its own model.
 *
 *   SPACE    a material each, or one shared
 *   Escape   quit, where quitting means anything
 *
 * libwgt's gfx-instancing (wgrender's), which differs for the size table: its six walkers
 * are a skinned, animated glTF, and libwgf has no skinning until milestone 3; six of the toy
 * car (tools/gen_model.py) stand in their place, turning. And the readout is fixed labels,
 * no snprintf, so the program links no formatting, as gfx-shadows. */

#define FIELD_SIDE 20
#define FIELD_COUNT (FIELD_SIDE * FIELD_SIDE)
#define CARS 6
#define GLASS 5

static wgf_actor_t stage, camera;
static wgf_material_t shared_material;
static wgf_actor_t cubes[FIELD_COUNT], cars[CARS];
static bool own_materials;

static wgf_color_t field_tint(int i)
{
    const float hue = (float)i / (float)FIELD_COUNT;
    return wgf_color_make((unsigned char)(120 + 135 * sinf(hue * 6.28f)),
                          (unsigned char)(120 + 135 * sinf(hue * 6.28f + 2.1f)),
                          (unsigned char)(120 + 135 * sinf(hue * 6.28f + 4.2f)), 255);
}

/* One material for every cube, or one each: the same picture, batched or not. */
static void set_materials(bool own)
{
    int i;
    for (i = 0; i < FIELD_COUNT; i++) {
        if (own) {
            const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
            wgf_material_set_float(material, "roughness", 0.5f);
            wgf_model_set_material(cubes[i], -1, material);
            wgf_resource_release(material); /* the model keeps its reference */
        } else {
            wgf_model_set_material(cubes[i], -1, shared_material);
        }
    }
    own_materials = own;
}

static void init(void *user)
{
    wgf_actor_t sun, floor;
    wgf_mesh_t floor_mesh, cube, car;
    wgf_material_t floor_material;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(28, 30, 38, 255));
    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);

    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_intensity(sun, 3.0f);
    wgf_light_set_shadow_distance(sun, 60.0f);
    wgf_light_set_shadow_casting(sun, true); /* the depth pass batches the same way */
    wgf_actor_set_parent(sun, stage);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.4f, 0, 1, 0);
    wgf_stage3d_set_ambient(stage, wgf_color_make(160, 180, 220, 255), 0.35f);

    /* something for the field to stand over */
    floor_mesh = wgf_mesh_create_plane(60.0f, 60.0f, 0);
    floor_material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(floor_material, "base_color", 0.45f, 0.47f, 0.5f, 1.0f);
    wgf_material_set_float(floor_material, "roughness", 0.9f);
    floor = wgf_model_create(floor_mesh);
    wgf_model_set_material(floor, -1, floor_material);
    wgf_actor_set_position(floor, 0, -0.6f, 0);
    wgf_actor_set_parent(floor, stage);
    wgf_resource_release(floor_mesh);
    wgf_resource_release(floor_material);

    /* the field: one mesh, one material, a tint each */
    cube = wgf_mesh_create_cube(0.6f, 0.6f, 0.6f);
    shared_material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_float(shared_material, "roughness", 0.5f);
    for (i = 0; i < FIELD_COUNT; i++) {
        cubes[i] = wgf_model_create(cube);
        wgf_model_set_tint(cubes[i], field_tint(i));
        wgf_actor_set_parent(cubes[i], stage);
    }
    set_materials(false);

    /* see-through copies of the same cube: same mesh, same material, alpha in the tint */
    for (i = 0; i < GLASS; i++) {
        const wgf_actor_t glass = wgf_model_create(cube);
        wgf_model_set_material(glass, -1, shared_material);
        wgf_model_set_tint(glass, wgf_color_make(255, 255, 255, 110));
        wgf_actor_set_transform(glass, (float)i * 2.0f - 4.0f, 5.2f, 5.0f, 0, 0, 0, 2, 2, 2);
        wgf_actor_set_parent(glass, stage);
    }
    wgf_resource_release(cube);

    /* six cars from one file: each of its parts one draw for all six */
    car = wgf_mesh_create("models/toy_car.glb");
    for (i = 0; i < CARS; i++) {
        cars[i] = wgf_model_create(car);
        wgf_actor_set_transform(cars[i], (float)i * 2.4f - 6.0f, -0.6f, -2.0f, 0, 3.14159f, 0, 1, 1, 1);
        wgf_actor_set_parent(cars[i], stage);
    }
    wgf_resource_release(car);
}

static void frame(void *user)
{
    const float t = (float)wgf_time_get_seconds();
    int i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) set_materials(!own_materials);

    wgf_actor_set_position(camera, sinf(t * 0.15f) * 22.0f, 12.0f, cosf(t * 0.15f) * 22.0f);
    wgf_actor_look_at(camera, 0, 1, 0, 0, 1, 0);
    for (i = 0; i < FIELD_COUNT; i++) {
        const float x = ((float)(i % FIELD_SIDE) - FIELD_SIDE * 0.5f + 0.5f) * 1.5f;
        const float z = ((float)(i / FIELD_SIDE) - FIELD_SIDE * 0.5f + 0.5f) * 1.5f;
        const float wave = sinf(t * 1.5f + x * 0.6f + z * 0.4f);
        wgf_actor_set_transform(cubes[i], x, 2.4f + wave * 0.5f, z, 0, t * 0.3f + (float)i, 0, 1, 1, 1);
    }
    for (i = 0; i < CARS; i++) {
        /* one file, each its own turn */
        wgf_actor_set_rotation(cars[i], 0, 3.14159f + sinf(t + (float)i * 0.35f) * 0.6f, 0);
    }
    wgf_stage3d_draw(stage);

    wgf_draw_text(0, "libwgf instancing: models that share a mesh and a material go up as one draw", 12, 36, 20,
                  WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, own_materials ? "400 cubes, a material each (a draw each)   SPACE toggles"
                                   : "400 cubes, one material (one draw)   SPACE toggles",
                  12, 64, 16, WGF_COLOR_LIGHTGRAY);
}

static void on_shutdown(void *user)
{
    (void)user;
    wgf_resource_release(shared_material);
}

int main(void)
{
    wgf_window_set_title("libwgf instancing");
    wgf_window_set_size(1024, 720);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's wgt_loop_draw_fps */
    wgf_app_run(init, NULL, frame, on_shutdown, NULL);
    return 0;
}
