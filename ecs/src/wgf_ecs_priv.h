#ifndef WGF_ECS_PRIV_H
#define WGF_ECS_PRIV_H

#include "wgf_core_handle_priv.h" /* WGF_CORE_PRIV_CALLER */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wgf_ecs_store_priv.h"
#include "wgf_component.h"
#include "wgf_world.h"

/* The ecs's own code: the store (wgf_ecs_store_priv.h), the records of the actors with
 * components or behaviors, the systems, and the events (wgf_ecs.c); an actor's components and behaviors
 * added and let go of (wgf_ecs_actor.c); the components' calls (wgf_ecs_components.c,
 * wgf_ecs_behavior.c); scenes (wgf_ecs_scene.c) and the dump (wgf_ecs_dump.c).
 *
 * One kind of object, the actor (docs/HISTORY.md, "One kind of object, the actor"): an actor
 * with a component or a behavior has a record here, its handle in the actor's own record
 * (wgf_gfx_priv_actor_t's `components`). The systems' data -- motion, bounds, lifetime, a
 * collider -- is the store's components, plain data the queries walk; the transform is the
 * actor's, simulated (wgf_gfx_priv_actor_set_simulated); what isn't plain data -- the voice,
 * the behaviors' names and parameters -- is in the record, with its entity in the store. */

#define WGF_ECS_PRIV_NAME_MAX 64

/* The store's components, plain data. */
typedef struct wgf_ecs_priv_ref_t {
    wgf_actor_t actor; /* the actor the store's entity is the components of, for a query's rows */
} wgf_ecs_priv_ref_t;

typedef struct wgf_ecs_priv_motion_t {
    float velocity[3], spin[3];
    float damping, max_speed;
} wgf_ecs_priv_motion_t;

typedef struct wgf_ecs_priv_bounds_t {
    float rect[4];
    int mode; /* wgf_bounds_mode_t */
    float margin;
    bool visible; /* the rectangle is the presentation's visible area, each tick */
} wgf_ecs_priv_bounds_t;

typedef struct wgf_ecs_priv_lifetime_t {
    float seconds;
} wgf_ecs_priv_lifetime_t;

typedef struct wgf_ecs_priv_collider_t {
    float radius;
    int32_t layer, mask;
    bool enabled; /* off: it meets nothing, its settings kept */
} wgf_ecs_priv_collider_t;

/* The store's ids of the components, made as it starts. */
typedef struct wgf_ecs_priv_ids_t {
    wgf_ecs_priv_id_t ref, motion, bounds, lifetime, collider;
    wgf_ecs_priv_id_t voice; /* a tag: the voice itself is in the record */
} wgf_ecs_priv_ids_t;

#define WGF_ECS_PRIV_PARAMS_MAX 32
#define WGF_ECS_PRIV_VALUE_MAX 256

/* A parameter, its key and value malloc'd, so a behavior costs what its parameters do
 * (the actor benchmark's: inline, 32 slots were 10 KB an actor). */
typedef struct wgf_ecs_priv_param_t {
    char *key;
    char *value;
    wgf_actor_t actor; /* a value "@...": the actor it refers to, found when set or when its scene's actors were made */
} wgf_ecs_priv_param_t;

typedef struct wgf_ecs_priv_behavior_t {
    int id; /* the actor's for it: from 1, never given again on the actor */
    char name[WGF_ECS_PRIV_NAME_MAX];
    wgf_ecs_priv_param_t *params; /* malloc'd, in the order first set */
    int param_count, param_capacity;
} wgf_ecs_priv_behavior_t;

/* A behavior let go of, its parameters with it. */
void wgf_ecs_priv_behavior_free(wgf_ecs_priv_behavior_t *behavior);

/* The record of an actor with components or behaviors, by its own handle (the actor's
 * `components`). */
typedef struct wgf_ecs_priv_record_t {
    wgf_ecs_priv_id_t id; /* its entity in the store */
    uint64_t order; /* made the order-th: "oldest first" */
    wgf_actor_t actor;
    wgf_voice_t voice; /* 0: no voice component */
    wgf_ecs_priv_behavior_t **behaviors; /* each malloc'd, in the order added */
    int behavior_count, behavior_capacity;
    int next_behavior; /* the id the next one added gets */
} wgf_ecs_priv_record_t;

/* The world, made with the first record, and the component ids; NULL before. */
bool wgf_ecs_priv_started(void);
const wgf_ecs_priv_ids_t *wgf_ecs_priv_ids(void);

/* The ecs part installed, and the world made: false when it couldn't be (logged). */
bool wgf_ecs_priv_start(void);

/* `actor`'s record, made the first time (the actor then simulated); NULL for a handle that
 * isn't an actor, or no room. And the record of an actor, or NULL when it has none: records
 * move when one is made, so don't keep the pointer across a make. */
wgf_ecs_priv_record_t *wgf_ecs_priv_record_make(wgf_actor_t actor);
wgf_ecs_priv_record_t *wgf_ecs_priv_record_of_at(wgf_actor_t actor, const char *caller);
#define wgf_ecs_priv_record_of(actor) wgf_ecs_priv_record_of_at((actor), WGF_CORE_PRIV_CALLER)

/* A record let go of as its actor goes (the actor's hook): DESTROYED raised for each
 * behavior, its collider's pairs dropped, its voice destroyed, its entity in the store deleted. */
void wgf_ecs_priv_record_free(wgf_actor_t actor);

/* A component of an actor, for writing; NULL when it hasn't that one. */
void *wgf_ecs_priv_get_at(wgf_actor_t actor, wgf_ecs_priv_id_t component, const char *caller);
#define wgf_ecs_priv_get(actor, component) wgf_ecs_priv_get_at((actor), (component), WGF_CORE_PRIV_CALLER)

/* Every actor with a record, oldest first, into a malloc'd array the caller frees (NULL
 * with none, or out of memory); how many in *count. */
wgf_actor_t *wgf_ecs_priv_actors(int *count);

/* The tag an actor with a behavior named `name` carries in the store, so finding them is
 * the tag's set rather than a look at every record; made the first time when `make`, else
 * 0 for a name no actor has had. */
wgf_ecs_priv_id_t wgf_ecs_priv_behavior_tag(const char *name, bool make);

/* Every reference ("@name", "@path") in the parameters of `actor`'s behaviors found again,
 * as a scene's actors are all made: one found by none warned. */
void wgf_ecs_priv_behaviors_resolve(wgf_actor_t actor);

/* An event raised: queued for the program to take (wgf_world_take_events). */
void wgf_ecs_priv_raise(wgf_world_event_t event, int a, int b, int c);

/* An actor's collider pairs dropped as it goes, or as its collider does, raising nothing. */
void wgf_ecs_priv_forget_pairs(wgf_actor_t actor);

/* The last dump freed, with the ecs's stop. */
void wgf_ecs_priv_dump_shutdown(void);

/* Text for a scene line's value: quoted, its " and \ escaped, into `out`. */
void wgf_ecs_priv_quote(const char *text, char *out, size_t out_size);

#endif
