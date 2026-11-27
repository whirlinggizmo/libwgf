#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* where it runs, in its JSON line: a page or a native program */
#ifdef __EMSCRIPTEN__
#define TARGET "page"
#else
#define TARGET "native"
#endif

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* The animated robots: raylib's models_animation, many at once, as helix measures it (../helix's
 * tools/speed/README.md, "robots"), so libwgf's rows read beside helix-raylib's, raylib C's,
 * three.js's, and Godot's. 1280 x 720, robot.glb (Quaternius's, raylib's example model;
 * examples/assets/CREDITS.md), `count` robots in a grid of ceil(sqrt(count)) columns 4 apart,
 * the camera at (0, 0.9w, 0.9w) looking at (0, 1.5, 0) with a 45 degree field of view (w the
 * columns times 4), each posed at frame (frame + 1 + i * 5) % frameCount of the file's first
 * animation, its frames taken at 60 a second as raylib samples a glTF's, drawn unlit. Skinned
 * on the GPU (wgf_model.h).
 *
 * Options, from the page's query (natively --count=N and so on): count=N (default 50), and
 * culling=0 to draw every robot without the stage's frustum culling (wgf_stage3d_set_culling),
 * as a renderer that doesn't cull draws them: here every robot is in view, so culling only
 * costs. It draws 120 frames to warm up once the file is READY, then times 600 frame to
 * frame, prints helix's JSON line (mean_ms, median_ms, p95_ms), and logs "robots: done". */

#define WIDTH 1280
#define HEIGHT 720
#define WARMUP 120
#define TIMED 600
#define FPS 60.0f

static struct {
    wgf_actor_t stage, camera, *robots;
    int count, frame_count, frame, ndeltas;
    bool culling;
    bool started, done;
    double last, deltas[WARMUP + TIMED];
} r;

static double now_ms(void)
{
#ifdef __EMSCRIPTEN__
    return emscripten_get_now();
#else
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
#endif
}

static int compare(const void *x, const void *y)
{
    const double a = *(const double *)x, c = *(const double *)y;
    return a < c ? -1 : a > c ? 1 : 0;
}

/* Every material of the models at and under `actor` drawn unlit, as raylib's example draws. */
static void unlit(wgf_actor_t actor, int depth)
{
    int i, n;
    if (depth > 64) return;
    if (wgf_actor_get_kind(actor) == WGF_ACTOR_KIND_MODEL) {
        const wgf_mesh_t mesh = wgf_model_get_mesh(actor);
        for (i = 0; i < wgf_mesh_get_material_count(mesh); i++) {
            wgf_material_set_shading(wgf_mesh_get_material(mesh, i), WGF_MATERIAL_SHADING_UNLIT);
        }
    }
    n = wgf_actor_get_child_count(actor);
    for (i = 0; i < n; i++) unlit(wgf_actor_get_child(actor, i), depth + 1);
}

static void init(void *user)
{
    const int columns = (int)ceilf(sqrtf((float)r.count));
    const float w = (float)columns * 4.0f;
    wgf_mesh_t mesh;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(0xF5F5F5FFu);
    r.stage = wgf_stage3d_create();
    r.camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(r.camera, 45.0f * 3.14159265f / 180.0f);
    wgf_actor_set_position(r.camera, 0.0f, 0.9f * w, 0.9f * w);
    wgf_actor_look_at(r.camera, 0.0f, 1.5f, 0.0f, 0.0f, 1.0f, 0.0f);
    wgf_actor_set_parent(r.camera, r.stage);
    wgf_stage3d_set_camera(r.stage, r.camera);
    wgf_stage3d_set_ambient(r.stage, 0xFFFFFFFFu, 1.0f);
    wgf_stage3d_set_culling(r.stage, r.culling);
    mesh = wgf_mesh_create("models/robot.glb");
    r.robots = (wgf_actor_t *)calloc((size_t)r.count, sizeof(wgf_actor_t));
    for (i = 0; r.robots != NULL && i < r.count; i++) {
        r.robots[i] = wgf_model_create(mesh);
        wgf_actor_set_position(r.robots[i], ((float)(i % columns) - (float)(columns - 1) * 0.5f) * 4.0f, 0.0f,
                               ((float)(i / columns) - (float)(columns - 1) * 0.5f) * 4.0f);
        wgf_actor_set_parent(r.robots[i], r.stage);
    }
    wgf_resource_release(mesh);
}

static void finish(void)
{
    r.done = true;
    wgf_log_message(WGF_LOG_LEVEL_INFO, "robots: done");
    if (wgf_app_can_quit()) wgf_app_quit();
}

static void frame(void *user)
{
    const double now = now_ms();
    int i;
    (void)user;
    if (r.done || r.robots == NULL) return;
    if (!r.started) {
        if (wgf_model_get_animation_count(r.robots[0]) == 0) return; /* the file still loading */
        r.frame_count = (int)ceilf(wgf_model_get_animation_duration(r.robots[0], 0) * FPS);
        if (r.frame_count < 1) r.frame_count = 1;
        for (i = 0; i < r.count; i++) {
            wgf_model_play_animation(r.robots[i], 0, 0.0f);
            wgf_model_pause_animation(r.robots[i]); /* posed by frame, below, as raylib's */
        }
        unlit(r.robots[0], 0);
        r.started = true;
        r.last = now;
    } else if (r.ndeltas < WARMUP + TIMED) {
        r.deltas[r.ndeltas++] = now - r.last;
    }
    r.last = now;
    for (i = 0; i < r.count; i++) {
        wgf_model_set_animation_time(r.robots[i], (float)((r.frame + 1 + i * 5) % r.frame_count) / FPS);
    }
    r.frame++;
    wgf_stage3d_draw(r.stage);
    if (r.ndeltas == WARMUP + TIMED) {
        static double sorted[TIMED];
        double sum = 0, mean;
        memcpy(sorted, r.deltas + WARMUP, sizeof(sorted));
        for (i = 0; i < TIMED; i++) sum += sorted[i];
        qsort(sorted, TIMED, sizeof(double), compare);
        mean = sum / TIMED;
        printf("{\"contender\":\"libwgf\",\"target\":\"" TARGET "\",\"mode\":\"count\",\"count\":%d,\"mean_ms\":%.6f,"
               "\"median_ms\":%.6f,\"p95_ms\":%.6f,\"fps\":%.3f}\n",
               r.count, mean, sorted[TIMED / 2], sorted[TIMED * 95 / 100], 1000.0 / mean);
        fflush(stdout);
        finish();
    }
}

int main(void)
{
    r.count = wgf_app_get_option_int("count", 50);
    r.culling = wgf_app_get_option_int("culling", 1) != 0;
    if (r.count < 1) r.count = 1;
    wgf_window_set_title("libwgf robots");
    wgf_window_set_size(WIDTH, HEIGHT);
    wgf_window_set_flags(WGF_WINDOW_FLAG_VSYNC_OFF | WGF_WINDOW_FLAG_LOW_DPI);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
