#include "wgf_random.h"

#include <stdint.h>
#include <string.h>

#include "wgf_core_handle_priv.h"
#include "wgf_core_random_priv.h"
#include "wgf_time.h"

/* The program's random numbers (wgf_random.h): PCG32 (M. E. O'Neill, "PCG: A Family of
 * Simple Fast Space-Efficient Statistically Good Algorithms for Random Number
 * Generation", 2014; pcg32_random_r and pcg32_srandom_r of its minimal C version), with
 * one fixed stream, so a seed alone says the sequence. The program's generator and each
 * of a game's own (wgf_random_create) are the same steps on a state of their own. */

#define INCREMENT 1442695040888963407ull /* the stream: an odd constant, PCG's own default */

typedef struct pcg_t {
    uint64_t state;
    int seed;
} pcg_t;

static pcg_t program;

static struct {
    bool ready;
    wgf_core_priv_handle_pool_t pool;
    pcg_t *generators;
} gp;

static uint32_t next(pcg_t *g)
{
    const uint64_t old = g->state;
    const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    const uint32_t rot = (uint32_t)(old >> 59u);
    g->state = old * 6364136223846793005ull + INCREMENT;
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

static void seed_of(pcg_t *g, int seed)
{
    g->seed = seed;
    g->state = 0u;
    next(g);
    g->state += (uint64_t)(uint32_t)seed;
    next(g);
}

static float float_of(pcg_t *g)
{
    return (float)(next(g) >> 8) * (1.0f / 16777216.0f); /* 24 bits: every float in [0, 1) equally spaced */
}

static float range_of(pcg_t *g, float min, float max)
{
    float r;
    if (max < min) {
        const float swap = min;
        min = max;
        max = swap;
    }
    r = min + (max - min) * float_of(g);
    return r < max ? r : min; /* rounding can't reach max */
}

static int int_of(pcg_t *g, int min, int max)
{
    uint64_t span;
    uint32_t limit, r;
    if (max < min) {
        const int swap = min;
        min = max;
        max = swap;
    }
    span = (uint64_t)((int64_t)max - (int64_t)min) + 1u;
    if (span > 0xFFFFFFFFull) return (int)((int64_t)min + (int64_t)next(g)); /* the whole range of int */
    /* drop the top of the range that would favour the low numbers (Lemire's rejection) */
    limit = (uint32_t)((0x100000000ull - span) % span);
    do {
        r = next(g);
    } while (r < limit);
    return (int)((int64_t)min + (int64_t)(r % (uint32_t)span));
}

/* Fisher-Yates, from the end. */
static int shuffle_of(pcg_t *g, int *values, int count)
{
    int i;
    if (values == NULL || count <= 0) return 0;
    for (i = count - 1; i > 0; i--) {
        const int j = int_of(g, 0, i), swap = values[i];
        values[i] = values[j];
        values[j] = swap;
    }
    return count;
}

/* ---------------------------------------------------------------- the program's ---- */

void wgf_random_set_seed(int seed)
{
    seed_of(&program, seed);
}

int wgf_random_get_seed(void)
{
    return program.seed;
}

void wgf_core_priv_random_init(void)
{
    /* the clock's fraction of a second, so two runs differ; an autopilot sets its own */
    const double now = wgf_time_get_seconds();
    wgf_random_set_seed((int)(uint32_t)((now - (double)(int64_t)now) * 4294967296.0) ^ (int)0x5eed5eed);
}

float wgf_random_get_float(void)
{
    return float_of(&program);
}

float wgf_random_get_range(float min, float max)
{
    return range_of(&program, min, max);
}

int wgf_random_get_int(int min, int max)
{
    return int_of(&program, min, max);
}

int wgf_random_shuffle(int *values, int count)
{
    return shuffle_of(&program, values, count);
}

/* ---------------------------------------------------------------- a game's own ---- */

static pcg_t *generator_of_at(wgf_generator_t generator, const char *caller)
{
    uint16_t index;
    if (!gp.ready || !wgf_core_priv_handle_pool_resolve_at(&gp.pool, generator, &index, caller)) return NULL;
    return &gp.generators[index];
}
#define generator_of(generator) generator_of_at((generator), WGF_CORE_PRIV_CALLER)

wgf_generator_t wgf_random_create(int seed)
{
    wgf_generator_t generator;
    pcg_t *g;
    if (!gp.ready) {
        gp.ready = wgf_core_priv_handle_pool_init(&gp.pool, WGF_CORE_PRIV_HANDLE_KIND_GENERATOR,
                                                  (void **)&gp.generators, sizeof(pcg_t), 16, 4096);
        if (!gp.ready) return 0;
    }
    generator = wgf_core_priv_handle_pool_alloc(&gp.pool);
    g = generator_of(generator);
    if (g == NULL) return 0;
    seed_of(g, seed);
    return generator;
}

void wgf_random_destroy(wgf_generator_t generator)
{
    if (generator_of(generator) != NULL) wgf_core_priv_handle_pool_free(&gp.pool, generator);
}

bool wgf_random_set_generator_seed(wgf_generator_t generator, int seed)
{
    pcg_t *g = generator_of(generator);
    if (g == NULL) return false;
    seed_of(g, seed);
    return true;
}

int wgf_random_get_generator_seed(wgf_generator_t generator)
{
    const pcg_t *g = generator_of(generator);
    return g != NULL ? g->seed : 0;
}

float wgf_random_next_float(wgf_generator_t generator)
{
    pcg_t *g = generator_of(generator);
    return g != NULL ? float_of(g) : 0.0f;
}

float wgf_random_next_range(wgf_generator_t generator, float min, float max)
{
    pcg_t *g = generator_of(generator);
    return g != NULL ? range_of(g, min, max) : 0.0f;
}

int wgf_random_next_int(wgf_generator_t generator, int min, int max)
{
    pcg_t *g = generator_of(generator);
    return g != NULL ? int_of(g, min, max) : 0;
}

int wgf_random_next_shuffle(wgf_generator_t generator, int *values, int count)
{
    pcg_t *g = generator_of(generator);
    return g != NULL ? shuffle_of(g, values, count) : 0;
}

void wgf_core_priv_random_shutdown(void)
{
    if (gp.ready) wgf_core_priv_handle_pool_destroy(&gp.pool);
    memset(&gp, 0, sizeof(gp));
}
