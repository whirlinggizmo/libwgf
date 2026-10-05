#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_probe.h"

/* Probes: set, read, listed in order, refused when the rule says, gone with core. */

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
    char name[16];
    int i;
    expect(!wgf_probe_set_value("score", 1), "refused before core starts");
    wgf_core_priv_init();
    expect(wgf_probe_get_count() == 0, "none at first");
    expect(!wgf_probe_has_value("score") && wgf_probe_get_value("score") == 0.0, "an unset probe reads 0");
    expect(wgf_probe_set_value("score", 120), "set");
    expect(wgf_probe_set_value("ecs.entities", 7) && wgf_probe_set_value("level:1-a_b", -2.5), "names with . : - _");
    expect(wgf_probe_has_value("score") && wgf_probe_get_value("score") == 120.0, "read back");
    expect(wgf_probe_set_value("score", 130) && wgf_probe_get_value("score") == 130.0, "set again");
    expect(wgf_probe_get_count() == 3, "three");
    expect(strcmp(wgf_probe_get_name(0), "score") == 0 && strcmp(wgf_probe_get_name(1), "ecs.entities") == 0 &&
               strcmp(wgf_probe_get_name(2), "level:1-a_b") == 0,
           "listed in the order first set");
    expect(strcmp(wgf_probe_get_name(3), "") == 0 && strcmp(wgf_probe_get_name(-1), "") == 0, "out of range: \"\"");

    expect(!wgf_probe_set_value("", 1) && !wgf_probe_set_value(NULL, 1), "an empty or NULL name is refused");
    expect(!wgf_probe_set_value("has space", 1) && !wgf_probe_set_value("slash/no", 1), "a character outside the rule");
    expect(!wgf_probe_set_value("0123456789012345678901234567890123456789012345678901234567890123", 1),
           "a 64-byte name is refused");
    expect(wgf_probe_set_value("012345678901234567890123456789012345678901234567890123456789012", 1),
           "a 63-byte one isn't");
    expect(!wgf_probe_set_value("nan", NAN) && !wgf_probe_set_value("inf", INFINITY), "a value that isn't finite");
    expect(!wgf_probe_has_value("nan"), "and nothing was added for it");

    for (i = wgf_probe_get_count(); i < 256; i++) {
        snprintf(name, sizeof(name), "p%d", i);
        if (!wgf_probe_set_value(name, i)) break;
    }
    expect(wgf_probe_get_count() == 256, "256 probes");
    expect(!wgf_probe_set_value("one_more", 1), "the 257th is refused");
    expect(wgf_probe_set_value("p100", 5) && wgf_probe_get_value("p100") == 5.0, "a full table still sets old ones");

    wgf_core_priv_shutdown();
    expect(wgf_probe_get_count() == 0 && !wgf_probe_has_value("score"), "gone with core");
    wgf_core_priv_init();
    expect(wgf_probe_get_count() == 0, "a new run starts empty");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
