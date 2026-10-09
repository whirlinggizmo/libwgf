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
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* A casting light and what it does to a stage. The sun casts (wgf_light_set_shadow_casting):
 * once a frame it draws everything that casts into a depth map, and the lit shading darkens
 * what is behind something. The stage is a floor, a wall, some generated shapes, and the toy
 * car, so the shadows fall across each other and across themselves. A spot light circling
 * overhead casts through its own cone: two lights casting at once, a layer of the map each.
 * The ball on the left casts nothing (wgf_model_set_shadow_casting), so it floats as
 * everything did before shadows; the one on the right receives nothing
 * (wgf_model_set_shadow_receiving), so the wall's shadow passes over it; a small sphere, a
 * 3D shape, marks the spot, casting nothing, as shapes don't.
 *
 *   1 / 2        the sun's / the spot's shadows on and off
 *   UP / DOWN    shadow distance: less covers less of the stage with the same map, so the
 *                shadows sharpen
 *   [ / ]        depth bias: too little stripes the lit surfaces ("acne"), too much lifts a
 *                shadow away from what casts it
 *   M            map size (512, 1024, 2048, 4096)
 *   S / T        how much light a shadow blocks / what it tints what is left
 *   O            orbit the camera, or hold it
 *   Escape       quit, where quitting means anything
 *
 * libwgt's gfx-shadows (wgrender's shadows), which differs for the size table: its walking
 * character is a skinned, animated glTF, and libwgf has no skinning until milestone 3; the
 * toy car (tools/gen_model.py) stands in its place. And the readout is libwgf's overlay and
 * fixed labels, no snprintf, so the program links no formatting, as gfx-lights. */

enum { SIZE_COUNT = 4, TINT_COUNT = 3 };
static const int MAP_SIZES[SIZE_COUNT] = {512, 1024, 2048, 4096};
/* what a shadow keeps of the light: nothing (physical), then two stylized tints */
static const wgf_color_t TINTS[TINT_COUNT] = {0x000000FFu, 0x1E3C64FFu, 0x64321EFFu};

static struct {
    wgf_actor_t stage, camera, sun, spot, spot_marker;
    bool shadows, spot_shadows, orbit;
    float distance, bias, strength;
    int size_index, tint_index;
    float angle, time;
} g = {.shadows = true, .spot_shadows = true, .orbit = true, .distance = 30.0f, .bias = 1.0f, .strength = 1.0f,
       .size_index = 1};

/* A model of `mesh` at (x, y, z) in one color, on the stage. */
static wgf_actor_t place(wgf_mesh_t mesh, float x, float y, float z, float r, float gr, float b, float roughness)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_resource_release(mesh); /* the model holds it */
    wgf_actor_set_position(model, x, y, z);
    wgf_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgf_material_set_float(material, "metallic", 0.0f);
    wgf_material_set_float(material, "roughness", roughness);
    wgf_model_set_material(model, -1, material);
    wgf_resource_release(material);
    wgf_actor_set_parent(model, g.stage);
    return model;
}

static void init(void *user)
{
    wgf_mesh_t car;
    wgf_actor_t car_model;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(120, 150, 200, 255));
    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_actor_set_parent(g.camera, g.stage);
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(140, 170, 225, 255), 0.25f);

    g.sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_color(g.sun, wgf_color_make(255, 244, 224, 255));
    wgf_light_set_intensity(g.sun, 3.2f);
    wgf_light_set_shadow_casting(g.sun, true);
    wgf_light_set_shadow_distance(g.sun, g.distance);
    wgf_light_set_shadow_map_size(g.sun, MAP_SIZES[g.size_index]);
    wgf_light_set_shadow_strength(g.sun, g.strength);
    wgf_light_set_shadow_color(g.sun, TINTS[g.tint_index]);
    wgf_actor_set_parent(g.sun, g.stage);
    wgf_actor_look_at(g.sun, -0.75f, -0.85f, -0.35f, 0, 1, 0);

    /* a second caster: a spot light circling the stage, shadowing through its own cone.
       Both share the map, so both use the same size */
    g.spot = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
    wgf_light_set_color(g.spot, wgf_color_make(150, 210, 255, 255));
    wgf_light_set_intensity(g.spot, 260.0f);
    wgf_light_set_range(g.spot, 24.0f);
    wgf_light_set_spot_cone(g.spot, 0.30f, 0.44f);
    wgf_light_set_shadow_casting(g.spot, true);
    wgf_light_set_shadow_map_size(g.spot, MAP_SIZES[g.size_index]);
    wgf_light_set_shadow_distance(g.spot, 24.0f);
    wgf_actor_set_parent(g.spot, g.stage);
    g.spot_marker = wgf_shape3d_create();
    wgf_shape3d_set_sphere(g.spot_marker, 0.16f);
    wgf_shape3d_set_color(g.spot_marker, wgf_color_make(150, 210, 255, 255));
    wgf_actor_set_parent(g.spot_marker, g.stage);

    place(wgf_mesh_create_plane(40.0f, 40.0f, 0), 0, 0, 0, 0.42f, 0.44f, 0.46f, 0.9f);
    /* a wall to throw a long shadow across the floor */
    place(wgf_mesh_create_cube(0.5f, 3.0f, 7.0f), -4.5f, 1.5f, 0, 0.55f, 0.5f, 0.45f, 0.85f);
    place(wgf_mesh_create_torus(0.7f, 0.22f, 48, 24), 2.6f, 1.1f, -1.6f, 0.9f, 0.55f, 0.2f, 0.4f);
    place(wgf_mesh_create_capsule(0.4f, 1.6f, 16, 32), 1.2f, 0.8f, 1.8f, 0.35f, 0.75f, 0.45f, 0.5f);
    place(wgf_mesh_create_cube(1.0f, 1.0f, 1.0f), 3.8f, 0.5f, 1.4f, 0.3f, 0.5f, 0.85f, 0.6f);

    /* one that casts nothing, and one that nothing shadows */
    wgf_model_set_shadow_casting(place(wgf_mesh_create_sphere(0.6f, 24, 48), -2.0f, 0.6f, 2.4f, 0.95f, 0.85f, 0.3f, 0.35f),
                                 false);
    wgf_model_set_shadow_receiving(
        place(wgf_mesh_create_sphere(0.6f, 24, 48), -2.6f, 0.6f, -1.2f, 0.9f, 0.3f, 0.5f, 0.35f), false);

    /* in the walking character's place */
    car = wgf_mesh_create("models/toy_car.glb");
    car_model = wgf_model_create(car);
    wgf_resource_release(car);
    wgf_actor_set_position(car_model, 0, 0, 0);
    wgf_actor_set_parent(car_model, g.stage);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    float sx, sz;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_1)) wgf_light_set_shadow_casting(g.sun, g.shadows = !g.shadows);
    if (wgf_keyboard_is_pressed(WGF_KEY_2)) wgf_light_set_shadow_casting(g.spot, g.spot_shadows = !g.spot_shadows);
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (wgf_keyboard_is_pressed(WGF_KEY_S)) {
        g.strength = g.strength > 0.9f ? 0.65f : g.strength > 0.5f ? 0.35f : 1.0f;
        wgf_light_set_shadow_strength(g.sun, g.strength);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_T)) {
        g.tint_index = (g.tint_index + 1) % TINT_COUNT;
        wgf_light_set_shadow_color(g.sun, TINTS[g.tint_index]);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_M)) {
        g.size_index = (g.size_index + 1) % SIZE_COUNT;
        wgf_light_set_shadow_map_size(g.sun, MAP_SIZES[g.size_index]);
        wgf_light_set_shadow_map_size(g.spot, MAP_SIZES[g.size_index]);
    }
    if (wgf_keyboard_is_down(WGF_KEY_UP) || wgf_keyboard_is_down(WGF_KEY_DOWN)) {
        const float step = wgf_keyboard_is_down(WGF_KEY_UP) ? dt * 20.0f : -dt * 20.0f;
        g.distance = fmaxf(2.0f, fminf(g.distance + step, 200.0f));
        wgf_light_set_shadow_distance(g.sun, g.distance);
    }
    if (wgf_keyboard_is_down(WGF_KEY_LEFT_BRACKET) || wgf_keyboard_is_down(WGF_KEY_RIGHT_BRACKET)) {
        const float step = wgf_keyboard_is_down(WGF_KEY_RIGHT_BRACKET) ? dt * 4.0f : -dt * 4.0f;
        g.bias = fmaxf(0.0f, fminf(g.bias + step, 16.0f));
        wgf_light_set_shadow_bias(g.sun, g.bias, g.bias * 4.0f);
    }

    g.time += dt;
    /* the spot circles overhead, always aimed at the middle of the stage */
    sx = 7.0f * sinf(g.time * 0.35f);
    sz = 7.0f * cosf(g.time * 0.35f);
    wgf_actor_set_position(g.spot, sx, 6.5f, sz);
    wgf_actor_look_at(g.spot, 0, 0, 0, 0, 1, 0);
    wgf_actor_set_position(g.spot_marker, sx, 6.5f, sz);
    if (g.orbit) g.angle += dt * 0.18f;
    wgf_actor_set_position(g.camera, 11.0f * sinf(g.angle), 5.0f, 11.0f * cosf(g.angle));
    wgf_actor_look_at(g.camera, 0, 1.2f, 0, 0, 1, 0);
    wgf_stage3d_draw(g.stage);

    wgf_draw_text(0, "libwgf shadows: lights casting into a depth map", 12, 36, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, g.shadows ? "[1] sun on" : "[1] sun off", 12, 64, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, g.spot_shadows ? "[2] spot on" : "[2] spot off", 132, 64, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, "[S] strength   [T] tint   M map size", 12, 86, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, "UP/DOWN distance   [ ] bias   O camera", 12, 108, 16, WGF_COLOR_GRAY);
    wgf_draw_text(0, "left ball casts nothing; right ball receives nothing", 12, 130, 16, WGF_COLOR_GRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf shadows");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's wgt_loop_draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
