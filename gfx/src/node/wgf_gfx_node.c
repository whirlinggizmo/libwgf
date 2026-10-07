#include "wgf_node.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "node/wgf_gfx_node_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_log.h"

/* Nodes: one pool for every kind, so a node call finds any node with one lookup.
 * A node's children are an array of handles, in drawing order: a child that leaves
 * leaves a hole, closed when the array is next read whole (wgf_gfx_priv_node_children),
 * as libwgt's. */

static bool pool_ready;
static wgf_core_priv_handle_pool_t node_pool;
static wgf_gfx_priv_node_t *nodes;

/* --- the pool ---------------------------------------------------------------- */

wgf_gfx_priv_node_t *wgf_gfx_priv_node_of(wgf_node_t node)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve(&node_pool, node, &index)) return NULL;
    return &nodes[index];
}

wgf_node_t wgf_gfx_priv_node_create(wgf_node_type_t type)
{
    wgf_node_t handle;
    uint16_t index;
    wgf_gfx_priv_node_t *node_ptr;
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&node_pool, WGF_CORE_PRIV_HANDLE_KIND_NODE, (void **)&nodes,
                                                   sizeof(wgf_gfx_priv_node_t), 64, 65535);
        if (!pool_ready) return 0;
    }
    handle = wgf_core_priv_handle_pool_alloc(&node_pool);
    if (handle == 0 || !wgf_core_priv_handle_pool_resolve(&node_pool, handle, &index)) {
        wgf_log_error("wgf_gfx_node: no room for another node");
        return 0;
    }
    node_ptr = &nodes[index];
    memset(node_ptr, 0, sizeof(*node_ptr));
    node_ptr->type = type;
    node_ptr->rotation = wgf_quat_identity();
    node_ptr->scale = wgf_vec3_make(1.0f, 1.0f, 1.0f);
    node_ptr->local_dirty = true;
    node_ptr->world_dirty = true;
    node_ptr->enabled = true;
    node_ptr->visible = true;
    return handle;
}

wgf_mat4_t wgf_gfx_priv_node_get_local_matrix(wgf_gfx_priv_node_t *node_ptr)
{
    if (node_ptr->local_dirty) {
        const wgf_quat_t q = node_ptr->rotation;
        if (q.x == 0.0f && q.y == 0.0f && q.z == 0.0f) { /* not turned, as most 2D nodes: scale, then move */
            const wgf_vec3_t p = node_ptr->position, k = node_ptr->scale;
            wgf_mat4_t m = wgf_mat4_identity();
            m.m[0] = k.x;
            m.m[5] = k.y;
            m.m[10] = k.z;
            m.m[12] = p.x;
            m.m[13] = p.y;
            m.m[14] = p.z;
            node_ptr->local = m;
        } else {
            node_ptr->local = wgf_mat4_from_trs(node_ptr->position, q, node_ptr->scale);
        }
        node_ptr->local_dirty = false;
    }
    return node_ptr->local;
}

/* Mark `node` and everything under it world-dirty. A node already dirty has its
 * whole subtree dirty, so the walk stops there: moving one node every frame costs
 * little more than the first time. */
static void mark_world_dirty(wgf_node_t node)
{
    wgf_node_t stack[64];
    wgf_node_t *todo = stack;
    int count = 0, capacity = 64;
    todo[count++] = node;
    while (count > 0) {
        wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(todo[--count]);
        int i;
        if (node_ptr == NULL || node_ptr->world_dirty) continue;
        node_ptr->world_dirty = true;
        for (i = 0; i < node_ptr->child_count; i++) {
            if (count == capacity) {
                wgf_node_t *grown = (wgf_node_t *)malloc(sizeof(wgf_node_t) * (size_t)capacity * 2);
                if (grown == NULL) {
                    wgf_log_error("wgf_gfx_node: out of memory marking moved nodes; some may draw where they were");
                    break;
                }
                memcpy(grown, todo, sizeof(wgf_node_t) * (size_t)count);
                if (todo != stack) free(todo);
                todo = grown;
                capacity *= 2;
            }
            todo[count++] = node_ptr->children[i];
        }
    }
    if (todo != stack) free(todo);
}

/* `node_ptr` (`node`'s record) moved: its local matrix out of date, and its world and
 * everything under it. A node with no children, as most moved ones, is one flag. */
static void moved(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr)
{
    node_ptr->local_dirty = true;
    if (node_ptr->live_children == 0) {
        node_ptr->world_dirty = true;
        return;
    }
    mark_world_dirty(node);
}

void wgf_gfx_priv_node_transform_changed(wgf_node_t node)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr != NULL) moved(node, node_ptr);
}

wgf_mat4_t wgf_gfx_priv_node_world_of(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr)
{
    const wgf_gfx_priv_node_t *parent_ptr;
    if (!node_ptr->world_dirty) return node_ptr->world;
    parent_ptr = node_ptr->parent != 0 ? wgf_gfx_priv_node_of(node_ptr->parent) : NULL;
    if (parent_ptr != NULL && parent_ptr->world_dirty) return wgf_gfx_priv_node_get_world_matrix(node);
    node_ptr->world = parent_ptr != NULL ? wgf_mat4_mul(parent_ptr->world, wgf_gfx_priv_node_get_local_matrix(node_ptr))
                                         : wgf_gfx_priv_node_get_local_matrix(node_ptr);
    node_ptr->world_dirty = false;
    return node_ptr->world;
}

wgf_mat4_t wgf_gfx_priv_node_get_world_matrix(wgf_node_t node)
{
    wgf_node_t stack[64];
    wgf_node_t *chain = stack;
    int count = 0, capacity = 64;
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    wgf_mat4_t world;

    if (node_ptr == NULL) return wgf_mat4_identity();
    if (!node_ptr->world_dirty) return node_ptr->world;
    /* the dirty nodes from this one up to the first clean one (or the root) ... */
    while (node_ptr != NULL && node_ptr->world_dirty) {
        if (count == capacity) {
            wgf_node_t *grown = (wgf_node_t *)malloc(sizeof(wgf_node_t) * (size_t)capacity * 2);
            if (grown == NULL) break;
            memcpy(grown, chain, sizeof(wgf_node_t) * (size_t)count);
            if (chain != stack) free(chain);
            chain = grown;
            capacity *= 2;
        }
        chain[count++] = node;
        node = node_ptr->parent;
        node_ptr = wgf_gfx_priv_node_of(node);
    }
    /* ... then rebuilt from the top down */
    world = node_ptr != NULL ? node_ptr->world : wgf_mat4_identity();
    while (count > 0) {
        wgf_gfx_priv_node_t *next_ptr = wgf_gfx_priv_node_of(chain[--count]);
        const wgf_mat4_t local = wgf_gfx_priv_node_get_local_matrix(next_ptr);
        world = wgf_mat4_mul(world, local);
        next_ptr->world = world;
        next_ptr->world_dirty = false;
    }
    if (chain != stack) free(chain);
    return world;
}

/* --- the tree ------------------------------------------------------------------ */

/* Close the holes children left, in order, each moved child told its new slot. */
static void compact(wgf_gfx_priv_node_t *parent_ptr)
{
    int kept = 0;
    if (!parent_ptr->child_holes) return;
    for (int i = 0; i < parent_ptr->child_count; i++) {
        const wgf_node_t child = parent_ptr->children[i];
        if (child == 0) continue;
        wgf_gfx_priv_node_of(child)->slot = kept;
        parent_ptr->children[kept++] = child;
    }
    parent_ptr->child_count = kept;
    parent_ptr->child_holes = false;
}

const wgf_node_t *wgf_gfx_priv_node_children(wgf_node_t node, int *count)
{
    return wgf_gfx_priv_node_children_of(wgf_gfx_priv_node_of(node), count);
}

const wgf_node_t *wgf_gfx_priv_node_children_of(wgf_gfx_priv_node_t *node_ptr, int *count)
{
    if (node_ptr == NULL || node_ptr->live_children == 0) {
        *count = 0;
        return NULL;
    }
    compact(node_ptr);
    *count = node_ptr->child_count;
    return node_ptr->children;
}

/* `child` out of `parent`'s children: a hole where it was, or, the last, one slot fewer. */
static void remove_child(wgf_node_t parent, wgf_node_t child)
{
    wgf_gfx_priv_node_t *parent_ptr = wgf_gfx_priv_node_of(parent);
    const wgf_gfx_priv_node_t *child_ptr = wgf_gfx_priv_node_of(child);
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
static bool insert_child(wgf_node_t parent, wgf_node_t child, int index)
{
    wgf_gfx_priv_node_t *parent_ptr = wgf_gfx_priv_node_of(parent);
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
        wgf_node_t *grown =
            (wgf_node_t *)realloc(parent_ptr->children, sizeof(wgf_node_t) * (size_t)capacity);
        if (grown == NULL) return false;
        parent_ptr->children = grown;
        parent_ptr->child_capacity = capacity;
    }
    memmove(&parent_ptr->children[index + 1], &parent_ptr->children[index],
            sizeof(wgf_node_t) * (size_t)(parent_ptr->child_count - index));
    parent_ptr->children[index] = child;
    parent_ptr->child_count++;
    parent_ptr->live_children++;
    for (int i = index; i < parent_ptr->child_count; i++) { /* it, and those it moved along */
        if (parent_ptr->children[i] != 0) wgf_gfx_priv_node_of(parent_ptr->children[i])->slot = i;
    }
    return true;
}

static bool is_under(wgf_node_t node, wgf_node_t ancestor)
{
    const wgf_gfx_priv_node_t *node_ptr;
    while ((node_ptr = wgf_gfx_priv_node_of(node)) != NULL) {
        if (node == ancestor) return true;
        node = node_ptr->parent;
    }
    return false;
}

#define NODE_TYPES 16 /* more than wgf_node_type_t's last */
_Static_assert(WGF_NODE_TYPE_SHAPE3D < NODE_TYPES, "a node type past the kinds' table");
static const wgf_gfx_priv_node_kind_t *kinds[NODE_TYPES];

static const wgf_gfx_priv_model_hooks_t *model_hooks;

void wgf_gfx_priv_set_model_hooks(const wgf_gfx_priv_model_hooks_t *hooks)
{
    model_hooks = hooks;
}

const wgf_gfx_priv_model_hooks_t *wgf_gfx_priv_get_model_hooks(void)
{
    return model_hooks;
}

void wgf_gfx_priv_node_set_kind(wgf_node_type_t type, const wgf_gfx_priv_node_kind_t *kind)
{
    if ((int)type >= 0 && type < NODE_TYPES) kinds[type] = kind;
}

const wgf_gfx_priv_node_kind_t *wgf_gfx_priv_node_get_kind(wgf_node_type_t type)
{
    return (int)type >= 0 && type < NODE_TYPES ? kinds[type] : NULL;
}

/* A node gone: what it held let go, its children list freed, its slot freed. */
static void free_node(wgf_node_t node)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    const wgf_gfx_priv_node_kind_t *kind;
    if (node_ptr == NULL) return;
    kind = wgf_gfx_priv_node_get_kind(node_ptr->type);
    if (kind != NULL && kind->free != NULL) kind->free(node, node_ptr);
    free(node_ptr->name);
    free(node_ptr->children);
    wgf_core_priv_handle_pool_free(&node_pool, node);
}

/* --- the public API ------------------------------------------------------------ */

wgf_node_t wgf_node_create(void)
{
    return wgf_gfx_priv_node_create(WGF_NODE_TYPE_NODE);
}

void wgf_node_destroy(wgf_node_t node, wgf_node_destroy_t children)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    wgf_node_t parent;
    int index;

    if (node_ptr == NULL) return;
    parent = node_ptr->parent;
    if (children == WGF_NODE_KEEP_CHILDREN && parent != 0) {
        compact(wgf_gfx_priv_node_of(parent)); /* its place, among its siblings as they read */
        node_ptr = wgf_gfx_priv_node_of(node);
    }
    index = node_ptr->slot;
    remove_child(parent, node);

    if (children == WGF_NODE_KEEP_CHILDREN) {
        /* each child up a level, in the node's place, where it is now */
        const wgf_mat4_t local = wgf_gfx_priv_node_get_local_matrix(node_ptr);
        int i;
        compact(node_ptr);
        for (i = 0; i < node_ptr->child_count; i++) {
            const wgf_node_t child = node_ptr->children[i];
            wgf_gfx_priv_node_t *child_ptr = wgf_gfx_priv_node_of(child);
            const wgf_mat4_t child_local = wgf_gfx_priv_node_get_local_matrix(child_ptr);
            const wgf_mat4_t moved = wgf_mat4_mul(local, child_local);
            child_ptr->position = wgf_mat4_get_translation(moved);
            child_ptr->scale = wgf_mat4_get_scale(moved);
            child_ptr->rotation = wgf_mat4_get_rotation(moved);
            child_ptr->parent = 0;
            if (parent != 0 && insert_child(parent, child, index + i)) child_ptr->parent = parent;
            wgf_gfx_priv_node_transform_changed(child);
        }
        free_node(node);
        return;
    }

    {
        /* the node and everything under it, with a list rather than C recursion, so
           a deep tree can't run out of stack */
        int count = 1, capacity = 64;
        wgf_node_t *todo = (wgf_node_t *)malloc(sizeof(wgf_node_t) * (size_t)capacity);
        if (todo == NULL) {
            wgf_log_error("wgf_gfx_node: out of memory destroying a node");
            return;
        }
        todo[0] = node;
        while (count > 0) {
            const wgf_node_t next = todo[--count];
            const wgf_gfx_priv_node_t *next_ptr = wgf_gfx_priv_node_of(next);
            int i;
            if (next_ptr == NULL) continue;
            for (i = 0; i < next_ptr->child_count; i++) {
                if (next_ptr->children[i] == 0) continue; /* a hole */
                if (count == capacity) {
                    wgf_node_t *grown =
                        (wgf_node_t *)realloc(todo, sizeof(wgf_node_t) * (size_t)(capacity * 2));
                    if (grown == NULL) break; /* what is left stays, detached */
                    todo = grown;
                    capacity *= 2;
                }
                todo[count++] = next_ptr->children[i];
            }
            free_node(next);
        }
        free(todo);
    }
}

wgf_node_type_t wgf_node_get_type(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL ? node_ptr->type : WGF_NODE_TYPE_NONE;
}

bool wgf_node_set_parent(wgf_node_t node, wgf_node_t parent)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL || node_ptr->type == WGF_NODE_TYPE_CANVAS || node_ptr->type == WGF_NODE_TYPE_STAGE) {
        return false;
    }
    if (parent != 0 && (wgf_gfx_priv_node_of(parent) == NULL || is_under(parent, node))) return false;
    if (parent == node_ptr->parent) return true;
    remove_child(node_ptr->parent, node);
    wgf_gfx_priv_node_of(node)->parent = 0;
    if (parent != 0 && insert_child(parent, node, 1 << 30)) wgf_gfx_priv_node_of(node)->parent = parent;
    mark_world_dirty(node); /* a new parent: a new world */
    return wgf_gfx_priv_node_of(node)->parent == parent;
}

wgf_node_t wgf_node_get_parent(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL ? node_ptr->parent : 0;
}

int wgf_node_get_child_count(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL ? node_ptr->live_children : 0;
}

wgf_node_t wgf_node_get_child(wgf_node_t node, int index)
{
    int count;
    const wgf_node_t *children = wgf_gfx_priv_node_children(node, &count);
    return index >= 0 && index < count ? children[index] : 0;
}

bool wgf_node_set_index(wgf_node_t node, int index)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    wgf_node_t parent;
    if (node_ptr == NULL || node_ptr->parent == 0) return false;
    parent = node_ptr->parent;
    remove_child(parent, node);
    return insert_child(parent, node, index); /* the slot it left is free, so this can't run out */
}

int wgf_node_get_index(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    wgf_gfx_priv_node_t *parent_ptr;
    if (node_ptr == NULL || (parent_ptr = wgf_gfx_priv_node_of(node_ptr->parent)) == NULL) return 0;
    compact(parent_ptr);
    return wgf_gfx_priv_node_of(node)->slot;
}

bool wgf_node_set_position(wgf_node_t node, float x, float y, float z)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL) return false;
    node_ptr->position = wgf_vec3_make(x, y, z);
    moved(node, node_ptr);
    return true;
}

wgf_vec3_t wgf_node_get_position(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL ? node_ptr->position : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_node_set_rotation(wgf_node_t node, float x, float y, float z)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL) return false;
    node_ptr->rotation = wgf_quat_from_euler(wgf_vec3_make(x, y, z));
    wgf_gfx_priv_node_transform_changed(node);
    return true;
}

bool wgf_node_look_at(wgf_node_t node, float x, float y, float z, float up_x, float up_y, float up_z)
{
    const wgf_vec3_t up = wgf_vec3_make(up_x, up_y, up_z);
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    wgf_vec3_t forward;
    wgf_quat_t parent_rotation;
    if (node_ptr == NULL) return false;
    forward = wgf_vec3_sub(wgf_vec3_make(x, y, z), wgf_mat4_get_translation(wgf_gfx_priv_node_get_world_matrix(node)));
    if (wgf_vec3_length(forward) < 1e-6f ||
        wgf_vec3_length(wgf_vec3_cross(up, forward)) < 1e-6f * wgf_vec3_length(forward)) {
        return false;
    }
    /* relative to the parent: its world rotation taken back off */
    parent_rotation = node_ptr->parent != 0
                          ? wgf_mat4_get_rotation(wgf_gfx_priv_node_get_world_matrix(node_ptr->parent))
                          : wgf_quat_identity();
    node_ptr = wgf_gfx_priv_node_of(node);
    node_ptr->rotation = wgf_quat_mul(wgf_quat_conjugate(parent_rotation), wgf_quat_look_rotation(forward, up));
    wgf_gfx_priv_node_transform_changed(node);
    return true;
}

wgf_vec3_t wgf_node_get_rotation(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL ? wgf_quat_to_euler(node_ptr->rotation) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_node_set_scale(wgf_node_t node, float x, float y, float z)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL) return false;
    node_ptr->scale = wgf_vec3_make(x, y, z);
    wgf_gfx_priv_node_transform_changed(node);
    return true;
}

wgf_vec3_t wgf_node_get_scale(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL ? node_ptr->scale : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_node_set_transform(wgf_node_t node, float position_x, float position_y, float position_z,
                               float rotation_x, float rotation_y, float rotation_z, float scale_x, float scale_y,
                               float scale_z)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL) return false;
    node_ptr->position = wgf_vec3_make(position_x, position_y, position_z);
    node_ptr->rotation = wgf_quat_from_euler(wgf_vec3_make(rotation_x, rotation_y, rotation_z));
    node_ptr->scale = wgf_vec3_make(scale_x, scale_y, scale_z);
    wgf_gfx_priv_node_transform_changed(node);
    return true;
}

wgf_vec3_t wgf_node_get_world_position(wgf_node_t node)
{
    const wgf_mat4_t world = wgf_gfx_priv_node_get_world_matrix(node);
    if (wgf_gfx_priv_node_of(node) == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(world.m[12], world.m[13], world.m[14]);
}

bool wgf_node_set_name(wgf_node_t node, const char *name)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    char *copy = NULL;
    if (node_ptr == NULL) return false;
    if (name != NULL && name[0] != '\0') {
        const size_t n = strlen(name);
        copy = (char *)malloc(n + 1);
        if (copy == NULL) {
            wgf_log_error("wgf_gfx_node: out of memory");
            return false;
        }
        memcpy(copy, name, n + 1);
    }
    free(node_ptr->name);
    node_ptr->name = copy;
    return true;
}

const char *wgf_node_get_name(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL && node_ptr->name != NULL ? node_ptr->name : "";
}

wgf_node_t wgf_node_find(wgf_node_t root, const char *name)
{
    wgf_node_t *todo;
    wgf_node_t found = 0;
    int count = 0, capacity = 64;
    if (name == NULL || name[0] == '\0' || wgf_gfx_priv_node_of(root) == NULL) return 0;
    todo = (wgf_node_t *)malloc(sizeof(wgf_node_t) * (size_t)capacity);
    if (todo == NULL) return 0;
    todo[count++] = root;
    while (count > 0 && found == 0) {
        const wgf_node_t next = todo[--count];
        const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(next);
        if (node_ptr->name != NULL && strcmp(node_ptr->name, name) == 0) {
            found = next;
            break;
        }
        for (int i = node_ptr->child_count - 1; i >= 0; i--) { /* the first child comes off first */
            if (node_ptr->children[i] == 0) continue; /* a hole */
            if (count == capacity) {
                wgf_node_t *grown = (wgf_node_t *)realloc(todo, sizeof(wgf_node_t) * (size_t)(capacity * 2));
                if (grown == NULL) break;
                todo = grown;
                capacity *= 2;
            }
            todo[count++] = node_ptr->children[i];
        }
    }
    free(todo);
    return found;
}

bool wgf_node_set_enabled(wgf_node_t node, bool enabled)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL) return false;
    node_ptr->enabled = enabled;
    return true;
}

bool wgf_node_is_enabled(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL && node_ptr->enabled;
}

bool wgf_node_set_visible(wgf_node_t node, bool visible)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    if (node_ptr == NULL) return false;
    node_ptr->visible = visible;
    return true;
}

bool wgf_node_is_visible(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    return node_ptr != NULL && node_ptr->visible;
}

void wgf_gfx_priv_node_shutdown(void)
{
    uint16_t i;
    if (!pool_ready) return;
    for (i = 1; i < node_pool.capacity; i++) {
        const wgf_node_t handle = wgf_core_priv_handle_pool_handle_from_index(&node_pool, i);
        if (handle != 0) free_node(handle);
    }
    wgf_core_priv_handle_pool_destroy(&node_pool);
    pool_ready = false;
    for (i = 0; i < NODE_TYPES; i++) kinds[i] = NULL; /* the parts set them again at their first create */
}

wgf_node_type_t wgf_gfx_priv_node_get_root_type(wgf_node_t node)
{
    const wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(node);
    while (node_ptr != NULL && node_ptr->parent != 0) {
        const wgf_gfx_priv_node_t *parent_ptr = wgf_gfx_priv_node_of(node_ptr->parent);
        if (parent_ptr == NULL) break;
        node_ptr = parent_ptr;
    }
    return node_ptr != NULL ? node_ptr->type : WGF_NODE_TYPE_NONE;
}
