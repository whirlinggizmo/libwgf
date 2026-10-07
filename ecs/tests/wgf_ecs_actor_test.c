#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_stage2d.h"
#include "wgf_collider.h"
#include "wgf_component.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_world.h"
#include "wgf_lifetime.h"
#include "wgf_motion.h"
#include "wgf_actor.h"
#include "wgf_shape2d.h"
#include "wgf_voice.h"

/* Components and behaviors on actors, headless: every component added with its defaults
 * and removed, on a plain actor and on a shape; several behaviors on an actor, their ids and
 * parameters; the actor simulated once it has one (drawn between its ticks, snapped);
 * positions in bulk; the refusals; and the lifecycle's events, an actor's going included. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near3(wgf_vec3_t v, float x, float y, float z)
{
    return fabsf(v.x - x) < 1e-5f && fabsf(v.y - y) < 1e-5f && fabsf(v.z - z) < 1e-5f;
}

/* The events waiting, taken: how many, and each one's four ints into `out`. */
static int take(int *out, int most)
{
    return wgf_world_take_events(out, 4 * most) / 4;
}

int main(void)
{
    wgf_actor_t stage, ship, shape, child;
    int events[4 * 64], rock, shield, second;

    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "gfx");
    stage = wgf_stage2d_create();
    expect(wgf_world_dump()[0] == '\0' && wgf_actor_get_count() == 0, "before the first component: nothing");

    /* components, with their defaults, on a plain actor */
    ship = wgf_actor_create();
    wgf_actor_set_parent(ship, stage);
    expect(!wgf_actor_has_component(ship, WGF_COMPONENT_MOTION) && wgf_actor_add_component(ship, WGF_COMPONENT_MOTION) &&
               wgf_actor_has_component(ship, WGF_COMPONENT_MOTION) && wgf_actor_get_count() == 1,
           "motion added: the actor counted");
    expect(near3(wgf_motion_get_velocity(ship), 0, 0, 0) && wgf_motion_get_damping(ship) == 0 &&
               wgf_motion_get_max_speed(ship) == 0,
           "still, undamped, unlimited");
    wgf_motion_set_velocity(ship, 5, 0, 0);
    expect(wgf_actor_add_component(ship, WGF_COMPONENT_MOTION) && near3(wgf_motion_get_velocity(ship), 5, 0, 0),
           "added again: kept as it is");
    expect(wgf_actor_add_component(ship, WGF_COMPONENT_BOUNDS) && wgf_bounds_get_rect(ship).z == 800 &&
               wgf_bounds_get_mode(ship) == WGF_BOUNDS_MODE_WRAP,
           "bounds: 800 by 600, wrapping");
    expect(wgf_actor_add_component(ship, WGF_COMPONENT_LIFETIME) && wgf_lifetime_get_seconds(ship) == 1,
           "a second's life");
    expect(wgf_actor_add_component(ship, WGF_COMPONENT_COLLIDER) && wgf_collider_get_radius(ship) == 1 &&
               wgf_collider_get_layer(ship) == 1 && wgf_collider_get_mask(ship) == -1,
           "a collider of radius 1, layer 1, meeting everything");
    expect(wgf_actor_add_component(ship, WGF_COMPONENT_VOICE) && wgf_actor_get_voice(ship) != 0 &&
               wgf_voice_get_sound(wgf_actor_get_voice(ship)) == 0,
           "a voice of no sound yet");
    expect(!wgf_actor_add_component(ship, WGF_COMPONENT_NONE) && !wgf_actor_add_component(ship, (wgf_component_t)99) &&
               !wgf_actor_add_component(12345, WGF_COMPONENT_MOTION) && !wgf_motion_set_velocity(stage, 1, 0, 0),
           "a component that isn't one, a handle that isn't an actor, or an actor without it: refused");
    expect(wgf_actor_remove_component(ship, WGF_COMPONENT_LIFETIME) && wgf_lifetime_get_seconds(ship) == 0 &&
               !wgf_lifetime_set_seconds(ship, 3) && !wgf_actor_remove_component(ship, WGF_COMPONENT_LIFETIME),
           "removed: its calls refuse, and removing it again is refused");

    /* on an actor of a kind: a shape is what it draws, and it moves as its components say */
    shape = wgf_shape2d_create();
    wgf_actor_set_parent(shape, stage);
    expect(wgf_actor_add_component(shape, WGF_COMPONENT_MOTION) && wgf_motion_set_velocity(shape, 0, 10, 0),
           "a shape with motion");

    /* simulated: set at the tick rate, drawn between the last two ticks */
    {
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(shape);
        wgf_mat4_t world;
        wgf_actor_set_position(shape, 0, 0, 0);
        expect(wgf_actor_snap(shape) && actor_ptr->previous != NULL, "simulated, and snapped");
        wgf_core_priv_part_tick_begin();
        wgf_core_priv_part_tick(0.5f); /* 10 a second for half a second */
        expect(near3(wgf_actor_get_position(shape), 0, 5, 0), "moved by its motion, at the tick");
        wgf_core_priv_part_set_fraction(0.5f);
        wgf_core_priv_part_update(0.0f);
        world = wgf_gfx_priv_actor_get_world_matrix(shape);
        expect(fabsf(world.m[13] - 2.5f) < 1e-4f, "drawn halfway between the two ticks");
        wgf_actor_snap(shape);
        world = wgf_gfx_priv_actor_get_world_matrix(shape);
        expect(fabsf(world.m[13] - 5.0f) < 1e-4f, "snapped: drawn where it is");
        wgf_core_priv_part_set_fraction(1.0f);
    }
    {
        const wgf_actor_t both[3] = {ship, shape, 12345};
        const float positions[9] = {10, 11, 12, 20, 21, 22, 30, 31, 32};
        float out[9] = {0};
        expect(wgf_actor_set_positions(both, 3, positions, 9) && near3(wgf_actor_get_position(shape), 20, 21, 22),
               "positions set in bulk, a handle that isn't an actor skipped");
        expect(wgf_actor_get_positions(both, 3, out, 9) == 9 && out[0] == 10 && out[5] == 22 && out[6] == 0,
               "read in bulk, a handle that isn't an actor as 0, 0, 0");
        expect(wgf_actor_get_positions(both, 3, out, 7) == 6, "as many whole positions as fit");
        expect(!wgf_actor_set_positions(both, 3, positions, 8), "too few floats for them is refused");
    }

    /* behaviors: several, two of one name, each its own parameters */
    take(events, 64);
    rock = wgf_actor_add_behavior(ship, "Ship");
    shield = wgf_actor_add_behavior(ship, "Shield");
    second = wgf_actor_add_behavior(ship, "Shield");
    expect(rock == 1 && shield == 2 && second == 3 && wgf_actor_get_behavior_count(ship) == 3,
           "three behaviors, ids from 1");
    expect(take(events, 64) == 3 && events[0] == WGF_WORLD_EVENT_CREATED && events[1] == (int)ship && events[2] == 1 &&
               events[10] == 3,
           "CREATED for each: the actor, the id");
    expect(wgf_actor_find_behavior(ship, "Shield") == shield && wgf_actor_find_behavior(ship, "Nobody") == 0 &&
               strcmp(wgf_behavior_get_name(ship, second), "Shield") == 0 && wgf_actor_get_behavior(ship, 2) == second,
           "found by name, named, in order");
    expect(wgf_behavior_set_param(ship, shield, "hits", "3") && wgf_behavior_set_param(ship, second, "hits", "5") &&
               wgf_behavior_get_param_number(ship, shield, "hits") == 3 &&
               wgf_behavior_get_param_number(ship, second, "hits") == 5 && !wgf_behavior_has_param(ship, rock, "hits"),
           "each its own parameters");
    expect(wgf_behavior_get_param_count(ship, shield) == 1 &&
               strcmp(wgf_behavior_get_param_key(ship, shield, 0), "hits") == 0 &&
               wgf_behavior_set_param(ship, shield, "hits", NULL) && wgf_behavior_get_param_count(ship, shield) == 0,
           "listed, and removed");
    expect(wgf_actor_add_behavior(ship, "") == 0 &&
               wgf_actor_add_behavior(ship, "0123456789012345678901234567890123456789012345678901234567890123") == 0 &&
               wgf_actor_add_behavior(12345, "Ship") == 0 && !wgf_behavior_set_param(ship, 99, "a", "b"),
           "an empty or overlong name, a handle that isn't an actor, an id it hasn't: refused");
    expect(wgf_actor_remove_behavior(ship, shield) && wgf_actor_get_behavior_count(ship) == 2 &&
               wgf_actor_get_behavior(ship, 1) == second && !wgf_actor_remove_behavior(ship, shield),
           "one removed, the others' ids kept");
    expect(take(events, 64) == 1 && events[0] == WGF_WORLD_EVENT_DESTROYED && events[2] == shield,
           "DESTROYED for it");
    expect(wgf_actor_add_behavior(ship, "Shield") == 4, "an id is never given again on the actor");
    expect(wgf_actor_count_with_behavior("Shield") == 1 && wgf_actor_count_with_behavior("Ship") == 1, "actors counted by behavior");
    {
        wgf_actor_t found[4];
        expect(wgf_actor_find_with_behavior("Shield", found, 4) == 1 && found[0] == ship, "and found");
        wgf_actor_add_behavior(shape, "Shield");
        expect(wgf_actor_find_with_behavior("Shield", found, 4) == 2 && found[0] == ship && found[1] == shape &&
                   wgf_actor_find_with_behavior("Shield", found, 1) == 1 && found[0] == ship,
               "oldest first, as many as fit");
        wgf_actor_remove_behavior(shape, wgf_actor_find_behavior(shape, "Shield"));
        expect(wgf_actor_count_with_behavior("Shield") == 1 && wgf_actor_count_with_behavior("Nobody") == 0 &&
                   wgf_actor_find_with_behavior("Nobody", found, 4) == 0 && wgf_actor_find_with_behavior(NULL, found, 4) == 0,
               "the last of a name removed: not found; a name never had: none");
        expect(wgf_actor_count_with_component(WGF_COMPONENT_MOTION) == 2 &&
                   wgf_actor_find_with_component(WGF_COMPONENT_MOTION, found, 4) == 2 && found[0] == ship &&
                   found[1] == shape,
               "found by component, oldest first");
        expect(wgf_actor_count_with_component(WGF_COMPONENT_VOICE) == 1 &&
                   wgf_actor_find_with_component(WGF_COMPONENT_VOICE, found, 4) == 1 && found[0] == ship &&
                   wgf_actor_count_with_component(WGF_COMPONENT_LIFETIME) == 0 &&
                   wgf_actor_find_with_component(WGF_COMPONENT_NONE, found, 4) == 0 &&
                   wgf_actor_count_with_component((wgf_component_t)99) == 0,
               "a voice too; a component none has, or none at all: none");
        wgf_actor_remove_component(ship, WGF_COMPONENT_VOICE);
        expect(wgf_actor_count_with_component(WGF_COMPONENT_VOICE) == 0, "a removed one isn't found");
    }

    /* destroyed: its components and behaviors with it, and what is under it */
    take(events, 64);
    child = wgf_actor_create();
    wgf_actor_set_parent(child, ship);
    wgf_actor_add_component(child, WGF_COMPONENT_MOTION);
    wgf_actor_add_behavior(child, "Flame");
    take(events, 64);
    wgf_actor_destroy(ship, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_actor_get_kind(ship) == WGF_ACTOR_KIND_NONE && wgf_actor_get_kind(child) == WGF_ACTOR_KIND_NONE &&
               wgf_actor_get_count() == 1,
           "destroyed, the actor under it with it; the shape left");
    expect(take(events, 64) == 4 && events[0] == WGF_WORLD_EVENT_DESTROYED && events[1] == (int)ship &&
               events[12] == WGF_WORLD_EVENT_DESTROYED && events[13] == (int)child,
           "DESTROYED for each of their behaviors, the handles stale");
    expect(!wgf_motion_set_velocity(ship, 1, 0, 0) && wgf_actor_get_behavior_count(ship) == 0, "a stale handle: refused");

    /* clearing */
    wgf_actor_add_component(wgf_actor_create(), WGF_COMPONENT_LIFETIME);
    wgf_world_clear();
    expect(wgf_actor_get_count() == 0 && wgf_actor_get_kind(shape) == WGF_ACTOR_KIND_NONE, "cleared: the actors destroyed");

    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
