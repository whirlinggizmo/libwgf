#include "wgf_probe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_probe_priv.h"

/* The probes (wgf_probe.h): a table in the order they were first set, found by name
 * through a small open-addressed index of their name's hash. */

#define NAME_MAX_BYTES 63
#define INDEX_SIZE 512 /* twice the probes, a power of two */

typedef struct probe_t {
    char name[NAME_MAX_BYTES + 1];
    double value;
} probe_t;

static probe_t probes[WGF_CORE_PRIV_PROBE_MAX];
static int count;
static short index_of[INDEX_SIZE]; /* a probe's slot + 1, 0 for empty */
static bool running;

static unsigned hash(const char *name)
{
    unsigned h = 2166136261u; /* FNV-1a */
    for (; *name != '\0'; name++) h = (h ^ (unsigned char)*name) * 16777619u;
    return h;
}

static bool valid_name(const char *name)
{
    size_t n;
    if (name == NULL) return false;
    for (n = 0; name[n] != '\0'; n++) {
        const char c = name[n];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '.' || c == ':' || c == '-';
        if (!ok || n >= NAME_MAX_BYTES) return false;
    }
    return n > 0;
}

/* The slot of `name`'s probe, or -1; with `add`, a new one when there is room. */
static int find(const char *name, bool add)
{
    unsigned at = hash(name) & (INDEX_SIZE - 1);
    for (;;) {
        const int slot = index_of[at] - 1;
        if (slot < 0) break;
        if (strcmp(probes[slot].name, name) == 0) return slot;
        at = (at + 1) & (INDEX_SIZE - 1);
    }
    if (!add || count == WGF_CORE_PRIV_PROBE_MAX) return -1;
    snprintf(probes[count].name, sizeof(probes[count].name), "%s", name);
    probes[count].value = 0.0;
    index_of[at] = (short)(count + 1);
    return count++;
}

void wgf_core_priv_probe_init(void)
{
    count = 0;
    memset(index_of, 0, sizeof(index_of));
    running = true;
}

void wgf_core_priv_probe_deinit(void)
{
    count = 0;
    memset(index_of, 0, sizeof(index_of));
    running = false;
}

bool wgf_probe_set_value(const char *name, double value)
{
    int slot;
    if (!running || !valid_name(name) || !isfinite(value)) return false;
    slot = find(name, true);
    if (slot < 0) return false;
    probes[slot].value = value;
    return true;
}

double wgf_probe_get_value(const char *name)
{
    const int slot = running && valid_name(name) ? find(name, false) : -1;
    return slot >= 0 ? probes[slot].value : 0.0;
}

bool wgf_probe_has_value(const char *name)
{
    return running && valid_name(name) && find(name, false) >= 0;
}

int wgf_probe_get_count(void)
{
    return running ? count : 0;
}

const char *wgf_probe_get_name(int index)
{
    return running && index >= 0 && index < count ? probes[index].name : "";
}
