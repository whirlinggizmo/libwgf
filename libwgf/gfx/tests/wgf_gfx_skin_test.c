#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"

/* Skins and animations, headless: a glTF file of two joints ("hip", and "arm" a unit above
 * it) and a triangle the arm's joint weights, with three animations -- "raise" (the arm's
 * place, linear, 1 to 3 up over 2 s; the hip's turn, a step to a quarter turn about z at
 * 1 s), "spline" (the arm's place on a cubic spline, 1 to 9 up over 1 s, flat tangents), and
 * "empty" (no channels) -- read: its clips listed and found by name with their lengths, the
 * triangle's joints and weights and the skin kept; played on the file's root (before it has
 * loaded too), its joints' actors posed at known times, the time moved on by the part's update,
 * wrapped looping and COMPLETE not, paused, resumed, stopped, backwards; a crossfade's weight
 * and its blended pose; the skinned part drawn placed by the root, its joint matrices among
 * the frame's as the pose says; and refusals: a model that isn't a file's root, an animation
 * the file hasn't, numbers that aren't. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void write_file(const char *path, const void *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static void settle(wgf_mesh_t mesh)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

static void put32(unsigned char *at, unsigned int value)
{
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

#define S2 0.70710678f

/* The file's buffer: what each accessor below reads, at its offset. */
static int make_bin(unsigned char *bin)
{
    static const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};                       /* 0 */
    static const unsigned char joints[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};          /* 36 */
    static const float weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};                 /* 48 */
    static const float bind[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,        /* 96: the hip's */
                                   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};      /* the arm's, 1 up */
    static const float raise_times[2] = {0, 2};                                           /* 224 */
    static const float raise_places[6] = {0, 1, 0, 0, 3, 0};                              /* 232 */
    static const float step_times[2] = {0, 1};                                            /* 256 */
    static const float step_turns[8] = {0, 0, 0, 1, 0, 0, S2, S2};                        /* 264 */
    static const float spline_times[2] = {0, 1};                                          /* 296 */
    static const float spline_places[18] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0}; /* 304 */
    memcpy(bin + 0, positions, sizeof(positions));
    memcpy(bin + 36, joints, sizeof(joints));
    memcpy(bin + 48, weights, sizeof(weights));
    memcpy(bin + 96, bind, sizeof(bind));
    memcpy(bin + 224, raise_times, sizeof(raise_times));
    memcpy(bin + 232, raise_places, sizeof(raise_places));
    memcpy(bin + 256, step_times, sizeof(step_times));
    memcpy(bin + 264, step_turns, sizeof(step_turns));
    memcpy(bin + 296, spline_times, sizeof(spline_times));
    memcpy(bin + 304, spline_places, sizeof(spline_places));
    return 376;
}

static const char json[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,2]}],"
    "\"nodes\":[{\"name\":\"hip\",\"children\":[1]},{\"name\":\"arm\",\"translation\":[0,1,0]},"
    "{\"name\":\"body\",\"mesh\":0,\"skin\":0}],"
    "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":3}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2}}]}],"
    "\"animations\":["
    "{\"name\":\"raise\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
    "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"rotation\"}}],"
    "\"samplers\":[{\"input\":4,\"output\":5},{\"input\":6,\"output\":7,\"interpolation\":\"STEP\"}]},"
    "{\"name\":\"spline\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
    "\"samplers\":[{\"input\":8,\"output\":9,\"interpolation\":\"CUBICSPLINE\"}]},"
    "{\"name\":\"empty\",\"channels\":[],\"samplers\":[]}],"
    "\"buffers\":[{\"byteLength\":376}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":376}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],"
    "\"max\":[1,1,0]},"
    "{\"bufferView\":0,\"byteOffset\":36,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":96,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
    "{\"bufferView\":0,\"byteOffset\":224,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],"
    "\"max\":[2]},"
    "{\"bufferView\":0,\"byteOffset\":232,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
    "{\"bufferView\":0,\"byteOffset\":256,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],"
    "\"max\":[1]},"
    "{\"bufferView\":0,\"byteOffset\":264,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":296,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],"
    "\"max\":[1]},"
    "{\"bufferView\":0,\"byteOffset\":304,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"}]}";

static void write_glb(const char *path)
{
    static unsigned char glb[8192];
    unsigned char bin[512];
    const int bin_size = make_bin(bin);
    const unsigned int json_size = (unsigned int)((sizeof(json) - 1 + 3) & ~3u);
    const unsigned int total = 12 + 8 + json_size + 8 + (unsigned int)bin_size;
    memset(glb, ' ', sizeof(glb));
    memcpy(glb, "glTF", 4);
    put32(glb + 4, 2);
    put32(glb + 8, total);
    put32(glb + 12, json_size);
    memcpy(glb + 16, "JSON", 4);
    memcpy(glb + 20, json, sizeof(json) - 1);
    put32(glb + 20 + json_size, (unsigned int)bin_size);
    memcpy(glb + 24 + json_size, "BIN\0", 4);
    memcpy(glb + 28 + json_size, bin, (size_t)bin_size);
    write_file(path, glb, (int)total);
}

/* Where the player is, briefly. */
static float time_of(wgf_actor_t root)
{
    return wgf_model_get_animation_time(root);
}

static wgf_play_state_t state_of(wgf_actor_t root)
{
    return wgf_model_get_animation_state(root);
}

static float length_of(wgf_actor_t root, int animation)
{
    return wgf_model_get_animation_duration(root, animation);
}

/* The arm's height above the hip, as its actor has it now. */
static float arm_y(wgf_actor_t root)
{
    return wgf_actor_get_position(wgf_actor_find(root, "hip/arm")).y;
}

int main(void)
{
    wgf_mesh_t mesh;
    wgf_actor_t root, stage, camera, body, hip, plain;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals below warn, on purpose */
    write_glb("models/arm.glb");
    wgf_platform_priv_set_size(640, 480);
    expect(wgf_gfx_priv_start(), "gfx starts");
    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_stage3d_set_camera(stage, camera);
    wgf_actor_set_parent(camera, stage);
    wgf_actor_set_position(camera, 0, 0, 10);

    /* played before the file has loaded: it plays once the file is there */
    mesh = wgf_mesh_create("models/arm.glb");
    root = wgf_model_create(mesh);
    wgf_actor_set_parent(root, stage);
    expect(wgf_model_get_animation_count(root) == 0 && wgf_model_find_animation(root, "raise") == -1,
           "no animations while the file loads");
    expect(wgf_model_play_animation(root, 0, 0.0f), "played while it loads");
    expect(state_of(root) == WGF_PLAY_STATE_PLAYING && wgf_model_get_animation(root) == 0,
           "PLAYING it");
    settle(mesh);
    expect(wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_READY, "the file READY");

    /* the clips */
    expect(wgf_model_get_animation_count(root) == 3, "three animations");
    expect(strcmp(wgf_model_get_animation_name(root, 1), "spline") == 0 &&
               strcmp(wgf_model_get_animation_name(root, 7), "") == 0,
           "their names; \"\" for one it hasn't");
    expect(wgf_model_find_animation(root, "raise") == 0 && wgf_model_find_animation(root, "empty") == 2 &&
               wgf_model_find_animation(root, "nope") == -1 && wgf_model_find_animation(root, NULL) == -1,
           "found by name");
    expect(near(length_of(root, 0), 2.0f) && near(length_of(root, 1), 1.0f) && near(length_of(root, 2), 0.0f) &&
               near(length_of(root, 9), 0.0f),
           "their lengths, the last key's time");
    {
        const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(mesh);
        const wgf_gfx_priv_mesh_record_t *part;
        expect(record != NULL && record->skin_count == 1 && record->skins[0].joint_count == 2 &&
                   record->skins[0].joints[0] == 0 && record->skins[0].joints[1] == 1 &&
                   near(record->skins[0].inverse_bind[1].m[13], -1.0f) && near(record->skins[0].centers[1].y, 1.0f),
               "the skin: its joints the hip's and the arm's nodes, the arm's bind a unit up");
        expect(record != NULL && record->animated != NULL && record->animated[0] && record->animated[1] &&
                   !record->animated[2],
               "the nodes an animation moves: the hip and the arm, not the body");
        part = record != NULL && record->part_count == 1 ? wgf_gfx_priv_mesh_record(record->parts[0]) : NULL;
        expect(part != NULL && part->primitive_count == 1 && part->primitives[0].skin != NULL &&
                   near(part->primitives[0].skin[0], 1.0f) && near(part->primitives[0].skin[4], 1.0f),
               "the triangle's joints and weights kept, the arm's whole");
    }
    body = wgf_actor_find(root, "body");
    hip = wgf_actor_find(root, "hip");
    expect(body != 0 && hip != 0 && near(arm_y(root), 1.0f), "posed at its start once loaded: the arm 1 up");

    /* a joint's pose at known times */
    expect(wgf_model_set_animation_time(root, 1.0f), "set to 1 s");
    expect(near(arm_y(root), 2.0f), "at 1 s the arm halfway, 2 up");
    {
        const wgf_vec3_t turn = wgf_actor_get_rotation(hip);
        expect(near(turn.z, 1.5707963f), "at 1 s the hip's step to its quarter turn");
    }
    wgf_model_set_animation_time(root, 0.5f);
    expect(near(arm_y(root), 1.5f) && near(wgf_actor_get_rotation(hip).z, 0.0f),
           "at 0.5 s the arm 1.5 up, the hip's step not yet taken");

    /* drawn: the skinned part placed by the root, its joint matrices as the pose says */
    wgf_actor_set_position(root, 10, 0, 0);
    wgf_actor_set_position(camera, 10, 0, 10);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    {
        wgf_mat4_t world, arm;
        expect(wgf_gfx_priv_stage3d_get_item_count() == 1, "the skinned part drawn");
        expect(wgf_gfx_priv_stage3d_get_item_world(0, &world) && near(world.m[12], 10.0f),
               "placed by its root, not its node");
        expect(wgf_gfx_priv_stage3d_get_joint_count() == 2 && wgf_gfx_priv_stage3d_get_joint(1, &arm) &&
                   near(arm.m[13], 0.5f) && near(arm.m[12], 0.0f) && near(arm.m[0], 1.0f),
               "the arm's joint matrix: half a unit up from its bind, in the root's space");
    }
    wgf_gfx_priv_end_frame();

    /* moved on by the part's update: wrapped looping, COMPLETE not */
    wgf_core_priv_part_update(1.0f);
    expect(near(time_of(root), 1.5f) && near(arm_y(root), 2.5f), "moved on 1 s, posed there");
    wgf_core_priv_part_update(1.0f);
    expect(near(time_of(root), 0.5f) && state_of(root) == WGF_PLAY_STATE_PLAYING,
           "looping: wrapped at its end");
    expect(wgf_model_set_animation_loop(root, false) && !wgf_model_is_animation_loop(root), "not looping");
    wgf_core_priv_part_update(5.0f);
    expect(state_of(root) == WGF_PLAY_STATE_COMPLETE && near(time_of(root), 2.0f) &&
               near(arm_y(root), 3.0f),
           "COMPLETE at its end, held there");
    expect(!wgf_model_pause_animation(root) && !wgf_model_resume_animation(root),
           "pause and resume only from where they apply");

    /* play, pause, resume, stop */
    expect(wgf_model_play_animation(root, 0, 0.0f) && near(time_of(root), 0.0f), "played again from its start");
    wgf_core_priv_part_update(0.5f);
    expect(wgf_model_pause_animation(root) && state_of(root) == WGF_PLAY_STATE_PAUSED, "paused");
    wgf_core_priv_part_update(1.0f);
    expect(near(time_of(root), 0.5f), "held while paused");
    expect(wgf_model_resume_animation(root) && state_of(root) == WGF_PLAY_STATE_PLAYING, "resumed");
    wgf_core_priv_part_update(0.25f);
    expect(near(time_of(root), 0.75f), "moving again");
    expect(wgf_model_stop_animation(root) && state_of(root) == WGF_PLAY_STATE_STOPPED &&
               near(time_of(root), 0.0f) && near(arm_y(root), 1.0f),
           "stopped: at its start, its first pose");
    wgf_core_priv_part_update(1.0f);
    expect(near(time_of(root), 0.0f), "stopped stays");

    /* backwards: from its end */
    expect(wgf_model_set_animation_speed(root, -1.0f) && near(wgf_model_get_animation_speed(root), -1.0f), "speed -1");
    wgf_model_play_animation(root, 0, 0.0f);
    expect(near(time_of(root), 2.0f) && near(arm_y(root), 3.0f), "backwards: from its end");
    wgf_core_priv_part_update(0.5f);
    expect(near(time_of(root), 1.5f), "moving toward its start");
    wgf_core_priv_part_update(3.0f);
    expect(state_of(root) == WGF_PLAY_STATE_COMPLETE && near(time_of(root), 0.0f),
           "COMPLETE at its start");
    wgf_model_set_animation_speed(root, 1.0f);

    /* a cubic spline: flat tangents, halfway between its keys at half its time */
    wgf_model_play_animation(root, 1, 0.0f);
    wgf_model_set_animation_time(root, 0.5f);
    expect(near(arm_y(root), 5.0f), "the spline halfway at 0.5 s");
    wgf_model_set_animation_time(root, 0.25f);
    expect(near(arm_y(root), 1.0f + 8.0f * (3.0f * 0.0625f - 2.0f * 0.015625f)), "and its curve at 0.25 s");

    /* a crossfade: from "raise" held at its end into "spline", over 1 s */
    wgf_model_play_animation(root, 0, 0.0f);
    wgf_core_priv_part_update(5.0f); /* not looping: COMPLETE at its end, the arm 3 up */
    expect(near(arm_y(root), 3.0f) && near(wgf_model_get_animation_weight(root), 1.0f),
           "the weight 1 with no crossfade");
    expect(wgf_model_play_animation(root, 1, 1.0f), "crossfaded into the spline");
    expect(near(wgf_model_get_animation_weight(root), 0.0f) && near(arm_y(root), 3.0f) &&
               wgf_model_get_animation(root) == 1,
           "the weight 0 as it begins: the pose the one before's");
    wgf_core_priv_part_update(0.5f);
    expect(near(wgf_model_get_animation_weight(root), 0.5f), "halfway through, the weight 0.5");
    expect(near(arm_y(root), 0.5f * 3.0f + 0.5f * 5.0f),
           "the pose blended: halfway between the held 3 and the spline's 5");
    wgf_core_priv_part_update(0.75f);
    expect(near(wgf_model_get_animation_weight(root), 1.0f) && near(arm_y(root), 9.0f), "through: the spline's alone");

    /* a crossfade from one still moving: it moves on as it fades */
    wgf_model_set_animation_loop(root, true);
    wgf_model_play_animation(root, 0, 0.0f); /* the arm 1 up, rising a unit a second */
    expect(wgf_model_play_animation(root, 2, 1.0f), "into the empty clip, from one moving");
    wgf_core_priv_part_update(0.5f);
    expect(near(arm_y(root), 0.5f * 1.5f + 0.5f * 1.0f),
           "the one faded from moved on (1.5 up), blended with the rest (1)");

    /* refusals */
    plain = wgf_model_create(0);
    expect(!wgf_model_play_animation(plain, 0, 0.0f) && wgf_model_get_animation_count(plain) == 0 &&
               wgf_model_get_animation(plain) == -1 && !wgf_model_stop_animation(plain) &&
               !wgf_model_set_animation_speed(plain, 1.0f),
           "a model that isn't a file's root has none");
    expect(!wgf_model_play_animation(body, 0, 0.0f), "nor a node of a file");
    expect(!wgf_model_play_animation(root, 3, 0.0f) && !wgf_model_play_animation(root, -1, 0.0f),
           "an animation the file hasn't refused");
    expect(!wgf_model_play_animation(root, 0, NAN) && !wgf_model_set_animation_speed(root, INFINITY) &&
               !wgf_model_set_animation_time(root, NAN),
           "numbers that aren't refused");
    wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_core_priv_part_update(0.5f); /* a destroyed player leaves the list */
    expect(state_of(root) == WGF_PLAY_STATE_STOPPED, "a destroyed model reads STOPPED");

    wgf_resource_release(mesh);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_skin_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
