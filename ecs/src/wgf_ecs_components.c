#include <math.h>

#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_ecs_priv.h"
#include "wgf_lifetime.h"
#include "wgf_motion.h"

/* The systems' components' calls: motion, bounds, lifetime, collider (their headers). */

static wgf_ecs_priv_motion_t *motion_of(wgf_entity_t entity)
{
    return wgf_ecs_priv_world() != NULL ? (wgf_ecs_priv_motion_t *)wgf_ecs_priv_get(entity, wgf_ecs_priv_ids()->motion)
                                        : NULL;
}

static wgf_ecs_priv_bounds_t *bounds_of(wgf_entity_t entity)
{
    return wgf_ecs_priv_world() != NULL ? (wgf_ecs_priv_bounds_t *)wgf_ecs_priv_get(entity, wgf_ecs_priv_ids()->bounds)
                                        : NULL;
}

static wgf_ecs_priv_lifetime_t *lifetime_of(wgf_entity_t entity)
{
    return wgf_ecs_priv_world() != NULL
               ? (wgf_ecs_priv_lifetime_t *)wgf_ecs_priv_get(entity, wgf_ecs_priv_ids()->lifetime)
               : NULL;
}

static wgf_ecs_priv_collider_t *collider_of(wgf_entity_t entity)
{
    return wgf_ecs_priv_world() != NULL
               ? (wgf_ecs_priv_collider_t *)wgf_ecs_priv_get(entity, wgf_ecs_priv_ids()->collider)
               : NULL;
}

static bool finite3(float x, float y, float z)
{
    return isfinite(x) && isfinite(y) && isfinite(z);
}

/* ---- motion ------------------------------------------------------------------------ */

bool wgf_motion_set_velocity(wgf_entity_t entity, float x, float y, float z)
{
    wgf_ecs_priv_motion_t *m = motion_of(entity);
    if (m == NULL || !finite3(x, y, z)) return false;
    m->velocity[0] = x;
    m->velocity[1] = y;
    m->velocity[2] = z;
    return true;
}

wgf_vec3_t wgf_motion_get_velocity(wgf_entity_t entity)
{
    const wgf_ecs_priv_motion_t *m = motion_of(entity);
    return m != NULL ? wgf_vec3_make(m->velocity[0], m->velocity[1], m->velocity[2]) : wgf_vec3_make(0, 0, 0);
}

bool wgf_motion_set_spin(wgf_entity_t entity, float x, float y, float z)
{
    wgf_ecs_priv_motion_t *m = motion_of(entity);
    if (m == NULL || !finite3(x, y, z)) return false;
    m->spin[0] = x;
    m->spin[1] = y;
    m->spin[2] = z;
    return true;
}

wgf_vec3_t wgf_motion_get_spin(wgf_entity_t entity)
{
    const wgf_ecs_priv_motion_t *m = motion_of(entity);
    return m != NULL ? wgf_vec3_make(m->spin[0], m->spin[1], m->spin[2]) : wgf_vec3_make(0, 0, 0);
}

bool wgf_motion_set_damping(wgf_entity_t entity, float damping)
{
    wgf_ecs_priv_motion_t *m = motion_of(entity);
    if (m == NULL || isnan(damping)) return false;
    m->damping = damping < 0.0f ? 0.0f : (damping > 1.0f ? 1.0f : damping);
    return true;
}

float wgf_motion_get_damping(wgf_entity_t entity)
{
    const wgf_ecs_priv_motion_t *m = motion_of(entity);
    return m != NULL ? m->damping : 0.0f;
}

bool wgf_motion_set_max_speed(wgf_entity_t entity, float speed)
{
    wgf_ecs_priv_motion_t *m = motion_of(entity);
    if (m == NULL || isnan(speed)) return false;
    m->max_speed = speed > 0.0f ? speed : 0.0f;
    return true;
}

float wgf_motion_get_max_speed(wgf_entity_t entity)
{
    const wgf_ecs_priv_motion_t *m = motion_of(entity);
    return m != NULL ? m->max_speed : 0.0f;
}

/* ---- bounds ------------------------------------------------------------------------ */

bool wgf_bounds_set_rect(wgf_entity_t entity, float x, float y, float width, float height)
{
    wgf_ecs_priv_bounds_t *b = bounds_of(entity);
    if (b == NULL || !finite3(x, y, width) || !isfinite(height) || width < 0.0f || height < 0.0f) return false;
    b->rect[0] = x;
    b->rect[1] = y;
    b->rect[2] = width;
    b->rect[3] = height;
    return true;
}

wgf_vec4_t wgf_bounds_get_rect(wgf_entity_t entity)
{
    const wgf_ecs_priv_bounds_t *b = bounds_of(entity);
    return b != NULL ? wgf_vec4_make(b->rect[0], b->rect[1], b->rect[2], b->rect[3]) : wgf_vec4_make(0, 0, 0, 0);
}

bool wgf_bounds_set_mode(wgf_entity_t entity, wgf_bounds_mode_t mode)
{
    wgf_ecs_priv_bounds_t *b = bounds_of(entity);
    if (b == NULL || (int)mode < WGF_BOUNDS_MODE_WRAP || mode > WGF_BOUNDS_MODE_DESTROY) return false;
    b->mode = mode;
    return true;
}

wgf_bounds_mode_t wgf_bounds_get_mode(wgf_entity_t entity)
{
    const wgf_ecs_priv_bounds_t *b = bounds_of(entity);
    return b != NULL ? (wgf_bounds_mode_t)b->mode : WGF_BOUNDS_MODE_WRAP;
}

bool wgf_bounds_set_margin(wgf_entity_t entity, float margin)
{
    wgf_ecs_priv_bounds_t *b = bounds_of(entity);
    if (b == NULL || isnan(margin)) return false;
    b->margin = margin > 0.0f ? margin : 0.0f;
    return true;
}

float wgf_bounds_get_margin(wgf_entity_t entity)
{
    const wgf_ecs_priv_bounds_t *b = bounds_of(entity);
    return b != NULL ? b->margin : 0.0f;
}

/* ---- lifetime ---------------------------------------------------------------------- */

bool wgf_lifetime_set_seconds(wgf_entity_t entity, float seconds)
{
    wgf_ecs_priv_lifetime_t *l = lifetime_of(entity);
    if (l == NULL || !isfinite(seconds) || seconds < 0.0f) return false;
    l->seconds = seconds;
    return true;
}

float wgf_lifetime_get_seconds(wgf_entity_t entity)
{
    const wgf_ecs_priv_lifetime_t *l = lifetime_of(entity);
    return l != NULL ? l->seconds : 0.0f;
}

/* ---- collider ---------------------------------------------------------------------- */

bool wgf_collider_set_radius(wgf_entity_t entity, float radius)
{
    wgf_ecs_priv_collider_t *c = collider_of(entity);
    if (c == NULL || !isfinite(radius) || radius < 0.0f) return false;
    c->radius = radius;
    return true;
}

float wgf_collider_get_radius(wgf_entity_t entity)
{
    const wgf_ecs_priv_collider_t *c = collider_of(entity);
    return c != NULL ? c->radius : 0.0f;
}

bool wgf_collider_set_layer(wgf_entity_t entity, int layer)
{
    wgf_ecs_priv_collider_t *c = collider_of(entity);
    if (c == NULL) return false;
    c->layer = layer;
    return true;
}

int wgf_collider_get_layer(wgf_entity_t entity)
{
    const wgf_ecs_priv_collider_t *c = collider_of(entity);
    return c != NULL ? c->layer : 0;
}

bool wgf_collider_set_mask(wgf_entity_t entity, int mask)
{
    wgf_ecs_priv_collider_t *c = collider_of(entity);
    if (c == NULL) return false;
    c->mask = mask;
    return true;
}

int wgf_collider_get_mask(wgf_entity_t entity)
{
    const wgf_ecs_priv_collider_t *c = collider_of(entity);
    return c != NULL ? c->mask : 0;
}

bool wgf_collider_set_enabled(wgf_entity_t entity, bool enabled)
{
    wgf_ecs_priv_collider_t *c = collider_of(entity);
    if (c == NULL) return false;
    c->enabled = enabled;
    return true;
}

bool wgf_collider_is_enabled(wgf_entity_t entity)
{
    const wgf_ecs_priv_collider_t *c = collider_of(entity);
    return c != NULL && c->enabled;
}
