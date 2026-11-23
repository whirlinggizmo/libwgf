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
#include "wgf_shader.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Skinned, animated models: Quaternius's robot (raylib's example model), its glTF file's
 * skin posed on the GPU by its animations. Five robots of the one file, each a model of
 * its mesh, play its clips at their own times: the one in front steps through every clip,
 * crossfading into each over 0.3 s (N now, or every 3 seconds), and the four behind dance,
 * walk, run (waving from the torso up as it runs: a layer over its clip, masked to the torso
 * and what is under it), and wave (drawn by the toon shader, a custom material on skinned
 * parts), each started at another point of its clip. The sun casts, so their
 * shadows are posed too. Clips are found by name once the file has loaded. Beside the front
 * robot, the Adventurer twice: on the right its four part models, each posed, and on the left
 * the four merged into one mesh on the body's skeleton (wgf_mesh_create_merged), posed once and
 * drawn as one primitive, its colors in its vertices; the two run alike. D draws the
 * Adventurer over itself for debugging, as it is posed this frame: its triangles' wires,
 * its vertices as points, or its skeleton (immediate mode's model draws), the skeleton
 * alone, the Adventurer hidden, since its lines are inside it.
 *
 *   N      the front robot's next clip, crossfaded
 *   D      the Adventurer's debug view: wires, points, skeleton, none
 *   SPACE  pause or resume them all
 *   O      stop or start the camera's circling
 *   ESC    quit, where quitting means anything */

#define MODEL_PATH "models/robot.glb"
#define ROBOTS 5
#define FADE 0.3f
#define STEP 3.0f

static const char *const clips[ROBOTS] = {"Robot_Idle", "Robot_Dance", "Robot_Walking", "Robot_Running", "Robot_Wave"};

/* A modular character: four part models on one skeleton, each given the clips of a file of
 * animations alone and played alike, so they move as one; and the same four merged. */
#define CLIPS_PATH "models/characters/Animations.glb"
#define PARTS 4
static const char *const part_paths[PARTS] = {"models/characters/Adventurer_Body.glb",
                                              "models/characters/Adventurer_Legs.glb",
                                              "models/characters/Adventurer_Head.glb",
                                              "models/characters/Adventurer_Feet.glb"};

static struct {
    wgf_actor_t stage, camera, robots[ROBOTS], character, parts[PARTS], merged;
    bool dressed;
    bool started, orbit, paused;
    int clip; /* the front robot's */
    int debug; /* the Adventurer's debug view: 0 none, then wires, points, skeleton */
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

    g.character = wgf_actor_create();
    wgf_actor_set_parent(g.character, g.stage);
    wgf_actor_set_position(g.character, 5.0f, 0.0f, 3.0f);
    wgf_actor_set_scale(g.character, 2.0f, 2.0f, 2.0f); /* the pack's people are half a robot's height */
    mesh = wgf_mesh_create(CLIPS_PATH);
    for (i = 0; i < PARTS; i++) {
        wgf_mesh_t part = wgf_mesh_create(part_paths[i]);
        g.parts[i] = wgf_model_create(part);
        wgf_resource_release(part);
        wgf_model_set_animations(g.parts[i], mesh); /* the clips file's channels, by its nodes' names */
        wgf_actor_set_parent(g.parts[i], g.character);
    }
    { /* the four merged on the body's skeleton: the files shared by their paths, so loaded once */
        wgf_mesh_t pieces[PARTS], merged;
        for (i = 0; i < PARTS; i++) pieces[i] = wgf_mesh_create(part_paths[i]);
        merged = wgf_mesh_create_merged(pieces, PARTS, pieces[0]);
        for (i = 0; i < PARTS; i++) wgf_resource_release(pieces[i]); /* the merge holds them until it is made */
        g.merged = wgf_model_create(merged);
        wgf_resource_release(merged);
        wgf_model_set_animations(g.merged, mesh);
        wgf_actor_set_parent(g.merged, g.stage);
        wgf_actor_set_position(g.merged, -5.0f, 0.0f, 3.0f);
        wgf_actor_set_scale(g.merged, 2.0f, 2.0f, 2.0f);
    }
    wgf_resource_release(mesh); /* each part holds the clips */
}

/* Once the clips and the parts are READY: every part plays the same clip from the same time. */
static void dress(void)
{
    int i;
    for (i = 0; i < PARTS; i++) {
        const int clip = wgf_model_find_animation(g.parts[i], "CharacterArmature|Run");
        wgf_model_play_animation(g.parts[i], clip >= 0 ? clip : 0, 0.0f);
    }
    {
        const int clip = wgf_model_find_animation(g.merged, "CharacterArmature|Run");
        wgf_model_play_animation(g.merged, clip >= 0 ? clip : 0, 0.0f);
    }
    g.dressed = true;
}

/* `material` in every slot of every model under `actor`: a file's models are its tree's
 * actors, made once it has loaded, each with its own slots. */
static void set_below(wgf_actor_t actor, wgf_material_t material)
{
    int i;
    for (i = 0; i < wgf_actor_get_child_count(actor); i++) {
        const wgf_actor_t child = wgf_actor_get_child(actor, i);
        if (wgf_actor_get_kind(child) == WGF_ACTOR_KIND_MODEL) wgf_model_set_material(child, -1, material);
        set_below(child, material);
    }
}

/* Every model under `actor` shown or hidden (a file's tree, made once it has loaded). */
static void show_below(wgf_actor_t actor, bool visible)
{
    int i;
    for (i = 0; i < wgf_actor_get_child_count(actor); i++) {
        const wgf_actor_t child = wgf_actor_get_child(actor, i);
        if (wgf_actor_get_kind(child) == WGF_ACTOR_KIND_MODEL) wgf_actor_set_visible(child, visible);
        show_below(child, visible);
    }
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
    { /* the runner waves from the torso up as it runs: a layer over its run */
        const int wave = wgf_model_find_animation(g.robots[3], "Robot_Wave");
        if (wave >= 0) wgf_model_play_layer_animation(g.robots[3], 0, wave, "Torso", 0.5f, true);
    }
    { /* the waving robot drawn by a custom shader of its own, posed as the others */
        const wgf_shader_t shader = wgf_shader_create("shaders/toon.wgfshader");
        const wgf_material_t toon = wgf_material_create_custom(shader);
        wgf_resource_release(shader); /* the material holds it */
        wgf_material_set_color(toon, "color", wgf_color_make(120, 200, 255, 255));
        wgf_material_set_float(toon, "bands", 3.0f);
        wgf_material_set_float(toon, "rim", 0.35f);
        set_below(g.robots[4], toon);
        wgf_resource_release(toon); /* its models hold it */
    }
    g.clip = wgf_model_get_animation(g.robots[0]);
    g.started = true;
}

static const char *const debug_views[4] = {"", "wires", "points", "skeleton"};

/* The Adventurer drawn over itself as the debug view has it, through the stage's camera. */
static void draw_debug(void)
{
    int i;
    if (g.debug == 0 || !wgf_draw_begin_3d(g.camera)) return;
    for (i = 0; i < PARTS; i++) {
        if (g.debug == 1) wgf_draw_model_wires(g.parts[i], wgf_color_make(80, 255, 120, 255));
        else if (g.debug == 2) wgf_draw_model_points(g.parts[i], wgf_color_make(255, 220, 60, 255));
        else if (i == 0) wgf_draw_model_skeleton(g.parts[i], wgf_color_make(255, 80, 200, 255)); /* one: the body's */
    }
    wgf_draw_end_3d();
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
    if (wgf_keyboard_is_pressed(WGF_KEY_D)) g.debug = (g.debug + 1) % 4;
    show_below(g.character, g.debug != 3); /* the skeleton alone; every frame, as the parts load */
    if (!g.started && wgf_model_get_animation_count(g.robots[0]) > 0) start();
    if (!g.dressed && wgf_model_get_animation_count(g.parts[0]) > 0) {
        bool ready = true; /* every part's file too: one loaded later would start behind */
        for (i = 0; i < PARTS; i++) {
            ready = ready && wgf_resource_get_status(wgf_model_get_mesh(g.parts[i])) == WGF_RESOURCE_STATUS_READY;
        }
        ready = ready && wgf_resource_get_status(wgf_model_get_mesh(g.merged)) == WGF_RESOURCE_STATUS_READY;
        if (ready) dress();
    }
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
    draw_debug();
    if (g.started) {
        snprintf(line, sizeof(line), "libwgf skinning: %s, %.2f s%s%s%s",
                 wgf_model_get_animation_name(g.robots[0], g.clip), (double)wgf_model_get_animation_time(g.robots[0]),
                 wgf_model_get_animation_weight(g.robots[0]) < 1.0f ? " (crossfading)" : "",
                 g.debug != 0 ? "; the Adventurer's " : "", debug_views[g.debug]);
    } else {
        snprintf(line, sizeof(line), "libwgf skinning: loading %s", MODEL_PATH);
    }
    wgf_draw_text(0, line, 12, 36, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_app_can_quit() ? "N: next clip   SPACE: pause   O: the camera   D: debug view   ESC: quit"
                                        : "N: next clip   SPACE: pause   O: the camera   D: debug view",
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
