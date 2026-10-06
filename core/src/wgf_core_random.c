#include "wgf_random.h"

#include <stdint.h>

#include "wgf_core_random_priv.h"
#include "wgf_time.h"

/* The program's random numbers (wgf_random.h): PCG32 (M. E. O'Neill, "PCG: A Family of
 * Simple Fast Space-Efficient Statistically Good Algorithms for Random Number
 * Generation", 2014; pcg32_random_r and pcg32_srandom_r of its minimal C version), with
 * one fixed stream, so a seed alone says the sequence. */

#define INCREMENT 1442695040888963407ull /* the stream: an odd constant, PCG's own default */

static uint64_t state;
static int seed_used;

static uint32_t next(void)
{
    const uint64_t old = state;
    const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    const uint32_t rot = (uint32_t)(old >> 59u);
    state = old * 6364136223846793005ull + INCREMENT;
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

void wgf_random_set_seed(int seed)
{
    seed_used = seed;
    state = 0u;
    next();
    state += (uint64_t)(uint32_t)seed;
    next();
}

int wgf_random_get_seed(void)
{
    return seed_used;
}

void wgf_core_priv_random_init(void)
{
    /* the clock's fraction of a second, so two runs differ; an autopilot sets its own */
    const double now = wgf_time_get_seconds();
    wgf_random_set_seed((int)(uint32_t)((now - (double)(int64_t)now) * 4294967296.0) ^ (int)0x5eed5eed);
}

float wgf_random_get_float(void)
{
    return (float)(next() >> 8) * (1.0f / 16777216.0f); /* 24 bits: every float in [0, 1) equally spaced */
}

float wgf_random_get_range(float min, float max)
{
    float r;
    if (max < min) {
        const float swap = min;
        min = max;
        max = swap;
    }
    r = min + (max - min) * wgf_random_get_float();
    return r < max ? r : min; /* rounding can't reach max */
}

int wgf_random_get_int(int min, int max)
{
    uint64_t span;
    uint32_t limit, r;
    if (max < min) {
        const int swap = min;
        min = max;
        max = swap;
    }
    span = (uint64_t)((int64_t)max - (int64_t)min) + 1u;
    if (span > 0xFFFFFFFFull) return (int)((int64_t)min + (int64_t)next()); /* the whole range of int */
    /* drop the top of the range that would favour the low numbers (Lemire's rejection) */
    limit = (uint32_t)((0x100000000ull - span) % span);
    do {
        r = next();
    } while (r < limit);
    return (int)((int64_t)min + (int64_t)(r % (uint32_t)span));
}
