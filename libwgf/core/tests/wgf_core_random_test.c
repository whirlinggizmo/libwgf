#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_random.h"

/* seed 42's first four get_int(0, 1000000), from a reference PCG32 written apart from
 * the C (Python, at the time of writing) */
#define GOLDEN_0 864656
#define GOLDEN_1 669414
#define GOLDEN_2 639511
#define GOLDEN_3 33612

/* The program's random numbers: a seed's sequence is the same on every platform (the
 * golden values below, which every preset's run checks), and the ranges are kept. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    int i, low = 0, high = 0, seen[7] = {0};
    double sum = 0.0;
    float a[4], b[4];

    wgf_core_priv_init();
    wgf_random_set_seed(42);
    expect(wgf_random_get_seed() == 42, "the seed reads back");
    for (i = 0; i < 4; i++) a[i] = wgf_random_get_float();
    wgf_random_set_seed(42);
    for (i = 0; i < 4; i++) b[i] = wgf_random_get_float();
    expect(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3], "a seed gives the same sequence again");
    wgf_random_set_seed(42);
    {
        const int golden[4] = {GOLDEN_0, GOLDEN_1, GOLDEN_2, GOLDEN_3};
        for (i = 0; i < 4; i++) {
            const int got = wgf_random_get_int(0, 1000000);
            if (got != golden[i]) printf("seed 42, draw %d: %d (expected %d)\n", i, got, golden[i]);
            expect(got == golden[i], "seed 42's sequence is the same on every platform");
        }
    }
    wgf_random_set_seed(-7);
    expect(wgf_random_get_seed() == -7, "a negative seed");

    for (i = 0; i < 100000; i++) {
        const float f = wgf_random_get_float();
        const float r = wgf_random_get_range(-2.0f, 3.0f);
        const int n = wgf_random_get_int(-3, 3);
        if (f < 0.0f || f >= 1.0f) low++;
        if (r < -2.0f || r >= 3.0f) high++;
        if (n >= -3 && n <= 3) seen[n + 3]++;
        sum += f;
    }
    expect(low == 0, "get_float stays in [0, 1)");
    expect(high == 0, "get_range stays in [min, max)");
    for (i = 0; i < 7; i++) expect(seen[i] > 13000 && seen[i] < 15600, "get_int reaches every number, about evenly");
    expect(seen[0] + seen[1] + seen[2] + seen[3] + seen[4] + seen[5] + seen[6] == 100000, "and nothing outside");
    expect(sum / 100000.0 > 0.49 && sum / 100000.0 < 0.51, "get_float's mean is about one half");
    expect(wgf_random_get_range(2.0f, 2.0f) == 2.0f, "an empty range gives its min");
    {
        const float r = wgf_random_get_range(5.0f, 1.0f);
        expect(r >= 1.0f && r < 5.0f, "a range given backwards is swapped");
    }
    expect(wgf_random_get_int(4, 4) == 4, "a range of one number");
    {
        const int n = wgf_random_get_int(10, 8);
        expect(n >= 8 && n <= 10, "an int range given backwards is swapped");
        const int whole = wgf_random_get_int(-2147483647 - 1, 2147483647);
        (void)whole; /* the whole range of int, without overflow (ubsan's to catch) */
    }
    { /* gen_a shuffle, and gen_a game's own generators */
        int deck[10], counted[10] = {0}, k, all = 1, moved = 0;
        wgf_generator_t gen_a, gen_b;
        for (k = 0; k < 10; k++) deck[k] = k;
        expect(wgf_random_shuffle(deck, 10) == 10 && wgf_random_shuffle(NULL, 3) == 0, "a shuffle, how many");
        for (k = 0; k < 10; k++) counted[deck[k]]++, moved += deck[k] != k;
        for (k = 0; k < 10; k++) all = all && counted[k] == 1;
        expect(all && moved > 0, "every one still there, in another order");
        wgf_random_set_seed(99);
        gen_a = wgf_random_create(7);
        gen_b = wgf_random_create(7);
        expect(gen_a != 0 && gen_b != 0 && wgf_random_get_generator_seed(gen_a) == 7, "two generators of one seed");
        {
            const float first = wgf_random_next_float(gen_a);
            const int program = wgf_random_get_int(0, 1000000);
            expect(wgf_random_next_float(gen_b) == first && first >= 0.0f && first < 1.0f,
                   "the same numbers from the same seed");
            wgf_random_set_seed(99);
            expect(wgf_random_get_int(0, 1000000) == program, "the program's sequence untouched by them");
        }
        wgf_random_set_generator_seed(gen_a, 7);
        wgf_random_set_generator_seed(gen_b, 7);
        expect(wgf_random_next_int(gen_a, 0, 100) == wgf_random_next_int(gen_b, 0, 100) &&
                   wgf_random_next_range(gen_a, 2, 3) == wgf_random_next_range(gen_b, 2, 3),
               "seeded again: again the same");
        {
            int x[5] = {1, 2, 3, 4, 5}, y[5] = {1, 2, 3, 4, 5};
            wgf_random_next_shuffle(gen_a, x, 5);
            wgf_random_next_shuffle(gen_b, y, 5);
            expect(memcmp(x, y, sizeof(x)) == 0, "and shuffled alike");
        }
        wgf_random_destroy(gen_a);
        expect(wgf_random_next_float(gen_a) == 0.0f && !wgf_random_set_generator_seed(gen_a, 1), "destroyed: refused");
        wgf_random_destroy(gen_b);
    }
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
