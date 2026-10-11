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
 * stage and a 2D one. The frame update is the parts' (wgf_core_priv_part_update), as the
 * runtime runs it. Pixels: wgf_gfx_emitter_web_test. */

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
        expect(wgf_emitter3d_set_velocity(e, 0, 10, 0, 0.3f, 0.2f) && wgf_emitter3d_get_spread(e) == 0.3f, "a velocity");
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
    expect(wgf_emitter2d_set_velocity(flat, 100, 0, 0.5f, 0) && wgf_emitter2d_get_velocity(flat).x == 100, "2D velocity");
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
               wgf_emitter3d_get_color_key_value(e, 0) == WGF_COLOR_RED && wgf_emitter3d_get_color_key_time(e, 0) == 0.3f,
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
