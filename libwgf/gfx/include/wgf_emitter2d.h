#ifndef WGF_EMITTER2D_H
#define WGF_EMITTER2D_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D particle emitter: an actor that owns many particles, on a 2D stage, in its units,
 * through its camera (3D emitters are wgf_emitter3d.h). A particle is decided when it is
 * born (where, how fast, how long it lives, how big, how it spins, within the ranges set
 * here) and the GPU works out where it is from its age, so thousands cost about what
 * spawning them does. Particles are unlit: the texture (white squares with none) times
 * their color.
 *
 * Every emitter is advanced once a frame, by the frame's time, before the frame is drawn,
 * unless it or an actor above it is disabled. Its position is its actor's: particles stay
 * where they were born when it moves (trails), a move spreads the next frame's steady
 * spawns along the way, and the move is the velocity they can inherit; their velocity is
 * turned as the actor is (an exhaust leaves a ship's back however it faces). Hidden, it
 * still spawns, but draws nothing. Its particles are drawn in tree order, unsorted. Its
 * random numbers are its own, seeded from wgf_random as it is made, so an autopilot run's
 * particles are the same every time. It holds a reference to its texture. Emitters are
 * actors (wgf_actor.h): place, parent, and destroy them with the actor calls. */

/* An emitter, emitting at no rate yet: white 8-unit squares living a second, added. 0
 * when there is no room for another actor. */
WGF_API wgf_actor_t wgf_emitter2d_create(void);

/* What each particle shows (0: a white square). False when `texture` isn't a texture.
 * One still loading shows nothing until it is READY. */
WGF_API bool wgf_emitter2d_set_texture(wgf_actor_t emitter, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_emitter2d_get_texture(wgf_actor_t emitter);

/* The region of the texture each particle shows, in texture pixels (an atlas cell).
 * Default: the whole texture; a width or height of 0 or less goes back to that. */
WGF_API bool wgf_emitter2d_set_source(wgf_actor_t emitter, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_emitter2d_get_source(wgf_actor_t emitter);

/* Flipbook: the source split into `columns` by `rows` frames (left to right, top to
 * bottom), of which the first `count` are used (0 or less: all). With `per_second` 0
 * each particle plays them once over its life (puffs); above 0 it loops at that rate
 * from a random frame (flames). Default: one frame. False for less than 1 by 1, more
 * than 4096 frames, a count past them, or a negative rate. */
WGF_API bool wgf_emitter2d_set_frames(wgf_actor_t emitter, int columns, int rows, int count, float per_second);
WGF_API int wgf_emitter2d_get_frame_columns(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_frame_rows(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_frame_count(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_frame_rate(wgf_actor_t emitter);

/* Its actor's next move isn't a move: nothing spawns along the way, and particles
 * inherit nothing of it (a teleport). */
WGF_API bool wgf_emitter2d_skip_trail(wgf_actor_t emitter);

/* Emission: a steady rate (particles a second; 0, the default, for bursts only; false
 * below 0), and bursts of `count` at once, where the actor is (false below 0).
 * set_emitting(false) stops the steady rate; the particles alive finish their lives. At
 * most `max` are alive at once (default 1024): new ones replace the oldest; a new max
 * empties it. False for a max below 1 or above 65536 -- refused, not clamped, since fewer
 * particles than asked is a different effect. Life: each particle's, in seconds, between
 * min and max (default 1); false for min 0 or less, or max below min. */
WGF_API bool wgf_emitter2d_set_rate(wgf_actor_t emitter, float per_second);
WGF_API float wgf_emitter2d_get_rate(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_burst(wgf_actor_t emitter, int count);
WGF_API bool wgf_emitter2d_set_emitting(wgf_actor_t emitter, bool emitting);
WGF_API bool wgf_emitter2d_is_emitting(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_max(wgf_actor_t emitter, int count);
WGF_API int wgf_emitter2d_get_max(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_life(wgf_actor_t emitter, float min_seconds, float max_seconds);
WGF_API float wgf_emitter2d_get_life_min(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_life_max(wgf_actor_t emitter);

/* Start over as if the steady rate had been running for `seconds` (smoke already rising
 * when it appears): the particles alive go, and those the rate would have made over that
 * time, and still alive now, are made where the actor is. False below 0. */
WGF_API bool wgf_emitter2d_prewarm(wgf_actor_t emitter, float seconds);

/* Birth: anywhere in a box around the position (half sizes; default a point), or evenly
 * within a circle (each replaces the other; a radius below 0 is refused), moving along
 * (x, y) at its length's speed (default still), in the emitter's own frame, so turned as
 * its actor is: (0, -1) is up the screen for an emitter not turned. Turned up to `spread`
 * radians either way (TAU: every way) and faster or slower by up to `speed_variance`
 * (0..1) of it. Gravity: an acceleration, stage units a second squared, along the stage's
 * axes (default none). */
WGF_API bool wgf_emitter2d_set_spawn_box(wgf_actor_t emitter, float half_width, float half_height);
WGF_API wgf_vec2_t wgf_emitter2d_get_spawn_box(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_spawn_circle(wgf_actor_t emitter, float radius);
WGF_API float wgf_emitter2d_get_spawn_circle(wgf_actor_t emitter); /* 0: the box */
WGF_API bool wgf_emitter2d_set_velocity(wgf_actor_t emitter, float x, float y, float spread, float speed_variance);
WGF_API wgf_vec2_t wgf_emitter2d_get_velocity(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_spread(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_speed_variance(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_gravity(wgf_actor_t emitter, float x, float y);
WGF_API wgf_vec2_t wgf_emitter2d_get_gravity(wgf_actor_t emitter);

/* After birth: drag slows particles in proportion to their speed (a second: 1 loses about
 * 63% of the speed in a second, less gravity's pull; false below 0). Inherit velocity adds
 * that fraction of the emitter's own velocity, its actor's movement, at birth: 1 carries
 * them along with it. Stretch along the motion (sparks, streaks): each particle's texture
 * points its top along its velocity across the screen, as long as the distance it moves in
 * `seconds` (plus its size), trailing behind; stretched particles don't spin (false below
 * 0). Defaults 0. */
WGF_API bool wgf_emitter2d_set_drag(wgf_actor_t emitter, float per_second);
WGF_API float wgf_emitter2d_get_drag(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_inherit_velocity(wgf_actor_t emitter, float fraction);
WGF_API float wgf_emitter2d_get_inherit_velocity(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_stretch(wgf_actor_t emitter, float seconds);
WGF_API float wgf_emitter2d_get_stretch(wgf_actor_t emitter);

/* Over a particle's life, curves: its size (stage units; each particle's scaled by up to
 * `variance`, 0..1) and color (a tint, alpha included, so a fade). set_size and set_color
 * make two keys, at 0 (birth) and 1 (death); for more, clear the keys and add up to 8,
 * each at `t` (0..1 of a life). Between two keys the value moves in a line, before the
 * first and after the last it holds; keys at the same time make a step. An emitter
 * without size or color keys draws nothing. Defaults: size 8 to 8, white to white. False
 * for a negative size, a `t` outside 0..1, or a ninth key. The keys read back by index,
 * in time order (0 past the last). */
WGF_API bool wgf_emitter2d_set_size(wgf_actor_t emitter, float start, float end, float variance);
WGF_API float wgf_emitter2d_get_size_variance(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_add_size_key(wgf_actor_t emitter, float t, float size);
WGF_API bool wgf_emitter2d_clear_size_keys(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_size_key_count(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_size_key_time(wgf_actor_t emitter, int index);
WGF_API float wgf_emitter2d_get_size_key_value(wgf_actor_t emitter, int index);
WGF_API bool wgf_emitter2d_set_color(wgf_actor_t emitter, wgf_color_t start, wgf_color_t end);
WGF_API bool wgf_emitter2d_add_color_key(wgf_actor_t emitter, float t, wgf_color_t color);
WGF_API bool wgf_emitter2d_clear_color_keys(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_color_key_count(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_color_key_time(wgf_actor_t emitter, int index);
WGF_API wgf_color_t wgf_emitter2d_get_color_key_value(wgf_actor_t emitter, int index);

/* Palette: up to 8 colors (false for a ninth); each particle picks one at birth, and it
 * tints its color over its life (confetti from one emitter). Empty by default: no tint. */
WGF_API bool wgf_emitter2d_add_palette_color(wgf_actor_t emitter, wgf_color_t color);
WGF_API bool wgf_emitter2d_clear_palette(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_palette_count(wgf_actor_t emitter);
WGF_API wgf_color_t wgf_emitter2d_get_palette_color(wgf_actor_t emitter, int index); /* 0 past the last */

/* Spin: radians a second between min and max, positive clockwise on screen, from a random
 * angle (default none). */
WGF_API bool wgf_emitter2d_set_spin(wgf_actor_t emitter, float min, float max);
WGF_API float wgf_emitter2d_get_spin_min(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_spin_max(wgf_actor_t emitter);

/* How particles use alpha (wgf_material.h's modes; default ADD: glows, sparks, fire), and
 * the cutoff MASK cuts out below (0..1, clamped; default 0.5). False when `mode` isn't
 * one. */
WGF_API bool wgf_emitter2d_set_alpha_mode(wgf_actor_t emitter, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_emitter2d_get_alpha_mode(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_alpha_cutoff(wgf_actor_t emitter);

/* Where its random numbers start: the same seed and settings give the same particles
 * (default: drawn from wgf_random as it is made). Read back as last set. */
WGF_API bool wgf_emitter2d_set_seed(wgf_actor_t emitter, int seed);
WGF_API int wgf_emitter2d_get_seed(wgf_actor_t emitter);

/* Ending: once it has nothing left to emit (not emitting, or at no rate) and the last
 * particle it made has died, the emitter's actor is destroyed, with what is under it, in
 * the update before a frame: a one-shot effect (an explosion's burst) ends itself. One
 * that has made no particle yet waits for its first. Default off. */
WGF_API bool wgf_emitter2d_set_ending(wgf_actor_t emitter, bool ending);
WGF_API bool wgf_emitter2d_is_ending(wgf_actor_t emitter);

/* Lingering: destroyed, directly or with an actor above it, while it has particles alive
 * on a stage, they finish their lives where they are: the emitter goes on as an actor of
 * its own at the stage's top, not emitting, ending (as above) with its last particle, made
 * in the update before the next frame (a ship's exhaust outlives the ship). Default off. */
WGF_API bool wgf_emitter2d_set_lingering(wgf_actor_t emitter, bool lingering);
WGF_API bool wgf_emitter2d_is_lingering(wgf_actor_t emitter);

/* The particles alive now, and all of them gone (it keeps emitting as it was). */
WGF_API int wgf_emitter2d_get_count(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_clear(wgf_actor_t emitter);

#ifdef __cplusplus
}
#endif

#endif
