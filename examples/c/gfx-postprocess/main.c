#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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
#include "wgf_shader.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Screen effects over the finished frame (wgf_render_add_effect).
 *
 * The frame is an ordinary lit stage: the toy car on a floor, generated shapes, a circling
 * point light. The effects are custom materials whose shaders are screen effects
 * (examples/shaders/vignette.glsl and scanlines.glsl, packed by tools/pack_shader.py;
 * tools/gen_shaders.py):
 *
 *   - the vignette darkens the corners and warms the middle
 *   - the scanlines darken alternating rows, shift red and blue apart, and flicker
 *
 * Both apply in the order they were added, so turning one off and on rebuilds the chain
 * (wgf_render_clear_effects, then wgf_render_add_effect again). Their parameters are
 * ordinary material parameters, so they can change any frame: here the vignette breathes
 * in and out, and the arrow keys change how dark it gets.
 *
 *   1 / 2        the vignette / the scanlines on and off
 *   UP / DOWN    the vignette's strength
 *   Space        stop the breathing
 *   O            stop the camera
 *   Esc          quit, where quitting means anything
 *
 * libwgt's postprocess example (wgrender's), but for the walker: libwgt's is a skinned
 * character, playing its animation, and skinning is milestone 3's, so the toy car stands
 * in its place. */

#define CAR_PATH "models/toy_car.glb"

enum { SHAPE_COUNT = 3 };

static struct {
    wgf_actor_t stage, camera, car, lamp, lamp_marker;
    wgf_actor_t shapes[SHAPE_COUNT];
    wgf_material_t vignette, scanlines; /* the effects' materials, their shaders loading */
    bool vignette_on, scanlines_on, breathing, orbit;
    float strength; /* the vignette's, before breathing */
    float angle, time;
} g = {.vignette_on = true, .scanlines_on = false, .breathing = true, .orbit = true, .strength = 0.85f};

/* The chain, in order: the vignette darkens the corners, then the scanlines go over
 * everything. Rebuilt whenever one is switched on or off. */
static void rebuild_effects(void)
{
    wgf_render_clear_effects();
    if (g.vignette_on) wgf_render_add_effect(g.vignette);
    if (g.scanlines_on) wgf_render_add_effect(g.scanlines);
}

/* A material drawn by the shader in examples/assets/shaders/<name>.wgfshader. Its shader
 * loads on create: the values set on it are kept until it has, and an effect joins the
 * chain from the frame its shader is READY. */
static wgf_material_t custom(const char *path)
{
    const wgf_shader_t shader = wgf_shader_create(path);
    const wgf_material_t material = wgf_material_create_custom(shader);
    wgf_resource_release(shader); /* the material holds its own reference */
    return material;
}

static void init(void *user)
{
    /* the shapes beside the car, and their colors */
    const struct {
        wgf_mesh_t mesh;
        float x, y;
        float r, gr, b;
    } shapes[SHAPE_COUNT] = {
        {wgf_mesh_create_sphere(0.5f, 24, 48), -2.2f, 0.5f, 0.2f, 0.55f, 0.9f},
        {wgf_mesh_create_torus(0.45f, 0.16f, 48, 24), 2.2f, 0.7f, 0.95f, 0.6f, 0.25f},
        {wgf_mesh_create_cube(0.8f, 0.8f, 0.8f), 3.6f, 0.4f, 0.35f, 0.85f, 0.5f},
    };
    wgf_actor_t sun, floor;
    wgf_material_t material;
    wgf_mesh_t mesh;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(16, 18, 24, 255));

    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(90, 110, 160, 255), 0.12f);

    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_color(sun, wgf_color_make(255, 215, 170, 255));
    wgf_light_set_intensity(sun, 2.2f);
    wgf_actor_look_at(sun, -0.4f, -1.0f, -0.5f, 0, 1, 0); /* from the origin, shining that way */
    wgf_actor_set_parent(sun, g.stage);

    g.lamp = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_color(g.lamp, wgf_color_make(80, 220, 255, 255));
    wgf_light_set_intensity(g.lamp, 18.0f);
    wgf_light_set_range(g.lamp, 6.0f);
    wgf_actor_set_parent(g.lamp, g.stage);
    g.lamp_marker = wgf_shape3d_create();
    wgf_shape3d_set_sphere(g.lamp_marker, 0.1f);
    wgf_shape3d_set_color(g.lamp_marker, wgf_color_make(80, 220, 255, 255));
    wgf_actor_set_parent(g.lamp_marker, g.lamp);

    mesh = wgf_mesh_create_plane(16.0f, 16.0f, 0);
    floor = wgf_model_create(mesh);
    wgf_resource_release(mesh);
    material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", 0.07f, 0.07f, 0.08f, 1.0f);
    wgf_material_set_float(material, "roughness", 0.85f);
    wgf_model_set_material(floor, -1, material);
    wgf_resource_release(material);
    wgf_actor_set_parent(floor, g.stage);

    for (i = 0; i < SHAPE_COUNT; i++) {
        material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
        g.shapes[i] = wgf_model_create(shapes[i].mesh);
        wgf_resource_release(shapes[i].mesh);
        wgf_actor_set_position(g.shapes[i], shapes[i].x, shapes[i].y, -0.6f);
        wgf_material_set_vec4(material, "base_color", shapes[i].r, shapes[i].gr, shapes[i].b, 1.0f);
        wgf_material_set_float(material, "roughness", 0.4f);
        wgf_model_set_material(g.shapes[i], 0, material);
        wgf_resource_release(material);
        wgf_actor_set_parent(g.shapes[i], g.stage);
    }

    mesh = wgf_mesh_create(CAR_PATH);
    g.car = wgf_model_create(mesh);
    wgf_resource_release(mesh);
    wgf_actor_set_rotation(g.car, 0, 0.5f, 0);
    wgf_actor_set_parent(g.car, g.stage);

    g.vignette = custom("shaders/vignette.wgfshader");
    wgf_material_set_float(g.vignette, "strength", g.strength);
    wgf_material_set_float(g.vignette, "radius", 0.25f);
    wgf_material_set_vec4(g.vignette, "tint", 1.04f, 1.0f, 0.94f, 1.0f);
    g.scanlines = custom("shaders/scanlines.wgfshader");
    wgf_material_set_float(g.scanlines, "lines", 220.0f);
    wgf_material_set_float(g.scanlines, "darkness", 0.35f);
    wgf_material_set_float(g.scanlines, "offset", 1.5f);
    wgf_material_set_float(g.scanlines, "flicker", 1.0f);
    rebuild_effects();
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    float lamp_x, lamp_z, strength;
    char line[96];
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_1)) {
        g.vignette_on = !g.vignette_on;
        rebuild_effects();
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_2)) {
        g.scanlines_on = !g.scanlines_on;
        rebuild_effects();
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) g.breathing = !g.breathing;
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (wgf_keyboard_is_down(WGF_KEY_UP)) g.strength = fminf(g.strength + dt, 1.0f);
    if (wgf_keyboard_is_down(WGF_KEY_DOWN)) g.strength = fmaxf(g.strength - dt, 0.0f);

    g.time += dt;
    if (g.orbit) g.angle += dt * 0.25f;
    wgf_actor_set_position(g.camera, 9.0f * sinf(g.angle), 3.2f, 9.0f * cosf(g.angle));
    wgf_actor_look_at(g.camera, 0, 1.0f, 0, 0, 1, 0);
    lamp_x = 3.0f * sinf(g.time * 0.9f);
    lamp_z = 2.2f + 1.2f * cosf(g.time * 0.9f);
    wgf_actor_set_position(g.lamp, lamp_x, 1.4f, lamp_z);
    wgf_actor_set_rotation(g.shapes[1], 0, g.time * 40.0f * 3.14159265f / 180.0f, g.time * 25.0f * 3.14159265f / 180.0f);

    /* the effect's parameters are the material's: change them any frame */
    strength = g.breathing ? g.strength * (0.55f + 0.45f * sinf(g.time * 0.8f)) : g.strength;
    wgf_material_set_float(g.vignette, "strength", strength);

    wgf_stage3d_draw(g.stage);
    wgf_draw_text(0, "libwgf post-processing: screen effects over the finished frame", 12, 36, 20, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] vignette %s   [2] scanlines %s   effects: %d", g.vignette_on ? "on" : "off",
             g.scanlines_on ? "on" : "off", wgf_render_get_effect_count());
    wgf_draw_text(0, line, 12, 64, 16, WGF_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "UP/DOWN strength %.2f   SPACE %s   O camera%s", (double)g.strength,
             g.breathing ? "stop breathing" : "breathe", wgf_app_can_quit() ? "   Esc quit" : "");
    wgf_draw_text(0, line, 12, 86, 16, WGF_COLOR_GRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf postprocess");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
