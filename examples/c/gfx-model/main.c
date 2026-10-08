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
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* A glTF file on a lit stage, under a circling camera: a model of it is made at once, and
 * the file's nodes appear under it once it has loaded (tools/gen_model.py's toy car: a body,
 * a see-through cabin, four wheels, emissive headlights, and a spot light at each). Its parts
 * are found by name, as any actor is: the wheels turn, and H switches the headlights' lights.
 * O stops the camera, and Escape quits where quitting means anything.
 *
 * libwgt's model example, on a model of libwgf's own: libwgt's is a skinned character,
 * playing its animation, and skinning is milestone 3's. */

#define MODEL_PATH "models/toy_car.glb"

static const char *const wheel_names[4] = {"toy_car/body/wheel_fl", "toy_car/body/wheel_fr", "toy_car/body/wheel_rl",
                                            "toy_car/body/wheel_rr"};

static struct {
    wgf_actor_t stage, camera, car;
    bool orbit, lights;
    float angle, roll;
} g = {.orbit = true, .lights = true};

static void init(void *user)
{
    wgf_mesh_t mesh;
    wgf_actor_t sun, floor;
    wgf_material_t ground;
    (void)user;
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));

    g.camera = wgf_camera3d_create();
    g.stage = wgf_stage3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, WGF_COLOR_WHITE, 0.2f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.4f, 0, 1, 0); /* from the origin, shining that way */
    wgf_light_set_intensity(sun, 2.0f);
    wgf_actor_set_parent(sun, g.stage);

    mesh = wgf_mesh_create_plane(14.0f, 14.0f, 0);
    floor = wgf_model_create(mesh);
    wgf_resource_release(mesh); /* the model holds its own reference */
    ground = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(ground, "base_color", 0.12f, 0.12f, 0.13f, 1.0f);
    wgf_material_set_float(ground, "roughness", 0.9f);
    wgf_model_set_material(floor, -1, ground);
    wgf_resource_release(ground);
    wgf_actor_set_parent(floor, g.stage);

    mesh = wgf_mesh_create(MODEL_PATH); /* loading: its model draws nothing until it is READY */
    g.car = wgf_model_create(mesh);
    wgf_resource_release(mesh);
    wgf_actor_set_parent(g.car, g.stage);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    const bool loaded = wgf_actor_get_child_count(g.car) > 0; /* the file's nodes, made once it is READY */
    int i;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (loaded && wgf_keyboard_is_pressed(WGF_KEY_H)) {
        g.lights = !g.lights;
        wgf_actor_set_enabled(wgf_actor_find(g.car, "toy_car/body/headlight_l/headlight_l_beam"), g.lights);
        wgf_actor_set_enabled(wgf_actor_find(g.car, "toy_car/body/headlight_r/headlight_r_beam"), g.lights);
    }
    if (g.orbit) g.angle += dt * 0.25f;
    wgf_actor_set_position(g.camera, 7.0f * sinf(g.angle), 3.0f, 7.0f * cosf(g.angle));
    wgf_actor_look_at(g.camera, 0, 0.5f, 0, 0, 1, 0);
    g.roll += dt * 3.0f; /* the wheels, found by name */
    for (i = 0; loaded && i < 4; i++) wgf_actor_set_rotation(wgf_actor_find(g.car, wheel_names[i]), g.roll, 0, 0);

    wgf_stage3d_draw(g.stage);
    wgf_draw_text(0, loaded ? "libwgf glTF model: " MODEL_PATH : "libwgf glTF model: loading " MODEL_PATH,
                  12, 36, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_app_can_quit() ? "O: the camera   H: the headlights   ESC: quit" : "O: the camera   H: the headlights",
                  12, 64, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf model");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
