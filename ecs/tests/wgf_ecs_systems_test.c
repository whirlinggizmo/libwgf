#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_stage2d.h"
#include "wgf_collider.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_ecs.h"
#include "wgf_component.h"
#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_lifetime.h"
#include "wgf_motion.h"
#include "wgf_actor.h"
#include "wgf_platform_priv.h"
#include "wgf_presentation.h"
#include "wgf_probe.h"

/* The systems, headless, ticked as app's runtime ticks them (core's part list): motion
 * with damping and a top speed, bounds wrapping, clamping and destroying, lifetimes,
 * colliders' enter and exit by layer and mask and parent, the probes, and the actors drawn
 * between the last two ticks -- a wrap drawn moving on, not across the screen. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static void step(float dt)
{
    wgf_core_priv_part_tick_begin();
    wgf_core_priv_part_tick(dt);
}

static void draw(float fraction)
{
    wgf_core_priv_part_set_fraction(fraction);
    wgf_core_priv_part_update(0.0f);
}

/* A plain actor under `parent`. */
static wgf_actor_t make(wgf_actor_t parent)
{
    const wgf_actor_t actor = wgf_actor_create();
    wgf_actor_set_parent(actor, parent);
    return actor;
}

/* Where it is drawn, across: its world matrix's, a child of a 2D stage at the origin. */
static float drawn_x(wgf_actor_t actor)
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(actor);
    return world.m[12];
}

static wgf_actor_t mover(wgf_actor_t parent, float x, float y)
{
    const wgf_actor_t e = make(parent);
    wgf_actor_set_position(e, x, y, 0);
    wgf_actor_add_component(e, WGF_COMPONENT_MOTION);
    return e;
}

/* The events waiting, and how many of `kind` for `actor` meeting `other`. */
static int count_events(int kind, wgf_actor_t actor, wgf_actor_t other)
{
    int out[3 * 64], n, i, found = 0;
    n = wgf_ecs_take_events(out, 3 * 64) / 3;
    for (i = 0; i < n; i++) {
        if (out[3 * i] == kind && (wgf_actor_t)out[3 * i + 1] == actor && (wgf_actor_t)out[3 * i + 2] == other)
            found++;
    }
    return found;
}

int main(void)
{
    wgf_actor_t stage, other_stage;
    wgf_actor_t e, a, b, c;

    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "gfx");
    stage = wgf_stage2d_create();
    other_stage = wgf_stage2d_create();

    /* motion */
    e = mover(stage, 0, 0);
    wgf_motion_set_velocity(e, 60, 0, 0);
    wgf_motion_set_spin(e, 0, 0, 1);
    step(0.5f);
    expect(near(wgf_actor_get_position(e).x, 30) && near(wgf_actor_get_rotation(e).z, 0.5f), "moved and turned");
    wgf_motion_set_max_speed(e, 10);
    step(1.0f);
    expect(near(wgf_motion_get_velocity(e).x, 10) && near(wgf_actor_get_position(e).x, 40), "held to its top speed");
    wgf_motion_set_max_speed(e, 0);
    expect(wgf_motion_set_damping(e, 2) && wgf_motion_get_damping(e) == 1, "damping clamped to 1");
    wgf_motion_set_damping(e, 0.75f);
    step(1.0f);
    expect(near(wgf_motion_get_velocity(e).x, 2.5f), "damped: a quarter kept a second");
    expect(!wgf_motion_set_velocity(make(stage), 1, 0, 0), "no motion: refused");
    wgf_ecs_clear();

    /* bounds: wrapping, drawn moving on */
    e = mover(stage, 95, 50);
    wgf_actor_add_component(e, WGF_COMPONENT_BOUNDS);
    expect(wgf_bounds_set_rect(e, 0, 0, 100, 100) && !wgf_bounds_set_rect(e, 0, 0, -1, 1), "a rectangle");
    wgf_motion_set_velocity(e, 10, 0, 0);
    step(1.0f);
    expect(near(wgf_actor_get_position(e).x, 5), "wrapped to the other side");
    draw(0.5f);
    expect(near(drawn_x(e), 0) ||
               near(drawn_x(e), 100),
           "drawn halfway: at the edge, not back across");
    draw(1.0f);
    expect(near(drawn_x(e), 5), "drawn at the tick: where it is");
    expect(wgf_bounds_set_margin(e, 10) && wgf_bounds_get_margin(e) == 10, "a margin");
    wgf_actor_set_position(e, -8, 50, 0);
    wgf_motion_set_velocity(e, 0, 0, 0);
    step(1.0f);
    expect(near(wgf_actor_get_position(e).x, -8), "within the margin: kept");

    /* clamping */
    expect(wgf_bounds_set_mode(e, WGF_BOUNDS_MODE_CLAMP) && !wgf_bounds_set_mode(e, (wgf_bounds_mode_t)7), "clamping");
    wgf_bounds_set_margin(e, 0);
    wgf_motion_set_velocity(e, -5, 3, 0);
    step(1.0f);
    expect(near(wgf_actor_get_position(e).x, 0) && near(wgf_motion_get_velocity(e).x, 0) &&
               near(wgf_motion_get_velocity(e).y, 3),
           "held at the edge, its velocity across stopped and along kept");

    /* destroying */
    /* the visible area as the rectangle: a fit 100 by 100 over a 200 by 100 framebuffer
       shows the design's 100 by 100, so a position past 100 wraps */
    wgf_bounds_set_mode(e, WGF_BOUNDS_MODE_WRAP);
    wgf_platform_priv_headless_set_framebuffer(200, 100);
    wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 100, 100);
    expect(!wgf_bounds_is_visible(e) && wgf_bounds_set_visible(e, true) && wgf_bounds_is_visible(e),
           "bounds following the visible area");
    expect(near(wgf_bounds_get_rect(e).z, 100) && near(wgf_bounds_get_rect(e).w, 100),
           "its rectangle, read back as it is now");
    wgf_actor_set_position(e, 130, 50, 0);
    step(0.0f);
    expect(near(wgf_actor_get_position(e).x, 30), "past the visible area's edge: wrapped there");
    wgf_presentation_set(WGF_PRESENTATION_MODE_EXPAND, 100, 100); /* wider than the design: -50 to 150 */
    wgf_actor_set_position(e, 130, 50, 0);
    step(0.0f);
    expect(near(wgf_actor_get_position(e).x, 130), "expanded past the design: no wrap until the window's edge");
    expect(wgf_bounds_set_rect(e, 0, 0, 100, 100) && !wgf_bounds_is_visible(e), "a rectangle of its own turns it off");
    wgf_presentation_set(WGF_PRESENTATION_MODE_NONE, 0, 0);

    wgf_bounds_set_mode(e, WGF_BOUNDS_MODE_DESTROY);
    wgf_actor_set_position(e, 200, 0, 0);
    step(0.0f);
    expect(wgf_actor_get_kind(e) == WGF_ACTOR_KIND_NONE, "outside: destroyed");

    /* lifetimes */
    e = make(stage);
    wgf_actor_add_component(e, WGF_COMPONENT_LIFETIME);
    expect(wgf_lifetime_set_seconds(e, 1) && !wgf_lifetime_set_seconds(e, -1) && !wgf_lifetime_set_seconds(e, NAN),
           "a lifetime");
    step(0.75f);
    expect(wgf_actor_get_kind(e) != WGF_ACTOR_KIND_NONE && near(wgf_lifetime_get_seconds(e), 0.25f), "counted down");
    step(0.25f);
    expect(wgf_actor_get_kind(e) == WGF_ACTOR_KIND_NONE, "at 0: destroyed");
    wgf_ecs_clear();

    /* colliders */
    a = make(stage);
    b = make(stage);
    c = make(other_stage);
    wgf_actor_add_component(a, WGF_COMPONENT_COLLIDER);
    wgf_actor_add_component(b, WGF_COMPONENT_COLLIDER);
    wgf_actor_add_component(c, WGF_COMPONENT_COLLIDER);
    wgf_collider_set_radius(a, 5);
    wgf_collider_set_radius(b, 5);
    expect(!wgf_collider_set_radius(a, -1), "a radius below 0 refused");
    wgf_actor_set_position(b, 20, 0, 0);
    wgf_ecs_take_events(NULL, 0);
    step(0.0f);
    {
        int out[64];
        expect(wgf_ecs_take_events(out, 64) == 0, "apart: nothing");
    }
    wgf_actor_set_position(b, 9, 0, 0);
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 2, "two events");
    {
        int out[6];
        expect(wgf_ecs_take_events(out, 6) == 6 &&
                   ((out[0] == WGF_ECS_EVENT_TRIGGER_ENTER && (wgf_actor_t)out[1] == a && (wgf_actor_t)out[2] == b) ||
                    (out[0] == WGF_ECS_EVENT_TRIGGER_ENTER && (wgf_actor_t)out[1] == b && (wgf_actor_t)out[2] == a)) &&
                   out[3] == WGF_ECS_EVENT_TRIGGER_ENTER && (wgf_actor_t)out[4] == (wgf_actor_t)out[2],
               "overlapping: entered, told to each");
    }
    {
        wgf_actor_t overlaps[4];
        expect(wgf_collider_get_overlaps(a, overlaps, 4) == 1 && overlaps[0] == b, "a overlaps b");
    }
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 0, "still overlapping: nothing new");
    wgf_actor_set_scale(b, 0.5f, 0.5f, 1);
    step(0.0f);
    expect(count_events(WGF_ECS_EVENT_TRIGGER_EXIT, a, b) == 1, "b scaled smaller, out of reach: exited");
    wgf_actor_set_scale(b, 1, 1, 1);
    wgf_collider_set_layer(b, 2);
    wgf_collider_set_mask(b, 2);
    wgf_collider_set_mask(a, 1);
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 0, "layers that don't meet: nothing");
    wgf_collider_set_mask(a, 2);
    step(0.0f);
    expect(count_events(WGF_ECS_EVENT_TRIGGER_ENTER, b, a) == 1, "a's mask meeting b's layer: entered");
    wgf_collider_set_mask(a, 0);
    wgf_collider_set_mask(b, 1);
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 0, "a's mask cleared, b's meeting a's layer: still met (either side)");
    expect(wgf_collider_is_enabled(a) && wgf_collider_set_enabled(a, false) && !wgf_collider_is_enabled(a),
           "a collider switched off");
    step(0.0f);
    expect(count_events(WGF_ECS_EVENT_TRIGGER_EXIT, a, b) == 1, "switched off: its pair ended");
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 0, "switched off: meets nothing, from either side");
    expect(wgf_collider_get_radius(a) == 5 && wgf_collider_get_layer(a) == 1 && wgf_collider_get_mask(a) == 0,
           "switched off: its settings kept");
    wgf_collider_set_enabled(a, true);
    step(0.0f);
    expect(count_events(WGF_ECS_EVENT_TRIGGER_ENTER, a, b) == 1, "switched on again: met as its settings say");
    expect(!wgf_collider_set_enabled(c + 999, false) && !wgf_collider_is_enabled(c + 999), "not a collider: false");
    wgf_actor_set_position(c, 0, 0, 0);
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 0, "under another parent: not compared");
    wgf_actor_destroy(b, WGF_ACTOR_DESTROY_CHILDREN);
    step(0.0f);
    expect(wgf_ecs_get_event_count() == 0, "one destroyed: its pairs forgotten, no exit");

    /* the probes */
    wgf_actor_add_behavior(a, "Rock");
    wgf_actor_add_behavior(a, "Rock"); /* two of a name: the actor counted once */
    step(0.0f);
    expect(wgf_probe_get_value("ecs.entities") == 2 && wgf_probe_get_value("ecs.behavior.Rock") == 1, "probes");
    wgf_actor_destroy(a, WGF_ACTOR_DESTROY_CHILDREN);
    step(0.0f);
    expect(wgf_probe_get_value("ecs.entities") == 1 && wgf_probe_has_value("ecs.behavior.Rock") &&
               wgf_probe_get_value("ecs.behavior.Rock") == 0,
           "a behavior gone: 0, not missing");

    /* interpolation */
    wgf_ecs_clear();
    e = mover(stage, 0, 0);
    wgf_motion_set_velocity(e, 10, 0, 0);
    wgf_actor_set_rotation(e, 0, 0, 3.0f);
    wgf_motion_set_spin(e, 0, 0, 0.5f);
    step(1.0f);
    draw(0.5f);
    expect(near(drawn_x(e), 5), "drawn halfway between the ticks");
    wgf_actor_snap(e);
    draw(0.5f);
    expect(near(drawn_x(e), 10), "snapped: drawn where it is");

    wgf_ecs_clear();
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(other_stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
