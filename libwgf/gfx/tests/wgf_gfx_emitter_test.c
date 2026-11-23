#include <math.h>
#include <stdio.h>

#include "emitter/wgf_gfx_emitter_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_emitter2d.h"
#include "wgf_emitter3d.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_random.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"

/* Particle emitters on sokol's dummy backend (a headless build), libwgt's emitter test
 * (wgrender's): spawning by rate and burst, lives, the ring's limit, seeds, the velocity
 * cone and fan, the clock's rebase, curves and palettes, prewarming, the spawn sphere and
 * circle, flipbooks, and moving -- the trail along a move, velocity inherited, a skipped
 * trail -- with the position its actor's; and libwgf's own: velocity turned with the actor,
 * the default seed drawn from wgf_random, ending, lingering, and an emitter drawn on a 3D
 * stage and a 2D one. helix-raylib's: wander, orbit, pulse, shells, local space, and
 * alignment to motion, their particles worked out as vs_particle does
 * (wgf_gfx_priv_emitter_particle_now). The frame update is the parts'
 * (wgf_core_priv_part_update), as the runtime runs it. Pixels: wgf_gfx_emitter_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void update(float dt)
{
    wgf_core_priv_part_update(dt);
}

static void test_spawning(void)
{
    const wgf_actor_t e = wgf_emitter3d_create();
    expect(e != 0 && wgf_actor_get_kind(e) == WGF_ACTOR_KIND_EMITTER3D, "an emitter node");
    expect(wgf_emitter3d_is_emitting(e) && wgf_emitter3d_get_count(e) == 0, "emitting, nothing yet");
    expect(wgf_emitter3d_get_max(e) == 1024 && wgf_emitter3d_get_alpha_mode(e) == WGF_ALPHA_MODE_ADD &&
               wgf_emitter3d_get_size_key_count(e) == 2 && wgf_emitter3d_get_size_key_value(e, 0) == 1.0f,
           "wgrender's defaults: 1024, added, size 1 to 1");

    /* a steady rate: 100 a second for half a second, each living 1 s */
    expect(wgf_emitter3d_set_rate(e, 100) && wgf_emitter3d_get_rate(e) == 100, "a rate");
    expect(wgf_emitter3d_set_life(e, 1, 1) && wgf_emitter3d_get_life_max(e) == 1, "a life");
    for (int i = 0; i < 10; i++) update(0.05f);
    expect(wgf_emitter3d_get_count(e) == 50, "50 in half a second");
    for (int i = 0; i < 20; i++) update(0.05f); /* 1.5 s in: the first 0.5 s have died */
    expect(wgf_emitter3d_get_count(e) >= 99 && wgf_emitter3d_get_count(e) <= 101, "the steady state");

    /* stop: the living finish their lives */
    expect(wgf_emitter3d_set_emitting(e, false), "stopped");
    update(0.5f);
    expect(wgf_emitter3d_get_count(e) > 0, "the living finish");
    update(0.6f);
    expect(wgf_emitter3d_get_count(e) == 0, "and then none");

    /* bursts, capped by max (the newest replace the oldest) */
    expect(wgf_emitter3d_burst(e, 30) && wgf_emitter3d_get_count(e) == 30, "a burst");
    expect(wgf_emitter3d_set_max(e, 64) && wgf_emitter3d_get_count(e) == 0, "a new max: emptied");
    expect(wgf_emitter3d_burst(e, 100) && wgf_emitter3d_get_count(e) == 64, "a burst, capped");
    expect(wgf_emitter3d_clear(e) && wgf_emitter3d_get_count(e) == 0, "cleared");

    /* a disabled node isn't ticked */
    expect(wgf_emitter3d_set_emitting(e, true), "emitting again");
    wgf_actor_set_enabled(e, false);
    update(0.5f);
    expect(wgf_emitter3d_get_count(e) == 0, "disabled: not ticked");
    wgf_actor_set_enabled(e, true);
    update(0.1f);
    expect(wgf_emitter3d_get_count(e) == 10, "enabled again: ticked");
    wgf_actor_set_paused(e, true);
    update(0.5f);
    expect(wgf_emitter3d_get_count(e) == 10, "paused: held, its particles kept");
    wgf_actor_set_paused(e, false);

    /* refused */
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(!wgf_emitter3d_set_life(e, 0, 1) && !wgf_emitter3d_set_life(e, 2, 1), "a bad life");
    expect(!wgf_emitter3d_set_max(e, 0) && !wgf_emitter3d_set_rate(e, -1), "a bad max or rate");
    expect(!wgf_emitter3d_set_alpha_mode(e, (wgf_alpha_mode_t)9, 0), "a bad alpha mode");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    expect(!wgf_emitter2d_set_rate(e, 10), "a 3D emitter isn't a 2D one");
    wgf_actor_destroy(e, WGF_ACTOR_DESTROY_CHILDREN);
    expect(!wgf_emitter3d_set_rate(e, 10), "gone");
}

static void test_particles(void)
{
    float born_a[4], motion_a[4], born_b[4], motion_b[4], shape[4];
    bool same = true, in_cone = true, in_fan = true;
    const wgf_actor_t a = wgf_emitter3d_create(), b = wgf_emitter3d_create(), flat = wgf_emitter2d_create();

    /* the same seed and settings: the same particles */
    for (int k = 0; k < 2; k++) {
        const wgf_actor_t e = k == 0 ? a : b;
        expect(wgf_emitter3d_set_seed(e, 42) && wgf_emitter3d_get_seed(e) == 42, "a seed");
        wgf_actor_set_position(e, 1, 2, 3);
        expect(wgf_emitter3d_set_spawn_box(e, 0.5f, 0, 0), "a spawn box");
        expect(wgf_emitter3d_set_velocity(e, 0, 10, 0, 0.3f, 0.2f) && wgf_emitter3d_get_spread(e) == 0.3f,
               "a velocity");
        expect(wgf_emitter3d_set_life(e, 1, 2), "a life");
        expect(wgf_emitter3d_burst(e, 200), "a burst, where the node is");
    }
    for (int i = 0; i < 200; i++) {
        float speed;
        expect(wgf_gfx_priv_emitter_particle(a, i, born_a, motion_a, shape) &&
                   wgf_gfx_priv_emitter_particle(b, i, born_b, motion_b, shape),
               "a particle");
        for (int c = 0; c < 4; c++) same = same && born_a[c] == born_b[c] && motion_a[c] == motion_b[c];
        /* within the box, the cone (0.3 rad around +y), the speed, and the life */
        speed = sqrtf(motion_a[0] * motion_a[0] + motion_a[1] * motion_a[1] + motion_a[2] * motion_a[2]);
        in_cone = in_cone && fabsf(born_a[0] - 1) <= 0.5f && born_a[1] == 2 && born_a[2] == 3 && speed >= 8 - 1e-3f &&
                  speed <= 12 + 1e-3f && motion_a[1] / speed >= cosf(0.3f) - 1e-4f && motion_a[3] >= 1 &&
                  motion_a[3] <= 2;
    }
    expect(same, "the same seed: the same particles");
    expect(in_cone, "within the box, the cone, the speed, and the life");
    expect(!wgf_gfx_priv_emitter_particle(a, 200, born_a, motion_a, shape), "no more than were born");

    /* 2D: turned up to `spread` either way, in the screen's plane */
    expect(wgf_emitter2d_set_velocity(flat, 100, 0, 0.5f, 0) && wgf_emitter2d_get_velocity(flat).x == 100,
           "2D velocity");
    expect(wgf_emitter2d_burst(flat, 100), "a 2D burst");
    for (int i = 0; i < 100; i++) {
        wgf_gfx_priv_emitter_particle(flat, i, born_a, motion_a, shape);
        in_fan = in_fan && motion_a[2] == 0 && fabsf(atan2f(motion_a[1], motion_a[0])) <= 0.5f + 1e-4f;
    }
    expect(in_fan, "a fan, in the plane");
    expect(!wgf_emitter3d_set_rate(flat, 10), "a 2D emitter isn't a 3D one");

    /* the clock rebases (the shader's time is a float): particles keep their ages */
    expect(wgf_emitter3d_set_life(b, 8000, 8000) && wgf_emitter3d_clear(b) && wgf_emitter3d_burst(b, 3), "long lives");
    update(3000.0f);
    update(3000.0f); /* past 4096: rebased, 6000 s old of 8000 */
    expect(wgf_emitter3d_get_count(b) == 3, "rebased: still alive");
    update(3000.0f); /* 9000 > 8000 */
    expect(wgf_emitter3d_get_count(b) == 0, "and then not");
    wgf_actor_destroy(a, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(b, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Moving emitters: steady spawns spread along the way, velocity inherited from the
 * move, a skipped trail and the first position aren't moves. */
static void test_motion(void)
{
    float born[4], motion[4], shape[4];
    bool along = true, inherited = true, jumped = true;
    const wgf_actor_t parent = wgf_actor_create(), e = wgf_emitter3d_create();
    wgf_actor_set_parent(e, parent);
    expect(wgf_emitter3d_set_life(e, 10, 10), "long lives");
    wgf_actor_set_position(parent, 5, 0, 0); /* the first position: not a move; and its parent's */
    expect(wgf_emitter3d_set_rate(e, 100), "a rate");
    update(0.1f); /* 10 at x = 5 */
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle(e, i, born, motion, shape);
        jumped = jumped && born[0] == 5 && motion[0] == 0;
    }
    expect(jumped, "the first position isn't a move, and is its node's in the world");

    /* moved 10 in 0.1 s: the next 10 spread from 5 to 15, a tenth of the way each */
    expect(wgf_emitter3d_set_inherit_velocity(e, 0.5f), "inherit half");
    wgf_actor_set_position(parent, 15, 0, 0);
    update(0.1f);
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle(e, 10 + i, born, motion, shape);
        along = along && fabsf(born[0] - (5.0f + (float)(i + 1))) < 1e-4f;
        inherited = inherited && fabsf(motion[0] - 50.0f) < 1e-2f; /* half of 100 a second */
    }
    expect(along, "spread along the move");
    expect(inherited, "half the move's velocity");

    /* a skipped trail: no trail, nothing to inherit */
    jumped = true;
    expect(wgf_emitter3d_skip_trail(e), "skip the trail");
    wgf_actor_set_position(parent, 100, 0, 0);
    update(0.1f);
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle(e, 20 + i, born, motion, shape);
        jumped = jumped && born[0] == 100 && motion[0] == 0;
    }
    expect(jumped, "a skipped trail: all at the new place, nothing inherited");

    /* standing still: nothing inherited */
    update(0.1f);
    wgf_gfx_priv_emitter_particle(e, 30, born, motion, shape);
    expect(born[0] == 100 && motion[0] == 0, "standing still");

    /* uneven frames: the move was made over the last frame (0.1 s), not the next (0.02 s) */
    wgf_actor_set_position(parent, 110, 0, 0);
    update(0.02f);
    expect(wgf_emitter3d_get_count(e) == 42, "42 alive");
    wgf_gfx_priv_emitter_particle(e, 41, born, motion, shape);
    expect(fabsf(motion[0] - 50.0f) < 1e-2f, "half of 10 over 0.1 s"); /* the frame the move was made in */

    expect(wgf_emitter3d_set_drag(e, 2) && wgf_emitter3d_set_stretch(e, 0.05f), "drag and stretch");
    expect(!wgf_emitter3d_set_drag(e, -1) && !wgf_emitter3d_set_stretch(e, -1), "not negative");
    expect(!wgf_emitter2d_skip_trail(e), "a 3D emitter isn't a 2D one");
    wgf_actor_destroy(parent, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Curves over life: keys kept in order, bounded; the palette's pick spread. */
static void test_curves(void)
{
    float born[4], motion[4], shape[4];
    int picks[4] = {0};
    const wgf_actor_t e = wgf_emitter3d_create();
    expect(wgf_emitter3d_set_size(e, 2, 5, 0) && wgf_emitter3d_get_size_key_count(e) == 2 &&
               wgf_emitter3d_get_size_key_time(e, 0) == 0 && wgf_emitter3d_get_size_key_value(e, 0) == 2 &&
               wgf_emitter3d_get_size_key_time(e, 1) == 1 && wgf_emitter3d_get_size_key_value(e, 1) == 5,
           "two keys");

    /* added out of order: kept in order; the same time twice is a step, in added order */
    expect(wgf_emitter3d_clear_size_keys(e) && wgf_emitter3d_get_size_key_count(e) == 0, "cleared");
    expect(wgf_emitter3d_add_size_key(e, 1, 0) && wgf_emitter3d_add_size_key(e, 0, 1) &&
               wgf_emitter3d_add_size_key(e, 0.5f, 3) && wgf_emitter3d_add_size_key(e, 0.5f, 4),
           "four keys");
    expect(wgf_emitter3d_get_size_key_count(e) == 4 && wgf_emitter3d_get_size_key_value(e, 0) == 1 &&
               wgf_emitter3d_get_size_key_value(e, 1) == 3 && wgf_emitter3d_get_size_key_value(e, 2) == 4 &&
               wgf_emitter3d_get_size_key_value(e, 3) == 0 && wgf_emitter3d_get_size_key_time(e, 2) == 0.5f,
           "in time order, a step in added order");
    for (int i = 0; i < 4; i++) wgf_emitter3d_add_size_key(e, 0.9f, 1);
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(!wgf_emitter3d_add_size_key(e, 0.9f, 1), "8 at most");
    expect(!wgf_emitter3d_add_color_key(e, 1.5f, WGF_COLOR_RED), "outside the life");
    expect(!wgf_emitter3d_add_size_key(e, 0.5f, -1), "no negative size");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    expect(wgf_emitter3d_clear_color_keys(e) && wgf_emitter3d_add_color_key(e, 0.3f, WGF_COLOR_RED) &&
               wgf_emitter3d_get_color_key_value(e, 0) == WGF_COLOR_RED &&
               wgf_emitter3d_get_color_key_time(e, 0) == 0.3f,
           "a color key");

    /* the palette: 8 at most; particles carry a random pick, spread over it */
    for (int i = 0; i < 8; i++) wgf_emitter3d_add_palette_color(e, WGF_COLOR_RED);
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(!wgf_emitter3d_add_palette_color(e, WGF_COLOR_RED) && wgf_emitter3d_get_palette_count(e) == 8, "8 at most");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    expect(wgf_emitter3d_clear_palette(e) && wgf_emitter3d_get_palette_count(e) == 0, "cleared");
    expect(wgf_emitter3d_set_max(e, 4000) && wgf_emitter3d_burst(e, 4000), "4000");
    for (int i = 0; i < 4000; i++) {
        wgf_gfx_priv_emitter_particle(e, i, born, motion, shape);
        if (shape[3] >= 0 && shape[3] < 1) picks[(int)(shape[3] * 4)]++;
    }
    for (int k = 0; k < 4; k++) expect(picks[k] > 850 && picks[k] < 1150, "the pick spread evenly");
    wgf_actor_destroy(e, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Prewarming, the spawn sphere and circle, flipbook settings. */
static void test_start(void)
{
    float born[4], motion[4], shape[4];
    bool inside = true, filled = false;
    const wgf_actor_t e = wgf_emitter3d_create(), flat = wgf_emitter2d_create();

    /* as if running for 5 s at 100 a second, each living 1 s: about 100 alive at once */
    expect(wgf_emitter3d_set_rate(e, 100) && wgf_emitter3d_set_life(e, 1, 1), "a rate");
    expect(wgf_emitter3d_burst(e, 10) && wgf_emitter3d_prewarm(e, 5), "prewarmed");
    expect(wgf_emitter3d_get_count(e) >= 99 && wgf_emitter3d_get_count(e) <= 100, "the steady state, the burst gone");
    update(0.5f);
    expect(wgf_emitter3d_get_count(e) >= 99 && wgf_emitter3d_get_count(e) <= 101, "and on");
    expect(wgf_emitter3d_set_max(e, 50) && wgf_emitter3d_prewarm(e, 5) && wgf_emitter3d_get_count(e) == 50,
           "only as many as fit");
    expect(wgf_emitter3d_set_max(e, 1000) && wgf_emitter3d_prewarm(e, 0.25f) && wgf_emitter3d_get_count(e) >= 24 &&
               wgf_emitter3d_get_count(e) <= 25,
           "less time, fewer");
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(!wgf_emitter3d_prewarm(e, -1), "no negative time");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);

    /* the sphere: within its radius, and not only near the middle */
    wgf_actor_set_position(e, 1, 2, 3);
    expect(wgf_emitter3d_set_spawn_sphere(e, 2) && wgf_emitter3d_get_spawn_sphere(e) == 2 &&
               wgf_emitter3d_get_spawn_box(e).x == 0,
           "a sphere instead of the box");
    expect(wgf_emitter3d_burst(e, 500), "a burst");
    for (int i = 0; i < 500; i++) {
        float r;
        wgf_gfx_priv_emitter_particle(e, wgf_emitter3d_get_count(e) - 500 + i, born, motion, shape);
        r = sqrtf((born[0] - 1) * (born[0] - 1) + (born[1] - 2) * (born[1] - 2) + (born[2] - 3) * (born[2] - 3));
        inside = inside && r <= 2 + 1e-4f;
        filled = filled || r > 1.8f;
    }
    expect(inside && filled, "within the sphere, out to its edge");
    expect(!wgf_emitter3d_set_spawn_sphere(e, -1), "no negative radius");

    /* 2D: the circle stays in the screen's plane */
    expect(wgf_emitter2d_set_spawn_circle(flat, 10) && wgf_emitter2d_burst(flat, 100), "a circle");
    inside = true;
    for (int i = 0; i < 100; i++) {
        wgf_gfx_priv_emitter_particle(flat, i, born, motion, shape);
        inside = inside && born[2] == 0 && born[0] * born[0] + born[1] * born[1] <= 100 + 1e-3f;
    }
    expect(inside, "within the circle, in the plane");

    /* flipbooks */
    expect(wgf_emitter3d_set_frames(e, 4, 4, 0, 0) && wgf_emitter3d_get_frame_count(e) == 16, "all 16 frames");
    expect(wgf_emitter3d_set_frames(e, 4, 4, 13, 24) && wgf_emitter3d_get_frame_rate(e) == 24, "13 at 24 a second");
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(!wgf_emitter3d_set_frames(e, 4, 4, 17, 0) && !wgf_emitter3d_set_frames(e, 0, 4, 0, 0) &&
               !wgf_emitter3d_set_frames(e, 4, 4, 0, -1),
           "bad flipbooks");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    expect(!wgf_emitter2d_set_frames(e, 4, 4, 0, 0), "a 3D emitter isn't a 2D one");
    wgf_actor_destroy(e, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
}

/* libwgf's own: velocity turned with the actor; the default seed wgf_random's. */
static void test_turned(void)
{
    float born[4], motion[4], shape[4];
    const wgf_actor_t ship = wgf_actor_create(), flame = wgf_emitter2d_create();
    wgf_actor_t again;
    wgf_actor_set_parent(flame, ship);
    wgf_actor_set_rotation(ship, 0, 0, 1.5707963f); /* a quarter turn: +x becomes +y */
    expect(wgf_emitter2d_set_velocity(flame, -10, 0, 0, 0) && wgf_emitter2d_burst(flame, 1), "a burst backwards");
    expect(wgf_gfx_priv_emitter_particle(flame, 0, born, motion, shape) && fabsf(motion[0]) < 1e-3f &&
               fabsf(motion[1] + 10.0f) < 1e-3f,
           "turned as its actor is");
    wgf_actor_destroy(ship, WGF_ACTOR_DESTROY_CHILDREN);

    wgf_random_set_seed(7);
    again = wgf_emitter2d_create();
    {
        const int seed = wgf_emitter2d_get_seed(again);
        wgf_actor_destroy(again, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_random_set_seed(7);
        again = wgf_emitter2d_create();
        expect(wgf_emitter2d_get_seed(again) == seed, "the same wgf_random seed: the same emitter seed");
    }
    wgf_actor_destroy(again, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Ending, and lingering. */
static void test_ending(void)
{
    const wgf_actor_t stage = wgf_stage2d_create(), ship = wgf_actor_create();
    const wgf_actor_t boom = wgf_emitter2d_create(), flame = wgf_emitter2d_create();
    wgf_actor_t left;
    wgf_actor_set_parent(boom, stage);
    expect(wgf_emitter2d_set_ending(boom, true) && wgf_emitter2d_is_ending(boom), "ending");
    update(0.1f);
    expect(wgf_actor_get_kind(boom) == WGF_ACTOR_KIND_EMITTER2D, "no particle yet: waits for its first");
    expect(wgf_emitter2d_set_life(boom, 0.5f, 0.5f) && wgf_emitter2d_burst(boom, 10), "a burst");
    update(0.3f);
    expect(wgf_actor_get_kind(boom) == WGF_ACTOR_KIND_EMITTER2D, "alive while its particles are");
    update(0.3f);
    expect(wgf_actor_get_kind(boom) == WGF_ACTOR_KIND_NONE, "ended with its last particle");

    /* lingering: its ship destroyed, the flame's particles finish at the stage's top */
    wgf_actor_set_parent(ship, stage);
    wgf_actor_set_parent(flame, ship);
    wgf_actor_set_position(ship, 40, 30, 0);
    expect(wgf_emitter2d_set_lingering(flame, true) && wgf_emitter2d_is_lingering(flame), "lingering");
    expect(wgf_emitter2d_set_life(flame, 1, 1) && wgf_emitter2d_set_rate(flame, 100), "a rate");
    update(0.2f);
    wgf_actor_destroy(ship, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_actor_get_kind(flame) == WGF_ACTOR_KIND_NONE && wgf_actor_get_child_count(stage) == 0,
           "the ship and its flame gone");
    update(0.1f);
    left = wgf_actor_get_child(stage, 0);
    expect(wgf_actor_get_kind(left) == WGF_ACTOR_KIND_EMITTER2D && wgf_emitter2d_get_count(left) == 20 &&
               !wgf_emitter2d_is_emitting(left) && wgf_emitter2d_is_ending(left) &&
               fabsf(wgf_actor_get_position(left).x - 40.0f) < 1e-3f,
           "its particles at the stage's top, where it was, not emitting, ending");
    update(1.0f);
    expect(wgf_actor_get_kind(left) == WGF_ACTOR_KIND_NONE, "and gone with them");

    /* not lingering, or nothing alive: gone with its actor */
    {
        const wgf_actor_t quiet = wgf_emitter2d_create();
        wgf_actor_set_parent(quiet, stage);
        wgf_emitter2d_set_lingering(quiet, true);
        wgf_actor_destroy(quiet, WGF_ACTOR_DESTROY_CHILDREN);
        update(0.1f);
        expect(wgf_actor_get_child_count(stage) == 0, "nothing alive: nothing left");
    }
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Emitters drawn: 3D ones on a 3D stage and a 2D one on a 2D stage, each a draw, and none
 * hidden or on the other's stage. */
static void test_drawing(void)
{
    const wgf_actor_t world = wgf_stage3d_create(), camera = wgf_camera3d_create(), flat = wgf_stage2d_create();
    const wgf_actor_t fire = wgf_emitter3d_create(), smoke = wgf_emitter3d_create();
    const wgf_actor_t confetti = wgf_emitter2d_create();
    wgf_actor_set_position(camera, 0, 2, 10);
    wgf_actor_set_parent(camera, world);
    wgf_stage3d_set_camera(world, camera);
    wgf_emitter3d_set_alpha_mode(smoke, WGF_ALPHA_MODE_BLEND, 0);
    wgf_actor_set_parent(fire, world);
    wgf_actor_set_parent(smoke, world);
    wgf_actor_set_parent(confetti, flat);
    wgf_emitter3d_burst(fire, 50);
    wgf_emitter3d_burst(smoke, 50);
    wgf_emitter2d_burst(confetti, 50);
    wgf_platform_priv_set_size(64, 64);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(world);
    wgf_stage2d_draw(flat);
    wgf_gfx_priv_end_frame();
    expect(wgf_gfx_priv_emitter_get_draw_count() == 3, "a draw each");
    wgf_actor_set_visible(fire, false);
    wgf_actor_set_parent(confetti, world);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(world);
    wgf_stage2d_draw(flat);
    wgf_gfx_priv_end_frame();
    expect(wgf_gfx_priv_emitter_get_draw_count() == 1, "hidden, or a 2D one on a 3D stage: none");
    wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
}

/* The quad's axes before spin: a 2D stage's (right, and up the screen, -y) and a 3D
 * camera's looking down -z. */
static const wgf_vec3_t SCREEN_X = {1, 0, 0}, SCREEN_UP_2D = {0, -1, 0}, SCREEN_UP_3D = {0, 1, 0};

static float wrapped(float angle) /* to -PI..PI */
{
    while (angle > 3.14159265f) angle -= 6.28318531f;
    while (angle < -3.14159265f) angle += 6.28318531f;
    return angle;
}

/* The settings: defaults, read back, clamped or refused, and a 2D call on a 3D emitter. */
static void test_behavior_settings(void)
{
    const wgf_actor_t flat = wgf_emitter2d_create(), deep = wgf_emitter3d_create();
    expect(wgf_emitter2d_get_wander_distance(flat) == 0 && wgf_emitter2d_get_wander_rate(flat) == 1 &&
               wgf_emitter2d_get_orbit_min(flat) == 0 && wgf_emitter2d_get_orbit_max(flat) == 0 &&
               wgf_emitter2d_get_pulse_rate(flat) == 0 && wgf_emitter2d_get_pulse_depth(flat) == 1 &&
               !wgf_emitter2d_is_spawn_shell(flat) && !wgf_emitter2d_is_local_space(flat) &&
               !wgf_emitter2d_is_aligned_to_motion(flat),
           "2D defaults: no wander (1 a second), orbit, pulse (depth 1), shell, local space, or alignment");
    expect(wgf_emitter3d_get_wander_rate(deep) == 1 && wgf_emitter3d_get_pulse_depth(deep) == 1 &&
               !wgf_emitter3d_is_spawn_shell(deep) && !wgf_emitter3d_is_local_space(deep) &&
               !wgf_emitter3d_is_aligned_to_motion(deep),
           "3D defaults");
    expect(wgf_emitter2d_set_wander(flat, 12, 0.5f) && wgf_emitter2d_get_wander_distance(flat) == 12 &&
               wgf_emitter2d_get_wander_rate(flat) == 0.5f,
           "2D wander");
    expect(wgf_emitter3d_set_wander(deep, 0.25f, 3) && wgf_emitter3d_get_wander_distance(deep) == 0.25f &&
               wgf_emitter3d_get_wander_rate(deep) == 3,
           "3D wander");
    expect(wgf_emitter2d_set_orbit(flat, -1, 2) && wgf_emitter2d_get_orbit_min(flat) == -1 &&
               wgf_emitter2d_get_orbit_max(flat) == 2 && wgf_emitter3d_set_orbit(deep, 0.5f, 0.5f) &&
               wgf_emitter3d_get_orbit_min(deep) == 0.5f && wgf_emitter3d_get_orbit_max(deep) == 0.5f,
           "orbits");
    expect(wgf_emitter2d_set_pulse(flat, 3, 2) && wgf_emitter2d_get_pulse_rate(flat) == 3 &&
               wgf_emitter2d_get_pulse_depth(flat) == 1 && wgf_emitter3d_set_pulse(deep, 1, -1) &&
               wgf_emitter3d_get_pulse_depth(deep) == 0,
           "pulses, the depth held to 0..1");
    expect(wgf_emitter2d_set_spawn_shell(flat, true) && wgf_emitter2d_is_spawn_shell(flat) &&
               wgf_emitter2d_set_local_space(flat, true) && wgf_emitter2d_is_local_space(flat) &&
               wgf_emitter2d_set_aligned_to_motion(flat, true) && wgf_emitter2d_is_aligned_to_motion(flat) &&
               wgf_emitter3d_set_spawn_shell(deep, true) && wgf_emitter3d_is_spawn_shell(deep) &&
               wgf_emitter3d_set_local_space(deep, true) && wgf_emitter3d_is_local_space(deep) &&
               wgf_emitter3d_set_aligned_to_motion(deep, true) && wgf_emitter3d_is_aligned_to_motion(deep),
           "shell, local space, and alignment");
    expect(!wgf_emitter2d_set_wander(flat, -1, 1) && !wgf_emitter2d_set_wander(flat, 1, -1) &&
               !wgf_emitter3d_set_wander(deep, NAN, 1) && !wgf_emitter2d_set_pulse(flat, -1, 0.5f) &&
               !wgf_emitter3d_set_pulse(deep, NAN, 0.5f),
           "refused: a negative wander, course, or pulse rate");
    expect(wgf_emitter2d_get_wander_distance(flat) == 12 && wgf_emitter2d_get_pulse_rate(flat) == 3,
           "a refusal changes nothing");
    expect(!wgf_emitter2d_set_orbit(deep, 1, 1) && !wgf_emitter3d_set_local_space(flat, true) &&
               !wgf_emitter3d_is_spawn_shell(flat) && wgf_emitter3d_get_pulse_rate(flat) == 0,
           "a 2D call on a 3D emitter, and the other way, refused");
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
    expect(!wgf_emitter2d_set_wander(flat, 1, 1) && !wgf_emitter2d_is_aligned_to_motion(flat) &&
               wgf_emitter2d_get_orbit_max(flat) == 0,
           "gone: refused");
    wgf_actor_destroy(deep, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Wander: off the path by at most twice its distance an axis (its course's two waves),
 * starting on it, none across a 2D stage's depth, and the same for the same seed. */
static void test_wander(void)
{
    float now[6], again[6], born[4], motion[4], shape[4];
    bool bounded = true, flat = true, same = true, differs = false;
    float farthest = 0.0f;
    wgf_actor_t a = wgf_emitter2d_create(), b = wgf_emitter2d_create(), c = wgf_emitter2d_create();
    for (int k = 0; k < 3; k++) {
        const wgf_actor_t e = k == 0 ? a : k == 1 ? b : c;
        wgf_emitter2d_set_seed(e, k == 2 ? 8 : 7);
        wgf_emitter2d_set_life(e, 100, 100);
        wgf_emitter2d_set_spawn_box(e, 50, 50);
        expect(wgf_emitter2d_set_wander(e, 10, 1.5f), "a wander");
        wgf_emitter2d_burst(e, 40);
    }
    expect(wgf_gfx_priv_emitter_particle_now(a, 0, SCREEN_X, SCREEN_UP_2D, now) &&
               wgf_gfx_priv_emitter_particle(a, 0, born, motion, shape) && now[0] == born[0] && now[1] == born[1],
           "born on its path");
    for (int step = 0; step < 40; step++) {
        update(0.05f);
        for (int i = 0; i < 40; i++) {
            wgf_gfx_priv_emitter_particle(a, i, born, motion, shape);
            if (!wgf_gfx_priv_emitter_particle_now(a, i, SCREEN_X, SCREEN_UP_2D, now) ||
                !wgf_gfx_priv_emitter_particle_now(b, i, SCREEN_X, SCREEN_UP_2D, again)) {
                bounded = false;
                continue;
            }
            bounded = bounded && fabsf(now[0] - born[0]) <= 20.0f + 1e-3f && fabsf(now[1] - born[1]) <= 20.0f + 1e-3f;
            flat = flat && now[2] == 0.0f;
            same = same && now[0] == again[0] && now[1] == again[1];
            farthest = fmaxf(farthest, fabsf(now[0] - born[0]));
            wgf_gfx_priv_emitter_particle_now(c, i, SCREEN_X, SCREEN_UP_2D, again);
            differs = differs || now[0] != again[0];
        }
    }
    expect(bounded && farthest > 5.0f, "wandering, within twice its distance an axis");
    expect(flat, "a 2D particle wanders in the plane");
    expect(same, "the same seed: the same courses");
    expect(differs, "another seed: other courses");
    wgf_actor_destroy(a, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(b, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(c, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Orbit: the distance from the axis kept, the angle advancing at its speed: on a 2D stage
 * clockwise on screen about where the emitter was; in 3D about its velocity's line (up,
 * with none), counterclockwise seen from above. */
static void test_orbit(void)
{
    float now[6], born[4], motion[4], shape[4];
    bool kept = true, advanced = true, ranged = true;
    const wgf_actor_t flat = wgf_emitter2d_create(), deep = wgf_emitter3d_create(), range = wgf_emitter2d_create();
    wgf_actor_set_position(flat, 200, 100, 0);
    wgf_emitter2d_set_life(flat, 100, 100);
    wgf_emitter2d_set_spawn_circle(flat, 40);
    expect(wgf_emitter2d_set_orbit(flat, 2, 2), "a 2D orbit");
    wgf_emitter2d_burst(flat, 20);
    wgf_actor_set_position(deep, 1, 2, 3);
    wgf_emitter3d_set_life(deep, 100, 100);
    wgf_emitter3d_set_spawn_sphere(deep, 2);
    expect(wgf_emitter3d_set_orbit(deep, 0.5f, 0.5f), "a 3D orbit");
    wgf_emitter3d_burst(deep, 20);
    for (int step = 1; step <= 10; step++) {
        update(0.1f);
        for (int i = 0; i < 20; i++) {
            const float age = 0.1f * (float)step;
            float r0, r1, a0, a1;
            wgf_gfx_priv_emitter_particle(flat, i, born, motion, shape);
            wgf_gfx_priv_emitter_particle_now(flat, i, SCREEN_X, SCREEN_UP_2D, now);
            r0 = hypotf(born[0] - 200, born[1] - 100);
            r1 = hypotf(now[0] - 200, now[1] - 100);
            a0 = atan2f(born[1] - 100, born[0] - 200);
            a1 = atan2f(now[1] - 100, now[0] - 200);
            kept = kept && fabsf(r0 - r1) < 1e-3f;
            advanced = advanced && fabsf(wrapped(a1 - a0 - 2.0f * age)) < 1e-3f; /* y down: clockwise on screen */
            wgf_gfx_priv_emitter_particle(deep, i, born, motion, shape);
            wgf_gfx_priv_emitter_particle_now(deep, i, SCREEN_X, SCREEN_UP_3D, now);
            r0 = hypotf(born[0] - 1, born[2] - 3);
            r1 = hypotf(now[0] - 1, now[2] - 3);
            a0 = atan2f(-(born[2] - 3), born[0] - 1);
            a1 = atan2f(-(now[2] - 3), now[0] - 1);
            kept = kept && fabsf(r0 - r1) < 1e-3f && fabsf(now[1] - born[1]) < 1e-4f;
            advanced = advanced && fabsf(wrapped(a1 - a0 - 0.5f * age)) < 1e-3f;
        }
    }
    expect(kept, "the distance from the axis kept");
    expect(advanced, "the angle advancing at its speed");

    /* a range: each particle's speed within it */
    wgf_actor_set_position(range, 0, 0, 0);
    wgf_emitter2d_set_life(range, 100, 100);
    wgf_emitter2d_set_spawn_box(range, 10, 0);
    expect(wgf_emitter2d_set_spawn_shell(range, true) && wgf_emitter2d_set_orbit(range, 1, 3), "an orbit's range");
    wgf_emitter2d_burst(range, 50);
    update(0.25f);
    for (int i = 0; i < 50; i++) {
        float turn;
        wgf_gfx_priv_emitter_particle(range, i, born, motion, shape);
        wgf_gfx_priv_emitter_particle_now(range, i, SCREEN_X, SCREEN_UP_2D, now);
        turn = wrapped(atan2f(now[1], now[0]) - atan2f(born[1], born[0]));
        ranged = ranged && turn >= 0.25f - 1e-3f && turn <= 0.75f + 1e-3f;
    }
    expect(ranged, "each orbit's speed within its range");
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(deep, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(range, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Pulse: the alpha left at known points of a beat (its high, low, and between, on each
 * particle's own phase), one beat apart the same, the phases spread; none without one. */
static void test_pulse(void)
{
    float now[6];
    bool known = true, periodic = true, bounded = true;
    float lowest = 1.0f, highest = 0.0f, first = -1.0f;
    bool phases_differ = false;
    const wgf_actor_t e = wgf_emitter2d_create();
    wgf_gfx_priv_emitter_t *emitter = wgf_gfx_priv_emitter_of_at(e, true, NULL);
    wgf_emitter2d_set_life(e, 100, 100);
    wgf_emitter2d_burst(e, 30);
    update(0.1f);
    expect(wgf_gfx_priv_emitter_particle_now(e, 0, SCREEN_X, SCREEN_UP_2D, now) && now[5] == 1.0f,
           "no pulse: all alpha");
    expect(wgf_emitter2d_set_pulse(e, 2, 0.6f), "a pulse, two beats a second");
    for (int step = 0; step < 20; step++) {
        update(0.05f);
        for (int i = 0; i < 30; i++) {
            const wgf_gfx_priv_particle_t *p = &emitter->ring[i];
            const float age = emitter->time - p->born[3];
            float beat = 2.0f * age + p->axis[3], expected;
            beat -= floorf(beat);
            expected = 1.0f - 0.6f * (0.5f + 0.5f * sinf(6.28318531f * beat));
            wgf_gfx_priv_emitter_particle_now(e, i, SCREEN_X, SCREEN_UP_2D, now);
            known = known && fabsf(now[5] - expected) < 1e-4f;
            bounded = bounded && now[5] >= 0.4f - 1e-5f && now[5] <= 1.0f + 1e-5f;
            lowest = fminf(lowest, now[5]);
            highest = fmaxf(highest, now[5]);
            if (step == 0) {
                if (i == 0) first = p->axis[3];
                phases_differ = phases_differ || p->axis[3] != first;
            }
        }
    }
    expect(known, "the alpha left on each particle's phase");
    expect(bounded && lowest < 0.45f && highest > 0.95f, "from all of it down to 1 less its depth");
    expect(phases_differ, "a phase each");
    {
        /* a beat's high (a quarter before its low), its low, and its middle, at known ages */
        float values[3];
        const wgf_gfx_priv_particle_t *p = &emitter->ring[0];
        const float phase = p->axis[3];
        const float ages[3] = {0.75f, 0.25f, 0.0f}; /* in beats: sin's low (-1), high (+1), and 0 */
        for (int k = 0; k < 3; k++) {
            float target = ages[k] - phase; /* in beats from birth: where fract(beat) is ages[k] */
            target -= floorf(target);
            target += 10.0f; /* a while after birth, half a second a beat */
            emitter->time = p->born[3] + target / 2.0f;
            wgf_gfx_priv_emitter_particle_now(e, 0, SCREEN_X, SCREEN_UP_2D, now);
            values[k] = now[5];
            emitter->time = p->born[3] + target / 2.0f + 0.5f; /* a beat on */
            wgf_gfx_priv_emitter_particle_now(e, 0, SCREEN_X, SCREEN_UP_2D, now);
            periodic = periodic && fabsf(now[5] - values[k]) < 1e-3f;
        }
        expect(fabsf(values[0] - 1.0f) < 1e-3f && fabsf(values[1] - 0.4f) < 1e-3f && fabsf(values[2] - 0.7f) < 1e-3f,
               "all of it at a beat's high, 1 less its depth at its low, half its depth between");
        expect(periodic, "one beat on, the same");
    }
    wgf_actor_destroy(e, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Shells: born on the circle's and sphere's radius, and on a box's or rectangle's faces
 * (edges), never inside; without one, inside. */
static void test_shells(void)
{
    float born[4], motion[4], shape[4];
    bool circle = true, sphere = true, box = true, outline = true, inside = false;
    const wgf_actor_t ring = wgf_emitter2d_create(), ball = wgf_emitter3d_create(), crate = wgf_emitter3d_create(),
                      flat = wgf_emitter3d_create();
    wgf_actor_set_position(ring, 10, 20, 0);
    wgf_emitter2d_set_spawn_circle(ring, 30);
    expect(wgf_emitter2d_set_spawn_shell(ring, true), "a circle's shell");
    wgf_emitter2d_burst(ring, 200);
    wgf_emitter3d_set_spawn_sphere(ball, 2);
    wgf_emitter3d_set_spawn_shell(ball, true);
    wgf_emitter3d_burst(ball, 200);
    wgf_emitter3d_set_spawn_box(crate, 1, 2, 3);
    wgf_emitter3d_set_spawn_shell(crate, true);
    wgf_emitter3d_burst(crate, 200);
    wgf_emitter3d_set_spawn_box(flat, 1, 0, 3); /* a rectangle: its outline */
    wgf_emitter3d_set_spawn_shell(flat, true);
    wgf_emitter3d_burst(flat, 200);
    for (int i = 0; i < 200; i++) {
        float x, y, z;
        wgf_gfx_priv_emitter_particle(ring, i, born, motion, shape);
        circle = circle && fabsf(hypotf(born[0] - 10, born[1] - 20) - 30) < 1e-3f && born[2] == 0;
        wgf_gfx_priv_emitter_particle(ball, i, born, motion, shape);
        sphere = sphere && fabsf(sqrtf(born[0] * born[0] + born[1] * born[1] + born[2] * born[2]) - 2) < 1e-4f;
        wgf_gfx_priv_emitter_particle(crate, i, born, motion, shape);
        x = fabsf(born[0]), y = fabsf(born[1]), z = fabsf(born[2]);
        box = box && x <= 1 && y <= 2 && z <= 3 && (x == 1 || y == 2 || z == 3);
        wgf_gfx_priv_emitter_particle(flat, i, born, motion, shape);
        x = fabsf(born[0]), z = fabsf(born[2]);
        outline = outline && born[1] == 0 && x <= 1 && z <= 3 && (x == 1 || z == 3);
    }
    expect(circle, "on the circle");
    expect(sphere, "on the sphere");
    expect(box, "on the box's faces");
    expect(outline, "on the rectangle's outline");
    wgf_emitter2d_set_spawn_shell(ring, false);
    wgf_emitter2d_clear(ring);
    wgf_emitter2d_burst(ring, 50);
    for (int i = 0; i < 50; i++) {
        wgf_gfx_priv_emitter_particle(ring, i, born, motion, shape);
        inside = inside || hypotf(born[0] - 10, born[1] - 20) < 29;
    }
    expect(inside, "no shell: within");
    wgf_actor_destroy(ring, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(ball, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(crate, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Local space: particles follow a moved emitter (paused, they hold, as it does); in world
 * space they stay; and in local space no velocity is inherited. */
static void test_local_space(void)
{
    float now[6], born[4], motion[4], shape[4];
    bool followed = true, stayed = true, held = true, none_inherited = true;
    const wgf_actor_t local = wgf_emitter2d_create(), world = wgf_emitter2d_create();
    for (int k = 0; k < 2; k++) {
        const wgf_actor_t e = k == 0 ? local : world;
        wgf_actor_set_position(e, 100, 100, 0);
        wgf_emitter2d_set_life(e, 100, 100);
        wgf_emitter2d_set_spawn_circle(e, 20);
        wgf_emitter2d_set_inherit_velocity(e, 1);
        wgf_emitter2d_burst(e, 10);
    }
    expect(wgf_emitter2d_set_local_space(local, true), "local space");
    update(0.1f);
    wgf_actor_set_position(local, 150, 120, 0);
    wgf_actor_set_position(world, 150, 120, 0);
    update(0.1f);
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle(local, i, born, motion, shape);
        wgf_gfx_priv_emitter_particle_now(local, i, SCREEN_X, SCREEN_UP_2D, now);
        followed = followed && fabsf(now[0] - (born[0] + 50)) < 1e-3f && fabsf(now[1] - (born[1] + 20)) < 1e-3f;
        wgf_gfx_priv_emitter_particle(world, i, born, motion, shape);
        wgf_gfx_priv_emitter_particle_now(world, i, SCREEN_X, SCREEN_UP_2D, now);
        stayed = stayed && now[0] == born[0] && now[1] == born[1];
    }
    expect(followed, "local space: moved with the emitter");
    expect(stayed, "world space: left where they were");

    /* paused, it isn't moved on: they hold; then follow */
    wgf_actor_set_paused(local, true);
    wgf_actor_set_position(local, 300, 300, 0);
    update(0.1f);
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle(local, i, born, motion, shape);
        wgf_gfx_priv_emitter_particle_now(local, i, SCREEN_X, SCREEN_UP_2D, now);
        held = held && fabsf(now[0] - (born[0] + 50)) < 1e-3f;
    }
    expect(held, "paused: held");
    wgf_actor_set_paused(local, false);
    update(0.1f);
    wgf_gfx_priv_emitter_particle(local, 0, born, motion, shape);
    wgf_gfx_priv_emitter_particle_now(local, 0, SCREEN_X, SCREEN_UP_2D, now);
    expect(fabsf(now[0] - (born[0] + 200)) < 1e-3f, "resumed: following");

    /* moving while emitting: nothing inherited in local space; world space inherits */
    wgf_emitter2d_set_rate(local, 100);
    wgf_emitter2d_set_rate(world, 100);
    wgf_actor_set_position(local, 400, 300, 0);
    wgf_actor_set_position(world, 400, 300, 0);
    update(0.1f);
    update(0.1f);
    for (int i = 10; i < wgf_emitter2d_get_count(local); i++) {
        wgf_gfx_priv_emitter_particle(local, i, born, motion, shape);
        none_inherited = none_inherited && motion[0] == 0 && motion[1] == 0;
    }
    wgf_gfx_priv_emitter_particle(world, 10, born, motion, shape); /* born in the update after the move */
    expect(none_inherited && wgf_emitter2d_get_count(local) > 10, "local space: nothing inherited");
    expect(motion[0] > 0, "world space: inherited");
    wgf_actor_destroy(local, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Aligned to motion: the quad's top along its motion on screen, its spin added; standing
 * still, upright; not aligned, its random angle. */
static void test_aligned(void)
{
    float now[6], born[4], motion[4], shape[4];
    bool along = true, spun = true, still = true, unaligned = true;
    const wgf_actor_t thrown = wgf_emitter2d_create(), deep = wgf_emitter3d_create();
    wgf_emitter2d_set_life(thrown, 100, 100);
    wgf_emitter2d_set_velocity(thrown, 100, 0, 0.8f, 0.5f);
    wgf_emitter2d_set_gravity(thrown, 0, 200);
    expect(wgf_emitter2d_set_aligned_to_motion(thrown, true), "aligned");
    wgf_emitter2d_burst(thrown, 30);
    for (int step = 1; step <= 10; step++) {
        update(0.05f);
        for (int i = 0; i < 30; i++) {
            const float age = 0.05f * (float)step;
            float vx, vy;
            wgf_gfx_priv_emitter_particle(thrown, i, born, motion, shape);
            wgf_gfx_priv_emitter_particle_now(thrown, i, SCREEN_X, SCREEN_UP_2D, now);
            vx = motion[0];
            vy = motion[1] + 200.0f * age;
            along = along && fabsf(wrapped(now[4] - atan2f(vx, -vy))) < 1e-3f; /* up the screen is -y */
        }
    }
    expect(along, "its top along its motion: the velocity's angle");
    wgf_emitter2d_set_spin(thrown, 1, 1);
    wgf_emitter2d_clear(thrown);
    wgf_emitter2d_burst(thrown, 10);
    update(0.5f);
    for (int i = 0; i < 10; i++) {
        float vy;
        wgf_gfx_priv_emitter_particle(thrown, i, born, motion, shape);
        wgf_gfx_priv_emitter_particle_now(thrown, i, SCREEN_X, SCREEN_UP_2D, now);
        vy = motion[1] + 200.0f * 0.5f;
        spun = spun && fabsf(wrapped(now[4] - atan2f(motion[0], -vy) - 0.5f)) < 1e-3f;
    }
    expect(spun, "its spin added");

    /* 3D: across the screen as the camera sees it; along the view, upright */
    wgf_emitter3d_set_life(deep, 100, 100);
    wgf_emitter3d_set_aligned_to_motion(deep, true);
    wgf_emitter3d_set_velocity(deep, 0, 0, 5, 0, 0);
    wgf_emitter3d_burst(deep, 5);
    wgf_emitter3d_set_velocity(deep, 3, 4, 0, 0, 0);
    wgf_emitter3d_burst(deep, 5);
    update(0.01f);
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle_now(deep, i, SCREEN_X, SCREEN_UP_3D, now);
        if (i < 5) still = still && now[4] == 0.0f;
        else along = along && fabsf(now[4] - atan2f(3, 4)) < 1e-4f;
    }
    expect(still, "3D, moving along the view: upright");
    expect(along, "3D, across the screen: its top along");
    wgf_emitter3d_set_aligned_to_motion(deep, false);
    for (int i = 0; i < 10; i++) {
        wgf_gfx_priv_emitter_particle(deep, i, born, motion, shape);
        wgf_gfx_priv_emitter_particle_now(deep, i, SCREEN_X, SCREEN_UP_3D, now);
        unaligned = unaligned && fabsf(wrapped(now[4] - shape[2])) < 1e-4f;
    }
    expect(unaligned, "not aligned: its random angle");
    wgf_actor_destroy(thrown, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(deep, WGF_ACTOR_DESTROY_CHILDREN);
}

/* Every behavior at once, drawn: on a 2D stage and a 3D one. */
static void test_behaviors_drawn(void)
{
    const wgf_actor_t world = wgf_stage3d_create(), camera = wgf_camera3d_create(), flat = wgf_stage2d_create();
    const wgf_actor_t deep = wgf_emitter3d_create(), thin = wgf_emitter2d_create();
    wgf_actor_set_position(camera, 0, 2, 10);
    wgf_actor_set_parent(camera, world);
    wgf_stage3d_set_camera(world, camera);
    wgf_actor_set_parent(deep, world);
    wgf_actor_set_parent(thin, flat);
    wgf_emitter3d_set_wander(deep, 1, 1);
    wgf_emitter3d_set_orbit(deep, 1, 2);
    wgf_emitter3d_set_pulse(deep, 2, 1);
    wgf_emitter3d_set_spawn_sphere(deep, 1);
    wgf_emitter3d_set_spawn_shell(deep, true);
    wgf_emitter3d_set_local_space(deep, true);
    wgf_emitter3d_set_aligned_to_motion(deep, true);
    wgf_emitter2d_set_wander(thin, 5, 1);
    wgf_emitter2d_set_aligned_to_motion(thin, true);
    wgf_emitter2d_set_local_space(thin, true);
    wgf_emitter3d_burst(deep, 50);
    wgf_emitter2d_burst(thin, 50);
    update(0.1f);
    wgf_platform_priv_set_size(64, 64);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(world);
    wgf_stage2d_draw(flat);
    wgf_gfx_priv_end_frame();
    expect(wgf_gfx_priv_emitter_get_draw_count() == 2, "drawn with every behavior");
    wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
}

int main(void)
{
    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "setup");
    test_spawning();
    test_particles();
    test_motion();
    test_curves();
    test_start();
    test_turned();
    test_ending();
    test_drawing();
    test_behavior_settings();
    test_wander();
    test_orbit();
    test_pulse();
    test_shells();
    test_local_space();
    test_aligned();
    test_behaviors_drawn();
    {
        /* gfx stopping with an emitter alive frees it (asan) */
        const wgf_actor_t e = wgf_emitter3d_create();
        wgf_emitter3d_burst(e, 10);
    }
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
