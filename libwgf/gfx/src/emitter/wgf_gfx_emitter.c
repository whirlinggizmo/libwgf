#include "emitter/wgf_gfx_emitter_priv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_random.h"

/* the shader's sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_particle.glsl.h"

/* Particle emitters, libwgt's (wgrender's wgr_emitter.c, its docs/PLAN-sprites.md, steps 4
 * and 5): stateless particles simulated on the GPU. A particle is written once, at birth,
 * and vs_particle works out where it is, how big, what color, and how turned from its age;
 * the CPU only spawns, and drops the dead from the front of each emitter's ring. Each
 * emitter draws with one instanced draw of its live window, copied into the frame's
 * particle buffer: on a 2D stage, a command among the frame's immediate mode
 * (wgf_gfx_render_commands.c); on a 3D stage, an item of its draw (wgf_gfx_stage3d.c).
 *
 * libwgf's own: an emitter's velocity is turned by its actor's world rotation (milestone
 * 1's 2D emitter's, a ship's exhaust leaving its back); its default seed is drawn from
 * wgf_random, so an autopilot run's particles are its seed's; with no texture it draws
 * white squares; and an emitter can end its actor once it has nothing left to show, and
 * let its particles finish when it is destroyed (FRICTION.md: Asteroids' explosions). The
 * part is installed by the first emitter, so a program with none links none of it. */

#define DEFAULT_MAX 1024
#define REBASE_SECONDS 4096.0f /* the shader's time is a float: keep it small */
#define PARTICLES_INITIAL 1024
#define DRAWS_INITIAL 16
#define EMITTERS_INITIAL 16
#define TAU 6.28318530718f

/* One emitter's draw this frame. */
typedef struct particle_draw_t {
    int pipeline;
    uint32_t view, sampler;
    int first, count; /* its particles in the frame's buffer */
    particle_params_t params;
} particle_draw_t;

enum {
    PIPELINE_3D_OPAQUE, /* opaque and masked: depth written */
    PIPELINE_3D_BLEND,  /* blended and added: depth tested, not written */
    PIPELINE_3D_ADD,
    PIPELINE_2D_OPAQUE, /* 2D: no depth */
    PIPELINE_2D_BLEND,
    PIPELINE_2D_ADD,
    PIPELINE_COUNT
};

/* A lingering emitter's particles, its actor destroyed: made an actor of its own again at
 * the next update, under the stage it was on, where it was. */
typedef struct lingering_t {
    wgf_gfx_priv_emitter_t *emitter;
    wgf_actor_t root;
} lingering_t;

static struct {
    bool ready;
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    sg_buffer quad;
    sg_buffer buffer;
    int buffer_capacity;
    sg_image white_image; /* what an emitter with no texture draws */
    sg_view white_view;
    sg_sampler white_sampler;
    wgf_gfx_priv_particle_t *particles; /* the frame's, for all emitters */
    int particle_count, particle_capacity;
    particle_draw_t *draws;
    int draw_count, draw_capacity;
    int last_draw_count; /* the last ended frame's */
    wgf_actor_t *emitters; /* every emitter actor, to update */
    wgf_actor_t *roots;    /* the stage each was under at the last update (0: none) */
    int emitter_count, emitter_capacity;
    lingering_t *lingering;
    int lingering_count, lingering_capacity;
} px;

static bool reserve(void **items, int *capacity, int wanted, size_t item_size, int initial)
{
    int grown = *capacity > 0 ? *capacity : initial;
    void *moved;
    if (wanted <= *capacity) return true;
    while (grown < wanted) grown *= 2;
    moved = realloc(*items, item_size * (size_t)grown);
    if (moved == NULL) {
        wgf_log_error("wgf_gfx_emitter: out of memory");
        return false;
    }
    *items = moved;
    *capacity = grown;
    return true;
}

/* -------------------------------------------------------------- random ---- */

static float random01(wgf_gfx_priv_emitter_t *e)
{
    uint32_t x = e->rng; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    e->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

static float random_between(wgf_gfx_priv_emitter_t *e, float a, float b)
{
    return a + (b - a) * random01(e);
}

static float signed_random(wgf_gfx_priv_emitter_t *e)
{
    return random01(e) * 2.0f - 1.0f;
}

/* ------------------------------------------------------------ spawning ---- */

/* A velocity for a new particle: the emitter's, turned within its spread and scaled
 * within its speed variance, then turned as its actor is. */
static wgf_vec3_t birth_velocity(wgf_gfx_priv_emitter_t *e)
{
    const wgf_vec3_t v = e->velocity;
    const float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    float speed;
    wgf_vec3_t dir;

    if (length <= 0.0f) return wgf_vec3_make(0, 0, 0);
    speed = length * fmaxf(0.0f, 1.0f + e->speed_variance * signed_random(e));
    if (e->two_d) { /* turned up to `spread` either way */
        const float angle = atan2f(v.y, v.x) + e->spread * signed_random(e);
        dir = wgf_vec3_make(cosf(angle), sinf(angle), 0.0f);
    } else {
        dir = wgf_vec3_make(v.x / length, v.y / length, v.z / length);
        if (e->spread > 0.0f) { /* uniformly within a cone around the direction */
            const float cos_theta = 1.0f - random01(e) * (1.0f - cosf(fminf(e->spread, 3.14159265f)));
            const float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
            const float phi = TAU * random01(e);
            const wgf_vec3_t helper = fabsf(dir.y) < 0.99f ? wgf_vec3_make(0, 1, 0) : wgf_vec3_make(1, 0, 0);
            const wgf_vec3_t u = wgf_vec3_normalize(wgf_vec3_cross(helper, dir));
            const wgf_vec3_t w = wgf_vec3_cross(dir, u);
            dir = wgf_vec3_make(dir.x * cos_theta + (u.x * cosf(phi) + w.x * sinf(phi)) * sin_theta,
                                dir.y * cos_theta + (u.y * cosf(phi) + w.y * sinf(phi)) * sin_theta,
                                dir.z * cos_theta + (u.z * cosf(phi) + w.z * sinf(phi)) * sin_theta);
        }
    }
    return wgf_vec3_scale(wgf_mat4_transform_direction(e->turn, dir), speed);
}

/* Where in the spawn shape, from its middle: anywhere in the box, or evenly within the
 * sphere (circle). */
static wgf_vec3_t spawn_offset(wgf_gfx_priv_emitter_t *e)
{
    const wgf_vec3_t box = e->box;
    if (e->sphere > 0.0f) {
        const float phi = TAU * random01(e);
        if (e->two_d) {
            const float r = e->sphere * sqrtf(random01(e));
            return wgf_vec3_make(cosf(phi) * r, sinf(phi) * r, 0.0f);
        }
        {
            const float z = signed_random(e), ring = sqrtf(fmaxf(0.0f, 1.0f - z * z));
            const float r = e->sphere * cbrtf(random01(e));
            return wgf_vec3_make(cosf(phi) * ring * r, sinf(phi) * ring * r, z * r);
        }
    }
    return wgf_vec3_make(box.x * signed_random(e), box.y * signed_random(e), box.z * signed_random(e));
}

/* A new particle, born at `when` on the emitter's clock around `at` (replacing the oldest
 * when the ring is full). */
static void spawn(wgf_gfx_priv_emitter_t *e, float when, wgf_vec3_t at)
{
    const wgf_vec3_t v = birth_velocity(e);
    const wgf_vec3_t offset = spawn_offset(e);
    const wgf_vec3_t moving = wgf_vec3_scale(e->movement_velocity, e->inherit);
    wgf_gfx_priv_particle_t *p = &e->ring[e->next];

    p->born[0] = at.x + offset.x;
    p->born[1] = at.y + offset.y;
    p->born[2] = e->two_d ? 0.0f : at.z + offset.z;
    p->born[3] = when;
    p->motion[0] = v.x + moving.x;
    p->motion[1] = v.y + moving.y;
    p->motion[2] = e->two_d ? 0.0f : v.z + moving.z;
    p->motion[3] = random_between(e, e->life_min, e->life_max);
    p->shape[0] = fmaxf(0.0f, 1.0f + e->size_variance * signed_random(e));
    p->shape[1] = random_between(e, e->spin_min, e->spin_max);
    p->shape[2] = TAU * random01(e);
    p->shape[3] = random01(e);
    e->next = (e->next + 1) % e->max;
    if (e->live < e->max) e->live++;
    e->emitted = true;
}

static int oldest(const wgf_gfx_priv_emitter_t *e)
{
    return (e->next - e->live + e->max) % e->max;
}

/* Drop particles from the old end of the window once none of them can be alive. */
static void retire(wgf_gfx_priv_emitter_t *e)
{
    while (e->live > 0) {
        const wgf_gfx_priv_particle_t *p = &e->ring[oldest(e)];
        if (e->time - p->born[3] < p->motion[3]) break;
        e->live--;
    }
}

/* A move is spread over the next update (steady spawns along the way, velocity to
 * inherit); a skipped trail, or the first position, isn't a move. wgrender's move_to. */
static void move_to(wgf_gfx_priv_emitter_t *e, wgf_vec3_t position)
{
    e->position = position;
    if (e->skip_trail || !e->placed) {
        e->previous = position;
        e->movement_velocity = wgf_vec3_make(0, 0, 0);
    }
    e->skip_trail = false;
    e->placed = true;
}

static void update_emitter(wgf_gfx_priv_emitter_t *e, float dt)
{
    const wgf_vec3_t from = e->previous;
    const wgf_vec3_t moved = wgf_vec3_sub(e->position, from);
    int due, k;

    e->time += dt;
    if (e->time > REBASE_SECONDS) { /* keep the clock small for the shader's floats */
        int i, at;
        for (i = 0, at = oldest(e); i < e->live; i++, at = (at + 1) % e->max) e->ring[at].born[3] -= REBASE_SECONDS;
        e->time -= REBASE_SECONDS;
    }
    retire(e);
    /* the move was made after the last update, in a frame of that update's length (the game
       moves things by the frame's time): how fast it went is over that, not this */
    e->previous = e->position;
    if (e->last_dt > 0.0f) e->movement_velocity = wgf_vec3_scale(moved, 1.0f / e->last_dt);
    e->last_dt = dt;
    if (!e->emitting || e->rate <= 0.0f || dt <= 0.0f) {
        e->owed = 0.0f;
        return;
    }
    e->owed += e->rate * dt;
    due = (int)e->owed;
    e->owed -= (float)due;
    if (due > e->max) due = e->max;
    /* spread over the frame, in time and along the way the emitter moved, so a steady
       stream doesn't come in clumps */
    for (k = 0; k < due; k++) {
        const float along = (float)(k + 1) / (float)due;
        spawn(e, e->time - dt * (1.0f - along), wgf_vec3_add(from, wgf_vec3_scale(moved, along)));
    }
}

/* Whether `actor` and everything above it are enabled: a disabled one isn't moved on. */
static bool enabled_in_tree(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    while (actor_ptr != NULL) {
        if (!actor_ptr->enabled) return false;
        actor_ptr = actor_ptr->parent != 0 ? wgf_gfx_priv_actor_of(actor_ptr->parent) : NULL;
    }
    return true;
}

/* The top of `actor`'s tree: its stage, when it is on one. */
static wgf_actor_t root_of(wgf_actor_t actor)
{
    wgf_actor_t parent;
    while ((parent = wgf_actor_get_parent(actor)) != 0) actor = parent;
    return actor;
}

wgf_gfx_priv_emitter_t *wgf_gfx_priv_emitter_place(wgf_actor_t actor)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_gfx_priv_emitter_t *e;
    wgf_mat4_t world;
    wgf_vec3_t position, scale;
    int c;
    if (actor_ptr == NULL) return NULL;
    e = actor_ptr->as.emitter;
    world = wgf_gfx_priv_actor_get_world_matrix(actor);
    position = wgf_mat4_get_translation(world);
    if (e->two_d) position.z = 0.0f;
    /* its turn alone: the world matrix's columns made unit length, no translation */
    scale = wgf_mat4_get_scale(world);
    for (c = 0; c < 3; c++) {
        const float s = c == 0 ? scale.x : c == 1 ? scale.y : scale.z;
        int r;
        for (r = 0; r < 3; r++) world.m[c * 4 + r] = s != 0.0f ? world.m[c * 4 + r] / s : (c == r ? 1.0f : 0.0f);
    }
    world.m[12] = world.m[13] = world.m[14] = 0.0f;
    e->turn = world;
    move_to(e, position);
    return e;
}

/* An emitter that ends its actor: nothing left to emit, and its last particle gone. */
static bool finished(const wgf_gfx_priv_emitter_t *e)
{
    return e->ending && e->emitted && (!e->emitting || e->rate <= 0.0f) && wgf_gfx_priv_emitter_count_alive(e) == 0;
}

static void adopt_lingering(void);

static void update(float dt)
{
    wgf_actor_t done[16];
    int i, done_count = 0;
    adopt_lingering();
    for (i = 0; i < px.emitter_count; i++) {
        const wgf_actor_t actor = px.emitters[i];
        wgf_gfx_priv_emitter_t *e;
        if (wgf_gfx_priv_actor_of(actor) == NULL || !enabled_in_tree(actor)) continue;
        e = wgf_gfx_priv_emitter_place(actor);
        px.roots[i] = root_of(actor);
        update_emitter(e, dt);
        if (finished(e) && done_count < 16) done[done_count++] = actor; /* the rest next frame */
    }
    for (i = 0; i < done_count; i++) wgf_actor_destroy(done[i], WGF_ACTOR_DESTROY_CHILDREN);
}

/* ------------------------------------------------------- the actor's life ---- */

static void emitter_free(wgf_gfx_priv_emitter_t *e)
{
    if (e == NULL) return;
    if (e->texture != 0) wgf_resource_release(e->texture);
    free(e->ring);
    free(e);
}

/* An emitter actor let go of: its emitter freed, or, lingering with particles alive on a
 * stage, kept for the next update to give an actor of its own. */
static void free_actor(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    wgf_gfx_priv_emitter_t *e = actor_ptr->as.emitter;
    wgf_actor_t root = 0;
    int i;
    for (i = 0; i < px.emitter_count; i++) {
        if (px.emitters[i] == actor) {
            root = px.roots[i];
            px.emitter_count--;
            px.emitters[i] = px.emitters[px.emitter_count];
            px.roots[i] = px.roots[px.emitter_count];
            break;
        }
    }
    actor_ptr->as.emitter = NULL;
    if (e != NULL && e->lingering && root != 0 && root != actor && wgf_gfx_priv_emitter_count_alive(e) > 0 &&
        reserve((void **)&px.lingering, &px.lingering_capacity, px.lingering_count + 1, sizeof(lingering_t), 4)) {
        px.lingering[px.lingering_count].emitter = e;
        px.lingering[px.lingering_count].root = root;
        px.lingering_count++;
        return;
    }
    emitter_free(e);
}

static void draw_2d(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                    const wgf_mat4_t *view);
static void install(void);

/* Room for one more emitter on the update's list. */
static bool listed(wgf_actor_t actor)
{
    if (!reserve((void **)&px.emitters, &px.emitter_capacity, px.emitter_count + 1, sizeof(wgf_actor_t),
                 EMITTERS_INITIAL)) {
        return false;
    }
    px.roots = (wgf_actor_t *)realloc(px.roots, sizeof(wgf_actor_t) * (size_t)px.emitter_capacity);
    if (px.roots == NULL) {
        wgf_log_error("wgf_gfx_emitter: out of memory");
        return false;
    }
    px.emitters[px.emitter_count] = actor;
    px.roots[px.emitter_count] = 0;
    px.emitter_count++;
    return true;
}

wgf_actor_t wgf_gfx_priv_emitter_create(bool two_d)
{
    wgf_actor_t actor;
    wgf_gfx_priv_actor_t *actor_ptr;
    wgf_gfx_priv_emitter_t *e = (wgf_gfx_priv_emitter_t *)calloc(1, sizeof(*e));
    if (e != NULL) e->ring = (wgf_gfx_priv_particle_t *)calloc(DEFAULT_MAX, sizeof(wgf_gfx_priv_particle_t));
    if (e == NULL || e->ring == NULL) {
        if (e != NULL) free(e);
        wgf_log_error("wgf_gfx_emitter: out of memory");
        return 0;
    }
    install();
    actor = wgf_gfx_priv_actor_create(two_d ? WGF_ACTOR_KIND_EMITTER2D : WGF_ACTOR_KIND_EMITTER3D);
    actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL || !listed(actor)) {
        if (actor_ptr != NULL) wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
        emitter_free(e);
        return 0;
    }
    /* libwgt's defaults */
    e->two_d = two_d;
    e->emitting = true;
    e->max = DEFAULT_MAX;
    e->life_min = e->life_max = 1.0f;
    e->size_times[1] = 1.0f;
    e->size_values[0] = e->size_values[1] = two_d ? 8.0f : 1.0f;
    e->size_keys = 2;
    e->color_times[1] = 1.0f;
    e->color_values[0] = e->color_values[1] = 0xFFFFFFFFu;
    e->color_keys = 2;
    e->columns = e->rows = e->frames = 1;
    e->alpha_mode = WGF_ALPHA_MODE_ADD;
    e->alpha_cutoff = 0.5f;
    e->turn = wgf_mat4_identity();
    wgf_gfx_priv_emitter_set_seed(e, (uint32_t)(wgf_random_get_float() * 4294967040.0f) | 1u);
    wgf_gfx_priv_actor_of(actor)->as.emitter = e;
    return actor;
}

/* The lingering emitters given actors of their own, each under the stage it was on, where
 * it was, ending once its particles are done. */
static void adopt_lingering(void)
{
    int i;
    for (i = 0; i < px.lingering_count; i++) {
        wgf_gfx_priv_emitter_t *e = px.lingering[i].emitter;
        const wgf_actor_t actor = wgf_gfx_priv_actor_of(px.lingering[i].root) != NULL
                                      ? wgf_gfx_priv_actor_create(e->two_d ? WGF_ACTOR_KIND_EMITTER2D
                                                                           : WGF_ACTOR_KIND_EMITTER3D)
                                      : 0;
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
        if (actor_ptr == NULL || !listed(actor)) { /* its stage gone, or no room: its particles go */
            if (actor_ptr != NULL) wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
            emitter_free(e);
            continue;
        }
        actor_ptr->as.emitter = e;
        e->emitting = false;
        e->ending = true;
        e->lingering = false;
        wgf_actor_set_position(actor, e->position.x, e->position.y, e->position.z);
        wgf_actor_set_parent(actor, px.lingering[i].root);
        e->skip_trail = true; /* its new actor's place is where it was: no move */
    }
    px.lingering_count = 0;
}

wgf_gfx_priv_emitter_t *wgf_gfx_priv_emitter_of_at(wgf_actor_t actor, bool two_d, const char *caller)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(actor, caller);
    if (actor_ptr == NULL ||
        actor_ptr->type != (two_d ? WGF_ACTOR_KIND_EMITTER2D : WGF_ACTOR_KIND_EMITTER3D)) {
        return NULL;
    }
    return actor_ptr->as.emitter;
}

/* ------------------------------------------------------------ settings ---- */

static bool is_texture(wgf_texture_t texture)
{
    return texture == 0 || wgf_resource_get_status(texture) != WGF_RESOURCE_STATUS_NONE;
}

bool wgf_gfx_priv_emitter_set_texture(wgf_gfx_priv_emitter_t *e, wgf_texture_t texture)
{
    if (!is_texture(texture)) return false;
    if (texture != 0) wgf_gfx_priv_texture_retain(texture); /* before the release, in case it is the same */
    if (e->texture != 0) wgf_resource_release(e->texture);
    e->texture = texture;
    return true;
}

bool wgf_gfx_priv_emitter_set_max(wgf_gfx_priv_emitter_t *e, int count)
{
    wgf_gfx_priv_particle_t *ring;
    if (count < 1 || count > WGF_GFX_PRIV_EMITTER_MAX_PARTICLES) {
        wgf_log_warn("wgf_gfx_emitter: set_max %d: 1 to %d particles", count, WGF_GFX_PRIV_EMITTER_MAX_PARTICLES);
        return false;
    }
    ring = (wgf_gfx_priv_particle_t *)calloc((size_t)count, sizeof(wgf_gfx_priv_particle_t));
    if (ring == NULL) return false;
    free(e->ring);
    e->ring = ring;
    e->max = count;
    e->next = 0;
    e->live = 0; /* the particles alive are gone */
    return true;
}

bool wgf_gfx_priv_emitter_set_life(wgf_gfx_priv_emitter_t *e, float min_seconds, float max_seconds)
{
    if (!(min_seconds > 0.0f) || max_seconds < min_seconds) {
        wgf_log_warn("wgf_gfx_emitter: set_life %g .. %g: min has to be more than 0 and max at least min",
                     (double)min_seconds, (double)max_seconds);
        return false;
    }
    e->life_min = min_seconds;
    e->life_max = max_seconds;
    return true;
}

bool wgf_gfx_priv_emitter_set_alpha_mode(wgf_gfx_priv_emitter_t *e, wgf_alpha_mode_t mode, float cutoff)
{
    if ((int)mode < WGF_ALPHA_MODE_OPAQUE || mode > WGF_ALPHA_MODE_ADD) {
        wgf_log_warn("wgf_gfx_emitter: set_alpha_mode %d is not a wgf_alpha_mode_t", (int)mode);
        return false;
    }
    e->alpha_mode = mode;
    e->alpha_cutoff = fminf(fmaxf(cutoff, 0.0f), 1.0f);
    return true;
}

/* A key into a curve, in order of time (after any at the same time: a step). */
static bool insert_key(float *times, int *count, float t, int *at)
{
    int i;
    if (*count >= WGF_GFX_PRIV_EMITTER_MAX_KEYS || !(t >= 0.0f && t <= 1.0f)) {
        wgf_log_warn("wgf_gfx_emitter: a key at %g: t is 0 .. 1, and a curve holds %d keys", (double)t,
                     WGF_GFX_PRIV_EMITTER_MAX_KEYS);
        return false;
    }
    for (i = *count; i > 0 && times[i - 1] > t; i--) times[i] = times[i - 1];
    times[i] = t;
    (*count)++;
    *at = i;
    return true;
}

bool wgf_gfx_priv_emitter_add_size_key(wgf_gfx_priv_emitter_t *e, float t, float size)
{
    int at;
    const int n = e->size_keys;
    if (!(size >= 0.0f)) {
        wgf_log_warn("wgf_gfx_emitter: add_size_key %g: a size can't be negative", (double)size);
        return false;
    }
    if (!insert_key(e->size_times, &e->size_keys, t, &at)) return false;
    memmove(&e->size_values[at + 1], &e->size_values[at], sizeof(float) * (size_t)(n - at));
    e->size_values[at] = size;
    return true;
}

bool wgf_gfx_priv_emitter_add_color_key(wgf_gfx_priv_emitter_t *e, float t, wgf_color_t color)
{
    int at;
    const int n = e->color_keys;
    if (!insert_key(e->color_times, &e->color_keys, t, &at)) return false;
    memmove(&e->color_values[at + 1], &e->color_values[at], sizeof(wgf_color_t) * (size_t)(n - at));
    e->color_values[at] = color;
    return true;
}

bool wgf_gfx_priv_emitter_set_size(wgf_gfx_priv_emitter_t *e, float start, float end, float variance)
{
    if (!(start >= 0.0f) || !(end >= 0.0f)) {
        wgf_log_warn("wgf_gfx_emitter: set_size %g .. %g: a size can't be negative", (double)start, (double)end);
        return false;
    }
    e->size_keys = 0;
    wgf_gfx_priv_emitter_add_size_key(e, 0.0f, start);
    wgf_gfx_priv_emitter_add_size_key(e, 1.0f, end);
    e->size_variance = fminf(fmaxf(variance, 0.0f), 1.0f);
    return true;
}

void wgf_gfx_priv_emitter_set_color(wgf_gfx_priv_emitter_t *e, wgf_color_t start, wgf_color_t end)
{
    e->color_keys = 0;
    wgf_gfx_priv_emitter_add_color_key(e, 0.0f, start);
    wgf_gfx_priv_emitter_add_color_key(e, 1.0f, end);
}

bool wgf_gfx_priv_emitter_add_palette_color(wgf_gfx_priv_emitter_t *e, wgf_color_t color)
{
    if (e->palette_count >= WGF_GFX_PRIV_EMITTER_MAX_PALETTE) {
        wgf_log_warn("wgf_gfx_emitter: add_palette_color: a palette holds %d colors", WGF_GFX_PRIV_EMITTER_MAX_PALETTE);
        return false;
    }
    e->palette[e->palette_count++] = color;
    return true;
}

bool wgf_gfx_priv_emitter_set_frames(wgf_gfx_priv_emitter_t *e, int columns, int rows, int count, float per_second)
{
    if (columns < 1 || rows < 1 || columns * rows > 4096 || count > columns * rows || !(per_second >= 0.0f)) {
        wgf_log_warn("wgf_gfx_emitter: set_frames %d x %d, %d at %g a second: at least 1 x 1 and at most 4096 frames, "
                     "the count within them, the rate not negative",
                     columns, rows, count, (double)per_second);
        return false;
    }
    e->columns = columns;
    e->rows = rows;
    e->frames = count > 0 ? count : columns * rows;
    e->frames_per_second = per_second;
    return true;
}

/* Start over as if the steady rate had been running for `seconds`: particles born over
 * that time (as many as can still be alive, and fit), where the emitter is now. */
bool wgf_gfx_priv_emitter_prewarm(wgf_gfx_priv_emitter_t *e, float seconds)
{
    float window;
    int count, k;
    if (!(seconds >= 0.0f)) {
        wgf_log_warn("wgf_gfx_emitter: prewarm %g: seconds can't be negative", (double)seconds);
        return false;
    }
    e->live = 0;
    e->next = 0;
    if (!e->emitting || e->rate <= 0.0f) return true;
    window = fminf(seconds, e->life_max);
    count = (int)(e->rate * window);
    if (count > e->max) count = e->max; /* the newest */
    for (k = 0; k < count; k++) spawn(e, e->time - (float)(count - 1 - k) / e->rate, e->position);
    retire(e);
    return true;
}

bool wgf_gfx_priv_emitter_burst(wgf_gfx_priv_emitter_t *e, int count)
{
    int i;
    if (count < 0) {
        wgf_log_warn("wgf_gfx_emitter: burst %d: a count can't be negative", count);
        return false;
    }
    if (count > e->max) count = e->max;
    for (i = 0; i < count; i++) spawn(e, e->time, e->position);
    return true;
}

void wgf_gfx_priv_emitter_set_seed(wgf_gfx_priv_emitter_t *e, uint32_t seed)
{
    e->seed = seed;
    e->rng = seed != 0 ? seed : 1u;
}

int wgf_gfx_priv_emitter_count_alive(const wgf_gfx_priv_emitter_t *e)
{
    int alive = 0, i, at;
    for (i = 0, at = oldest(e); i < e->live; i++, at = (at + 1) % e->max) {
        const float age = e->time - e->ring[at].born[3];
        alive += age >= 0.0f && age < e->ring[at].motion[3] ? 1 : 0;
    }
    return alive;
}

void wgf_gfx_priv_emitter_clear(wgf_gfx_priv_emitter_t *e)
{
    e->live = 0;
    e->next = 0;
    e->owed = 0.0f;
}

bool wgf_gfx_priv_emitter_particle(wgf_actor_t actor, int index, float born[4], float motion[4], float shape[4])
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    const wgf_gfx_priv_emitter_t *e;
    const wgf_gfx_priv_particle_t *p;
    if (actor_ptr == NULL ||
        (actor_ptr->type != WGF_ACTOR_KIND_EMITTER2D && actor_ptr->type != WGF_ACTOR_KIND_EMITTER3D)) {
        return false;
    }
    e = actor_ptr->as.emitter;
    if (e == NULL || index < 0 || index >= e->live) return false;
    p = &e->ring[(oldest(e) + index) % e->max];
    memcpy(born, p->born, sizeof(p->born));
    memcpy(motion, p->motion, sizeof(p->motion));
    memcpy(shape, p->shape, sizeof(p->shape));
    return true;
}

/* ------------------------------------------------------------- drawing ---- */

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

/* The shader, pipelines, quad, and white texture, made with the first emitter drawn rather
 * than at startup: a program without particles doesn't compile them, as wgrender's. */
static void ensure_gpu(void)
{
    static const float corners[12] = {-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};
    static const uint8_t white[4] = {255, 255, 255, 255};
    sg_pipeline_desc desc;
    sg_blend_state blend, add;
    sg_buffer_desc quad;
    sg_image_desc image;
    sg_view_desc view;
    sg_sampler_desc sampler;

    if (px.ready) return;
    memset(&desc, 0, sizeof(desc));
    desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    desc.layout.attrs[ATTR_particle_corner].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[ATTR_particle_born].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_particle_born].buffer_index = 1;
    desc.layout.attrs[ATTR_particle_motion].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_particle_motion].buffer_index = 1;
    desc.layout.attrs[ATTR_particle_shape].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_particle_shape].buffer_index = 1;
    desc.label = "wgf-particles";
    memset(&blend, 0, sizeof(blend));
    blend.enabled = true;
    blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    add = blend;
    add.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    add.dst_factor_alpha = SG_BLENDFACTOR_ONE;

    px.shader = sg_make_shader(particle_shader_desc(shader_backend()));
    desc.shader = px.shader;
    desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    desc.depth.write_enabled = true;
    px.pipelines[PIPELINE_3D_OPAQUE] = sg_make_pipeline(&desc);
    desc.depth.write_enabled = false;
    desc.colors[0].blend = blend;
    px.pipelines[PIPELINE_3D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend = add;
    px.pipelines[PIPELINE_3D_ADD] = sg_make_pipeline(&desc);
    desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    memset(&desc.colors[0].blend, 0, sizeof(desc.colors[0].blend));
    px.pipelines[PIPELINE_2D_OPAQUE] = sg_make_pipeline(&desc);
    desc.colors[0].blend = blend;
    px.pipelines[PIPELINE_2D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend = add;
    px.pipelines[PIPELINE_2D_ADD] = sg_make_pipeline(&desc);
    memset(&quad, 0, sizeof(quad));
    quad.data.ptr = corners;
    quad.data.size = sizeof(corners);
    quad.label = "wgf-particle-quad";
    px.quad = sg_make_buffer(&quad);
    memset(&image, 0, sizeof(image));
    image.width = image.height = 1;
    image.pixel_format = SG_PIXELFORMAT_RGBA8;
    image.data.mip_levels[0].ptr = white;
    image.data.mip_levels[0].size = sizeof(white);
    px.white_image = sg_make_image(&image);
    memset(&view, 0, sizeof(view));
    view.texture.image = px.white_image;
    px.white_view = sg_make_view(&view);
    memset(&sampler, 0, sizeof(sampler));
    px.white_sampler = sg_make_sampler(&sampler);
    px.ready = true;
}

static void set4(float out[4], float a, float b, float c, float d)
{
    out[0] = a, out[1] = b, out[2] = c, out[3] = d;
}

/* A color as the shader takes it: sRGB values 0..1, like immediate mode's. */
static void color4(float out[4], wgf_color_t color)
{
    set4(out, (float)wgf_color_get_red(color) / 255.0f, (float)wgf_color_get_green(color) / 255.0f,
         (float)wgf_color_get_blue(color) / 255.0f, (float)wgf_color_get_alpha(color) / 255.0f);
}

int wgf_gfx_priv_emitter_record(wgf_actor_t actor, const float view_proj[16], wgf_vec3_t axis_x, wgf_vec3_t axis_y)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    const wgf_gfx_priv_emitter_t *e;
    particle_draw_t *d;
    sg_view view;
    sg_sampler sampler;
    int tw = 1, th = 1, first, at, left, i;
    bool placeholder = false;

    if (actor_ptr == NULL ||
        (actor_ptr->type != WGF_ACTOR_KIND_EMITTER2D && actor_ptr->type != WGF_ACTOR_KIND_EMITTER3D)) {
        return -1;
    }
    e = actor_ptr->as.emitter;
    if (e == NULL || e->live == 0 || e->size_keys == 0 || e->color_keys == 0) return -1;
    ensure_gpu();
    if (e->texture != 0) {
        if (!wgf_gfx_priv_texture_get_binding(e->texture, &view, &sampler, &tw, &th, &placeholder) || tw <= 0 ||
            th <= 0) {
            return -1; /* still loading */
        }
    } else {
        view = px.white_view;
        sampler = px.white_sampler;
    }
    if (!reserve((void **)&px.particles, &px.particle_capacity, px.particle_count + e->live,
                 sizeof(wgf_gfx_priv_particle_t), PARTICLES_INITIAL) ||
        !reserve((void **)&px.draws, &px.draw_capacity, px.draw_count + 1, sizeof(particle_draw_t), DRAWS_INITIAL)) {
        return -1;
    }
    d = &px.draws[px.draw_count];
    memset(d, 0, sizeof(*d));
    d->view = view.id;
    d->sampler = sampler.id;
    d->first = px.particle_count;
    d->count = e->live;
    memcpy(d->params.view_proj, view_proj, sizeof(d->params.view_proj));
    set4(d->params.axis_x, axis_x.x, axis_x.y, axis_x.z, 0);
    set4(d->params.axis_y, axis_y.x, axis_y.y, axis_y.z, 0);
    set4(d->params.gravity_now, e->gravity.x, e->gravity.y, e->gravity.z, e->time);
    set4(d->params.dynamics, e->drag, e->stretch, 0, 0);
    set4(d->params.frames, (float)e->columns, (float)e->rows, (float)e->frames, e->frames_per_second);
    set4(d->params.counts, (float)e->size_keys, (float)e->color_keys,
         e->alpha_mode == WGF_ALPHA_MODE_MASK     ? fmaxf(e->alpha_cutoff, 1e-6f)
         : e->alpha_mode == WGF_ALPHA_MODE_OPAQUE ? -1.0f
                                                  : 0.0f,
         (float)e->palette_count);
    for (i = 0; i < WGF_GFX_PRIV_EMITTER_MAX_KEYS; i++) { /* the curves, 4 keys to a vec4 */
        d->params.size_times[i / 4][i % 4] = e->size_times[i];
        d->params.size_values[i / 4][i % 4] = e->size_values[i];
        d->params.color_times[i / 4][i % 4] = e->color_times[i];
        color4(d->params.color_values[i], e->color_values[i]);
    }
    for (i = 0; i < e->palette_count; i++) color4(d->params.palette[i], e->palette[i]);
    if (e->texture != 0 && e->source[2] > 0.0f && e->source[3] > 0.0f && !placeholder) {
        set4(d->params.source, e->source[0] / (float)tw, e->source[1] / (float)th,
             (e->source[0] + e->source[2]) / (float)tw, (e->source[1] + e->source[3]) / (float)th);
    } else {
        set4(d->params.source, 0, 0, 1, 1);
    }
    d->pipeline = (e->two_d ? PIPELINE_2D_OPAQUE : PIPELINE_3D_OPAQUE) +
                  (e->alpha_mode == WGF_ALPHA_MODE_ADD ? 2 : e->alpha_mode == WGF_ALPHA_MODE_BLEND ? 1 : 0);

    /* the particles that may be alive, oldest first */
    first = px.particle_count;
    at = oldest(e);
    for (left = e->live; left > 0;) {
        const int run = at + left <= e->max ? left : e->max - at;
        memcpy(&px.particles[first], &e->ring[at], sizeof(wgf_gfx_priv_particle_t) * (size_t)run);
        first += run;
        left -= run;
        at = 0;
    }
    px.particle_count = first;
    return px.draw_count++;
}

/* The part's flush, before the frame's pass: the frame's particles up to the GPU, once. */
static void flush(void)
{
    sg_write_buffer_desc write;
    if (!px.ready || px.particle_count == 0) return;
    if (px.particle_count > px.buffer_capacity) {
        sg_buffer_desc desc;
        int capacity = px.buffer_capacity > 0 ? px.buffer_capacity : PARTICLES_INITIAL;
        while (capacity < px.particle_count) capacity *= 2;
        if (px.buffer.id != SG_INVALID_ID) sg_destroy_buffer(px.buffer);
        memset(&desc, 0, sizeof(desc));
        desc.size = sizeof(wgf_gfx_priv_particle_t) * (size_t)capacity;
        desc.usage.vertex_buffer = true;
        desc.usage.write_transient = true;
        desc.label = "wgf-particles";
        px.buffer = sg_make_buffer(&desc);
        px.buffer_capacity = capacity;
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = px.particles;
    write.src.data.size = sizeof(wgf_gfx_priv_particle_t) * (size_t)px.particle_count;
    write.dst.buffer = px.buffer;
    sg_write_buffer_transient(&write);
}

void wgf_gfx_priv_emitter_draw(int index)
{
    const particle_draw_t *d;
    sg_bindings bind;
    if (!px.ready || index < 0 || index >= px.draw_count || px.buffer.id == SG_INVALID_ID) return;
    d = &px.draws[index];
    sg_apply_pipeline(px.pipelines[d->pipeline]);
    memset(&bind, 0, sizeof(bind));
    bind.vertex_buffers[0] = px.quad;
    bind.vertex_buffers[1] = px.buffer;
    bind.vertex_buffer_offsets[1] = (int)(sizeof(wgf_gfx_priv_particle_t) * (size_t)d->first);
    bind.views[VIEW_particle_tex].id = d->view;
    bind.samplers[SMP_particle_smp].id = d->sampler;
    sg_apply_bindings(&bind);
    sg_apply_uniforms(UB_particle_params, &SG_RANGE(d->params));
    sg_draw(0, 6, d->count);
}

int wgf_gfx_priv_emitter_get_draw_count(void)
{
    return px.last_draw_count;
}

static void end_frame(void)
{
    px.last_draw_count = px.draw_count;
    px.particle_count = 0;
    px.draw_count = 0;
}

static void stop(void)
{
    int i;
    if (px.ready) { /* sokol_gfx is still up: gfx stops its parts first */
        if (px.buffer.id != SG_INVALID_ID) sg_destroy_buffer(px.buffer);
        sg_destroy_buffer(px.quad);
        for (i = 0; i < PIPELINE_COUNT; i++) sg_destroy_pipeline(px.pipelines[i]);
        sg_destroy_shader(px.shader);
        sg_destroy_view(px.white_view);
        sg_destroy_image(px.white_image);
        sg_destroy_sampler(px.white_sampler);
    }
    for (i = 0; i < px.lingering_count; i++) emitter_free(px.lingering[i].emitter);
    wgf_gfx_priv_set_particle_hooks(NULL);
    free(px.particles);
    free(px.draws);
    free(px.emitters); /* the emitters themselves go with the actors */
    free(px.roots);
    free(px.lingering);
    memset(&px, 0, sizeof(px));
}

/* ------------------------------------------------------------- the part ---- */

/* On a 2D stage: the particles in the stage's units, through its view and the frame's 2D
 * projection, a command among the frame's immediate mode. The quads' right and up are the
 * screen's, read back into the stage's units. */
static void draw_2d(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                    const wgf_mat4_t *view)
{
    const wgf_mat4_t view_proj = wgf_mat4_mul(wgf_gfx_priv_render_projection_2d(), *view);
    const wgf_mat4_t back = wgf_mat4_invert(*view);
    const wgf_vec3_t axis_x = wgf_vec3_normalize(wgf_mat4_transform_direction(back, wgf_vec3_make(1, 0, 0)));
    const wgf_vec3_t axis_y = wgf_vec3_normalize(wgf_mat4_transform_direction(back, wgf_vec3_make(0, -1, 0)));
    int index;
    (void)actor_ptr;
    (void)placed;
    index = wgf_gfx_priv_emitter_record(actor, view_proj.m, axis_x, axis_y);
    if (index >= 0) wgf_gfx_priv_render_add_command(wgf_gfx_priv_emitter_draw, index);
}

/* On a 3D stage, through its hook: the particles recorded, and their alpha mode, which
 * says where among the stage's parts they are drawn. */
static int record_3d(wgf_actor_t actor, const float view_proj[16], wgf_vec3_t axis_x, wgf_vec3_t axis_y, int *mode)
{
    const wgf_gfx_priv_emitter_t *e = wgf_gfx_priv_emitter_of_at(actor, false, NULL);
    if (e == NULL) return -1;
    *mode = (int)e->alpha_mode;
    return wgf_gfx_priv_emitter_record(actor, view_proj, axis_x, axis_y);
}

static const wgf_gfx_priv_particle_hooks_t particle_hooks = {record_3d, wgf_gfx_priv_emitter_draw};

static wgf_core_priv_part_t part = {.name = "particles",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_PARTICLES,
                                    .update = update,
                                    .flush = flush,
                                    .end_frame = end_frame,
                                    .stop = stop};
static const wgf_gfx_priv_actor_kind_t kind_2d = {free_actor, draw_2d};
static const wgf_gfx_priv_actor_kind_t kind_3d = {free_actor, NULL}; /* a 3D stage draws its own */

/* The first emitter: particles join the frame, and freeing and drawing actors. */
static void install(void)
{
    wgf_core_priv_part_install(&part);
    wgf_gfx_priv_set_particle_hooks(&particle_hooks);
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_EMITTER2D, &kind_2d);
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_EMITTER3D, &kind_3d);
}

/* A scene's emitter lines, through the hooks set as this file is linked (the scene's half
 * is wgf_gfx_emitter_scene.c, pulled with it). */
extern const wgf_gfx_priv_emitter_scene_t wgf_gfx_priv_emitter_scene_hooks;

WGF_CORE_PRIV_ON_LINK(link_scene_lines)
{
    wgf_gfx_priv_set_emitter_scene(&wgf_gfx_priv_emitter_scene_hooks);
}
