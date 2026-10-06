#ifndef WGF_EMITTER2D_H
#define WGF_EMITTER2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_node.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D particle emitter: a node owning many particles, in a canvas. Particles are born
 * at the emitter, in its canvas's units -- so a moving emitter leaves a trail -- each
 * decided at birth (its direction, speed, life, and spin, drawn from wgf_random, so a
 * autopilot run's particles are the same every time), and moved on every frame by
 * libwgf: velocity, gravity, and drag, their size and color running from start to end
 * over their life. A particle is a square, or with a stretch a streak along its
 * motion. Emitters are nodes (wgf_node.h): place, parent, and destroy them with the
 * node calls; destroying one takes its particles with it. Every setting reads back. */

/* An emitter that isn't emitting yet: rate 0, 256 particles at most, white squares of
 * 4 units living a second, going nowhere. 0 when there is no room for another node. */
WGF_API wgf_node_t wgf_emitter2d_create(void);

/* Particles a second while emitting, spread evenly over the frames. Clamped to 0 or
 * more. False for a handle that isn't an emitter. */
WGF_API bool wgf_emitter2d_set_rate(wgf_node_t emitter, float per_second);
WGF_API float wgf_emitter2d_get_rate(wgf_node_t emitter);

/* Whether it emits at its rate (default true; with the rate at 0 nothing comes). Bursts
 * come either way. False for a handle that isn't an emitter. */
WGF_API bool wgf_emitter2d_set_emitting(wgf_node_t emitter, bool emitting);
WGF_API bool wgf_emitter2d_is_emitting(wgf_node_t emitter);

/* `count` particles at once, where the emitter is now. Clamped to the room left. False
 * for a handle that isn't an emitter, or a count below 1. */
WGF_API bool wgf_emitter2d_burst(wgf_node_t emitter, int count);

/* The most particles it keeps, 1 to 65536 (default 256); a birth past it is dropped.
 * Lowering it drops the oldest. Clamped. False for a handle that isn't an emitter. */
WGF_API bool wgf_emitter2d_set_capacity(wgf_node_t emitter, int capacity);
WGF_API int wgf_emitter2d_get_capacity(wgf_node_t emitter);
/* Particles alive now. */
WGF_API int wgf_emitter2d_get_count(wgf_node_t emitter);

/* How long each lives, in seconds, chosen between min and max at birth (swapped when
 * max is less). False for a handle that isn't an emitter, or a min of 0 or less. */
WGF_API bool wgf_emitter2d_set_life(wgf_node_t emitter, float min, float max);
WGF_API float wgf_emitter2d_get_life_min(wgf_node_t emitter);
WGF_API float wgf_emitter2d_get_life_max(wgf_node_t emitter);

/* The direction they leave in, radians in the canvas (0 along +x, a quarter turn along
 * +y, down the screen), turned by the emitter's own world rotation, give or take
 * `spread` radians either side (TAU: every way); and their speed, in units a second,
 * between min and max. False for a handle that isn't an emitter, or a spread or speed
 * below 0. */
WGF_API bool wgf_emitter2d_set_direction(wgf_node_t emitter, float angle, float spread);
WGF_API float wgf_emitter2d_get_direction(wgf_node_t emitter);
WGF_API float wgf_emitter2d_get_spread(wgf_node_t emitter);
WGF_API bool wgf_emitter2d_set_speed(wgf_node_t emitter, float min, float max);
WGF_API float wgf_emitter2d_get_speed_min(wgf_node_t emitter);
WGF_API float wgf_emitter2d_get_speed_max(wgf_node_t emitter);

/* Born anywhere within `radius` of the emitter (0, the default: at it). False for a
 * handle that isn't an emitter, or a radius below 0. */
WGF_API bool wgf_emitter2d_set_radius(wgf_node_t emitter, float radius);
WGF_API float wgf_emitter2d_get_radius(wgf_node_t emitter);

/* Acceleration in units a second squared (default 0, 0), and drag, the part of its
 * velocity a particle loses each second (0, the default, to 1: none to all). Drag is
 * clamped to 0..1. False for a handle that isn't an emitter. */
WGF_API bool wgf_emitter2d_set_gravity(wgf_node_t emitter, float x, float y);
WGF_API wgf_vec2_t wgf_emitter2d_get_gravity(wgf_node_t emitter);
WGF_API bool wgf_emitter2d_set_drag(wgf_node_t emitter, float drag);
WGF_API float wgf_emitter2d_get_drag(wgf_node_t emitter);

/* Size in units, and color, at birth and at death, each running between them over the
 * particle's life. False for a handle that isn't an emitter, or a size below 0. */
WGF_API bool wgf_emitter2d_set_size(wgf_node_t emitter, float start, float end);
WGF_API float wgf_emitter2d_get_size_start(wgf_node_t emitter);
WGF_API float wgf_emitter2d_get_size_end(wgf_node_t emitter);
WGF_API bool wgf_emitter2d_set_color(wgf_node_t emitter, wgf_color_t start, wgf_color_t end);
WGF_API wgf_color_t wgf_emitter2d_get_color_start(wgf_node_t emitter);
WGF_API wgf_color_t wgf_emitter2d_get_color_end(wgf_node_t emitter);

/* A streak instead of a square: each particle drawn as a line from where it is back
 * along its velocity for `seconds` of it, its size the line's thickness; 0 (the
 * default) draws squares. Clamped to 0 or more. False for a handle that isn't an
 * emitter. */
WGF_API bool wgf_emitter2d_set_stretch(wgf_node_t emitter, float seconds);
WGF_API float wgf_emitter2d_get_stretch(wgf_node_t emitter);

/* Every particle gone at once; it keeps emitting as it was. False for a handle that
 * isn't an emitter. */
WGF_API bool wgf_emitter2d_clear(wgf_node_t emitter);

#ifdef __cplusplus
}
#endif

#endif
