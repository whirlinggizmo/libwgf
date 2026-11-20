#include "wgf_body2d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_actor.h"
#include "wgf_collide2d.h"
#include "wgf_ecs_priv.h"
#include "wgf_log.h"

/* 2D bodies (wgf_body2d.h), helix-legacy's Body2D as a component: its calls, the grids of
 * solid cells by parent, and its system, run each tick after motion: every body's box moved
 * and slid (wgf_collide2d_move_rects) against the solid colliders' bounds and the grid's
 * solid cells near its way, gathered as rectangles. */

typedef struct grid_t {
    wgf_actor_t parent;
    int *cells;
    int count, columns, rows;
    float size, x, y;
} grid_t;

/* A solid collider's bounds this tick. */
typedef struct solid_t {
    wgf_actor_t actor, parent;
    int32_t layer;
    float lo_x, lo_y, hi_x, hi_y;
} solid_t;

static struct {
    grid_t *grids;
    int grid_count, grid_capacity;
    solid_t *solids; /* kept between ticks, made again each */
    int solid_capacity;
    float *rects; /* a body's rectangles near its way, four floats each */
    int rect_capacity;
} bodies;

static wgf_ecs_priv_body2d_t *body_of(wgf_actor_t actor)
{
    return wgf_ecs_priv_started() ? (wgf_ecs_priv_body2d_t *)wgf_ecs_priv_get(actor, wgf_ecs_priv_ids()->body2d)
                                  : NULL;
}

/* ---- the calls ---------------------------------------------------------------------- */

bool wgf_body2d_set_size(wgf_actor_t actor, float width, float height)
{
    wgf_ecs_priv_body2d_t *b = body_of(actor);
    if (b == NULL || !isfinite(width) || !isfinite(height) || width < 0.0f || height < 0.0f) return false;
    b->width = width;
    b->height = height;
    return true;
}

wgf_vec2_t wgf_body2d_get_size(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL ? wgf_vec2_make(b->width, b->height) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_body2d_set_velocity(wgf_actor_t actor, float x, float y)
{
    wgf_ecs_priv_body2d_t *b = body_of(actor);
    if (b == NULL || !isfinite(x) || !isfinite(y)) return false;
    b->vx = x;
    b->vy = y;
    return true;
}

wgf_vec2_t wgf_body2d_get_velocity(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL ? wgf_vec2_make(b->vx, b->vy) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_body2d_set_gravity(wgf_actor_t actor, float gravity)
{
    wgf_ecs_priv_body2d_t *b = body_of(actor);
    if (b == NULL || !isfinite(gravity)) return false;
    b->gravity = gravity;
    return true;
}

float wgf_body2d_get_gravity(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL ? b->gravity : 0.0f;
}

bool wgf_body2d_set_max_fall_speed(wgf_actor_t actor, float speed)
{
    wgf_ecs_priv_body2d_t *b = body_of(actor);
    if (b == NULL || !isfinite(speed)) return false;
    b->max_fall = speed > 0.0f ? speed : 0.0f;
    return true;
}

float wgf_body2d_get_max_fall_speed(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL ? b->max_fall : 0.0f;
}

bool wgf_body2d_set_solid_mask(wgf_actor_t actor, int mask)
{
    wgf_ecs_priv_body2d_t *b = body_of(actor);
    if (b == NULL) return false;
    b->solid_mask = mask;
    return true;
}

int wgf_body2d_get_solid_mask(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL ? b->solid_mask : 0;
}

bool wgf_body2d_is_grounded(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL && b->grounded;
}

bool wgf_body2d_is_blocked_left(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL && b->left;
}

bool wgf_body2d_is_blocked_right(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL && b->right;
}

bool wgf_body2d_is_blocked_above(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL && b->above;
}

float wgf_body2d_get_air_time(wgf_actor_t actor)
{
    const wgf_ecs_priv_body2d_t *b = body_of(actor);
    return b != NULL ? b->air_time : 0.0f;
}

/* ---- the grids ---------------------------------------------------------------------- */

static grid_t *grid_of(wgf_actor_t parent)
{
    int i;
    for (i = 0; i < bodies.grid_count; i++) {
        if (bodies.grids[i].parent == parent) return &bodies.grids[i];
    }
    return NULL;
}

bool wgf_body2d_set_grid(wgf_actor_t parent, const int *cells, int count, int columns, float size, float x, float y)
{
    grid_t *grid;
    int *copy;
    if (wgf_gfx_priv_actor_of(parent) == NULL || cells == NULL || count <= 0 || columns <= 0 || !isfinite(size) ||
        size <= 0.0f || !isfinite(x) || !isfinite(y)) {
        return false;
    }
    copy = (int *)malloc(sizeof(int) * (size_t)count);
    if (copy == NULL) {
        wgf_log_error("wgf_body2d: out of memory for a grid");
        return false;
    }
    memcpy(copy, cells, sizeof(int) * (size_t)count);
    grid = grid_of(parent);
    if (grid == NULL) {
        if (bodies.grid_count == bodies.grid_capacity) {
            const int capacity = bodies.grid_capacity > 0 ? bodies.grid_capacity * 2 : 4;
            grid_t *grown = (grid_t *)realloc(bodies.grids, sizeof(grid_t) * (size_t)capacity);
            if (grown == NULL) {
                free(copy);
                wgf_log_error("wgf_body2d: out of memory for a grid");
                return false;
            }
            bodies.grids = grown;
            bodies.grid_capacity = capacity;
        }
        grid = &bodies.grids[bodies.grid_count++];
        memset(grid, 0, sizeof(*grid));
        grid->parent = parent;
    }
    free(grid->cells);
    grid->cells = copy;
    grid->count = count;
    grid->columns = columns;
    grid->rows = (count + columns - 1) / columns;
    grid->size = size;
    grid->x = x;
    grid->y = y;
    return true;
}

bool wgf_body2d_clear_grid(wgf_actor_t parent)
{
    grid_t *grid = grid_of(parent);
    if (grid == NULL) return false;
    free(grid->cells);
    *grid = bodies.grids[--bodies.grid_count];
    return true;
}

int wgf_body2d_get_grid_cell(wgf_actor_t parent, int column, int row)
{
    const grid_t *grid = grid_of(parent);
    long at;
    if (grid == NULL || column < 0 || row < 0 || column >= grid->columns) return 0;
    at = (long)row * grid->columns + column;
    return at < grid->count ? grid->cells[at] : 0;
}

bool wgf_body2d_set_grid_cell(wgf_actor_t parent, int column, int row, int value)
{
    grid_t *grid = grid_of(parent);
    long at;
    if (grid == NULL || column < 0 || row < 0 || column >= grid->columns) return false;
    at = (long)row * grid->columns + column;
    if (at >= grid->count) return false;
    grid->cells[at] = value;
    return true;
}

/* ---- the system --------------------------------------------------------------------- */

static bool add_rect(int *count, float x, float y, float w, float h)
{
    if ((*count + 1) * 4 > bodies.rect_capacity) {
        const int capacity = bodies.rect_capacity > 0 ? bodies.rect_capacity * 2 : 256;
        float *grown = (float *)realloc(bodies.rects, sizeof(float) * (size_t)capacity);
        if (grown == NULL) return false;
        bodies.rects = grown;
        bodies.rect_capacity = capacity;
    }
    bodies.rects[*count * 4] = x;
    bodies.rects[*count * 4 + 1] = y;
    bodies.rects[*count * 4 + 2] = w;
    bodies.rects[*count * 4 + 3] = h;
    (*count)++;
    return true;
}

/* The solid colliders' bounds this tick, into bodies.solids: every collider switched on, on
 * an actor without a body. */
static int gather_solids(wgf_ecs_priv_query_t *colliders)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    int count = 0;
    wgf_ecs_priv_store_walk(colliders);
    while (wgf_ecs_priv_store_next(colliders, fields, &entity)) {
        const wgf_ecs_priv_collider_t *c = (const wgf_ecs_priv_collider_t *)fields[0];
        const wgf_ecs_priv_ref_t *ref = (const wgf_ecs_priv_ref_t *)fields[1];
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref->actor);
        float box[4];
        if (actor_ptr == NULL || !c->enabled || wgf_actor_has_component(ref->actor, WGF_COMPONENT_BODY2D)) continue;
        if (count == bodies.solid_capacity) {
            const int capacity = bodies.solid_capacity > 0 ? bodies.solid_capacity * 2 : 64;
            solid_t *grown = (solid_t *)realloc(bodies.solids, sizeof(solid_t) * (size_t)capacity);
            if (grown == NULL) break;
            bodies.solids = grown;
            bodies.solid_capacity = capacity;
        }
        if (!wgf_ecs_priv_collider_bounds(c, actor_ptr, box)) continue;
        bodies.solids[count].actor = ref->actor;
        bodies.solids[count].parent = actor_ptr->parent;
        bodies.solids[count].layer = c->layer;
        bodies.solids[count].lo_x = box[0];
        bodies.solids[count].lo_y = box[1];
        bodies.solids[count].hi_x = box[2];
        bodies.solids[count].hi_y = box[3];
        count++;
    }
    return count;
}

/* The solid cells of `grid` within lo..hi, as rectangles. */
static void grid_rects(const grid_t *grid, float lo_x, float lo_y, float hi_x, float hi_y, int *count)
{
    const int c0 = (int)floorf((lo_x - grid->x) / grid->size), c1 = (int)floorf((hi_x - grid->x) / grid->size);
    const int r0 = (int)floorf((lo_y - grid->y) / grid->size), r1 = (int)floorf((hi_y - grid->y) / grid->size);
    int r, c;
    for (r = r0 < 0 ? 0 : r0; r <= r1 && r < grid->rows; r++) {
        for (c = c0 < 0 ? 0 : c0; c <= c1 && c < grid->columns; c++) {
            const long at = (long)r * grid->columns + c;
            if (at >= grid->count || grid->cells[at] == 0) continue;
            if (!add_rect(count, grid->x + (float)c * grid->size, grid->y + (float)r * grid->size, grid->size,
                          grid->size)) {
                return;
            }
        }
    }
}

void wgf_ecs_priv_body2d_step(float dt, wgf_ecs_priv_query_t *query, wgf_ecs_priv_query_t *colliders)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    int solid_count = -1; /* gathered once there is a body */
    if (!(dt > 0.0f)) return;
    wgf_ecs_priv_store_walk(query);
    while (wgf_ecs_priv_store_next(query, fields, &entity)) {
        wgf_ecs_priv_body2d_t *b = (wgf_ecs_priv_body2d_t *)fields[0];
        const wgf_ecs_priv_ref_t *ref = (const wgf_ecs_priv_ref_t *)fields[1];
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref->actor);
        const grid_t *grid;
        float w, h, x, y, dx, dy, out[6];
        int rect_count = 0, i;
        if (actor_ptr == NULL || wgf_gfx_priv_actor_is_frozen(ref->actor)) continue; /* paused: left as it is */
        if (solid_count < 0) solid_count = gather_solids(colliders);
        b->vy += b->gravity * dt;
        if (b->max_fall > 0.0f && b->vy > b->max_fall) b->vy = b->max_fall;
        w = b->width * fabsf(actor_ptr->scale.x);
        h = b->height * fabsf(actor_ptr->scale.y);
        x = actor_ptr->position.x - w * 0.5f;
        y = actor_ptr->position.y - h * 0.5f;
        dx = b->vx * dt;
        dy = b->vy * dt;
        { /* what is solid near its way: its box and where it would go, and a little around */
            const float lo_x = fminf(x, x + dx) - 1.0f, hi_x = fmaxf(x + w, x + w + dx) + 1.0f;
            const float lo_y = fminf(y, y + dy) - 1.0f, hi_y = fmaxf(y + h, y + h + dy) + 1.0f;
            for (i = 0; i < solid_count; i++) {
                const solid_t *s = &bodies.solids[i];
                if (s->parent != actor_ptr->parent || s->actor == ref->actor || (s->layer & b->solid_mask) == 0) continue;
                if (s->hi_x < lo_x || s->lo_x > hi_x || s->hi_y < lo_y || s->lo_y > hi_y) continue;
                if (!add_rect(&rect_count, s->lo_x, s->lo_y, s->hi_x - s->lo_x, s->hi_y - s->lo_y)) break;
            }
            grid = grid_of(actor_ptr->parent);
            if (grid != NULL) grid_rects(grid, lo_x, lo_y, hi_x, hi_y, &rect_count);
        }
        wgf_collide2d_move_rects(x, y, w, h, dx, dy, bodies.rects, rect_count * 4, out, 6);
        actor_ptr->position.x = out[0] + w * 0.5f;
        actor_ptr->position.y = out[1] + h * 0.5f;
        wgf_gfx_priv_actor_transform_changed(ref->actor);
        b->left = out[2] != 0.0f;
        b->right = out[3] != 0.0f;
        b->above = out[4] != 0.0f;
        b->grounded = out[5] != 0.0f;
        if ((b->left && b->vx < 0.0f) || (b->right && b->vx > 0.0f)) b->vx = 0.0f;
        if ((b->grounded && b->vy > 0.0f) || (b->above && b->vy < 0.0f)) b->vy = 0.0f;
        b->air_time = b->grounded ? 0.0f : b->air_time + dt;
    }
}

void wgf_ecs_priv_body2d_stop(void)
{
    int i;
    for (i = 0; i < bodies.grid_count; i++) free(bodies.grids[i].cells);
    free(bodies.grids);
    free(bodies.solids);
    free(bodies.rects);
    memset(&bodies, 0, sizeof(bodies));
}
