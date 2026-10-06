#ifndef WGF_ECS_PRIV_H
#define WGF_ECS_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "flecs.h"
#include "wgf_ecs.h"
#include "wgf_entity.h"
#include "wgf_quat.h"

/* The ecs module's own code: the world (flecs), the entity records, the systems, and the
 * events (wgf_ecs.c); the components' calls (wgf_ecs_components.c, wgf_ecs_behavior.c);
 * scenes (wgf_ecs_scene.c) and the dump (wgf_ecs_dump.c).
 *
 * The split (docs/HISTORY.md, "The entity and node split"): an entity's simulated state
 * -- its transform and the systems' components -- is flecs components, plain data the
 * systems' queries walk; what isn't plain data -- its node and its component nodes, its
 * voice, its name, its behavior's name and parameters -- is in libwgf's record for its
 * handle, which also holds the flecs id. */

#define WGF_ECS_PRIV_NAME_MAX 64

/* flecs components, plain data. */
typedef struct wgf_ecs_priv_ref_t {
    wgf_entity_t handle; /* the libwgf handle of the flecs entity, for a query's rows */
} wgf_ecs_priv_ref_t;

typedef struct wgf_ecs_priv_transform_t {
    float position[3], rotation[3], scale[3]; /* now: the simulation's */
    float prev_position[3], prev_scale[3];    /* as the last tick began: drawn from these to now */
    wgf_quat_t prev_rotation;
} wgf_ecs_priv_transform_t;

typedef struct wgf_ecs_priv_motion_t {
    float velocity[3], spin[3];
    float damping, max_speed;
} wgf_ecs_priv_motion_t;

typedef struct wgf_ecs_priv_bounds_t {
    float rect[4];
    int mode; /* wgf_bounds_mode_t */
    float margin;
} wgf_ecs_priv_bounds_t;

typedef struct wgf_ecs_priv_lifetime_t {
    float seconds;
} wgf_ecs_priv_lifetime_t;

typedef struct wgf_ecs_priv_collider_t {
    float radius;
    int32_t layer, mask;
} wgf_ecs_priv_collider_t;

/* The flecs ids of the components, made with the world. */
typedef struct wgf_ecs_priv_ids_t {
    ecs_entity_t ref, transform, motion, bounds, lifetime, collider;
} wgf_ecs_priv_ids_t;

#define WGF_ECS_PRIV_PARAMS_MAX 32
#define WGF_ECS_PRIV_VALUE_MAX 256

typedef struct wgf_ecs_priv_param_t {
    char key[WGF_ECS_PRIV_NAME_MAX];
    char value[WGF_ECS_PRIV_VALUE_MAX];
} wgf_ecs_priv_param_t;

typedef struct wgf_ecs_priv_behavior_t {
    char name[WGF_ECS_PRIV_NAME_MAX];
    wgf_ecs_priv_param_t params[WGF_ECS_PRIV_PARAMS_MAX];
    int param_count;
} wgf_ecs_priv_behavior_t;

/* The node kinds' slots, from WGF_COMPONENT_SHAPE2D. */
#define WGF_ECS_PRIV_NODE_KINDS 4

/* libwgf's record for an entity, by its handle. */
typedef struct wgf_ecs_priv_entity_t {
    ecs_entity_t id;
    uint64_t order; /* made the order-th: "oldest first" */
    wgf_node_t node;
    wgf_node_t parts[WGF_ECS_PRIV_NODE_KINDS]; /* SHAPE2D, SPRITE, TEXT, EMITTER2D; 0 for none */
    wgf_voice_t voice;                         /* 0: no voice component */
    char name[WGF_ECS_PRIV_NAME_MAX];
    wgf_ecs_priv_behavior_t *behavior;         /* malloc'd; NULL: no behavior component */
} wgf_ecs_priv_entity_t;

/* The world, made with the first entity, and the component ids; NULL before. */
ecs_world_t *wgf_ecs_priv_world(void);
const wgf_ecs_priv_ids_t *wgf_ecs_priv_ids(void);

/* The ecs part installed, and the world made: false when it couldn't be (logged). */
bool wgf_ecs_priv_start(void);

/* A record for a new entity drawn through `node`, its flecs entity made with its ref and
 * transform; 0 when there is no room. And a record freed: its flecs entity deleted, its
 * behavior freed, its handle stale (its nodes and voice are the caller's to end). */
wgf_entity_t wgf_ecs_priv_new_record(wgf_node_t node);
void wgf_ecs_priv_free_record(wgf_entity_t entity);

/* The record of a live entity; NULL for anything else. Records move when one is made:
 * don't keep the pointer across a create. */
wgf_ecs_priv_entity_t *wgf_ecs_priv_entity_of(wgf_entity_t entity);

/* A flecs component of a live entity, for writing; NULL when it hasn't that one. */
void *wgf_ecs_priv_get(wgf_entity_t entity, ecs_entity_t component);

/* Every live entity, oldest first, into a malloc'd array the caller frees (NULL with
 * none, or out of memory); how many in *count. */
wgf_entity_t *wgf_ecs_priv_entities(int *count);

/* An event raised: queued for the program to take (wgf_ecs_take_events). */
void wgf_ecs_priv_raise(wgf_ecs_event_t event, wgf_entity_t entity, wgf_entity_t other);

/* An entity's collider pairs dropped as it goes, raising nothing. */
void wgf_ecs_priv_forget_pairs(wgf_entity_t entity);

/* The node kind's slot in an entity's parts, or -1 for a component that isn't one. */
int wgf_ecs_priv_node_slot(wgf_component_t component);

/* The last dump freed, with the ecs's stop. */
void wgf_ecs_priv_dump_shutdown(void);

/* Text for a scene line's value: quoted, its " and \ escaped, into `out`. */
void wgf_ecs_priv_quote(const char *text, char *out, size_t out_size);

#endif
