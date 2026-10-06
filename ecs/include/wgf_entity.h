#ifndef WGF_ENTITY_H
#define WGF_ENTITY_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_node.h"
#include "wgf_vec3.h"
#include "wgf_voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Entities: the simulation's things, each with a transform and components, moved on at
 * the tick rate by libwgf's systems (wgf_ecs.h), and drawn through a node.
 *
 * An entity's transform is its simulation's: set and read in ticks, never interpolated.
 * Its node is how it is drawn: each frame the node is given the entity's transform
 * interpolated between the last two ticks, so motion is smooth at any frame rate, and
 * what an entity shows -- a shape, a sprite, text, an emitter -- are nodes under it
 * (components of the node kinds, below). A move that shouldn't be smoothed (a wrap
 * across the screen, a respawn) is snapped. The transform is relative to the node's
 * parent, as a node's is.
 *
 * An entity is an object: owned by whoever created it, ended with wgf_entity_destroy,
 * which takes its node and its component nodes with it. Every call on a handle that
 * isn't a live entity does nothing, returning false or 0. */
typedef wgf_handle_t wgf_entity_t;

/* What an entity can have, at most one of each. The first five are data the systems
 * read; the node kinds are a node under the entity's own (wgf_entity_get_component_node),
 * set up with its own calls; a voice plays a sound (wgf_entity_get_voice). */
typedef enum wgf_component_t {
    WGF_COMPONENT_NONE = 0,
    WGF_COMPONENT_MOTION = 1,    /* velocity, spin, damping (wgf_motion.h) */
    WGF_COMPONENT_BOUNDS = 2,    /* a rectangle it wraps around, is clamped to, or dies outside (wgf_bounds.h) */
    WGF_COMPONENT_LIFETIME = 3,  /* seconds until it is destroyed (wgf_lifetime.h) */
    WGF_COMPONENT_COLLIDER = 4,  /* a circle that overlaps others, as triggers (wgf_collider.h) */
    WGF_COMPONENT_BEHAVIOR = 5,  /* the program's own behavior, by name, with parameters (wgf_behavior.h) */
    WGF_COMPONENT_SHAPE2D = 6,   /* a 2D shape node (wgf_shape2d.h) */
    WGF_COMPONENT_SPRITE = 7,    /* a sprite node of no texture yet (wgf_sprite.h) */
    WGF_COMPONENT_TEXT = 8,      /* a text node in the default font (wgf_text.h) */
    WGF_COMPONENT_EMITTER2D = 9, /* a 2D particle emitter node (wgf_emitter2d.h) */
    WGF_COMPONENT_VOICE = 10     /* a voice of no sound yet (wgf_voice.h) */
} wgf_component_t;

/* An entity at the origin, with no components, its node put under `parent` (0: no
 * parent, so it is drawn by nothing until its node is given one). 0 when `parent` isn't
 * a node, or there is no room. */
WGF_API wgf_entity_t wgf_entity_create(wgf_node_t parent);

/* End it now, with its node, everything under its node, and its components. One with a
 * behavior is told so: a DESTROYED event (wgf_ecs.h). False for a handle that isn't a
 * live entity. */
WGF_API bool wgf_entity_destroy(wgf_entity_t entity);
WGF_API bool wgf_entity_is_alive(wgf_entity_t entity);

/* Live entities, now. */
WGF_API int wgf_entity_get_count(void);

/* The node it is drawn through, which its component nodes are under; a node of its own,
 * which libwgf moves every frame (set the entity's transform, never this node's). */
WGF_API wgf_node_t wgf_entity_get_node(wgf_entity_t entity);

/* A name, for finding it and for the scene dump; NULL or "" for none (the default).
 * get_name's is the entity's, valid until it changes or the entity goes. find gives the
 * first live entity so named, 0 for none. False for a handle that isn't an entity, or a
 * name of 64 bytes or more. */
WGF_API bool wgf_entity_set_name(wgf_entity_t entity, const char *name);
WGF_API const char *wgf_entity_get_name(wgf_entity_t entity);
WGF_API wgf_entity_t wgf_entity_find(const char *name);

/* Whether what the entity draws is drawn: hidden, none of its components' nodes (a
 * shape, a sprite, text, an emitter's particles) is drawn, while it moves, ticks, meets
 * others, and its emitters go on simulating. An entity's own node draws nothing, so
 * hiding it (wgf_node_set_visible) hides nothing: this sets each component node's
 * visible, and one added later takes the entity's. A component node made visible on its
 * own afterwards is drawn. Default: visible. False for a handle that isn't an entity. */
WGF_API bool wgf_entity_set_visible(wgf_entity_t entity, bool visible);
WGF_API bool wgf_entity_is_visible(wgf_entity_t entity);

/* The transform: position, rotation (three angles in radians, about x, then y, then z;
 * in 2D, about z alone), and scale, each set part by part or all at once. A change in a
 * tick is smoothed into the frames after it; snap ends the smoothing, so the node is
 * drawn where the entity is now. False for a handle that isn't an entity. */
WGF_API bool wgf_entity_set_position(wgf_entity_t entity, float x, float y, float z);
WGF_API wgf_vec3_t wgf_entity_get_position(wgf_entity_t entity);
WGF_API bool wgf_entity_set_rotation(wgf_entity_t entity, float x, float y, float z);
WGF_API wgf_vec3_t wgf_entity_get_rotation(wgf_entity_t entity);
WGF_API bool wgf_entity_set_scale(wgf_entity_t entity, float x, float y, float z);
WGF_API wgf_vec3_t wgf_entity_get_scale(wgf_entity_t entity);
WGF_API bool wgf_entity_set_transform(wgf_entity_t entity, float position_x, float position_y, float position_z,
                                      float rotation_x, float rotation_y, float rotation_z, float scale_x,
                                      float scale_y, float scale_z);
WGF_API bool wgf_entity_snap(wgf_entity_t entity);

/* Many positions at once: each of `entities` (count of them) read into `out` as x, y, z
 * in turn, as many as fit in `out_count` floats, returning how many floats it filled (a
 * handle that isn't an entity reads 0, 0, 0); and each set from `positions` (x, y, z in
 * turn, positions_count floats), false when there aren't three floats for each, skipping
 * a handle that isn't an entity. */
WGF_API int wgf_entity_get_positions(const wgf_entity_t *entities, int count, float *out, int out_count);
WGF_API bool wgf_entity_set_positions(const wgf_entity_t *entities, int count, const float *positions,
                                      int positions_count);

/* Components. add makes one with its defaults (each component's header says them; the
 * node kinds' as their create makes them); adding one it has keeps it as it is. remove
 * ends it (a node kind's node destroyed, a voice stopped and destroyed). False for a
 * handle that isn't an entity, or a kind that isn't one; remove is false too for one it
 * doesn't have. Adding a behavior is told as a CREATED event (wgf_ecs.h). */
WGF_API bool wgf_entity_add_component(wgf_entity_t entity, wgf_component_t component);
WGF_API bool wgf_entity_remove_component(wgf_entity_t entity, wgf_component_t component);
WGF_API bool wgf_entity_has_component(wgf_entity_t entity, wgf_component_t component);

/* A node kind's node (SHAPE2D, SPRITE, TEXT, EMITTER2D), and the voice; 0 for one the
 * entity doesn't have. The entity's: set them up, never destroy them. */
WGF_API wgf_node_t wgf_entity_get_component_node(wgf_entity_t entity, wgf_component_t component);
WGF_API wgf_voice_t wgf_entity_get_voice(wgf_entity_t entity);

#ifdef __cplusplus
}
#endif

#endif
