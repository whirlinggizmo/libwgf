#include <stdio.h>

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
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
