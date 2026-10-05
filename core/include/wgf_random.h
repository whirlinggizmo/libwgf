#ifndef WGF_RANDOM_H
#define WGF_RANDOM_H

#include <stdbool.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One random number generator for the program, seeded, so a run can be made again:
 * the same seed gives the same numbers in the same order on every platform (PCG32,
 * O'Neill's permuted congruential generator). Core seeds it from the clock when it
 * starts, unless a scripted run sets the seed (a script's `seed` line), so a scripted
 * run is the same run everywhere. Simulation that must replay draws from here; what
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

#ifdef __cplusplus
}
#endif

#endif
