#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_canvas.h"
#include "wgf_collider.h"
#include "wgf_core_priv.h"
#include "wgf_ecs.h"
#include "wgf_entity.h"
#include "wgf_model.h"
#include "wgf_stage.h"
#include "wgf_handle.h"
#include "wgf_lifetime.h"
#include "wgf_motion.h"
#include "wgf_node.h"
#include "wgf_shape2d.h"
#include "wgf_voice.h"

/* Entities, headless: made and destroyed with their nodes (and the entities under them),
 * names, the transform and its bulk calls, every component added with its defaults and
 * removed, the component nodes and the voice, the refusals, and the lifecycle's events. */

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

/* The events waiting, taken: their count, and the kinds in order into `kinds`. */
static int take(int *kinds, wgf_entity_t *entities, int most)
{
    int out[3 * 64], n, i;
    n = wgf_ecs_take_events(out, 3 * (most < 64 ? most : 64)) / 3;
    for (i = 0; i < n; i++) {
        kinds[i] = out[3 * i];
        entities[i] = (wgf_entity_t)out[3 * i + 1];
    }
    return n;
}

int main(void)
{
    wgf_node_t canvas;
    wgf_entity_t e, child, other;
    int kinds[64];
    wgf_entity_t who[64];

    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "gfx");
    canvas = wgf_canvas_create();
    expect(wgf_ecs_dump()[0] == '\0' && wgf_entity_get_count() == 0, "before the first entity: nothing");

    /* made, and its node */
    e = wgf_entity_create(canvas);
    expect(e != 0 && wgf_entity_is_alive(e) && wgf_entity_get_count() == 1, "an entity");
    expect(strcmp(wgf_handle_get_kind_name(e), "ecs.entity") == 0, "of kind ecs.entity");
    expect(wgf_node_get_parent(wgf_entity_get_node(e)) == canvas, "its node under the parent given");
    expect(wgf_entity_create(12345) == 0, "a parent that isn't a node is refused");
    other = wgf_entity_create(0);
    expect(other != 0 && wgf_node_get_parent(wgf_entity_get_node(other)) == 0, "or none at all");

    /* names */
    expect(wgf_entity_set_name(e, "ship") && strcmp(wgf_entity_get_name(e), "ship") == 0, "a name");
    expect(wgf_entity_find("ship") == e && wgf_entity_find("nobody") == 0, "found by it");
    expect(!wgf_entity_set_name(e, "0123456789012345678901234567890123456789012345678901234567890123"),
           "a name of 64 bytes is refused");
    expect(strcmp(wgf_node_get_name(wgf_entity_get_node(e)), "ship") == 0, "its node takes the name too");

    /* the transform */
    expect(near3(wgf_entity_get_scale(e), 1, 1, 1) && near3(wgf_entity_get_position(e), 0, 0, 0), "defaults");
    expect(wgf_entity_set_position(e, 1, 2, 3) && near3(wgf_entity_get_position(e), 1, 2, 3), "a position");
    expect(wgf_entity_set_rotation(e, 0, 0, 1.5f) && near3(wgf_entity_get_rotation(e), 0, 0, 1.5f), "a rotation");
    expect(wgf_entity_set_scale(e, 2, 2, 1) && near3(wgf_entity_get_scale(e), 2, 2, 1), "a scale");
    expect(wgf_entity_set_transform(e, 4, 5, 6, 0, 0, 0.5f, 1, 1, 1) && near3(wgf_entity_get_position(e), 4, 5, 6) &&
               near3(wgf_entity_get_rotation(e), 0, 0, 0.5f),
           "all at once");
    expect(!wgf_entity_set_position(e, NAN, 0, 0) && near3(wgf_entity_get_position(e), 4, 5, 6),
           "a position that isn't finite is refused");
    expect(wgf_entity_snap(e) && !wgf_entity_snap(12345), "snapped");
    {
        const wgf_entity_t both[3] = {e, other, 12345};
        const float positions[9] = {10, 11, 12, 20, 21, 22, 30, 31, 32};
        float out[9] = {0};
        expect(wgf_entity_set_positions(both, 3, positions, 9) && near3(wgf_entity_get_position(other), 20, 21, 22),
               "positions set in bulk, a handle that isn't an entity skipped");
        expect(wgf_entity_get_positions(both, 3, out, 9) == 9 && out[0] == 10 && out[5] == 22 && out[6] == 0,
               "read in bulk, a handle that isn't an entity as 0, 0, 0");
        expect(wgf_entity_get_positions(both, 3, out, 7) == 6, "as many whole positions as fit");
        expect(!wgf_entity_set_positions(both, 3, positions, 8), "too few floats for them is refused");
    }

    /* components, with their defaults */
    expect(!wgf_entity_has_component(e, WGF_COMPONENT_MOTION) && wgf_entity_add_component(e, WGF_COMPONENT_MOTION) &&
               wgf_entity_has_component(e, WGF_COMPONENT_MOTION),
           "motion added");
    expect(near3(wgf_motion_get_velocity(e), 0, 0, 0) && wgf_motion_get_damping(e) == 0 && wgf_motion_get_max_speed(e) == 0,
           "still, undamped, unlimited");
    wgf_motion_set_velocity(e, 5, 0, 0);
    expect(wgf_entity_add_component(e, WGF_COMPONENT_MOTION) && near3(wgf_motion_get_velocity(e), 5, 0, 0),
           "added again: kept as it is");
    expect(wgf_entity_add_component(e, WGF_COMPONENT_BOUNDS) && wgf_bounds_get_rect(e).z == 800 &&
               wgf_bounds_get_mode(e) == WGF_BOUNDS_MODE_WRAP,
           "bounds: 800 by 600, wrapping");
    expect(wgf_entity_add_component(e, WGF_COMPONENT_LIFETIME) && wgf_lifetime_get_seconds(e) == 1, "a second's life");
    expect(wgf_entity_add_component(e, WGF_COMPONENT_COLLIDER) && wgf_collider_get_radius(e) == 1 &&
               wgf_collider_get_layer(e) == 1 && wgf_collider_get_mask(e) == -1,
           "a collider of radius 1, layer 1, meeting everything");
    expect(wgf_entity_add_component(e, WGF_COMPONENT_SHAPE2D), "a shape");
    {
        const wgf_node_t shape = wgf_entity_get_component_node(e, WGF_COMPONENT_SHAPE2D);
        expect(wgf_node_get_type(shape) == WGF_NODE_TYPE_SHAPE2D && wgf_node_get_parent(shape) == wgf_entity_get_node(e),
               "a shape node under the entity's");
    }
    expect(wgf_entity_add_component(e, WGF_COMPONENT_SPRITE) && wgf_entity_add_component(e, WGF_COMPONENT_TEXT) &&
               wgf_entity_add_component(e, WGF_COMPONENT_EMITTER2D),
           "a sprite, text, an emitter");
    expect(wgf_node_get_type(wgf_entity_get_component_node(e, WGF_COMPONENT_SPRITE)) == WGF_NODE_TYPE_SPRITE &&
               wgf_node_get_type(wgf_entity_get_component_node(e, WGF_COMPONENT_TEXT)) == WGF_NODE_TYPE_TEXT &&
               wgf_node_get_type(wgf_entity_get_component_node(e, WGF_COMPONENT_EMITTER2D)) == WGF_NODE_TYPE_EMITTER2D,
           "each its node's type");
    /* what it draws, hidden and shown */
    expect(wgf_entity_is_visible(e) && wgf_entity_set_visible(e, false) && !wgf_entity_is_visible(e), "hidden");
    expect(!wgf_node_is_visible(wgf_entity_get_component_node(e, WGF_COMPONENT_SHAPE2D)) &&
               !wgf_node_is_visible(wgf_entity_get_component_node(e, WGF_COMPONENT_EMITTER2D)) &&
               wgf_node_is_visible(wgf_entity_get_node(e)) && wgf_node_is_enabled(wgf_entity_get_node(e)),
           "hidden: each component node not drawn; the entity's node and its updates as they were");
    {
        const wgf_entity_t later = wgf_entity_create(0);
        wgf_entity_set_visible(later, false);
        wgf_entity_add_component(later, WGF_COMPONENT_SPRITE);
        expect(!wgf_node_is_visible(wgf_entity_get_component_node(later, WGF_COMPONENT_SPRITE)),
               "a component added to a hidden entity is hidden too");
        wgf_entity_destroy(later);
    }
    expect(wgf_entity_set_visible(e, true) &&
               wgf_node_is_visible(wgf_entity_get_component_node(e, WGF_COMPONENT_SHAPE2D)) &&
               wgf_node_is_visible(wgf_entity_get_component_node(e, WGF_COMPONENT_TEXT)),
           "shown again: each drawn");
    expect(!wgf_entity_set_visible(12345, false) && !wgf_entity_is_visible(12345), "not an entity: false");
    expect(!wgf_entity_add_component(e, WGF_COMPONENT_MODEL), "a model before any stage: refused (logged)");
    wgf_stage_create();
    expect(wgf_entity_add_component(e, WGF_COMPONENT_MODEL) &&
               wgf_node_get_type(wgf_entity_get_component_node(e, WGF_COMPONENT_MODEL)) == WGF_NODE_TYPE_MODEL &&
               wgf_model_get_mesh(wgf_entity_get_component_node(e, WGF_COMPONENT_MODEL)) == 0,
           "a model of no mesh yet");
    expect(wgf_entity_remove_component(e, WGF_COMPONENT_MODEL) && !wgf_entity_has_component(e, WGF_COMPONENT_MODEL),
           "and taken off");
    expect(wgf_entity_get_component_node(e, WGF_COMPONENT_MOTION) == 0, "no node for a data component");
    expect(wgf_entity_add_component(e, WGF_COMPONENT_VOICE) && wgf_entity_get_voice(e) != 0 &&
               wgf_voice_get_sound(wgf_entity_get_voice(e)) == 0,
           "a voice of no sound yet");
    expect(!wgf_entity_add_component(e, WGF_COMPONENT_NONE) && !wgf_entity_add_component(e, (wgf_component_t)99) &&
               !wgf_entity_add_component(12345, WGF_COMPONENT_MOTION),
           "a component that isn't one, or a handle that isn't an entity, is refused");

    /* the behavior's lifecycle */
    take(kinds, who, 64); /* nothing before */
    expect(wgf_entity_add_component(e, WGF_COMPONENT_BEHAVIOR), "a behavior");
    expect(wgf_ecs_get_event_count() == 1 && take(kinds, who, 64) == 1 && kinds[0] == WGF_ECS_EVENT_CREATED &&
               who[0] == e,
           "CREATED raised");
    expect(wgf_behavior_set_name(e, "Ship") && strcmp(wgf_behavior_get_name(e), "Ship") == 0, "named");
    expect(wgf_ecs_get_event_count() == 0, "naming raises nothing");

    /* removed */
    {
        const wgf_node_t text = wgf_entity_get_component_node(e, WGF_COMPONENT_TEXT);
        expect(wgf_entity_remove_component(e, WGF_COMPONENT_TEXT) && wgf_node_get_type(text) == WGF_NODE_TYPE_NONE &&
                   !wgf_entity_has_component(e, WGF_COMPONENT_TEXT),
               "a node component removed: its node destroyed");
        expect(!wgf_entity_remove_component(e, WGF_COMPONENT_TEXT), "removing one it hasn't is refused");
        expect(wgf_entity_remove_component(e, WGF_COMPONENT_LIFETIME) && wgf_lifetime_get_seconds(e) == 0 &&
                   !wgf_lifetime_set_seconds(e, 3),
               "a data component removed: its calls refuse");
    }

    /* destroyed: with what is under its node */
    child = wgf_entity_create(wgf_entity_get_node(e));
    expect(wgf_node_get_parent(wgf_entity_get_node(child)) == wgf_entity_get_node(e), "an entity under another's node");
    {
        const wgf_node_t node = wgf_entity_get_node(e), shape = wgf_entity_get_component_node(e, WGF_COMPONENT_SHAPE2D);
        expect(wgf_entity_destroy(e) && !wgf_entity_is_alive(e), "destroyed");
        expect(!wgf_entity_is_alive(child), "the entity under its node with it");
        expect(wgf_node_get_type(node) == WGF_NODE_TYPE_NONE && wgf_node_get_type(shape) == WGF_NODE_TYPE_NONE,
               "and its nodes");
    }
    expect(take(kinds, who, 64) == 1 && kinds[0] == WGF_ECS_EVENT_DESTROYED && who[0] == e,
           "DESTROYED raised for the one with a behavior, its handle stale");
    expect(!wgf_entity_destroy(e) && !wgf_motion_set_velocity(e, 1, 0, 0) && wgf_entity_get_node(e) == 0,
           "a stale handle is refused");
    expect(wgf_entity_get_count() == 1, "one left");

    /* clearing */
    wgf_entity_create(canvas);
    wgf_entity_create(canvas);
    wgf_ecs_clear();
    expect(wgf_entity_get_count() == 0 && !wgf_entity_is_alive(other), "cleared");

    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
