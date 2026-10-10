#include "wgf_emitter2d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_random.h"

/* 2D particle emitters, simulated on the CPU (docs/HISTORY.md, "2D drawn through
 * sokol_gl, particles on the CPU"): each emitter's particles in an array of its own, in
 * stage units, moved on every frame by the particles part's update, after the ticks
 * and before the frame; drawn through the stage's view as quads in its immediate mode.
 * The part is installed by the first emitter, so a program with none links none of it. */

#define TAU 6.28318530717958647692f
#define MAX_CAPACITY 65536

typedef struct particle_t {
    float x, y, vx, vy;
    float age, life;
} particle_t;

typedef struct wgf_gfx_priv_emitter_t {
    particle_t *particles;
    int count, capacity;
    float rate, owed; /* particles a second, and the fraction of one carried to the next frame */
    bool emitting;
    float life_min, life_max;
    float direction, spread;
    float speed_min, speed_max;
    float radius;
    float gravity[2];
    float drag;
    float size_start, size_end;
    wgf_color_t color_start, color_end;
    float stretch;
    float last[2];  /* where the emitter was last frame, in stage units, for a trail */
    bool has_last;
} wgf_gfx_priv_emitter_t;

/* The emitters, for the part's update to move them on. */
static wgf_actor_t *emitters;
static int emitter_count, emitter_capacity;

static wgf_gfx_priv_emitter_t *emitter_of_at(wgf_actor_t emitter, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(emitter, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_EMITTER2D ? actor_ptr->as.emitter : NULL;
}
#define emitter_of(...) emitter_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

static float random_between(float a, float b)
{
    return a + (b - a) * wgf_random_get_float();
}

/* Where the emitter is in its stage's units, and its world rotation's angle. */
static void placement(wgf_actor_t emitter, float *x, float *y, float *angle)
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(emitter);
    *x = world.m[12];
    *y = world.m[13];
    *angle = atan2f(world.m[1], world.m[0]);
}

/* A particle born at (x, y), leaving in the emitter's direction from `angle`. */
static void spawn(wgf_gfx_priv_emitter_t *e, float x, float y, float angle)
{
    particle_t *p;
    float a, speed;
    if (e->count >= e->capacity) return;
    p = &e->particles[e->count++];
    if (e->radius > 0.0f) { /* evenly over the disc */
        const float r = e->radius * sqrtf(wgf_random_get_float()), t = TAU * wgf_random_get_float();
        x += cosf(t) * r;
        y += sinf(t) * r;
    }
    a = angle + e->direction + random_between(-e->spread, e->spread);
    speed = random_between(e->speed_min, e->speed_max);
    p->x = x;
    p->y = y;
    p->vx = cosf(a) * speed;
    p->vy = sinf(a) * speed;
    p->age = 0.0f;
    p->life = random_between(e->life_min, e->life_max);
}

static void move_on(wgf_actor_t emitter, wgf_gfx_priv_emitter_t *e, float dt)
{
    const float keep = powf(1.0f - e->drag, dt); /* the drag's share lost per second, at this frame's length */
    float x, y, angle;
    int i;
    for (i = 0; i < e->count;) {
        particle_t *p = &e->particles[i];
        p->age += dt;
        if (p->age >= p->life) {
            *p = e->particles[--e->count]; /* the last in its place */
            continue;
        }
        p->vx = (p->vx + e->gravity[0] * dt) * keep;
        p->vy = (p->vy + e->gravity[1] * dt) * keep;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        i++;
    }
    placement(emitter, &x, &y, &angle);
    if (!e->has_last) {
        e->last[0] = x;
        e->last[1] = y;
        e->has_last = true;
    }
    if (e->emitting && e->rate > 0.0f) {
        int n;
        e->owed += e->rate * dt;
        n = (int)e->owed;
        e->owed -= (float)n;
        for (i = 0; i < n; i++) { /* spread along the way it moved this frame: a trail, not a clump */
            const float t = ((float)i + 1.0f) / (float)n;
            spawn(e, e->last[0] + (x - e->last[0]) * t, e->last[1] + (y - e->last[1]) * t, angle);
        }
    }
    e->last[0] = x;
    e->last[1] = y;
}

/* Whether `actor` and everything above it are enabled: a disabled one, or one under a
 * disabled actor, isn't moved on, as the draw's walk skips it (libwgt's enabled_in_tree). */
static bool enabled_in_tree(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    while (actor_ptr != NULL) {
        if (!actor_ptr->enabled) return false;
        actor_ptr = actor_ptr->parent != 0 ? wgf_gfx_priv_actor_of(actor_ptr->parent) : NULL;
    }
    return true;
}

static void update(float dt)
{
    int i;
    for (i = 0; i < emitter_count; i++) {
        wgf_gfx_priv_emitter_t *e = emitter_of(emitters[i]);
        if (e != NULL && enabled_in_tree(emitters[i])) move_on(emitters[i], e, dt);
    }
}

static void stop(void)
{
    free(emitters); /* the emitters themselves go with the actors */
    emitters = NULL;
    emitter_count = emitter_capacity = 0;
}

static wgf_core_priv_part_t part = {.name = "particles",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_PARTICLES,
                                    .update = update,
                                    .stop = stop};

static void color_of(wgf_color_t a, wgf_color_t b, float t)
{
    const wgf_color_t c = wgf_color_lerp(a, b, t);
    sgl_c4b((uint8_t)wgf_color_get_red(c), (uint8_t)wgf_color_get_green(c), (uint8_t)wgf_color_get_blue(c),
            (uint8_t)wgf_color_get_alpha(c));
}

static void corner(const wgf_mat4_t *m, float x, float y)
{
    sgl_v2f(m->m[0] * x + m->m[4] * y + m->m[12], m->m[1] * x + m->m[5] * y + m->m[13]);
}

static void draw_emitter(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                         const wgf_mat4_t *view)
{
    const wgf_gfx_priv_emitter_t *e = actor_ptr->as.emitter;
    int i;
    (void)actor;
    (void)placed;
    if (e == NULL || e->count == 0) return;
    sgl_begin_triangles();
    for (i = 0; i < e->count; i++) {
        const particle_t *p = &e->particles[i];
        const float t = p->life > 0.0f ? p->age / p->life : 1.0f;
        const float h = (e->size_start + (e->size_end - e->size_start) * t) * 0.5f;
        float ax, ay, bx, by, nx, ny;
        color_of(e->color_start, e->color_end, t);
        if (e->stretch > 0.0f && (p->vx != 0.0f || p->vy != 0.0f)) {
            const float length = sqrtf(p->vx * p->vx + p->vy * p->vy);
            nx = -p->vy / length * h;
            ny = p->vx / length * h;
            ax = p->x - p->vx * e->stretch;
            ay = p->y - p->vy * e->stretch;
            bx = p->x;
            by = p->y;
        } else { /* a square: the line across it, a half size either side */
            nx = 0.0f;
            ny = h;
            ax = p->x - h;
            ay = p->y;
            bx = p->x + h;
            by = p->y;
        }
        corner(view, ax + nx, ay + ny);
        corner(view, bx + nx, by + ny);
        corner(view, bx - nx, by - ny);
        corner(view, ax + nx, ay + ny);
        corner(view, bx - nx, by - ny);
        corner(view, ax - nx, ay - ny);
    }
    sgl_end();
}

static void free_emitter(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    int i;
    if (actor_ptr->as.emitter != NULL) free(actor_ptr->as.emitter->particles);
    free(actor_ptr->as.emitter);
    actor_ptr->as.emitter = NULL;
    for (i = 0; i < emitter_count; i++) {
        if (emitters[i] == actor) {
            emitters[i] = emitters[--emitter_count];
            break;
        }
    }
}

static const wgf_gfx_priv_actor_kind_t kind = {free_emitter, draw_emitter};

wgf_actor_t wgf_emitter2d_create(void)
{
    wgf_actor_t actor;
    wgf_gfx_priv_emitter_t *e;
    if (emitter_count == emitter_capacity) {
        const int capacity = emitter_capacity > 0 ? emitter_capacity * 2 : 16;
        wgf_actor_t *grown = (wgf_actor_t *)realloc(emitters, sizeof(wgf_actor_t) * (size_t)capacity);
        if (grown == NULL) return 0;
        emitters = grown;
        emitter_capacity = capacity;
    }
    e = (wgf_gfx_priv_emitter_t *)calloc(1, sizeof(*e));
    if (e == NULL) return 0;
    e->capacity = 256;
    e->particles = (particle_t *)malloc(sizeof(particle_t) * (size_t)e->capacity);
    actor = e->particles != NULL ? wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_EMITTER2D) : 0;
    if (actor == 0) {
        free(e->particles);
        free(e);
        return 0;
    }
    e->emitting = true;
    e->life_min = e->life_max = 1.0f;
    e->size_start = e->size_end = 4.0f;
    e->color_start = e->color_end = 0xFFFFFFFFu;
    wgf_gfx_priv_actor_of(actor)->as.emitter = e;
    emitters[emitter_count++] = actor;
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_EMITTER2D, &kind);
    wgf_core_priv_part_install(&part);
    return actor;
}

bool wgf_emitter2d_set_rate(wgf_actor_t emitter, float per_second)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->rate = per_second > 0.0f ? per_second : 0.0f;
    return true;
}

float wgf_emitter2d_get_rate(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->rate : 0.0f;
}

bool wgf_emitter2d_set_emitting(wgf_actor_t emitter, bool emitting)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->emitting = emitting;
    return true;
}

bool wgf_emitter2d_is_emitting(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL && e->emitting;
}

bool wgf_emitter2d_burst(wgf_actor_t emitter, int count)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    float x, y, angle;
    int i;
    if (e == NULL || count < 1) return false;
    placement(emitter, &x, &y, &angle);
    for (i = 0; i < count && e->count < e->capacity; i++) spawn(e, x, y, angle);
    return true;
}

bool wgf_emitter2d_set_capacity(wgf_actor_t emitter, int capacity)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    particle_t *grown;
    if (e == NULL) return false;
    capacity = capacity < 1 ? 1 : (capacity > MAX_CAPACITY ? MAX_CAPACITY : capacity);
    if (capacity < e->count) { /* the oldest dropped: the array keeps no order, so the first */
        memmove(e->particles, e->particles + (e->count - capacity), sizeof(particle_t) * (size_t)capacity);
        e->count = capacity;
    }
    grown = (particle_t *)realloc(e->particles, sizeof(particle_t) * (size_t)capacity);
    if (grown == NULL) {
        wgf_log_error("wgf_emitter2d_set_capacity: out of memory for %d particles", capacity);
        return false;
    }
    e->particles = grown;
    e->capacity = capacity;
    return true;
}

int wgf_emitter2d_get_capacity(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->capacity : 0;
}

int wgf_emitter2d_get_count(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->count : 0;
}

bool wgf_emitter2d_set_life(wgf_actor_t emitter, float min, float max)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL || !(min > 0.0f) || !(max > 0.0f)) return false;
    e->life_min = min < max ? min : max;
    e->life_max = min < max ? max : min;
    return true;
}

float wgf_emitter2d_get_life_min(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->life_min : 0.0f;
}

float wgf_emitter2d_get_life_max(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->life_max : 0.0f;
}

bool wgf_emitter2d_set_direction(wgf_actor_t emitter, float angle, float spread)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL || !(spread >= 0.0f) || !isfinite(angle)) return false;
    e->direction = angle;
    e->spread = spread;
    return true;
}

float wgf_emitter2d_get_direction(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->direction : 0.0f;
}

float wgf_emitter2d_get_spread(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->spread : 0.0f;
}

bool wgf_emitter2d_set_speed(wgf_actor_t emitter, float min, float max)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL || !(min >= 0.0f) || !(max >= 0.0f)) return false;
    e->speed_min = min < max ? min : max;
    e->speed_max = min < max ? max : min;
    return true;
}

float wgf_emitter2d_get_speed_min(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->speed_min : 0.0f;
}

float wgf_emitter2d_get_speed_max(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->speed_max : 0.0f;
}

bool wgf_emitter2d_set_radius(wgf_actor_t emitter, float radius)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL || !(radius >= 0.0f)) return false;
    e->radius = radius;
    return true;
}

float wgf_emitter2d_get_radius(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->radius : 0.0f;
}

bool wgf_emitter2d_set_gravity(wgf_actor_t emitter, float x, float y)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->gravity[0] = x;
    e->gravity[1] = y;
    return true;
}

wgf_vec2_t wgf_emitter2d_get_gravity(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? wgf_vec2_make(e->gravity[0], e->gravity[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_emitter2d_set_drag(wgf_actor_t emitter, float drag)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->drag = drag > 0.0f ? (drag < 1.0f ? drag : 1.0f) : 0.0f;
    return true;
}

float wgf_emitter2d_get_drag(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->drag : 0.0f;
}

bool wgf_emitter2d_set_size(wgf_actor_t emitter, float start, float end)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL || !(start >= 0.0f) || !(end >= 0.0f)) return false;
    e->size_start = start;
    e->size_end = end;
    return true;
}

float wgf_emitter2d_get_size_start(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->size_start : 0.0f;
}

float wgf_emitter2d_get_size_end(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->size_end : 0.0f;
}

bool wgf_emitter2d_set_color(wgf_actor_t emitter, wgf_color_t start, wgf_color_t end)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->color_start = start;
    e->color_end = end;
    return true;
}

wgf_color_t wgf_emitter2d_get_color_start(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->color_start : 0u;
}

wgf_color_t wgf_emitter2d_get_color_end(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->color_end : 0u;
}

bool wgf_emitter2d_set_stretch(wgf_actor_t emitter, float seconds)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->stretch = seconds > 0.0f ? seconds : 0.0f;
    return true;
}

float wgf_emitter2d_get_stretch(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    return e != NULL ? e->stretch : 0.0f;
}

bool wgf_emitter2d_clear(wgf_actor_t emitter)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(emitter);
    if (e == NULL) return false;
    e->count = 0;
    e->owed = 0.0f;
    return true;
}
