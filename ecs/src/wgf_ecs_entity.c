#include "wgf_entity.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_bounds.h"
#include "wgf_ecs_priv.h"
#include "wgf_emitter2d.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_text.h"

/* Entities (wgf_entity.h): their records and nodes, transforms, and components. */

static const wgf_ecs_priv_ids_t *ids(void)
{
    return wgf_ecs_priv_ids();
}

int wgf_ecs_priv_node_slot(wgf_component_t component)
{
    switch (component) {
        case WGF_COMPONENT_SHAPE2D: return 0;
        case WGF_COMPONENT_SPRITE: return 1;
        case WGF_COMPONENT_TEXT: return 2;
        case WGF_COMPONENT_EMITTER2D: return 3;
        default: return -1;
    }
}

wgf_entity_t wgf_entity_create(wgf_node_t parent)
{
    wgf_node_t node;
    wgf_entity_t entity;
    if (parent != 0 && wgf_node_get_type(parent) == WGF_NODE_TYPE_NONE) return 0;
    if (!wgf_ecs_priv_start()) return 0;
    node = wgf_node_create();
    if (node == 0) return 0;
    if (parent != 0) wgf_node_set_parent(node, parent);
    entity = wgf_ecs_priv_new_record(node);
    if (entity == 0) wgf_node_destroy(node, WGF_NODE_DESTROY_CHILDREN);
    return entity;
}

static bool is_under(wgf_node_t node, wgf_node_t ancestor)
{
    for (node = wgf_node_get_parent(node); node != 0; node = wgf_node_get_parent(node)) {
        if (node == ancestor) return true;
    }
    return false;
}

bool wgf_entity_destroy(wgf_entity_t entity)
{
    wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    wgf_node_t node;
    wgf_entity_t *all;
    int count = 0, i;
    if (record == NULL) return false;
    node = record->node;
    if (record->behavior != NULL) wgf_ecs_priv_raise(WGF_ECS_EVENT_DESTROYED, entity, 0);
    wgf_ecs_priv_forget_pairs(entity);
    if (record->voice != 0) wgf_voice_destroy(record->voice);
    wgf_ecs_priv_free_record(entity);
    /* entities whose nodes are under its node go with it, before the nodes do */
    all = wgf_ecs_priv_entities(&count);
    for (i = 0; i < count; i++) {
        const wgf_ecs_priv_entity_t *other = wgf_ecs_priv_entity_of(all[i]);
        if (other != NULL && is_under(other->node, node)) wgf_entity_destroy(all[i]);
    }
    free(all);
    wgf_node_destroy(node, WGF_NODE_DESTROY_CHILDREN);
    return true;
}

bool wgf_entity_is_alive(wgf_entity_t entity)
{
    return wgf_ecs_priv_entity_of(entity) != NULL;
}

wgf_node_t wgf_entity_get_node(wgf_entity_t entity)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    return record != NULL ? record->node : 0;
}

bool wgf_entity_set_name(wgf_entity_t entity, const char *name)
{
    wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    const size_t n = name != NULL ? strlen(name) : 0;
    if (record == NULL || n >= WGF_ECS_PRIV_NAME_MAX) return false;
    memcpy(record->name, n > 0 ? name : "", n + 1);
    wgf_node_set_name(record->node, record->name);
    return true;
}

const char *wgf_entity_get_name(wgf_entity_t entity)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    return record != NULL ? record->name : "";
}

wgf_entity_t wgf_entity_find(const char *name)
{
    wgf_entity_t *all, found = 0;
    int count = 0, i;
    if (name == NULL || name[0] == '\0') return 0;
    all = wgf_ecs_priv_entities(&count);
    for (i = 0; i < count && found == 0; i++) {
        if (strcmp(wgf_ecs_priv_entity_of(all[i])->name, name) == 0) found = all[i];
    }
    free(all);
    return found;
}

/* ---- the transform -------------------------------------------------------------- */

static wgf_ecs_priv_transform_t *transform_of(wgf_entity_t entity)
{
    return wgf_ecs_priv_world() != NULL ? (wgf_ecs_priv_transform_t *)wgf_ecs_priv_get(entity, ids()->transform) : NULL;
}

static bool set3(float *v, float x, float y, float z)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return false;
    v[0] = x;
    v[1] = y;
    v[2] = z;
    return true;
}

bool wgf_entity_set_position(wgf_entity_t entity, float x, float y, float z)
{
    wgf_ecs_priv_transform_t *t = transform_of(entity);
    return t != NULL && set3(t->position, x, y, z);
}

wgf_vec3_t wgf_entity_get_position(wgf_entity_t entity)
{
    const wgf_ecs_priv_transform_t *t = transform_of(entity);
    return t != NULL ? wgf_vec3_make(t->position[0], t->position[1], t->position[2]) : wgf_vec3_make(0, 0, 0);
}

bool wgf_entity_set_rotation(wgf_entity_t entity, float x, float y, float z)
{
    wgf_ecs_priv_transform_t *t = transform_of(entity);
    return t != NULL && set3(t->rotation, x, y, z);
}

wgf_vec3_t wgf_entity_get_rotation(wgf_entity_t entity)
{
    const wgf_ecs_priv_transform_t *t = transform_of(entity);
    return t != NULL ? wgf_vec3_make(t->rotation[0], t->rotation[1], t->rotation[2]) : wgf_vec3_make(0, 0, 0);
}

bool wgf_entity_set_scale(wgf_entity_t entity, float x, float y, float z)
{
    wgf_ecs_priv_transform_t *t = transform_of(entity);
    return t != NULL && set3(t->scale, x, y, z);
}

wgf_vec3_t wgf_entity_get_scale(wgf_entity_t entity)
{
    const wgf_ecs_priv_transform_t *t = transform_of(entity);
    return t != NULL ? wgf_vec3_make(t->scale[0], t->scale[1], t->scale[2]) : wgf_vec3_make(0, 0, 0);
}

bool wgf_entity_set_transform(wgf_entity_t entity, float position_x, float position_y, float position_z,
                              float rotation_x, float rotation_y, float rotation_z, float scale_x, float scale_y,
                              float scale_z)
{
    wgf_ecs_priv_transform_t *t = transform_of(entity);
    float p[3], r[3], s[3];
    if (t == NULL || !set3(p, position_x, position_y, position_z) || !set3(r, rotation_x, rotation_y, rotation_z) ||
        !set3(s, scale_x, scale_y, scale_z)) {
        return false;
    }
    memcpy(t->position, p, sizeof(p));
    memcpy(t->rotation, r, sizeof(r));
    memcpy(t->scale, s, sizeof(s));
    return true;
}

bool wgf_entity_snap(wgf_entity_t entity)
{
    wgf_ecs_priv_transform_t *t = transform_of(entity);
    if (t == NULL) return false;
    memcpy(t->prev_position, t->position, sizeof(t->position));
    memcpy(t->prev_scale, t->scale, sizeof(t->scale));
    t->prev_rotation = wgf_quat_from_euler(wgf_vec3_make(t->rotation[0], t->rotation[1], t->rotation[2]));
    return true;
}

int wgf_entity_get_positions(const wgf_entity_t *entities, int count, float *out, int out_count)
{
    int i, filled = 0;
    if (entities == NULL || out == NULL) return 0;
    for (i = 0; i < count && filled + 3 <= out_count; i++) {
        const wgf_ecs_priv_transform_t *t = transform_of(entities[i]);
        out[filled] = t != NULL ? t->position[0] : 0.0f;
        out[filled + 1] = t != NULL ? t->position[1] : 0.0f;
        out[filled + 2] = t != NULL ? t->position[2] : 0.0f;
        filled += 3;
    }
    return filled;
}

bool wgf_entity_set_positions(const wgf_entity_t *entities, int count, const float *positions, int positions_count)
{
    int i;
    if (entities == NULL || positions == NULL || count < 0 || positions_count < 3 * count) return false;
    for (i = 0; i < count; i++) {
        wgf_ecs_priv_transform_t *t = transform_of(entities[i]);
        if (t != NULL) set3(t->position, positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
    }
    return true;
}

/* ---- components ------------------------------------------------------------------- */

static ecs_entity_t data_id(wgf_component_t component)
{
    switch (component) {
        case WGF_COMPONENT_MOTION: return ids()->motion;
        case WGF_COMPONENT_BOUNDS: return ids()->bounds;
        case WGF_COMPONENT_LIFETIME: return ids()->lifetime;
        case WGF_COMPONENT_COLLIDER: return ids()->collider;
        default: return 0;
    }
}

static wgf_node_t make_node(wgf_component_t component)
{
    switch (component) {
        case WGF_COMPONENT_SHAPE2D: return wgf_shape2d_create();
        case WGF_COMPONENT_SPRITE: return wgf_sprite_create(0);
        case WGF_COMPONENT_TEXT: return wgf_text_create(0);
        case WGF_COMPONENT_EMITTER2D: return wgf_emitter2d_create();
        default: return 0;
    }
}

bool wgf_entity_has_component(wgf_entity_t entity, wgf_component_t component)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    const int slot = wgf_ecs_priv_node_slot(component);
    if (record == NULL) return false;
    if (slot >= 0) return record->parts[slot] != 0;
    if (component == WGF_COMPONENT_VOICE) return record->voice != 0;
    if (component == WGF_COMPONENT_BEHAVIOR) return record->behavior != NULL;
    return data_id(component) != 0 && ecs_has_id(wgf_ecs_priv_world(), record->id, data_id(component));
}

bool wgf_entity_add_component(wgf_entity_t entity, wgf_component_t component)
{
    wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    const int slot = wgf_ecs_priv_node_slot(component);
    if (record == NULL || (int)component <= WGF_COMPONENT_NONE || component > WGF_COMPONENT_VOICE) return false;
    if (wgf_entity_has_component(entity, component)) return true;
    if (slot >= 0) {
        const wgf_node_t node = make_node(component);
        if (node == 0) return false;
        record = wgf_ecs_priv_entity_of(entity); /* making a node can't move records, but say so */
        wgf_node_set_parent(node, record->node);
        record->parts[slot] = node;
        return true;
    }
    if (component == WGF_COMPONENT_VOICE) {
        record->voice = wgf_voice_create(0);
        return record->voice != 0;
    }
    if (component == WGF_COMPONENT_BEHAVIOR) {
        record->behavior = (wgf_ecs_priv_behavior_t *)calloc(1, sizeof(wgf_ecs_priv_behavior_t));
        if (record->behavior == NULL) return false;
        wgf_ecs_priv_raise(WGF_ECS_EVENT_CREATED, entity, 0);
        return true;
    }
    switch (component) {
        case WGF_COMPONENT_MOTION: {
            wgf_ecs_priv_motion_t m;
            memset(&m, 0, sizeof(m));
            ecs_set_id(wgf_ecs_priv_world(), record->id, ids()->motion, sizeof(m), &m);
            break;
        }
        case WGF_COMPONENT_BOUNDS: {
            wgf_ecs_priv_bounds_t b;
            memset(&b, 0, sizeof(b));
            b.rect[2] = 800.0f;
            b.rect[3] = 600.0f;
            b.mode = WGF_BOUNDS_MODE_WRAP;
            ecs_set_id(wgf_ecs_priv_world(), record->id, ids()->bounds, sizeof(b), &b);
            break;
        }
        case WGF_COMPONENT_LIFETIME: {
            wgf_ecs_priv_lifetime_t l;
            l.seconds = 1.0f;
            ecs_set_id(wgf_ecs_priv_world(), record->id, ids()->lifetime, sizeof(l), &l);
            break;
        }
        default: { /* the collider */
            wgf_ecs_priv_collider_t c;
            c.radius = 1.0f;
            c.layer = 1;
            c.mask = -1;
            ecs_set_id(wgf_ecs_priv_world(), record->id, ids()->collider, sizeof(c), &c);
            break;
        }
    }
    return true;
}

bool wgf_entity_remove_component(wgf_entity_t entity, wgf_component_t component)
{
    wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    const int slot = wgf_ecs_priv_node_slot(component);
    if (record == NULL || !wgf_entity_has_component(entity, component)) return false;
    if (slot >= 0) {
        wgf_node_destroy(record->parts[slot], WGF_NODE_DESTROY_CHILDREN);
        record->parts[slot] = 0;
    } else if (component == WGF_COMPONENT_VOICE) {
        wgf_voice_destroy(record->voice);
        record->voice = 0;
    } else if (component == WGF_COMPONENT_BEHAVIOR) {
        free(record->behavior);
        record->behavior = NULL;
    } else {
        if (component == WGF_COMPONENT_COLLIDER) wgf_ecs_priv_forget_pairs(entity);
        ecs_remove_id(wgf_ecs_priv_world(), record->id, data_id(component));
    }
    return true;
}

wgf_node_t wgf_entity_get_component_node(wgf_entity_t entity, wgf_component_t component)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    const int slot = wgf_ecs_priv_node_slot(component);
    return record != NULL && slot >= 0 ? record->parts[slot] : 0;
}

wgf_voice_t wgf_entity_get_voice(wgf_entity_t entity)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    return record != NULL ? record->voice : 0;
}
