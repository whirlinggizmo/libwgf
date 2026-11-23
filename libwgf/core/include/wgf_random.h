#ifndef WGF_RANDOM_H
#define WGF_RANDOM_H

#include <stdbool.h>

#include "wgf.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Random numbers from one seeded generator for the whole program, so a run can be repeated.
 *
 * The same seed gives the same numbers in the same order on every platform. The generator
 * is PCG32, O'Neill's permuted congruential generator.
 *
 * Core seeds it from the clock when it starts, unless an autopilot run sets the seed with
 * its `seed` line. So an autopilot run is the same run everywhere.
 *
 * Simulation that must replay draws from here. Effects that only look random, such as a
 * particle's spin, may draw from here too. */

/* Starts the sequence again from `seed`. A seed's sequence is the same everywhere. */
WGF_API void wgf_random_set_seed(int seed);
/* Returns the seed the sequence last started from. */
WGF_API int wgf_random_get_seed(void);

/* Returns the next number, from 0 up to but not including 1. */
WGF_API float wgf_random_get_float(void);
/* Returns the next number from `min` up to but not including `max`.
 *
 * Returns `min` if they are equal. If `max` is less than `min`, they are swapped. */
WGF_API float wgf_random_get_range(float min, float max);
/* Returns the next whole number from `min` to `max`, both included, each equally likely.
 * If `max` is less than `min`, they are swapped. */
WGF_API int wgf_random_get_int(int min, int max);
/* Shuffles the `count` numbers in `values` into a random order, each order equally likely,
 * such as for a deck of cards or a bag of pieces. Returns how many were shuffled. */
WGF_API int wgf_random_shuffle(int *values, int count);

/* A generator of your own: a handle of kind "core.generator". */
typedef wgf_handle_t wgf_generator_t;

/* Creates a generator of the game's own, seeded with `seed`, and destroys one.
 *
 * Use one where a part of the game should draw its own numbers, such as a world's layout.
 * Then the program's sequence, which an autopilot seeds, stays the game's. A generator
 * takes the same steps as the program's, so a seed gives the same numbers everywhere.
 *
 * Returns 0 if there are already 4096 generators. */
WGF_API wgf_generator_t wgf_random_create(int seed);
WGF_API void wgf_random_destroy(wgf_generator_t generator);
/* Sets or gets a generator's seed. The setter starts its sequence again from `seed`, and
 * the getter returns the seed it last started from. They return false (or 0) for a handle
 * that isn't a generator. */
WGF_API bool wgf_random_set_generator_seed(wgf_generator_t generator, int seed);
WGF_API int wgf_random_get_generator_seed(wgf_generator_t generator);
/* Returns a generator's next numbers, as the program's calls above do, and shuffles as
 * wgf_random_shuffle does. Each returns 0 for a handle that isn't a generator. */
WGF_API float wgf_random_next_float(wgf_generator_t generator);
WGF_API float wgf_random_next_range(wgf_generator_t generator, float min, float max);
WGF_API int wgf_random_next_int(wgf_generator_t generator, int min, int max);
WGF_API int wgf_random_next_shuffle(wgf_generator_t generator, int *values, int count);

#ifdef __cplusplus
}
#endif

#endif
