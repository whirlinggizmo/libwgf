#include <stdio.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_time.h"

/* The clock: 0 outside core, advancing and never going back inside it. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void test_clock(void)
{
    double t0, t1;
    long spins = 0;

    expect(wgf_time_get_seconds() == 0.0, "0 before init");
    wgf_core_priv_init();
    t0 = wgf_time_get_seconds();
    expect(t0 >= 0.0 && t0 < 1.0, "starts near 0 at init");
    do {
        t1 = wgf_time_get_seconds();
    } while (t1 <= t0 && ++spins < 100000000L);
    expect(t1 > t0, "advances");
    expect(wgf_time_get_seconds() >= t1, "never goes back");
    wgf_core_priv_shutdown();
    expect(wgf_time_get_seconds() == 0.0, "0 after shutdown");
}

int main(void)
{
    test_clock();
    return failures == 0 ? 0 : 1;
}
