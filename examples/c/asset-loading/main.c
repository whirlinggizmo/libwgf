#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "wgf.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_texture.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* Loading during play without stalling frames. Creates two sounds (the music decoded
 * whole, the heavy one) and four textures while a square bobs and a graph shows every
 * frame's duration. Resources load on create: each comes back PENDING at once and is
 * READY or FAILED a few frames later, so this program creates them all, uses them at
 * once, and only reads their statuses, for the progress bar and a row per file.
 * Nothing is called back, and nothing waits. The other way to wait is for files alone
 * (E): a group of ensures makes them local, and everything is created once the group
 * is DONE.
 *
 *   A    load spread out: files are decoded on worker threads, and the main thread's
 *        part (GPU uploads) is given a few milliseconds a frame
 *        (wgf_resource_set_load_budget, 4 ms)
 *   S    load at once: no budget, so everything decoded is finished in the same
 *        frame, which the graph shows as a spike
 *   E    fetch first: ensure every file as a group (on the web, downloads; natively,
 *        done at the next update), then create them, spread out
 *   F    create a texture whose file isn't there: FAILED a frame or so later, drawn
 *        as the placeholder (the log says why, as a warning)
 *   U    unload
 *   ESC  quit, where quitting means anything
 *
 * Starts with a spread-out load.
 *
 * wgrender's loading example, done for the size table. Its differences, all because
 * libwgf has no 3D:
 *   - Its two HDR environments (about 330 ms of decoding each) are two sounds decoded
 *     whole as they load: the music (1.4 MB of MP3, the heavy one) and a click (4 KB
 *     of Ogg, as heavy as libwgf's examples have). A sound isn't drawn, so they show
 *     only in their rows, where the environment lit the scene and was its background.
 *   - Its two models are two textures, textures/flame.png and textures/particle.png,
 *     drawn where the character and the sphere stood.
 *   - The sphere's material is two textures drawn side by side: its normal map
 *     (textures/tiles_normal.png) and, after F, its base color, the missing file,
 *     drawn as the placeholder.
 *   - The wire cube bobbing over them is a square outline bobbing.
 *   - The logo is created and not drawn, as in wgrender's.
 *   - The second line says libwgf's version, where wgrender's named the renderer and
 *     whether decoding runs on worker threads: libwgf has no call for either. */

enum { SOUNDS = 2, MODELS = 2, TEXTURES = 2, FILES = SOUNDS + MODELS + TEXTURES + 1, GRAPH = 300 };
enum { MISSING = FILES - 1 }; /* created only on F */
enum { SLOT = 128 };          /* the side of a texture drawn */

static const char *PATHS[FILES] = {
    "music/a_hero_is_born.mp3", /* in place of environments/venice_sunset_1k.hdr */
    "sounds/click_004.ogg",     /* in place of environments/studio_small_09_1k.hdr */
    "textures/flame.png",       /* in place of the character's model */
    "textures/particle.png",    /* in place of models/sphere/sphere.glb */
    "textures/tiles_normal.png",
    "sprites/logo/wg-logo-white-alpha.png",
    "textures/not_there.png", /* missing on purpose */
};

static struct {
    wgf_color_t bar, graph_ok, graph_slow, line, cube;
    wgf_handle_t resources[FILES];
    wgf_asset_task_t fetch_group; /* E: the files being made local, before anything is created */
    bool loading, at_once, fetch_first;
    double load_started, load_seconds;
    double last_time;
    float frame_ms[GRAPH];
    int frame_next;
    float time;
} g;

static void release_all(void)
{
    for (int i = 0; i < FILES; i++) {
        wgf_resource_release(g.resources[i]); /* one call for every kind; false for 0 */
        g.resources[i] = 0;
    }
    wgf_asset_task_destroy(g.fetch_group); /* its members too; false for 0 */
    g.fetch_group = 0;
    g.loading = false;
}

/* Create every resource; each is drawn once it's READY. */
static void create_all(void)
{
    for (int i = 0; i < SOUNDS; i++) g.resources[i] = wgf_sound_create(PATHS[i]);
    for (int i = SOUNDS; i < MISSING; i++) g.resources[i] = wgf_texture_create(PATHS[i]);
}

/* How the last load was asked for, for the status line. */
static const char *how(void)
{
    return g.fetch_first ? "fetched first" : g.at_once ? "all at once" : "spread out";
}

static void start_clock(bool at_once)
{
    release_all();
    g.at_once = at_once;
    g.fetch_first = false;
    wgf_resource_set_load_budget(at_once ? 1000.0f : 4.0f); /* 4 ms is the default */
    g.load_started = wgf_time_get_seconds();
    g.loading = true;
    for (int i = 0; i < GRAPH; i++) g.frame_ms[i] = 0.0f; /* "worst" covers this load */
}

static void start_load(bool at_once)
{
    start_clock(at_once);
    create_all();
}

/* Ensure every file, as one group; frame() creates them all once it has finished. */
static void start_fetch(void)
{
    start_clock(false);
    g.fetch_first = true;
    g.fetch_group = wgf_asset_group_create();
    for (int i = 0; i < MISSING; i++) {
        wgf_asset_group_add(g.fetch_group, wgf_asset_ensure(PATHS[i], NULL, WGF_ASSET_ENSURE_NONE));
    }
}

/* How many of the files are done (READY or FAILED, or not asked for); the load is over
 * when all are. */
static int files_done(void)
{
    int done = 0;
    for (int i = 0; i < FILES; i++) done += wgf_resource_get_status(g.resources[i]) != WGF_RESOURCE_STATUS_PENDING;
    return done;
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_asset_set_manifest("manifest.json");
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));
    g.bar = wgf_color_make(0, 0, 0, 170);
    g.graph_ok = wgf_color_make(90, 200, 120, 255);
    g.graph_slow = wgf_color_make(235, 80, 70, 255);
    g.line = wgf_color_make(255, 255, 255, 90);
    g.cube = wgf_color_make(230, 180, 60, 255);

    g.last_time = wgf_time_get_seconds();
    start_load(false);
}

static void draw_graph(float x, float y, float width, float height)
{
    const float max_ms = 100.0f;
    const float bar = width / GRAPH;
    float worst = 0.0f;
    char text[96];

    wgf_draw_rectangle(x, y, width, height, g.bar);
    for (int i = 0; i < GRAPH; i++) {
        const float ms = g.frame_ms[(g.frame_next + i) % GRAPH];
        const float h = floorf(height * (ms < max_ms ? ms : max_ms) / max_ms);
        if (h > 0) {
            wgf_draw_rectangle(x + floorf(i * bar), y + height - h, bar > 1.0f ? floorf(bar) : 1, h,
                               ms > 34.0f ? g.graph_slow : g.graph_ok);
        }
        worst = ms > worst ? ms : worst;
    }
    {
        const float line_y = y + height - floorf(height * 16.7f / max_ms); /* a 60 Hz frame */
        wgf_draw_line(x, line_y, x + width, line_y, 1, g.line);
    }
    snprintf(text, sizeof(text), "frame times, 0-100 ms (line: 16.7 ms)   worst: %.0f ms", worst);
    wgf_draw_text(0, text, x + 6, y + 6, 10, WGF_COLOR_LIGHTGRAY);
}

/* The textures where wgrender's models and material were: drawn once READY, the
 * placeholder once FAILED. */
static void draw_scene(float width)
{
    static const int DRAWN[] = {2, 3, 4, MISSING}; /* the character, the sphere, its normal map, its base color */
    const float gap = 40.0f, left = (width - 4 * SLOT - 3 * gap) * 0.5f, top = 300.0f;
    const float bob = 10.0f * sinf(g.time * 3.0f);
    for (int i = 0; i < 4; i++) {
        const float x = left + i * (SLOT + gap);
        wgf_draw_rectangle_lines(x, top, SLOT, SLOT, 1, g.line);
        wgf_draw_texture(g.resources[DRAWN[i]], x, top, SLOT, SLOT, WGF_COLOR_WHITE);
    }
    wgf_draw_rectangle_lines(width * 0.5f - 25, top - 80 + bob, 50, 50, 2, g.cube);
}

static void frame(void *user)
{
    const float width = (float)wgf_render_get_width() / wgf_render_get_dpi_scale();
    const float height = (float)wgf_render_get_height() / wgf_render_get_dpi_scale();
    const double now = wgf_time_get_seconds();
    char line[160];
    (void)user;
    g.frame_ms[g.frame_next] = (float)((now - g.last_time) * 1000.0); /* real time, uncapped */
    g.frame_next = (g.frame_next + 1) % GRAPH;
    g.last_time = now;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_A)) start_load(false);
    if (wgf_keyboard_is_pressed(WGF_KEY_S)) start_load(true);
    if (wgf_keyboard_is_pressed(WGF_KEY_E)) start_fetch();
    if (wgf_keyboard_is_pressed(WGF_KEY_U)) release_all();
    if (wgf_keyboard_is_pressed(WGF_KEY_F) && g.resources[MISSING] == 0) {
        g.resources[MISSING] = wgf_texture_create(PATHS[MISSING]); /* the placeholder, once FAILED */
    }
    if (g.fetch_group != 0 && wgf_asset_task_get_status(g.fetch_group) != WGF_ASSET_TASK_STATUS_PENDING) {
        wgf_asset_task_destroy(g.fetch_group); /* local now (or not: the creates say why) */
        g.fetch_group = 0;
        create_all();
    }
    if (g.loading && g.fetch_group == 0 && files_done() == FILES) {
        g.loading = false;
        g.load_seconds = now - g.load_started;
    }
    g.time += wgf_loop_get_frame_delta();

    draw_scene(width);
    wgf_draw_rectangle(0, 0, width, 64, g.bar);
    wgf_draw_text(0,
                  "libwgf loading   A: spread out   S: all at once   E: fetch first   F: a missing file   "
                  "U: unload",
                  12, 12, 12, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "libwgf %s", wgf_version_get());
    wgf_draw_text(0, line, 12, 26, 12, WGF_COLOR_LIGHTGRAY);
    if (g.fetch_group != 0) {
        const float progress = wgf_asset_task_get_progress(g.fetch_group); /* the group's members' average */
        snprintf(line, sizeof(line), "fetching first... %.0f%%", progress * 100.0f);
        wgf_draw_rectangle(12, 54, floorf(240 * progress), 12, g.graph_ok);
        wgf_draw_rectangle_lines(12, 54, 240, 12, 1, g.line);
        wgf_draw_text(0, line, 264, 54, 12, WGF_COLOR_LIGHTGRAY);
    } else if (g.loading) {
        const float progress = (float)files_done() / FILES;
        snprintf(line, sizeof(line), "loading (%s)... %.0f%%", how(), progress * 100.0f);
        wgf_draw_rectangle(12, 54, floorf(240 * progress), 12, g.graph_ok);
        wgf_draw_rectangle_lines(12, 54, 240, 12, 1, g.line);
        wgf_draw_text(0, line, 264, 54, 12, WGF_COLOR_LIGHTGRAY);
    } else if (g.resources[0] != 0) {
        snprintf(line, sizeof(line), "loaded %s in %.2f s", how(), g.load_seconds);
        wgf_draw_text(0, line, 12, 54, 12, WGF_COLOR_LIGHTGRAY);
    }
    for (int i = 0; i < FILES; i++) { /* a row per file asked for: where it stands */
        static const char *const STATUS[] = {"", "pending", "ready", "FAILED"};
        wgf_resource_status_t status;
        const char *slash;
        if (g.resources[i] == 0) continue;
        status = wgf_resource_get_status(g.resources[i]);
        slash = strrchr(PATHS[i], '/');
        snprintf(line, sizeof(line), "%-8s %s", STATUS[status], slash != NULL ? slash + 1 : PATHS[i]);
        wgf_draw_text(0, line, 12, 80.0f + i * 16, 12,
                      status == WGF_RESOURCE_STATUS_READY    ? WGF_COLOR_LIME
                      : status == WGF_RESOURCE_STATUS_FAILED ? WGF_COLOR_RED
                                                             : WGF_COLOR_LIGHTGRAY);
    }
    draw_graph(12, height - 132, width - 24, 120);
}

int main(void)
{
    wgf_window_set_title("libwgf loading");
    wgf_window_set_size(1100, 720);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
