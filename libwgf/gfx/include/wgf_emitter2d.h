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

/* A 2D particle emitter: an actor that owns many particles, on a 2D stage.
 *
 * Particles are in the stage's units, and are seen through its camera. For 3D emitters, see
 * wgf_emitter3d.h.
 *
 * Each particle is decided when it is born, within the ranges set here: where it starts, how fast
 * it moves, how long it lives, how big it is, and how it spins. The GPU then works out where it
 * is from its age, so thousands of particles cost about what spawning them does.
 *
 * Particles are unlit: they draw the texture times their color. With no texture, they are white
 * squares.
 *
 * Every emitter is advanced once a frame, by the frame's time, before the frame is drawn. It
 * isn't advanced while it, or an actor above it, is disabled.
 *
 * The emitter's position is its actor's:
 * - Particles stay where they were born when the emitter moves, leaving trails. Local space
 *   (below) moves them with it instead.
 * - A move spreads the next frame's steady spawns along the way.
 * - The move is the velocity particles can inherit.
 * - Particles' velocity is turned as the actor is, so an exhaust leaves a ship's back however it
 *   faces.
 *
 * A hidden emitter still spawns, but draws nothing. Its particles are drawn in tree order,
 * unsorted.
 *
 * An emitter's random numbers are its own, seeded from wgf_random as it is made. So an autopilot
 * run's particles are the same every time.
 *
 * An emitter holds a reference to its texture. Emitters are actors (wgf_actor.h): place, parent,
 * and destroy them with the actor calls. */

/* Creates an emitter that doesn't emit yet: its rate is 0. Its particles are white 8-unit squares
 * that live a second, drawn added (ADD).
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_emitter2d_create(void);

/* Sets or gets the texture each particle shows (0: a white square). A texture still loading shows
 * nothing until it is READY. The setter returns false if `texture` isn't a texture. */
WGF_API bool wgf_emitter2d_set_texture(wgf_actor_t emitter, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_emitter2d_get_texture(wgf_actor_t emitter);

/* Sets or gets the region of the texture each particle shows, such as an atlas cell, in texture
 * pixels (default: the whole texture). A width or height of 0 or less goes back to the whole
 * texture. */
WGF_API bool wgf_emitter2d_set_source(wgf_actor_t emitter, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_emitter2d_get_source(wgf_actor_t emitter);

/* Sets or gets a flipbook: the source split into `columns` by `rows` frames, left to right, then
 * top to bottom (default: one frame).
 *
 * - The first `count` frames are used. With 0 or less, all of them are.
 * - With `per_second` 0, each particle plays the frames once over its life: puffs.
 * - With `per_second` above 0, each particle loops them at that rate, from a random frame:
 *   flames.
 *
 * Returns false for less than 1 by 1, more than 4096 frames, a `count` past them, or a negative
 * rate. */
WGF_API bool wgf_emitter2d_set_frames(wgf_actor_t emitter, int columns, int rows, int count, float per_second);
WGF_API int wgf_emitter2d_get_frame_columns(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_frame_rows(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_frame_count(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_frame_rate(wgf_actor_t emitter);

/* Makes the actor's next move not count as a move, as for a teleport. Nothing spawns along the
 * way, and particles inherit none of it. */
WGF_API bool wgf_emitter2d_skip_trail(wgf_actor_t emitter);

/* Sets or gets how the emitter emits particles, how many can be alive, and how long they live.
 *
 * - set_rate sets a steady rate, in particles a second (default: 0, for bursts only). It returns
 *   false below 0.
 * - burst emits `count` particles at once, where the actor is. It returns false below 0.
 * - set_emitting(false) stops the steady rate. The particles alive finish their lives.
 * - set_max sets how many particles can be alive at once (default: 1024). New particles replace
 *   the oldest. A new max empties the emitter.
 * - set_max returns false for a `count` below 1 or above 65536. It refuses rather than clamps,
 *   since fewer particles than asked is a different effect.
 * - set_life sets each particle's life, in seconds, between `min_seconds` and `max_seconds`
 *   (default: 1). It returns false for a min of 0 or less, or a max below the min. */
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

/* Starts over as if the steady rate had been running for `seconds`: smoke already rising when it
 * appears.
 *
 * The particles alive are removed. The particles the rate would have made over that time, and
 * that would still be alive now, are made where the actor is. Returns false below 0. */
WGF_API bool wgf_emitter2d_prewarm(wgf_actor_t emitter, float seconds);

/* Sets or gets where particles are born, and how they move.
 *
 * - set_spawn_box: particles are born anywhere in a box around the position, given by its half
 *   sizes (default: a point).
 * - set_spawn_circle: particles are born evenly within a circle. A radius below 0 is refused.
 * - The box and the circle each replace the other.
 * - set_velocity: particles move along (x, y), at its length's speed (default: still). The
 *   direction is in the emitter's own frame, so it turns as its actor does. (0, -1) is up the
 *   screen for an emitter that isn't turned.
 * - Each particle's direction is turned up to `spread` radians either way (TAU: every way). Its
 *   speed is faster or slower by up to `speed_variance` (0..1) of it.
 * - set_gravity: an acceleration, in stage units a second squared, along the stage's axes
 *   (default: none). */
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

/* Sets or gets whether particles are born on the spawn shape's outline, rather than inside it
 * (default: off).
 * - With a circle, they are born at its radius.
 * - With a box, they are born on its edges, with an edge picked by its length.
 * - A box with a half size of 0 is a line, and its shell is that line's ends. */
WGF_API bool wgf_emitter2d_set_spawn_shell(wgf_actor_t emitter, bool on);
WGF_API bool wgf_emitter2d_is_spawn_shell(wgf_actor_t emitter);

/* Sets or gets how particles move after birth (each defaults to 0).
 *
 * - Drag slows particles in proportion to their speed, per second. At 1, a particle loses about
 *   63% of its speed in a second, less gravity's pull. set_drag returns false below 0.
 * - Inherit velocity adds that fraction of the emitter's own velocity, its actor's movement, at
 *   birth. At 1, particles are carried along with it.
 * - Stretch draws each particle along its motion, for sparks and streaks. Its texture's top
 *   points along its velocity across the screen. It is as long as the distance it moves in
 *   `seconds`, plus its size, trailing behind. Stretched particles don't spin. set_stretch
 *   returns false below 0. */
WGF_API bool wgf_emitter2d_set_drag(wgf_actor_t emitter, float per_second);
WGF_API float wgf_emitter2d_get_drag(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_inherit_velocity(wgf_actor_t emitter, float fraction);
WGF_API float wgf_emitter2d_get_inherit_velocity(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_set_stretch(wgf_actor_t emitter, float seconds);
WGF_API float wgf_emitter2d_get_stretch(wgf_actor_t emitter);

/* Sets or gets whether each particle's top turns along its motion across the screen, like an
 * arrow or a thrown spark (default: off).
 *
 * Its spin, clockwise on screen, is added to that angle rather than to a random one. A particle
 * standing still keeps its top up the screen. A stretched particle is already aligned. */
WGF_API bool wgf_emitter2d_set_aligned_to_motion(wgf_actor_t emitter, bool on);
WGF_API bool wgf_emitter2d_is_aligned_to_motion(wgf_actor_t emitter);

/* Sets or gets wander: each particle strays off its path on a smooth course of its own, like
 * fireflies or dust motes.
 *
 * - `distance` is how far it strays: up to about that many stage units (default: 0, none).
 * - `per_second` is about how many times a second its course turns (default: 1).
 *
 * The course starts on the particle's path. Its motion, for alignment and stretch, includes the
 * stray. Returns false if either is below 0. */
WGF_API bool wgf_emitter2d_set_wander(wgf_actor_t emitter, float distance, float per_second);
WGF_API float wgf_emitter2d_get_wander_distance(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_wander_rate(wgf_actor_t emitter);

/* Sets or gets orbit: each particle circles where the emitter was at its birth (default: none).
 *
 * Its speed is picked between `min` and `max`, in radians a second, positive clockwise on screen.
 * Its whole path turns, its own motion included, so a particle thrown outward spirals. */
WGF_API bool wgf_emitter2d_set_orbit(wgf_actor_t emitter, float min, float max);
WGF_API float wgf_emitter2d_get_orbit_min(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_orbit_max(wgf_actor_t emitter);

/* Sets or gets pulse: each particle's alpha beats `per_second` times a second, on a phase of its
 * own, for twinkling.
 *
 * At each beat's low, the alpha falls by `depth` of itself (0..1, clamped). Defaults: a rate of 0
 * (none), and a depth of 1 (to clear). Returns false for a rate below 0. */
WGF_API bool wgf_emitter2d_set_pulse(wgf_actor_t emitter, float per_second, float depth);
WGF_API float wgf_emitter2d_get_pulse_rate(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_pulse_depth(wgf_actor_t emitter);

/* Sets or gets whether particles move with the emitter, like an aura around a moving character
 * (default: off).
 *
 * - Each particle is moved as far as its actor has moved since the particle's birth. Only the
 *   actor's place counts, not its turn.
 * - An orbit circles where the emitter is now.
 * - Inherited velocity then does nothing.
 *
 * When off, each particle keeps to its own path as the emitter moves. */
WGF_API bool wgf_emitter2d_set_local_space(wgf_actor_t emitter, bool on);
WGF_API bool wgf_emitter2d_is_local_space(wgf_actor_t emitter);

/* Sets or gets curves over a particle's life: its size and its color.
 *
 * - Size is in stage units. Each particle's size is scaled by up to `variance` (0..1).
 * - Color is a tint, alpha included, so a curve can fade particles out.
 * - set_size and set_color make two keys, at 0 (birth) and 1 (death).
 * - For more keys, clear the keys and add up to 8, each at `t` (0..1 of a life).
 *
 * Between two keys, the value moves in a line. Before the first key and after the last, it holds.
 * Keys at the same time make a step. An emitter with no size keys, or with no color keys, draws nothing.
 * Defaults: size 8 to 8, and white to white.
 *
 * The calls return false for a negative size, a `t` outside 0..1, or a ninth key. The getters
 * read the keys back by index, in time order (0 past the last). */
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

/* Sets or gets a palette of up to 8 colors (default: empty, no tint).
 *
 * Each particle picks one color at birth, and it tints the particle's color over its life:
 * confetti from one emitter. add_palette_color returns false for a ninth color. */
WGF_API bool wgf_emitter2d_add_palette_color(wgf_actor_t emitter, wgf_color_t color);
WGF_API bool wgf_emitter2d_clear_palette(wgf_actor_t emitter);
WGF_API int wgf_emitter2d_get_palette_count(wgf_actor_t emitter);
WGF_API wgf_color_t wgf_emitter2d_get_palette_color(wgf_actor_t emitter, int index); /* 0 past the last */

/* Sets or gets spin, in radians a second, between `min` and `max`, from a random angle (default:
 * none). Positive is clockwise on screen. */
WGF_API bool wgf_emitter2d_set_spin(wgf_actor_t emitter, float min, float max);
WGF_API float wgf_emitter2d_get_spin_min(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_spin_max(wgf_actor_t emitter);

/* Sets or gets how particles use alpha, one of wgf_material.h's modes (default: ADD, for glows,
 * sparks, and fire), and the cutoff MASK cuts out below (default: 0.5, clamped to 0..1).
 *
 * The setter returns false if `mode` isn't an alpha mode. */
WGF_API bool wgf_emitter2d_set_alpha_mode(wgf_actor_t emitter, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_emitter2d_get_alpha_mode(wgf_actor_t emitter);
WGF_API float wgf_emitter2d_get_alpha_cutoff(wgf_actor_t emitter);

/* Sets or gets where the emitter's random numbers start (default: drawn from wgf_random as it is
 * made). The same seed and settings give the same particles. The getter returns the seed as last
 * set. */
WGF_API bool wgf_emitter2d_set_seed(wgf_actor_t emitter, int seed);
WGF_API int wgf_emitter2d_get_seed(wgf_actor_t emitter);

/* Sets or gets whether the emitter ends itself, as a one-shot effect such as an explosion's burst
 * does (default: off).
 *
 * The emitter's actor is destroyed, with the actors under it, once both are true:
 * - it has nothing left to emit: it isn't emitting, or its rate is 0;
 * - the last particle it made has died.
 *
 * That happens in the update before a frame. An emitter that has made no particle yet waits for
 * its first. */
WGF_API bool wgf_emitter2d_set_ending(wgf_actor_t emitter, bool ending);
WGF_API bool wgf_emitter2d_is_ending(wgf_actor_t emitter);

/* Sets or gets whether the emitter's particles outlive it, as a ship's exhaust outlives the ship
 * (default: off).
 *
 * If the emitter is destroyed while it has particles alive on a stage, directly or with an actor
 * above it, they finish their lives where they are. The emitter goes on as an actor of its own at
 * the stage's top, not emitting. It is made in the update before the next frame, and ends (as
 * above) with its last particle. */
WGF_API bool wgf_emitter2d_set_lingering(wgf_actor_t emitter, bool lingering);
WGF_API bool wgf_emitter2d_is_lingering(wgf_actor_t emitter);

/* Returns how many particles are alive now. clear removes them all, and the emitter keeps
 * emitting as it was. */
WGF_API int wgf_emitter2d_get_count(wgf_actor_t emitter);
WGF_API bool wgf_emitter2d_clear(wgf_actor_t emitter);

#ifdef __cplusplus
}
#endif

#endif
