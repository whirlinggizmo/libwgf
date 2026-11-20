#include <math.h>
#include <stdbool.h>
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
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Skinned, animated models: Quaternius's robot (raylib's example model), its glTF file's
 * skin posed on the GPU by its animations. Five robots of the one file, each a model of
 * its mesh, play its clips at their own times: the one in front steps through every clip,
 * crossfading into each over 0.3 s (N now, or every 3 seconds), and the four behind dance,
 * walk, run, and wave, each started at another point of its clip. The sun casts, so their
 * shadows are posed too. Clips are found by name once the file has loaded.
 *
 *   N      the front robot's next clip, crossfaded
 *   SPACE  pause or resume them all
 *   O      stop or start the camera's circling
 *   ESC    quit, where quitting means anything */

#define MODEL_PATH "models/robot.glb"
#define ROBOTS 5
#define FADE 0.3f
#define STEP 3.0f

static const char *const clips[ROBOTS] = {"Robot_Idle", "Robot_Dance", "Robot_Walking", "Robot_Running", "Robot_Wave"};

static struct {
    wgf_actor_t stage, camera, robots[ROBOTS];
    bool started, orbit, paused;
    int clip; /* the front robot's */
    float angle, since;
} g = {.orbit = true};

static void init(void *user)
{
    wgf_mesh_t mesh;
    wgf_actor_t sun, floor;
    wgf_material_t ground;
    int i;
    (void)user;
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(24, 26, 34, 255));

    g.camera = wgf_camera3d_create();
    g.stage = wgf_stage3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, WGF_COLOR_WHITE, 0.35f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.4f, -1.0f, -0.6f, 0, 1, 0);
    wgf_light_set_intensity(sun, 2.5f);
    wgf_light_set_shadow_casting(sun, true);
    wgf_light_set_shadow_distance(sun, 30.0f);
    wgf_actor_set_parent(sun, g.stage);

    mesh = wgf_mesh_create_plane(40.0f, 40.0f, 0);
    floor = wgf_model_create(mesh);
    wgf_resource_release(mesh);
    ground = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(ground, "base_color", 0.18f, 0.2f, 0.24f, 1.0f);
    wgf_material_set_float(ground, "roughness", 0.9f);
    wgf_model_set_material(floor, -1, ground);
    wgf_resource_release(ground);
    wgf_actor_set_parent(floor, g.stage);

    mesh = wgf_mesh_create(MODEL_PATH); /* loading: the robots draw nothing until it is READY */
    for (i = 0; i < ROBOTS; i++) {
        g.robots[i] = wgf_model_create(mesh); /* the file's root: each its own tree, the one mesh */
        wgf_actor_set_parent(g.robots[i], g.stage);
        if (i == 0) {
            wgf_actor_set_position(g.robots[i], 0.0f, 0.0f, 3.0f);
        } else {
            wgf_actor_set_position(g.robots[i], -7.5f + 5.0f * (float)(i - 1), 0.0f, -3.0f);
        }
    }
    wgf_resource_release(mesh); /* each robot holds its own reference */
}

/* Once the file is READY: each robot's clip found by name and played, the ones behind each
 * from another point of its clip. */
static void start(void)
{
    int i;
    for (i = 0; i < ROBOTS; i++) {
        const int clip = wgf_model_find_animation(g.robots[i], clips[i]);
        wgf_model_play_animation(g.robots[i], clip >= 0 ? clip : 0, 0.0f);
        wgf_model_set_animation_time(g.robots[i], 0.37f * (float)i);
    }
    g.clip = wgf_model_get_animation(g.robots[0]);
    g.started = true;
}

static void next_clip(void)
{
    const int count = wgf_model_get_animation_count(g.robots[0]);
    if (count == 0) return;
    g.clip = (g.clip + 1) % count;
    wgf_model_play_animation(g.robots[0], g.clip, FADE);
    g.since = 0.0f;
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    char line[160];
    int i;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (!g.started && wgf_model_get_animation_count(g.robots[0]) > 0) start();
    if (g.started) {
        if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) {
            g.paused = !g.paused;
            for (i = 0; i < ROBOTS; i++) {
                if (g.paused) wgf_model_pause_animation(g.robots[i]);
                else wgf_model_resume_animation(g.robots[i]);
            }
        }
        if (!g.paused) g.since += dt;
        if (wgf_keyboard_is_pressed(WGF_KEY_N) || g.since >= STEP) next_clip();
    }
    if (g.orbit) g.angle += dt * 0.1f;
    wgf_actor_set_position(g.camera, 19.0f * sinf(g.angle), 7.0f, 19.0f * cosf(g.angle));
    wgf_actor_look_at(g.camera, 0, 1.5f, 0, 0, 1, 0);

    wgf_stage3d_draw(g.stage);
    if (g.started) {
        snprintf(line, sizeof(line), "libwgf skinning: %s, %.2f s%s", wgf_model_get_animation_name(g.robots[0], g.clip),
                 (double)wgf_model_get_animation_time(g.robots[0]),
                 wgf_model_get_animation_weight(g.robots[0]) < 1.0f ? " (crossfading)" : "");
    } else {
        snprintf(line, sizeof(line), "libwgf skinning: loading %s", MODEL_PATH);
    }
    wgf_draw_text(0, line, 12, 36, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_app_can_quit() ? "N: next clip   SPACE: pause   O: the camera   ESC: quit"
                                        : "N: next clip   SPACE: pause   O: the camera",
                  12, 64, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf skinning");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
