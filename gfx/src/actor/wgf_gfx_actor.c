#include "wgf_actor.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"

/* Actors: one pool for every kind, so an actor call finds any actor with one lookup.
 * An actor's children are an array of handles, in drawing order: a child that leaves
 * leaves a hole, closed when the array is next read whole (wgf_gfx_priv_actor_children),
 * as libwgt's. */

static bool pool_ready;
static wgf_core_priv_handle_pool_t actor_pool;
static wgf_gfx_priv_actor_t *records; /* the pool's items */

/* --- the pool ---------------------------------------------------------------- */

wgf_gfx_priv_actor_t *wgf_gfx_priv_actor_of_at(wgf_actor_t actor, const char *caller)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve_at(&actor_pool, actor, &index, caller)) return NULL;
    return &records[index];
}

wgf_actor_t wgf_gfx_priv_actor_create(wgf_actor_kind_t type)
{
    wgf_actor_t handle;
    uint16_t index;
    wgf_gfx_priv_actor_t *actor_ptr;
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&actor_pool, WGF_CORE_PRIV_HANDLE_KIND_ACTOR, (void **)&records,
                                                   sizeof(wgf_gfx_priv_actor_t), 64, 65535);
        if (!pool_ready) return 0;
    }
    handle = wgf_core_priv_handle_pool_alloc(&actor_pool);
    if (handle == 0 || !wgf_core_priv_handle_pool_resolve(&actor_pool, handle, &index)) {
        wgf_log_error("wgf_gfx_actor: no room for another actor");
        return 0;
    }
    actor_ptr = &records[index];
    memset(actor_ptr, 0, sizeof(*actor_ptr));
    actor_ptr->type = type;
    actor_ptr->rotation = wgf_quat_identity();
    actor_ptr->scale = wgf_vec3_make(1.0f, 1.0f, 1.0f);
    actor_ptr->local_dirty = true;
    actor_ptr->world_dirty = true;
    actor_ptr->enabled = true;
    actor_ptr->visible = true;
    return handle;
}

/* The transform it is drawn at: its own, or for a simulated actor, between its last two
 * ticks' at the frame's tick fraction, its rotation the shortest way round. */
static void drawn_transform(const wgf_gfx_priv_actor_t *actor_ptr, wgf_vec3_t *p, wgf_quat_t *q, wgf_vec3_t *k)
{
    float t;
    if (actor_ptr->previous == NULL) {
        *p = actor_ptr->position;
        *q = actor_ptr->rotation;
        *k = actor_ptr->scale;
        return;
    }
    t = wgf_core_priv_part_get_fraction();
    *p = wgf_vec3_lerp(actor_ptr->previous->position, actor_ptr->position, t);
    *q = wgf_quat_slerp(actor_ptr->previous->rotation, actor_ptr->rotation, t);
    *k = wgf_vec3_lerp(actor_ptr->previous->scale, actor_ptr->scale, t);
}

static wgf_mat4_t trs(wgf_vec3_t p, wgf_quat_t q, wgf_vec3_t k)
{
    if (q.x == 0.0f && q.y == 0.0f && q.z == 0.0f) { /* not turned, as most 2D actors: scale, then move */
        wgf_mat4_t m = wgf_mat4_identity();
        m.m[0] = k.x;
        m.m[5] = k.y;
        m.m[10] = k.z;
        m.m[12] = p.x;
        m.m[13] = p.y;
        m.m[14] = p.z;
        return m;
    }
    return wgf_mat4_from_trs(p, q, k);
}

wgf_mat4_t wgf_gfx_priv_actor_get_local_matrix(wgf_gfx_priv_actor_t *actor_ptr)
{
    if (actor_ptr->local_dirty) {
        wgf_vec3_t p, k;
        wgf_quat_t q;
        drawn_transform(actor_ptr, &p, &q, &k);
        actor_ptr->local = trs(p, q, k);
        actor_ptr->local_dirty = false;
    }
    return actor_ptr->local;
}

/* Its world matrix from the simulation's transforms, its own and every one above it: the
 * cached world matrix is the drawn one, which for a simulated actor is between its ticks.
 * The cached one when nothing in the chain is simulated, as it then is the same. */
wgf_mat4_t wgf_gfx_priv_actor_get_simulated_world(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor), *at;
    wgf_mat4_t world;
    bool simulated = false;
    if (actor_ptr == NULL) return wgf_mat4_identity();
    for (at = actor_ptr; at != NULL; at = at->parent != 0 ? wgf_gfx_priv_actor_of(at->parent) : NULL) {
        if (at->previous != NULL) simulated = true;
    }
    if (!simulated) return wgf_gfx_priv_actor_get_world_matrix(actor);
    world = trs(actor_ptr->position, actor_ptr->rotation, actor_ptr->scale);
    for (at = actor_ptr->parent != 0 ? wgf_gfx_priv_actor_of(actor_ptr->parent) : NULL; at != NULL;
         at = at->parent != 0 ? wgf_gfx_priv_actor_of(at->parent) : NULL) {
        world = wgf_mat4_mul(trs(at->position, at->rotation, at->scale), world);
    }
    return world;
}

/* Mark `actor` and everything under it world-dirty. An actor already dirty has its
 * whole subtree dirty, so the walk stops there: moving one actor every frame costs
 * little more than the first time. */
static void mark_world_dirty(wgf_actor_t actor)
{
    wgf_actor_t stack[64];
    wgf_actor_t *todo = stack;
    int count = 0, capacity = 64;
    todo[count++] = actor;
    while (count > 0) {
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(todo[--count]);
        int i;
        if (actor_ptr == NULL || actor_ptr->world_dirty) continue;
        actor_ptr->world_dirty = true;
        for (i = 0; i < actor_ptr->child_count; i++) {
            if (count == capacity) {
                wgf_actor_t *grown = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)capacity * 2);
                if (grown == NULL) {
                    wgf_log_error("wgf_gfx_actor: out of memory marking moved actors; some may draw where they were");
                    break;
                }
                memcpy(grown, todo, sizeof(wgf_actor_t) * (size_t)count);
                if (todo != stack) free(todo);
                todo = grown;
                capacity *= 2;
            }
            todo[count++] = actor_ptr->children[i];
        }
    }
    if (todo != stack) free(todo);
}

/* `actor_ptr` (`actor`'s record) moved: its local matrix out of date, and its world and
 * everything under it. An actor with no children, as most moved ones, is one flag. */
static void moved(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    actor_ptr->local_dirty = true;
    if (actor_ptr->live_children == 0) {
        actor_ptr->world_dirty = true;
        return;
    }
    mark_world_dirty(actor);
}

void wgf_gfx_priv_actor_transform_changed(wgf_actor_t actor)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr != NULL) moved(actor, actor_ptr);
}

wgf_mat4_t wgf_gfx_priv_actor_world_of(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    const wgf_gfx_priv_actor_t *parent_ptr;
    if (!actor_ptr->world_dirty) return actor_ptr->world;
    parent_ptr = actor_ptr->parent != 0 ? wgf_gfx_priv_actor_of(actor_ptr->parent) : NULL;
    if (parent_ptr != NULL && parent_ptr->world_dirty) return wgf_gfx_priv_actor_get_world_matrix(actor);
    actor_ptr->world = parent_ptr != NULL ? wgf_mat4_mul(parent_ptr->world, wgf_gfx_priv_actor_get_local_matrix(actor_ptr))
                                         : wgf_gfx_priv_actor_get_local_matrix(actor_ptr);
    actor_ptr->world_dirty = false;
    return actor_ptr->world;
}

wgf_mat4_t wgf_gfx_priv_actor_get_world_matrix(wgf_actor_t actor)
{
    wgf_actor_t stack[64];
    wgf_actor_t *chain = stack;
    int count = 0, capacity = 64;
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_mat4_t world;

    if (actor_ptr == NULL) return wgf_mat4_identity();
    if (!actor_ptr->world_dirty) return actor_ptr->world;
    /* the dirty actors from this one up to the first clean one (or the root) ... */
    while (actor_ptr != NULL && actor_ptr->world_dirty) {
        if (count == capacity) {
            wgf_actor_t *grown = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)capacity * 2);
            if (grown == NULL) break;
            memcpy(grown, chain, sizeof(wgf_actor_t) * (size_t)count);
            if (chain != stack) free(chain);
            chain = grown;
            capacity *= 2;
        }
        chain[count++] = actor;
        actor = actor_ptr->parent;
        actor_ptr = wgf_gfx_priv_actor_of(actor);
    }
    /* ... then rebuilt from the top down */
    world = actor_ptr != NULL ? actor_ptr->world : wgf_mat4_identity();
    while (count > 0) {
        wgf_gfx_priv_actor_t *next_ptr = wgf_gfx_priv_actor_of(chain[--count]);
        const wgf_mat4_t local = wgf_gfx_priv_actor_get_local_matrix(next_ptr);
        world = wgf_mat4_mul(world, local);
        next_ptr->world = world;
        next_ptr->world_dirty = false;
    }
    if (chain != stack) free(chain);
    return world;
}

/* --- the tree ------------------------------------------------------------------ */

/* Close the holes children left, in order, each moved child told its new slot. */
static void compact(wgf_gfx_priv_actor_t *parent_ptr)
{
    int kept = 0;
    if (!parent_ptr->child_holes) return;
    for (int i = 0; i < parent_ptr->child_count; i++) {
        const wgf_actor_t child = parent_ptr->children[i];
        if (child == 0) continue;
        wgf_gfx_priv_actor_of(child)->slot = kept;
        parent_ptr->children[kept++] = child;
    }
    parent_ptr->child_count = kept;
    parent_ptr->child_holes = false;
}

const wgf_actor_t *wgf_gfx_priv_actor_children(wgf_actor_t actor, int *count)
{
    return wgf_gfx_priv_actor_children_of(wgf_gfx_priv_actor_of(actor), count);
}

const wgf_actor_t *wgf_gfx_priv_actor_children_of(wgf_gfx_priv_actor_t *actor_ptr, int *count)
{
    if (actor_ptr == NULL || actor_ptr->live_children == 0) {
        *count = 0;
        return NULL;
    }
    compact(actor_ptr);
    *count = actor_ptr->child_count;
    return actor_ptr->children;
}

/* `child` out of `parent`'s children: a hole where it was, or, the last, one slot fewer. */
static void remove_child(wgf_actor_t parent, wgf_actor_t child)
{
    wgf_gfx_priv_actor_t *parent_ptr = wgf_gfx_priv_actor_of(parent);
    const wgf_gfx_priv_actor_t *child_ptr = wgf_gfx_priv_actor_of(child);
    int slot;
    if (parent_ptr == NULL || child_ptr == NULL) return;
    slot = child_ptr->slot;
    if (slot < 0 || slot >= parent_ptr->child_count || parent_ptr->children[slot] != child) return;
    parent_ptr->children[slot] = 0;
    parent_ptr->live_children--;
    if (slot == parent_ptr->child_count - 1) {
        parent_ptr->child_count--;
    } else {
        parent_ptr->child_holes = true;
    }
}

/* Put `child` among `parent`'s children at `index` (clamped); false out of memory.
 * Appending is constant; anywhere else closes the holes and shifts what follows. */
static bool insert_child(wgf_actor_t parent, wgf_actor_t child, int index)
{
    wgf_gfx_priv_actor_t *parent_ptr = wgf_gfx_priv_actor_of(parent);
    if (parent_ptr == NULL) return false;
    if (index < 0) index = 0;
    /* into the middle; or out of room with holes to close first, so a parent never read
       whole (disabled, detached, headless) keeps at most twice its live children */
    if (index < parent_ptr->live_children ||
        (parent_ptr->child_holes && parent_ptr->child_count == parent_ptr->child_capacity)) {
        compact(parent_ptr);
    }
    if (index >= parent_ptr->live_children) index = parent_ptr->child_count; /* after the last, holes or not */
    if (parent_ptr->child_count == parent_ptr->child_capacity) {
        const int capacity = parent_ptr->child_capacity > 0 ? parent_ptr->child_capacity * 2 : 4;
        wgf_actor_t *grown =
            (wgf_actor_t *)realloc(parent_ptr->children, sizeof(wgf_actor_t) * (size_t)capacity);
        if (grown == NULL) return false;
        parent_ptr->children = grown;
        parent_ptr->child_capacity = capacity;
    }
    memmove(&parent_ptr->children[index + 1], &parent_ptr->children[index],
            sizeof(wgf_actor_t) * (size_t)(parent_ptr->child_count - index));
    parent_ptr->children[index] = child;
    parent_ptr->child_count++;
    parent_ptr->live_children++;
    for (int i = index; i < parent_ptr->child_count; i++) { /* it, and those it moved along */
        if (parent_ptr->children[i] != 0) wgf_gfx_priv_actor_of(parent_ptr->children[i])->slot = i;
    }
    return true;
}

static bool is_under(wgf_actor_t actor, wgf_actor_t ancestor)
{
    const wgf_gfx_priv_actor_t *actor_ptr;
    while ((actor_ptr = wgf_gfx_priv_actor_of(actor)) != NULL) {
        if (actor == ancestor) return true;
        actor = actor_ptr->parent;
    }
    return false;
}

#define NODE_TYPES 16 /* more than wgf_actor_kind_t's last */
_Static_assert(WGF_ACTOR_KIND_SHAPE3D < NODE_TYPES, "an actor type past the kinds' table");
static const wgf_gfx_priv_actor_kind_t *kinds[NODE_TYPES];

static const wgf_gfx_priv_model_hooks_t *model_hooks;

void wgf_gfx_priv_set_model_hooks(const wgf_gfx_priv_model_hooks_t *hooks)
{
    model_hooks = hooks;
}

const wgf_gfx_priv_model_hooks_t *wgf_gfx_priv_get_model_hooks(void)
{
    return model_hooks;
}

void wgf_gfx_priv_actor_set_kind(wgf_actor_kind_t type, const wgf_gfx_priv_actor_kind_t *kind)
{
    if ((int)type >= 0 && type < NODE_TYPES) kinds[type] = kind;
}

const wgf_gfx_priv_actor_kind_t *wgf_gfx_priv_actor_get_kind(wgf_actor_kind_t type)
{
    return (int)type >= 0 && type < NODE_TYPES ? kinds[type] : NULL;
}

/* An actor gone: what it held let go, its children list freed, its slot freed. */
static void (*components_gone)(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr); /* the ecs's */

void wgf_gfx_priv_actor_set_components_hook(void (*gone)(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr))
{
    components_gone = gone;
}

void wgf_gfx_priv_actor_set_simulated(wgf_gfx_priv_actor_t *actor_ptr, bool simulated)
{
    if (!simulated) {
        free(actor_ptr->previous);
        actor_ptr->previous = NULL;
        return;
    }
    if (actor_ptr->previous == NULL) {
        actor_ptr->previous = (wgf_gfx_priv_previous_t *)malloc(sizeof(wgf_gfx_priv_previous_t));
        if (actor_ptr->previous == NULL) { /* drawn where it is, unsmoothed */
            wgf_log_error("wgf_gfx_actor: out of memory simulating an actor");
            return;
        }
    }
    actor_ptr->previous->position = actor_ptr->position;
    actor_ptr->previous->rotation = actor_ptr->rotation;
    actor_ptr->previous->scale = actor_ptr->scale;
}

static void unindex(wgf_actor_t actor, const char *name); /* names, below */
static void names_shutdown(void);

static void free_actor(wgf_actor_t actor)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    const wgf_gfx_priv_actor_kind_t *kind;
    if (actor_ptr == NULL) return;
    if (actor_ptr->components != 0 && components_gone != NULL) {
        components_gone(actor, actor_ptr);
        actor_ptr = wgf_gfx_priv_actor_of(actor);
    }
    kind = wgf_gfx_priv_actor_get_kind(actor_ptr->type);
    if (kind != NULL && kind->free != NULL) kind->free(actor, actor_ptr);
    unindex(actor, actor_ptr->name);
    free(actor_ptr->name);
    free(actor_ptr->children);
    free(actor_ptr->previous);
    wgf_core_priv_handle_pool_free(&actor_pool, actor);
}

/* --- the public API ------------------------------------------------------------ */

wgf_actor_t wgf_actor_create(void)
{
    return wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_PLAIN);
}

void wgf_actor_destroy(wgf_actor_t actor, wgf_actor_destroy_t children)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_actor_t parent;
    int index;

    if (actor_ptr == NULL) return;
    parent = actor_ptr->parent;
    if (children == WGF_ACTOR_KEEP_CHILDREN && parent != 0) {
        compact(wgf_gfx_priv_actor_of(parent)); /* its place, among its siblings as they read */
        actor_ptr = wgf_gfx_priv_actor_of(actor);
    }
    index = actor_ptr->slot;
    remove_child(parent, actor);

    if (children == WGF_ACTOR_KEEP_CHILDREN) {
        /* each child up a level, in the actor's place, where it is now */
        const wgf_mat4_t local = wgf_gfx_priv_actor_get_local_matrix(actor_ptr);
        int i;
        compact(actor_ptr);
        for (i = 0; i < actor_ptr->child_count; i++) {
            const wgf_actor_t child = actor_ptr->children[i];
            wgf_gfx_priv_actor_t *child_ptr = wgf_gfx_priv_actor_of(child);
            const wgf_mat4_t child_local = wgf_gfx_priv_actor_get_local_matrix(child_ptr);
            const wgf_mat4_t moved = wgf_mat4_mul(local, child_local);
            child_ptr->position = wgf_mat4_get_translation(moved);
            child_ptr->scale = wgf_mat4_get_scale(moved);
            child_ptr->rotation = wgf_mat4_get_rotation(moved);
            child_ptr->parent = 0;
            if (parent != 0 && insert_child(parent, child, index + i)) child_ptr->parent = parent;
            wgf_gfx_priv_actor_transform_changed(child);
        }
        free_actor(actor);
        return;
    }

    {
        /* the actor and everything under it, with a list rather than C recursion, so
           a deep tree can't run out of stack */
        int count = 1, capacity = 64;
        wgf_actor_t *todo = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)capacity);
        if (todo == NULL) {
            wgf_log_error("wgf_gfx_actor: out of memory destroying an actor");
            return;
        }
        todo[0] = actor;
        while (count > 0) {
            const wgf_actor_t next = todo[--count];
            const wgf_gfx_priv_actor_t *next_ptr = wgf_gfx_priv_actor_of(next);
            int i;
            if (next_ptr == NULL) continue;
            for (i = 0; i < next_ptr->child_count; i++) {
                if (next_ptr->children[i] == 0) continue; /* a hole */
                if (count == capacity) {
                    wgf_actor_t *grown =
                        (wgf_actor_t *)realloc(todo, sizeof(wgf_actor_t) * (size_t)(capacity * 2));
                    if (grown == NULL) break; /* what is left stays, detached */
                    todo = grown;
                    capacity *= 2;
                }
                todo[count++] = next_ptr->children[i];
            }
            free_actor(next);
        }
        free(todo);
    }
}

wgf_actor_kind_t wgf_actor_get_kind(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(actor, NULL); /* a check: never warned */
    return actor_ptr != NULL ? actor_ptr->type : WGF_ACTOR_KIND_NONE;
}

bool wgf_actor_set_parent(wgf_actor_t actor, wgf_actor_t parent)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL || actor_ptr->type == WGF_ACTOR_KIND_STAGE2D || actor_ptr->type == WGF_ACTOR_KIND_STAGE3D) {
        return false;
    }
    if (parent != 0 && (wgf_gfx_priv_actor_of(parent) == NULL || is_under(parent, actor))) return false;
    if (parent == actor_ptr->parent) return true;
    remove_child(actor_ptr->parent, actor);
    wgf_gfx_priv_actor_of(actor)->parent = 0;
    if (parent != 0 && insert_child(parent, actor, 1 << 30)) wgf_gfx_priv_actor_of(actor)->parent = parent;
    mark_world_dirty(actor); /* a new parent: a new world */
    return wgf_gfx_priv_actor_of(actor)->parent == parent;
}

wgf_actor_t wgf_actor_get_parent(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL ? actor_ptr->parent : 0;
}

int wgf_actor_get_child_count(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL ? actor_ptr->live_children : 0;
}

wgf_actor_t wgf_actor_get_child(wgf_actor_t actor, int index)
{
    int count;
    const wgf_actor_t *children = wgf_gfx_priv_actor_children(actor, &count);
    return index >= 0 && index < count ? children[index] : 0;
}

bool wgf_actor_set_index(wgf_actor_t actor, int index)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_actor_t parent;
    if (actor_ptr == NULL || actor_ptr->parent == 0) return false;
    parent = actor_ptr->parent;
    remove_child(parent, actor);
    return insert_child(parent, actor, index); /* the slot it left is free, so this can't run out */
}

int wgf_actor_get_index(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_gfx_priv_actor_t *parent_ptr;
    if (actor_ptr == NULL || (parent_ptr = wgf_gfx_priv_actor_of(actor_ptr->parent)) == NULL) return 0;
    compact(parent_ptr);
    return wgf_gfx_priv_actor_of(actor)->slot;
}

bool wgf_actor_set_position(wgf_actor_t actor, float x, float y, float z)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    actor_ptr->position = wgf_vec3_make(x, y, z);
    moved(actor, actor_ptr);
    return true;
}

bool wgf_actor_snap(wgf_actor_t actor)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    if (actor_ptr->previous != NULL) {
        wgf_gfx_priv_actor_set_simulated(actor_ptr, true);
        wgf_gfx_priv_actor_transform_changed(actor);
    }
    return true;
}

int wgf_actor_get_positions(const wgf_actor_t *actors, int count, float *out, int out_count)
{
    int i, filled = 0;
    if (actors == NULL || out == NULL) return 0;
    for (i = 0; i < count && filled + 3 <= out_count; i++) {
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actors[i]);
        out[filled] = actor_ptr != NULL ? actor_ptr->position.x : 0.0f;
        out[filled + 1] = actor_ptr != NULL ? actor_ptr->position.y : 0.0f;
        out[filled + 2] = actor_ptr != NULL ? actor_ptr->position.z : 0.0f;
        filled += 3;
    }
    return filled;
}

bool wgf_actor_set_positions(const wgf_actor_t *actors, int count, const float *positions, int positions_count)
{
    int i;
    if (actors == NULL || positions == NULL || count < 0 || positions_count < 3 * count) return false;
    for (i = 0; i < count; i++) {
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actors[i]);
        if (actor_ptr == NULL) continue;
        actor_ptr->position = wgf_vec3_make(positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
        wgf_gfx_priv_actor_transform_changed(actors[i]);
    }
    return true;
}

wgf_vec3_t wgf_actor_get_position(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL ? actor_ptr->position : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_actor_set_rotation(wgf_actor_t actor, float x, float y, float z)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    actor_ptr->rotation = wgf_quat_from_euler(wgf_vec3_make(x, y, z));
    wgf_gfx_priv_actor_transform_changed(actor);
    return true;
}

bool wgf_actor_look_at(wgf_actor_t actor, float x, float y, float z, float up_x, float up_y, float up_z)
{
    const wgf_vec3_t up = wgf_vec3_make(up_x, up_y, up_z);
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_vec3_t forward;
    wgf_quat_t parent_rotation;
    if (actor_ptr == NULL) return false;
    forward = wgf_vec3_sub(wgf_vec3_make(x, y, z), wgf_mat4_get_translation(wgf_gfx_priv_actor_get_world_matrix(actor)));
    if (wgf_vec3_length(forward) < 1e-6f ||
        wgf_vec3_length(wgf_vec3_cross(up, forward)) < 1e-6f * wgf_vec3_length(forward)) {
        return false;
    }
    /* relative to the parent: its world rotation taken back off */
    parent_rotation = actor_ptr->parent != 0
                          ? wgf_mat4_get_rotation(wgf_gfx_priv_actor_get_world_matrix(actor_ptr->parent))
                          : wgf_quat_identity();
    actor_ptr = wgf_gfx_priv_actor_of(actor);
    actor_ptr->rotation = wgf_quat_mul(wgf_quat_conjugate(parent_rotation), wgf_quat_look_rotation(forward, up));
    wgf_gfx_priv_actor_transform_changed(actor);
    return true;
}

wgf_vec3_t wgf_actor_get_rotation(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL ? wgf_quat_to_euler(actor_ptr->rotation) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_actor_set_scale(wgf_actor_t actor, float x, float y, float z)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    actor_ptr->scale = wgf_vec3_make(x, y, z);
    wgf_gfx_priv_actor_transform_changed(actor);
    return true;
}

wgf_vec3_t wgf_actor_get_scale(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL ? actor_ptr->scale : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_actor_set_transform(wgf_actor_t actor, float position_x, float position_y, float position_z,
                               float rotation_x, float rotation_y, float rotation_z, float scale_x, float scale_y,
                               float scale_z)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    actor_ptr->position = wgf_vec3_make(position_x, position_y, position_z);
    actor_ptr->rotation = wgf_quat_from_euler(wgf_vec3_make(rotation_x, rotation_y, rotation_z));
    actor_ptr->scale = wgf_vec3_make(scale_x, scale_y, scale_z);
    wgf_gfx_priv_actor_transform_changed(actor);
    return true;
}

wgf_vec3_t wgf_actor_get_world_position(wgf_actor_t actor)
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_simulated_world(actor);
    if (wgf_gfx_priv_actor_of(actor) == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(world.m[12], world.m[13], world.m[14]);
}

wgf_vec3_t wgf_actor_get_drawn_position(wgf_actor_t actor)
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(actor);
    if (wgf_gfx_priv_actor_of(actor) == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(world.m[12], world.m[13], world.m[14]);
}

/* (x, y, z) turned by `world`'s rotation, made unit length; 0, 0, 0 for none. */
static wgf_vec3_t direction(const wgf_mat4_t *world, float x, float y, float z)
{
    const wgf_vec3_t d = wgf_vec3_make(world->m[0] * x + world->m[4] * y + world->m[8] * z,
                                       world->m[1] * x + world->m[5] * y + world->m[9] * z,
                                       world->m[2] * x + world->m[6] * y + world->m[10] * z);
    const float length = wgf_vec3_length(d);
    return length > 0.0f ? wgf_vec3_scale(d, 1.0f / length) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

wgf_vec3_t wgf_actor_get_world_direction(wgf_actor_t actor, float x, float y, float z)
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_simulated_world(actor);
    if (wgf_gfx_priv_actor_of(actor) == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return direction(&world, x, y, z);
}

wgf_vec3_t wgf_actor_get_drawn_direction(wgf_actor_t actor, float x, float y, float z)
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(actor);
    if (wgf_gfx_priv_actor_of(actor) == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return direction(&world, x, y, z);
}

/* ---- names, indexed --------------------------------------------------------------- */

/* Every named actor, in buckets by its name's hash: a stage's find looks in one bucket,
 * never walks a tree. A bucket holds the actors whose names hash there, any stage's. */
static struct {
    wgf_actor_t **buckets; /* each an array of its counts[] actors, room for capacities[] */
    int *counts, *capacities;
    int bucket_count; /* a power of two; 0 before the first name */
    int named;
} names;

static uint32_t name_hash(const char *name)
{
    uint32_t h = 2166136261u; /* FNV-1a */
    while (*name != '\0') h = (h ^ (uint8_t)*name++) * 16777619u;
    return h;
}

static void unindex(wgf_actor_t actor, const char *name)
{
    int b, i;
    if (name == NULL || names.bucket_count == 0) return;
    b = (int)(name_hash(name) & (uint32_t)(names.bucket_count - 1));
    for (i = 0; i < names.counts[b]; i++) {
        if (names.buckets[b][i] == actor) {
            names.buckets[b][i] = names.buckets[b][--names.counts[b]];
            names.named--;
            return;
        }
    }
}

static bool insert_name(wgf_actor_t actor, const char *name);

/* The buckets made `count` of them, every name in the old ones put in again: grown as the
 * names do, so a bucket holds about two. */
static bool rehash(int count)
{
    wgf_actor_t **old_buckets = names.buckets;
    int *old_counts = names.counts, *old_capacities = names.capacities;
    const int old_count = names.bucket_count;
    int b, i;
    names.buckets = (wgf_actor_t **)calloc((size_t)count, sizeof(wgf_actor_t *));
    names.counts = (int *)calloc((size_t)count, sizeof(int));
    names.capacities = (int *)calloc((size_t)count, sizeof(int));
    if (names.buckets == NULL || names.counts == NULL || names.capacities == NULL) {
        free(names.buckets);
        free(names.counts);
        free(names.capacities);
        names.buckets = old_buckets; /* kept as they were */
        names.counts = old_counts;
        names.capacities = old_capacities;
        return false;
    }
    names.bucket_count = count;
    names.named = 0;
    for (b = 0; b < old_count; b++) {
        for (i = 0; i < old_counts[b]; i++) {
            const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(old_buckets[b][i]);
            if (actor_ptr != NULL && actor_ptr->name != NULL) insert_name(old_buckets[b][i], actor_ptr->name);
        }
        free(old_buckets[b]);
    }
    free(old_buckets);
    free(old_counts);
    free(old_capacities);
    return true;
}

static bool index_name(wgf_actor_t actor, const char *name)
{
    if (names.bucket_count == 0 && !rehash(64)) return false;
    if (names.named >= names.bucket_count * 2) rehash(names.bucket_count * 2); /* failing, the old ones serve */
    return insert_name(actor, name);
}

static bool insert_name(wgf_actor_t actor, const char *name)
{
    int b;
    b = (int)(name_hash(name) & (uint32_t)(names.bucket_count - 1));
    if (names.counts[b] == names.capacities[b]) {
        const int capacity = names.capacities[b] > 0 ? names.capacities[b] * 2 : 4;
        wgf_actor_t *grown = (wgf_actor_t *)realloc(names.buckets[b], sizeof(wgf_actor_t) * (size_t)capacity);
        if (grown == NULL) return false;
        names.buckets[b] = grown;
        names.capacities[b] = capacity;
    }
    names.buckets[b][names.counts[b]++] = actor;
    names.named++;
    return true;
}

/* The names' index let go of, with gfx's actors. */
static void names_shutdown(void)
{
    int b;
    for (b = 0; b < names.bucket_count; b++) free(names.buckets[b]);
    free(names.buckets);
    free(names.counts);
    free(names.capacities);
    memset(&names, 0, sizeof(names));
}

bool wgf_actor_set_name(wgf_actor_t actor, const char *name)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    char *copy = NULL;
    if (actor_ptr == NULL) return false;
    if (name != NULL && name[0] != '\0') {
        const size_t n = strlen(name);
        copy = (char *)malloc(n + 1);
        if (copy == NULL) {
            wgf_log_error("wgf_gfx_actor: out of memory");
            return false;
        }
        memcpy(copy, name, n + 1);
    }
    unindex(actor, actor_ptr->name);
    free(actor_ptr->name);
    actor_ptr->name = copy;
    if (copy != NULL && !index_name(actor, copy)) wgf_log_error("wgf_gfx_actor: out of memory indexing a name");
    return true;
}

const char *wgf_actor_get_name(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL && actor_ptr->name != NULL ? actor_ptr->name : "";
}

/* The child of `actor_ptr` named the `length` bytes at `name`; 0 for none. */
static wgf_actor_t child_named(const wgf_gfx_priv_actor_t *actor_ptr, const char *name, size_t length)
{
    int i;
    for (i = 0; i < actor_ptr->child_count; i++) {
        const wgf_gfx_priv_actor_t *child = wgf_gfx_priv_actor_of(actor_ptr->children[i]);
        if (child != NULL && child->name != NULL && strlen(child->name) == length &&
            memcmp(child->name, name, length) == 0) {
            return actor_ptr->children[i];
        }
    }
    return 0;
}

wgf_actor_t wgf_actor_find(wgf_actor_t actor, const char *path)
{
    const char *p = path;
    if (path == NULL || path[0] == '\0' || wgf_gfx_priv_actor_of(actor) == NULL) return 0;
    while (actor != 0) {
        const char *slash = strchr(p, '/');
        const size_t length = slash != NULL ? (size_t)(slash - p) : strlen(p);
        actor = length > 0 ? child_named(wgf_gfx_priv_actor_of(actor), p, length) : 0;
        if (slash == NULL) return actor;
        p = slash + 1;
    }
    return 0;
}

wgf_actor_t wgf_gfx_priv_actor_get_root(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    while (actor_ptr != NULL && actor_ptr->parent != 0) {
        actor = actor_ptr->parent;
        actor_ptr = wgf_gfx_priv_actor_of(actor);
    }
    return actor;
}

wgf_actor_t wgf_gfx_priv_actor_find_on_stage(wgf_actor_t stage, const char *name)
{
    wgf_actor_t found = 0;
    int b, i;
    if (name == NULL || name[0] == '\0' || names.bucket_count == 0 || wgf_gfx_priv_actor_of(stage) == NULL) return 0;
    b = (int)(name_hash(name) & (uint32_t)(names.bucket_count - 1));
    for (i = 0; i < names.counts[b]; i++) {
        const wgf_actor_t actor = names.buckets[b][i];
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
        if (actor_ptr == NULL || strcmp(actor_ptr->name, name) != 0 || actor == stage || wgf_gfx_priv_actor_get_root(actor) != stage) {
            continue;
        }
        if (found != 0) {
            wgf_log_warn("wgf_gfx_actor: two actors on a stage are named \"%s\": find one by its path instead", name);
            return 0;
        }
        found = actor;
    }
    return found;
}

bool wgf_actor_set_enabled(wgf_actor_t actor, bool enabled)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    actor_ptr->enabled = enabled;
    return true;
}

bool wgf_actor_is_enabled(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL && actor_ptr->enabled;
}

bool wgf_actor_set_visible(wgf_actor_t actor, bool visible)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return false;
    actor_ptr->visible = visible;
    return true;
}

bool wgf_actor_is_visible(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL && actor_ptr->visible;
}

void wgf_gfx_priv_actor_shutdown(void)
{
    uint16_t i;
    if (!pool_ready) return;
    for (i = 1; i < actor_pool.capacity; i++) {
        const wgf_actor_t handle = wgf_core_priv_handle_pool_handle_from_index(&actor_pool, i);
        if (handle != 0) free_actor(handle);
    }
    wgf_core_priv_handle_pool_destroy(&actor_pool);
    names_shutdown();
    pool_ready = false;
    for (i = 0; i < NODE_TYPES; i++) kinds[i] = NULL; /* the parts set them again at their first create */
}

wgf_actor_kind_t wgf_gfx_priv_actor_get_root_type(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    while (actor_ptr != NULL && actor_ptr->parent != 0) {
        const wgf_gfx_priv_actor_t *parent_ptr = wgf_gfx_priv_actor_of(actor_ptr->parent);
        if (parent_ptr == NULL) break;
        actor_ptr = parent_ptr;
    }
    return actor_ptr != NULL ? actor_ptr->type : WGF_ACTOR_KIND_NONE;
}
