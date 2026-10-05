#ifndef WGF_PROBE_H
#define WGF_PROBE_H

#include <stdbool.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probes: named numbers a program publishes about itself ("score", "lives"), for what
 * watches it from outside -- a scripted run's assertions, the `wgf` tool's dump, a
 * test. libwgf's modules publish their own under their layer's name ("ecs.entities").
 * A probe is a number, kept until it is set again or core stops; reading one costs a
 * lookup, so a program sets what changed, when it changes.
 *
 * A name is 1 to 63 bytes of letters, digits, and "_", ".", ":", "-"; there are at
 * most 256 probes. */

/* Set `name` to `value`, adding it the first time. False, and nothing set, for a name
 * that breaks the rule above, a value that isn't finite, a 257th probe, or core not
 * running. */
WGF_API bool wgf_probe_set_value(const char *name, double value);

/* The value of `name`; 0 for one never set (wgf_probe_has_value tells them apart). */
WGF_API double wgf_probe_get_value(const char *name);

/* Whether `name` has been set since core started. */
WGF_API bool wgf_probe_has_value(const char *name);

/* The probes, in the order they were first set: how many, and each one's name ("" for
 * an index out of range). Borrowed: valid until core stops. */
WGF_API int wgf_probe_get_count(void);
WGF_API const char *wgf_probe_get_name(int index);

#ifdef __cplusplus
}
#endif

#endif
