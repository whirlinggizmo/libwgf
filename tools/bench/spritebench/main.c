#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_emitter2d.h"
#include "wgf_emitter3d.h"
#include "wgf_log.h"
#include "wgf_loop.h"
#include "wgf_random.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
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
 *   field 3d           sprites scattered on the ground of a 3D stage under an orbiting
 *                      camera, facings mixed as a game would (camera-facing coins, upright
 *                      trees turning about Y, flat decals), blended and sorted back to front
 *                      each frame, from one atlas: the instanced batch's case
 *   field 3d, 4 tex    the same from 4 textures, sorted: neighbours in depth alike rarely
 *   field 3d, mask     the 4-texture field masked: depth-tested, so grouped by texture, not
 *                      sorted (4 draws)
 *   emitter 3d         a 3D emitter's particles, facing the camera
 *
 * Natively it runs too, and prints its own frame times. */

#define WARMUP_FRAMES 20
#define MEASURE_FRAMES 100
#define MAX_SPRITES 16384
#define PARTICLE_LIFE 120 /* frames */
#define SCREEN_W 960
#define SCREEN_H 600

typedef enum {
    CASE_GRID,
    CASE_GRID_4,
    CASE_PARTICLES,
    CASE_EMITTER,
    CASE_FIELD,
    CASE_FIELD_4,
    CASE_FIELD_MASK,
    CASE_EMITTER_3D,
    CASES
} bench_case_t;

static const char *CASE_NAMES[CASES] = {"grid 2d",        "grid 2d, 4 tex",   "particles 2d", "emitter 2d",
                                        "field 3d",       "field 3d, 4 tex",  "field 3d, mask", "emitter 3d"};
enum { COUNT_STEPS = 3 };
static const int SPRITE_COUNTS[COUNT_STEPS] = {1000, 4000, 16000};

typedef struct particle_t {
    float x, y, vx, vy;
    int age;
} particle_t;

static struct {
    wgf_actor_t stage, emitter;
    wgf_actor_t world, camera; /* the 3D cases' */
    float radius, angle;
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

/* A field sprite, scattered on the ground: a coin, a tree, or a decal (libwgt's). */
static void add_field_sprite(int i)
{
    const float r = b.radius * sqrtf(wgf_random_get_float()), angle = wgf_random_get_float() * 6.2831853f;
    const float x = cosf(angle) * r, z = sinf(angle) * r;
    const bool mixed = b.which != CASE_FIELD;
    const wgf_actor_t sprite = wgf_sprite_create(mixed ? b.textures[i % 4] : b.textures[2]);
    if (sprite == 0) return;
    switch (i % 3) {
        case 0: /* a coin: faces the camera */
            wgf_sprite_set_facing(sprite, WGF_SPRITE_FACING_CAMERA);
            if (!mixed) wgf_sprite_set_source(sprite, 32, 16, 16, 16);
            wgf_actor_set_position(sprite, x, 0.6f, z);
            wgf_sprite_set_size(sprite, 0.6f, 0.6f);
            break;
        case 1: /* a tree: upright, turns about Y, stands on its bottom edge */
            wgf_sprite_set_facing(sprite, WGF_SPRITE_FACING_CAMERA_FIXED_Y);
            if (!mixed) wgf_sprite_set_source(sprite, 0, 16, 16, 32);
            wgf_sprite_set_size(sprite, 1, 2);
            wgf_sprite_set_pivot(sprite, 0.5f, 1);
            wgf_actor_set_position(sprite, x, 0, z);
            break;
        default: /* a decal flat on the ground */
            wgf_sprite_set_facing(sprite, WGF_SPRITE_FACING_Y_UP);
            if (!mixed) wgf_sprite_set_source(sprite, 0, 0, 16, 16);
            wgf_sprite_set_size(sprite, 1, 1);
            wgf_actor_set_position(sprite, x, 0.01f, z);
            break;
    }
    if (b.which == CASE_FIELD_MASK) wgf_sprite_set_alpha_mode(sprite, WGF_ALPHA_MODE_MASK, 0.5f);
    b.sprites[i] = sprite;
    wgf_actor_set_parent(sprite, b.world);
}

static void setup(void)
{
    const bench_case_t which = (bench_case_t)(b.step / COUNT_STEPS);
    const int count = SPRITE_COUNTS[b.step % COUNT_STEPS];
    const int side = (int)ceilf(sqrtf((float)count));
    int i;
    b.which = which;
    b.count = count;
    b.radius = sqrtf((float)count) * 0.6f;
    if (which == CASE_FIELD || which == CASE_FIELD_4 || which == CASE_FIELD_MASK) {
        wgf_random_set_seed(12345);
        for (i = 0; i < count; i++) add_field_sprite(i);
    } else if (which == CASE_EMITTER_3D) {
        b.emitter = wgf_emitter3d_create();
        wgf_emitter3d_set_texture(b.emitter, b.textures[0]);
        wgf_emitter3d_set_max(b.emitter, count);
        wgf_emitter3d_set_rate(b.emitter, (float)count / 2.0f);
        wgf_emitter3d_set_life(b.emitter, 2.0f, 2.0f);
        wgf_emitter3d_set_velocity(b.emitter, 0, 6, 0, 0.5f, 0.3f);
        wgf_emitter3d_set_gravity(b.emitter, 0, -6, 0);
        wgf_emitter3d_set_size(b.emitter, 0.15f, 0.15f, 0);
        wgf_emitter3d_set_alpha_mode(b.emitter, WGF_ALPHA_MODE_BLEND, 0.5f);
        wgf_actor_set_parent(b.emitter, b.world);
        wgf_emitter3d_prewarm(b.emitter, 2.0f);
    } else if (which == CASE_EMITTER) {
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
    if (b.which == CASE_EMITTER || b.which == CASE_EMITTER_3D) {
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
    if (b.which >= CASE_FIELD) { /* the camera orbits the field, so the sort changes every frame */
        const float distance = b.which == CASE_EMITTER_3D ? 12.0f : b.radius * 1.4f + 4.0f;
        b.angle += dt * 0.3f;
        wgf_actor_set_position(b.camera, cosf(b.angle) * distance, distance * 0.45f, sinf(b.angle) * distance);
        wgf_actor_look_at(b.camera, 0, b.which == CASE_EMITTER_3D ? 3.0f : 0.0f, 0, 0, 1, 0);
        return;
    }
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
                                         "sprites/logo/wg-logo-bw-alpha.png"};
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(0x101420FFu);
    b.stage = wgf_stage2d_create();
    b.world = wgf_stage3d_create();
    b.camera = wgf_camera3d_create();
    wgf_actor_set_parent(b.camera, b.world);
    wgf_stage3d_set_camera(b.world, b.camera);
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
    if (b.which >= CASE_FIELD) {
        wgf_stage3d_draw(b.world);
    } else {
        wgf_stage2d_draw(b.stage);
    }
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
