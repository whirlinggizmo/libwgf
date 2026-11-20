#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_log.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_sprite_batch.h"
#include "wgf_stage2d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* The bunnymark: raylib's textures_bunnymark as helix measures it (../helix's
 * tools/speed/README.md, "The programs" and "The measure"), so libwgf's rows read beside
 * helix-raylib's, raylib C's, three.js's, and Godot's, which helix took on this machine.
 * Run in a browser by tools/bench/measure_frames.py (`bench:bunnymark`), unthrottled, its
 * frame rate cap lifted.
 *
 * 800 x 450, cleared to raylib's RAYWHITE; every bunny spawned from the same xorshift32
 * seed (2463534242) as helix's and raylib C's: x in [0, 768], y in [40, 418], a speed of
 * rnd(-250, 250) / 60 a frame on each axis, a tint of (rnd(50, 240), rnd(80, 240),
 * rnd(100, 240), 255); raylib's update and bounce, its positions truncated to whole pixels
 * as raylib draws them. Each bunny is a sprite on a 2D stage, an actor of its own, as a
 * libwgf game would write it (helix-legacy's row, a Sprite2D each, is the like of it).
 *
 * The bunny is raylib's wabbit_alpha.png, as helix's is (examples/assets/CREDITS.md).
 * Two ways to draw them: a sprite actor a bunny (the default), of which there are at most
 * 65535, so past about 65000 bunnies the program says so in its line ("limit") instead of
 * drawing them; or, with batch=1, one sprite batch (wgf_sprite_batch.h) holding every bunny
 * as a quad, its positions written as one array a frame, with no limit but memory's.
 *
 * Options, from the page's query (natively, --count=N and so on): count=N, or
 * incrementCount=N with startCount=M, and batch=1. A count run draws 120 frames to warm up, then
 * times 600 frame to frame, and prints helix's JSON line (mean_ms, median_ms, p95_ms); a
 * ramp starts at M bunnies (N without it), adds N each 60 frames while those averaged 59
 * frames a second or better, and prints the largest count that held (max_count). Then it
 * logs "bunnymark: done". */

#define SCREEN_W 800
#define SCREEN_H 450
#define BUNNY 32
#define WARMUP 120
#define TIMED 600
#define MAX_SPRITES 65536

typedef struct bunny_t {
    float x, y, vx, vy;
} bunny_t;

static struct {
    wgf_actor_t stage;
    wgf_texture_t texture;
    wgf_actor_t sprites[MAX_SPRITES];
    bunny_t *bunnies;
    float *positions; /* the batch's, x and y in turn */
    int count, capacity; /* bunnies made, and room for */
    bool batched;
    wgf_actor_t batch;
    bool limit; /* a sprite refused: no room for another actor */
    int asked, increment, start; /* the options */
    int frame; /* -1 until the first bunnies are made */
    int ndeltas, max_count;
    double last, window_start, deltas[WARMUP + TIMED];
    bool done;
} b = {.frame = -1, .asked = 1000};

static uint32_t seed = 2463534242u;

static int rnd(int min, int max)
{
    uint32_t x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    seed = x;
    return min + (int)((x >> 8) % (uint32_t)(max - min + 1));
}

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

static void spawn(int n)
{
    int i;
    if (b.count + n > b.capacity) {
        int capacity = b.capacity > 0 ? b.capacity : 1024;
        bunny_t *bunnies;
        float *positions;
        while (capacity < b.count + n) capacity *= 2;
        bunnies = (bunny_t *)realloc(b.bunnies, sizeof(bunny_t) * (size_t)capacity);
        if (bunnies != NULL) b.bunnies = bunnies;
        positions = (float *)realloc(b.positions, sizeof(float) * 2 * (size_t)capacity);
        if (positions != NULL) b.positions = positions;
        if (bunnies == NULL || positions == NULL) {
            b.limit = true;
            return;
        }
        b.capacity = capacity;
    }
    if (b.batched && !wgf_sprite_batch_set_count(b.batch, b.count + n)) {
        b.limit = true;
        return;
    }
    for (i = 0; i < n; i++) {
        bunny_t *bunny = &b.bunnies[b.count];
        wgf_actor_t sprite = 0;
        int red, green, blue, tint;
        if (!b.batched) {
            if (b.count == MAX_SPRITES || (sprite = wgf_sprite_create(b.texture)) == 0) {
                b.limit = true;
                return;
            }
        }
        bunny->x = (float)rnd(0, SCREEN_W - BUNNY);
        bunny->y = (float)rnd(40, SCREEN_H - BUNNY);
        bunny->vx = (float)rnd(-250, 250) / 60.0f;
        bunny->vy = (float)rnd(-250, 250) / 60.0f;
        red = rnd(50, 240), green = rnd(80, 240), blue = rnd(100, 240);
        tint = (int)(((uint32_t)red << 24) | ((uint32_t)green << 16) | ((uint32_t)blue << 8) | 0xFFu);
        if (b.batched) {
            wgf_sprite_batch_set_tints(b.batch, b.count, &tint, 1);
        } else {
            wgf_sprite_set_size(sprite, BUNNY, BUNNY);
            wgf_sprite_set_pivot(sprite, 0, 0); /* raylib's DrawTexture places the top-left */
            wgf_sprite_set_tint(sprite, (uint32_t)tint);
            wgf_actor_set_parent(sprite, b.stage);
            b.sprites[b.count] = sprite;
        }
        b.count++;
    }
}

static int compare(const void *x, const void *y)
{
    const double a = *(const double *)x, c = *(const double *)y;
    return a < c ? -1 : a > c ? 1 : 0;
}

static void finish(void)
{
    b.done = true;
    wgf_log_message(WGF_LOG_LEVEL_INFO, "bunnymark: done");
    if (wgf_app_can_quit()) wgf_app_quit();
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(0xF5F5F5FFu);
    b.stage = wgf_stage2d_create();
    b.texture = wgf_texture_create("textures/wabbit_alpha.png");
    if (b.batched) {
        b.batch = wgf_sprite_batch_create(b.texture);
        wgf_sprite_batch_set_pivot(b.batch, 0, 0); /* raylib's DrawTexture places the top-left */
        wgf_actor_set_parent(b.batch, b.stage);
    }
}

static void frame(void *user)
{
    double now;
    int i;
    (void)user;
    if (b.done || wgf_resource_get_status(b.texture) == WGF_RESOURCE_STATUS_PENDING) return;
    now = now_ms();
    if (b.frame < 0) {
        spawn(b.asked);
    } else if (b.ndeltas < WARMUP + TIMED) {
        b.deltas[b.ndeltas++] = now - b.last;
    } else {
        b.ndeltas++;
    }
    b.last = now;
    b.frame++;

    for (i = 0; i < b.count; i++) {
        bunny_t *bunny = &b.bunnies[i];
        bunny->x += bunny->vx;
        bunny->y += bunny->vy;
        if (bunny->x + BUNNY / 2 > SCREEN_W || bunny->x + BUNNY / 2 < 0) bunny->vx *= -1;
        if (bunny->y + BUNNY / 2 > SCREEN_H || bunny->y + BUNNY / 2 - 40 < 0) bunny->vy *= -1;
        if (b.batched) {
            b.positions[2 * i] = (float)(int)bunny->x;
            b.positions[2 * i + 1] = (float)(int)bunny->y;
        } else {
            wgf_actor_set_position(b.sprites[i], (float)(int)bunny->x, (float)(int)bunny->y, 0);
        }
    }
    if (b.batched) wgf_sprite_batch_set_positions(b.batch, 0, b.positions, 2 * b.count);
    wgf_stage2d_draw(b.stage);

    if (!b.increment) {
        if (b.ndeltas == WARMUP + TIMED) {
            static double sorted[TIMED];
            double sum = 0, mean;
            memcpy(sorted, b.deltas + WARMUP, sizeof(sorted));
            for (i = 0; i < TIMED; i++) sum += sorted[i];
            qsort(sorted, TIMED, sizeof(double), compare);
            mean = sum / TIMED;
            printf("{\"contender\":\"libwgf\",\"draw\":\"%s\",\"target\":\"page\",\"mode\":\"count\",\"count\":%d,\"asked\":%d,"
                   "\"limit\":%s,\"mean_ms\":%.6f,\"median_ms\":%.6f,\"p95_ms\":%.6f,"
                   "\"fps\":%.3f}\n",
                   b.batched ? "batch" : "actors", b.count, b.asked, b.limit ? "true" : "false", mean,
                   sorted[TIMED / 2], sorted[TIMED * 95 / 100], 1000.0 / mean);
            fflush(stdout);
            finish();
        }
    } else if (b.ndeltas >= WARMUP && (b.ndeltas - WARMUP) % 60 == 0) {
        if (b.ndeltas > WARMUP) {
            const double fps = 60 * 1000.0 / (now - b.window_start);
            if (fps >= 59 && !b.limit) {
                b.max_count = b.count;
                spawn(b.increment);
            } else {
                printf("{\"contender\":\"libwgf\",\"draw\":\"%s\",\"target\":\"page\",\"mode\":\"ramp\",\"max_count\":%d,"
                       "\"limit\":%s,\"last_fps\":%.3f}\n",
                       b.batched ? "batch" : "actors", b.max_count, b.limit ? "true" : "false", fps);
                fflush(stdout);
                finish();
            }
        }
        b.window_start = now;
    }
}

int main(void)
{
    /* from the page's query, natively --count=N and so on (wgf_app.h's options) */
    b.asked = wgf_app_get_option_int("count", 1000);
    b.increment = wgf_app_get_option_int("incrementCount", 0);
    b.start = wgf_app_get_option_int("startCount", 0);
    if (b.increment) b.asked = b.start > 0 ? b.start : b.increment;
    b.batched = wgf_app_get_option_int("batch", 0) != 0;
    wgf_window_set_title("libwgf bunnymark");
    wgf_window_set_size(SCREEN_W, SCREEN_H);
    wgf_window_set_vsync(false);
    wgf_window_set_high_dpi(false);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
