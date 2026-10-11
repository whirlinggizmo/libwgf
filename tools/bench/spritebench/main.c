#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_emitter2d.h"
#include "wgf_log.h"
#include "wgf_loop.h"
#include "wgf_random.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* The sprite benchmark: where the time goes in sprite-heavy frames, libwgt's spritebench
 * (wgrender-c's tools/bench/spritebench.c), its cases carried as libwgf grows them, run in a
 * browser by tools/bench/measure_frames.py (`bench:spritebench`), which traces each frame's
 * main-thread work and reads the cases apart by the marks this program leaves in the trace
 * (console.timeStamp): "spritebench <case> <sprites>" as a case's measured frames begin,
 * "spritebench rest" as they end.
 *
 *   grid 2d            sprites on a 2D stage in a grid, one texture: the simplest case
 *   grid 2d, 4 tex     the same, the texture changing from one sprite to the next: a 2D
 *                      stage keeps its order, so every sprite a batch (wgrender-c's
 *                      "texture switching is the cliff")
 *   particles 2d       sprites thrown up and falling, each moved every frame, 1/120 of them
 *                      replaced every frame (a 2 s life), blended
 *   emitter 2d         the same particles from one emitter, each written once at birth and
 *                      moved by the GPU
 *
 * libwgt's 3D cases (a grid and a field of sprites facing the camera, and 3D particles and
 * emitters) come with sprites in 3D. Natively it runs too, and prints its own frame times. */

#define WARMUP_FRAMES 20
#define MEASURE_FRAMES 100
#define MAX_SPRITES 16384
#define PARTICLE_LIFE 120 /* frames */
#define SCREEN_W 960
#define SCREEN_H 600

typedef enum { CASE_GRID, CASE_GRID_4, CASE_PARTICLES, CASE_EMITTER, CASES } bench_case_t;

static const char *CASE_NAMES[CASES] = {"grid 2d", "grid 2d, 4 tex", "particles 2d", "emitter 2d"};
enum { COUNT_STEPS = 3 };
static const int SPRITE_COUNTS[COUNT_STEPS] = {1000, 4000, 16000};

typedef struct particle_t {
    float x, y, vx, vy;
    int age;
} particle_t;

static struct {
    wgf_actor_t stage, emitter;
    wgf_actor_t sprites[MAX_SPRITES];
    particle_t particles[MAX_SPRITES];
    wgf_texture_t textures[4];
    int count;
    bench_case_t which;
    int step; /* case * COUNT_STEPS + count step */
    int frame;
    double frame_sum;
} b;

/* A mark in the browser's trace (and the log, natively), where measure_frames reads the
 * cases apart. */
static void mark(const char *text)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.timeStamp(UTF8ToString($0)); }, text);
#else
    (void)text;
#endif
}

static void throw_particle(particle_t *p)
{
    p->x = SCREEN_W * 0.5f + (wgf_random_get_float() - 0.5f) * 80.0f;
    p->y = SCREEN_H - 40.0f;
    p->vx = (wgf_random_get_float() - 0.5f) * 240.0f;
    p->vy = -300.0f - wgf_random_get_float() * 200.0f;
    p->age = (int)(wgf_random_get_float() * PARTICLE_LIFE);
}

static void setup(void)
{
    const bench_case_t which = (bench_case_t)(b.step / COUNT_STEPS);
    const int count = SPRITE_COUNTS[b.step % COUNT_STEPS];
    const int side = (int)ceilf(sqrtf((float)count));
    int i;
    b.which = which;
    b.count = count;
    if (which == CASE_EMITTER) {
        b.emitter = wgf_emitter2d_create();
        wgf_emitter2d_set_texture(b.emitter, b.textures[0]);
        wgf_emitter2d_set_max(b.emitter, count);
        wgf_emitter2d_set_rate(b.emitter, (float)count / 2.0f); /* a 2 s life: the count alive */
        wgf_emitter2d_set_life(b.emitter, 2.0f, 2.0f);
        wgf_emitter2d_set_velocity(b.emitter, 0, -400, 0.4f, 0.25f);
        wgf_emitter2d_set_gravity(b.emitter, 0, 300);
        wgf_emitter2d_set_size(b.emitter, 8, 8, 0);
        wgf_emitter2d_set_alpha_mode(b.emitter, WGF_ALPHA_MODE_BLEND, 0.5f);
        wgf_actor_set_position(b.emitter, SCREEN_W * 0.5f, SCREEN_H - 40.0f, 0);
        wgf_actor_set_parent(b.emitter, b.stage);
        wgf_emitter2d_prewarm(b.emitter, 2.0f);
    } else {
        for (i = 0; i < count; i++) {
            const wgf_texture_t texture = which == CASE_GRID_4 ? b.textures[i % 4] : b.textures[0];
            b.sprites[i] = wgf_sprite_create(texture);
            wgf_sprite_set_size(b.sprites[i], 8, 8);
            if (which == CASE_PARTICLES) {
                throw_particle(&b.particles[i]);
            } else {
                wgf_actor_set_position(b.sprites[i], 8.0f + (float)(i % side) * (SCREEN_W - 16.0f) / (float)side,
                                       8.0f + (float)(i / side) * (SCREEN_H - 16.0f) / (float)side, 0);
            }
            wgf_actor_set_parent(b.sprites[i], b.stage);
        }
    }
    b.frame = 0;
    b.frame_sum = 0.0;
}

static void teardown(void)
{
    int i;
    if (b.which == CASE_EMITTER) {
        wgf_actor_destroy(b.emitter, WGF_ACTOR_DESTROY_CHILDREN);
    } else {
        for (i = 0; i < b.count; i++) wgf_actor_destroy(b.sprites[i], WGF_ACTOR_DESTROY_CHILDREN);
    }
    b.count = 0;
}

/* Particles: each moved every frame, the oldest thrown again. */
static void update(float dt)
{
    int i;
    if (b.which != CASE_PARTICLES) return;
    for (i = 0; i < b.count; i++) {
        particle_t *p = &b.particles[i];
        if (++p->age >= PARTICLE_LIFE) throw_particle(p), p->age = 0;
        p->vy += 300.0f * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        wgf_actor_set_position(b.sprites[i], p->x, p->y, 0);
    }
}

static void init(void *user)
{
    static const char *const paths[4] = {"textures/particle.png", "textures/flame.png", "textures/tiles.png",
                                         "textures/tiles_normal.png"};
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(0x101420FFu);
    b.stage = wgf_stage2d_create();
    for (i = 0; i < 4; i++) b.textures[i] = wgf_texture_create(paths[i]);
}

static void frame(void *user)
{
    char text[64];
    int i;
    (void)user;
    if (b.count == 0 && b.step == 0) { /* waiting for the textures */
        for (i = 0; i < 4; i++) {
            if (wgf_resource_get_status(b.textures[i]) == WGF_RESOURCE_STATUS_PENDING) return;
        }
        setup();
    }
    if (b.frame == WARMUP_FRAMES) {
        snprintf(text, sizeof(text), "spritebench %s %d", CASE_NAMES[b.which], b.count);
        mark(text);
    }
    if (b.frame > WARMUP_FRAMES) b.frame_sum += wgf_loop_get_frame_delta() * 1000.0;
    if (b.frame == WARMUP_FRAMES + MEASURE_FRAMES) {
        mark("spritebench rest");
        printf("spritebench: %s %d: %.2f ms a frame (paced)\n", CASE_NAMES[b.which], b.count,
               b.frame_sum / (MEASURE_FRAMES - 1));
        teardown();
        if (++b.step == CASES * COUNT_STEPS) {
            wgf_log_message(WGF_LOG_LEVEL_INFO, "spritebench: done");
            if (wgf_app_can_quit()) wgf_app_quit();
            return;
        }
        setup();
        return;
    }
    if (b.step >= CASES * COUNT_STEPS) return;
    update(wgf_loop_get_frame_delta());
    wgf_stage2d_draw(b.stage);
    b.frame++;
}

int main(void)
{
    wgf_window_set_title("libwgf spritebench");
    wgf_window_set_size(SCREEN_W, SCREEN_H);
    wgf_window_set_vsync(false);
    wgf_window_set_high_dpi(false);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
