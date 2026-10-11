#ifndef WGF_GFX_EMITTER_PRIV_H
#define WGF_GFX_EMITTER_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "wgf_actor.h"
#include "wgf_color.h"
#include "wgf_mat4.h"
#include "wgf_material.h"
#include "wgf_texture.h"
#include "wgf_vec3.h"

/* Particle emitters, libwgt's (wgrender's wgr_emitter.c): what 2D and 3D emitters share --
 * the particles, decided at birth and moved by the GPU from their age (vs_particle in
 * wgf_gfx_particle.glsl), the spawning, and the drawing. An emitter is an actor
 * (wgf_emitter2d.h, wgf_emitter3d.h) holding one of these; its position and turn are its
 * actor's, read once a frame. */

#define WGF_GFX_PRIV_EMITTER_MAX_KEYS 8 /* a curve's */
#define WGF_GFX_PRIV_EMITTER_MAX_PALETTE 8
#define WGF_GFX_PRIV_EMITTER_MAX_PARTICLES 65536

/* One particle as the shader reads it (per-instance data, 48 bytes). */
typedef struct wgf_gfx_priv_particle_t {
    float born[4];   /* xyz where, w when (the emitter's clock) */
    float motion[4]; /* xyz velocity, w life (seconds) */
    float shape[4];  /* x size scale, y spin (radians / s), z angle at birth, w a random 0..1 */
} wgf_gfx_priv_particle_t;

typedef struct wgf_gfx_priv_emitter_t {
    bool two_d;
    bool emitting;
    wgf_texture_t texture;   /* a reference held; 0 for none (white) */
    float source[4];         /* texture pixels; width or height <= 0: the whole texture */
    wgf_vec3_t position;     /* its actor's, at the last update */
    wgf_vec3_t previous;     /* where it was at the update before: steady spawns spread along the way */
    wgf_vec3_t movement_velocity; /* how fast it moved over the last update */
    wgf_mat4_t turn;         /* its actor's world rotation, at the last update: velocity is turned by it */
    float last_dt;           /* the frame time the game moved it by since then */
    bool placed;             /* positioned once (the first position isn't a move) */
    bool skip_trail;         /* the next move isn't one (wgrender's jump) */
    float rate;              /* per second */
    float owed;              /* particles due but not yet spawned (fractions carry over) */
    int max;
    wgf_gfx_priv_particle_t *ring;
    int next;                /* where the next particle goes */
    int live;                /* particles in the window: the `live` before `next` may be alive */
    float life_min, life_max;
    wgf_vec3_t box;          /* spawn box half sizes */
    float sphere;            /* > 0: spawn in a sphere (2D: a circle) of this radius instead */
    wgf_vec3_t velocity;     /* direction x speed, in the emitter's own frame */
    float spread, speed_variance;
    wgf_vec3_t gravity;
    float drag;              /* per second */
    float stretch;           /* seconds of motion */
    float inherit;           /* of the emitter's own velocity */
    float size_times[WGF_GFX_PRIV_EMITTER_MAX_KEYS], size_values[WGF_GFX_PRIV_EMITTER_MAX_KEYS];
    int size_keys;
    float size_variance;
    float color_times[WGF_GFX_PRIV_EMITTER_MAX_KEYS];
    wgf_color_t color_values[WGF_GFX_PRIV_EMITTER_MAX_KEYS];
    int color_keys;
    wgf_color_t palette[WGF_GFX_PRIV_EMITTER_MAX_PALETTE];
    int palette_count;
    float spin_min, spin_max;
    int columns, rows, frames; /* flipbook: frames < 2 none */
    float frames_per_second;   /* 0: once over the life */
    wgf_alpha_mode_t alpha_mode;
    float alpha_cutoff;
    uint32_t seed;           /* as last set (or given) */
    uint32_t rng;
    float time;              /* the emitter's clock, seconds (rebased now and then) */
    bool ending;             /* its actor destroyed once it has nothing left to show */
    bool lingering;          /* destroyed with particles alive: they finish first */
    bool emitted;            /* it has had a particle: an ending emitter that never had one doesn't end */
} wgf_gfx_priv_emitter_t;

/* A new emitter actor, 2D or 3D; 0 when there is no room. Its default seed is drawn from
 * wgf_random, so an autopilot run's particles are its seed's. */
wgf_actor_t wgf_gfx_priv_emitter_create(bool two_d);
/* The emitter a 2D (two_d) or 3D emitter actor holds; NULL for any other handle. */
wgf_gfx_priv_emitter_t *wgf_gfx_priv_emitter_of_at(wgf_actor_t actor, bool two_d, const char *caller);

/* The settings with rules: false (and a warning) for what they refuse. */
bool wgf_gfx_priv_emitter_set_texture(wgf_gfx_priv_emitter_t *emitter, wgf_texture_t texture);
bool wgf_gfx_priv_emitter_set_max(wgf_gfx_priv_emitter_t *emitter, int count);
bool wgf_gfx_priv_emitter_set_life(wgf_gfx_priv_emitter_t *emitter, float min_seconds, float max_seconds);
bool wgf_gfx_priv_emitter_set_alpha_mode(wgf_gfx_priv_emitter_t *emitter, wgf_alpha_mode_t mode, float cutoff);
bool wgf_gfx_priv_emitter_set_size(wgf_gfx_priv_emitter_t *emitter, float start, float end, float variance);
bool wgf_gfx_priv_emitter_add_size_key(wgf_gfx_priv_emitter_t *emitter, float t, float size);
void wgf_gfx_priv_emitter_set_color(wgf_gfx_priv_emitter_t *emitter, wgf_color_t start, wgf_color_t end);
bool wgf_gfx_priv_emitter_add_color_key(wgf_gfx_priv_emitter_t *emitter, float t, wgf_color_t color);
bool wgf_gfx_priv_emitter_add_palette_color(wgf_gfx_priv_emitter_t *emitter, wgf_color_t color);
bool wgf_gfx_priv_emitter_set_frames(wgf_gfx_priv_emitter_t *emitter, int columns, int rows, int count,
                                     float per_second);
bool wgf_gfx_priv_emitter_prewarm(wgf_gfx_priv_emitter_t *emitter, float seconds);
bool wgf_gfx_priv_emitter_burst(wgf_gfx_priv_emitter_t *emitter, int count);
void wgf_gfx_priv_emitter_set_seed(wgf_gfx_priv_emitter_t *emitter, uint32_t seed);
int wgf_gfx_priv_emitter_count_alive(const wgf_gfx_priv_emitter_t *emitter);
void wgf_gfx_priv_emitter_clear(wgf_gfx_priv_emitter_t *emitter);

/* The emitter moved to its actor's place now, its move counted as the next update's:
 * before a burst or a prewarm, which spawn where the actor is. */
wgf_gfx_priv_emitter_t *wgf_gfx_priv_emitter_place(wgf_actor_t actor);

/* Record `actor`'s particles for the frame, seen through `view_proj` (OpenGL's clip
 * space) with `axis_x` and `axis_y` the quad's right and up before spin (3D: the camera's;
 * 2D: right and up the screen, in stage units), 2D or 3D's pipelines: its live window
 * into the frame's buffer, and one draw. The draw's number, to draw it in its place; -1
 * for nothing to draw. */
int wgf_gfx_priv_emitter_record(wgf_actor_t actor, const float view_proj[16], wgf_vec3_t axis_x, wgf_vec3_t axis_y);
/* Inside the frame's pass: draw `index`. */
void wgf_gfx_priv_emitter_draw(int index);

/* For tests: the emitter draws the last ended frame recorded, and particle `index` of the
 * window that may be alive, oldest first. */
int wgf_gfx_priv_emitter_get_draw_count(void);
bool wgf_gfx_priv_emitter_particle(wgf_actor_t actor, int index, float born[4], float motion[4], float shape[4]);

#endif
