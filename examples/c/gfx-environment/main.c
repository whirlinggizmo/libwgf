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
#include "wgf_environment.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Image-based light, a background, and tone mapping: the material spheres (red plastic
 * above, gold below, roughness 0 to 1 left to right), a normal-mapped sphere, and the toy
 * car, lit by an environment alone -- no lights, no ambient light. Metals mirror the
 * world around them; rougher surfaces blur it. The environment is prepared on a worker
 * as it loads (on the web, a step at a time within the load budget, which this program
 * raises while one is loading, as a loading screen would); until it is READY the stage is
 * lit by nothing.
 *
 *   E            environment: sunset, studio, none
 *   B            background: sharp, soft, blurred, off
 *   T            tone mapping: Neutral, ACES, none
 *   UP / DOWN    exposure, half a stop a press
 *   LEFT / RIGHT turn the environment
 *   Escape       quit, where quitting means anything
 *
 * libwgt's gfx-environment (wgrender's environment), which differs for the size table:
 * its walking character is a skinned, animated glTF, and libwgf has no skinning until
 * milestone 3, so the toy car (tools/gen_model.py) stands in its place. And the readout is
 * fixed labels, no snprintf, as gfx-shadows. */

enum { COLUMNS = 5, ENVIRONMENT_COUNT = 2, BLUR_COUNT = 3 };

static const char *ENVIRONMENT_PATHS[ENVIRONMENT_COUNT] = {"environments/venice_sunset_1k.hdr",
                                                           "environments/studio_small_09_1k.hdr"};
static const char *ENVIRONMENT_LABELS[ENVIRONMENT_COUNT + 1] = {"[E] sunset", "[E] studio", "[E] none"};
static const float BLURS[BLUR_COUNT] = {0.0f, 0.35f, 0.8f};
static const char *BLUR_LABELS[BLUR_COUNT + 1] = {"[B] sharp", "[B] soft", "[B] blurred", "[B] off"};
static const char *TONEMAP_LABELS[] = {"[T] no tone mapping", "[T] Neutral", "[T] ACES"};

static struct {
    wgf_actor_t stage, camera;
    wgf_environment_t environments[ENVIRONMENT_COUNT];
    int environment; /* an index; ENVIRONMENT_COUNT: none */
    int blur;        /* an index into BLURS; BLUR_COUNT: no background */
    wgf_stage3d_tonemap_t tonemap;
    float exposure, rotation, time;
    float load_budget; /* the default, given back once nothing loads */
} g;

static void apply_environment(void)
{
    const wgf_environment_t env = g.environment < ENVIRONMENT_COUNT ? g.environments[g.environment] : 0;
    wgf_stage3d_set_environment(g.stage, env, 1.0f, g.rotation);
    wgf_stage3d_set_background(g.stage, g.blur < BLUR_COUNT ? env : 0, g.blur < BLUR_COUNT ? BLURS[g.blur] : 0.0f);
    wgf_stage3d_set_tonemap(g.stage, g.tonemap, g.exposure);
}

/* A sphere of its own material at (x, y). */
static wgf_actor_t create_sphere(wgf_mesh_t mesh, float x, float y, float r, float gr, float b, float metallic,
                                 float roughness)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgf_material_set_float(material, "metallic", metallic);
    wgf_material_set_float(material, "roughness", roughness);
    wgf_actor_set_position(model, x, y, 0);
    wgf_model_set_material(model, 0, material);
    wgf_resource_release(material); /* the model holds it */
    wgf_actor_set_parent(model, g.stage);
    return model;
}

static void init(void *user)
{
    const float spacing = 1.3f;
    const wgf_mesh_t sphere = wgf_mesh_create_sphere(0.5f, 32, 64);
    wgf_texture_t normal_map;
    wgf_mesh_t car;
    wgf_actor_t tiles, car_model;
    int c, i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));
    g.tonemap = WGF_STAGE3D_TONEMAP_NEUTRAL;
    g.load_budget = wgf_resource_get_load_budget();
    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_actor_set_parent(g.camera, g.stage);
    wgf_stage3d_set_camera(g.stage, g.camera);

    for (c = 0; c < COLUMNS; c++) {
        const float x = ((float)c - (COLUMNS - 1) * 0.5f) * spacing;
        const float roughness = (float)c / (COLUMNS - 1);
        create_sphere(sphere, x, 1.9f, 0.8f, 0.05f, 0.04f, 0.0f, roughness);
        create_sphere(sphere, x, 0.6f, 1.0f, 0.77f, 0.34f, 1.0f, roughness);
    }
    tiles = create_sphere(sphere, -1.3f, -0.7f, 0.9f, 0.9f, 0.9f, 0.0f, 0.3f);
    normal_map = wgf_texture_create("textures/tiles_normal.png");
    wgf_material_set_texture(wgf_model_get_material(tiles, 0), "normal_texture", normal_map);
    wgf_resource_release(normal_map);
    wgf_resource_release(sphere);

    /* in the walking character's place */
    car = wgf_mesh_create("models/toy_car.glb");
    car_model = wgf_model_create(car);
    wgf_resource_release(car);
    wgf_actor_set_position(car_model, 1.3f, -1.2f, 0);
    wgf_actor_set_rotation(car_model, 0, 0.6f, 0);
    wgf_actor_set_parent(car_model, g.stage);

    /* the lighting is prepared on a worker: the stage shows it once it is READY */
    for (i = 0; i < ENVIRONMENT_COUNT; i++) g.environments[i] = wgf_environment_create(ENVIRONMENT_PATHS[i]);
    apply_environment();
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    bool changed = false;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_E)) g.environment = (g.environment + 1) % (ENVIRONMENT_COUNT + 1), changed = true;
    if (wgf_keyboard_is_pressed(WGF_KEY_B)) g.blur = (g.blur + 1) % (BLUR_COUNT + 1), changed = true;
    if (wgf_keyboard_is_pressed(WGF_KEY_T)) {
        g.tonemap = g.tonemap == WGF_STAGE3D_TONEMAP_NEUTRAL ? WGF_STAGE3D_TONEMAP_ACES
                    : g.tonemap == WGF_STAGE3D_TONEMAP_ACES  ? WGF_STAGE3D_TONEMAP_NONE
                                                             : WGF_STAGE3D_TONEMAP_NEUTRAL;
        changed = true;
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_UP)) g.exposure += 0.5f, changed = true;
    if (wgf_keyboard_is_pressed(WGF_KEY_DOWN)) g.exposure -= 0.5f, changed = true;
    if (wgf_keyboard_is_down(WGF_KEY_LEFT)) g.rotation -= dt, changed = true;
    if (wgf_keyboard_is_down(WGF_KEY_RIGHT)) g.rotation += dt, changed = true;
    if (changed) apply_environment();

    { /* while an environment loads, more of each frame for it */
        bool loading = false;
        int i;
        for (i = 0; i < ENVIRONMENT_COUNT; i++) {
            loading = loading || wgf_resource_get_status(g.environments[i]) == WGF_RESOURCE_STATUS_PENDING;
        }
        wgf_resource_set_load_budget(loading ? 16.0f : g.load_budget);
    }

    g.time += dt;
    wgf_actor_set_position(g.camera, sinf(g.time * 0.15f) * 7.5f, 1.2f, cosf(g.time * 0.15f) * 7.5f);
    wgf_actor_look_at(g.camera, 0, 0.3f, 0, 0, 1, 0);
    wgf_stage3d_draw(g.stage);

    wgf_draw_rectangle(0, 0, 4000, 112, wgf_color_make(0, 0, 0, 150));
    wgf_draw_text(0, "libwgf environment light: reflections, background and tone mapping", 12, 36, 20,
                  WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, ENVIRONMENT_LABELS[g.environment], 12, 64, 16, WGF_COLOR_LIGHTGRAY);
    if (g.environment < ENVIRONMENT_COUNT &&
        wgf_resource_get_status(g.environments[g.environment]) == WGF_RESOURCE_STATUS_PENDING) {
        wgf_draw_text(0, "(loading)", 140, 64, 16, WGF_COLOR_GRAY);
    }
    wgf_draw_text(0, BLUR_LABELS[g.blur], 240, 64, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, TONEMAP_LABELS[g.tonemap], 380, 64, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, "UP/DOWN exposure   LEFT/RIGHT turn it", 12, 86, 16, WGF_COLOR_GRAY);
}

static void on_shutdown(void *user)
{
    int i;
    (void)user;
    for (i = 0; i < ENVIRONMENT_COUNT; i++) wgf_resource_release(g.environments[i]);
}

int main(void)
{
    wgf_window_set_title("libwgf environment");
    wgf_window_set_size(1100, 720);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's wgt_loop_draw_fps */
    wgf_app_run(init, NULL, frame, on_shutdown, NULL);
    return 0;
}
