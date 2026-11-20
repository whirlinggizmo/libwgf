#ifndef WGF_RANDOM_H
#define WGF_RANDOM_H

#include <stdbool.h>

#include "wgf.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One random number generator for the program, seeded, so a run can be made again:
 * the same seed gives the same numbers in the same order on every platform (PCG32,
 * O'Neill's permuted congruential generator). Core seeds it from the clock when it
 * starts, unless an autopilot run sets the seed (an autopilot's `seed` line), so an
 * autopilot run is the same run everywhere. Simulation that must replay draws from here; what
 * only looks random (a particle's spin) may too. */

/* Start the sequence again from `seed`; a seed's sequence is the same everywhere. */
WGF_API void wgf_random_set_seed(int seed);
/* The seed the sequence last started from. */
WGF_API int wgf_random_get_seed(void);

/* The next number: from 0 up to but not including 1. */
WGF_API float wgf_random_get_float(void);
/* The next number from `min` up to but not including `max` (`min` when they are
 * equal; swapped when `max` is less). */
WGF_API float wgf_random_get_range(float min, float max);
/* The next whole number from `min` to `max`, both included (swapped when `max` is
 * less), each equally likely. */
WGF_API int wgf_random_get_int(int min, int max);
/* `values` (count of them) put in a random order, each order equally likely: a deck, a
 * bag of pieces; how many. */
WGF_API int wgf_random_shuffle(int *values, int count);

/* A generator, an object: a handle of kind "core.generator". */
typedef wgf_handle_t wgf_generator_t;

/* A game's own generator, seeded: a world's layout drawing its own numbers, so the
 * program's sequence (an autopilot's seed) stays the game's; the same steps as the
 * program's, so a seed gives the same numbers everywhere. 0 when there are 4096. */
WGF_API wgf_generator_t wgf_random_create(int seed);
WGF_API void wgf_random_destroy(wgf_generator_t generator);
/* Its sequence started again from `seed`, and the seed it last started from. False (0)
 * for a handle that isn't a generator. */
WGF_API bool wgf_random_set_generator_seed(wgf_generator_t generator, int seed);
WGF_API int wgf_random_get_generator_seed(wgf_generator_t generator);
/* Its next numbers, as the program's are drawn; a shuffle as wgf_random_shuffle. 0 for a
 * handle that isn't a generator. */
WGF_API float wgf_random_next_float(wgf_generator_t generator);
WGF_API float wgf_random_next_range(wgf_generator_t generator, float min, float max);
WGF_API int wgf_random_next_int(wgf_generator_t generator, int min, int max);
WGF_API int wgf_random_next_shuffle(wgf_generator_t generator, int *values, int count);

#ifdef __cplusplus
}
#endif

#endif
