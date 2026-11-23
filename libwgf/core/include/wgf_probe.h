#ifndef WGF_PROBE_H
#define WGF_PROBE_H

#include <stdbool.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probes: named numbers or text that a program publishes about itself, such as "score",
 * "lives", or the screen it is on.
 *
 * Probes are for whatever watches the program from outside: an autopilot run's
 * assertions, the `wgf` tool's dump, or a test. libwgf's modules publish their own probes
 * under their layer's name ("ecs.entities", "ui.focus").
 *
 * A probe is a number or a text. It is kept until it is set again or core stops. Reading
 * one costs a lookup, so set a probe when its value changes, and only then.
 *
 * - A name is 1 to 63 bytes of letters, digits, "_", ".", ":", and "-".
 * - There are at most 256 probes. */

/* Sets probe `name` to the number `value`, adding the probe the first time.
 *
 * Returns false, and sets nothing, if `name` breaks the rules above, if `value` isn't
 * finite, if it would be a 257th probe, or if core isn't running. */
WGF_API bool wgf_probe_set_value(const char *name, double value);

/* Returns the number of probe `name`, or 0 for a probe never set. wgf_probe_has_value tells
 * those apart. */
WGF_API double wgf_probe_get_value(const char *name);

/* Returns whether probe `name` has been set since core started, as a number or as text. */
WGF_API bool wgf_probe_has_value(const char *name);

/* Sets or gets probe `name` as text (UTF-8, at most 255 bytes).
 *
 * set_text adds the probe the first time. From then on the probe is text, and its number
 * is 0, until a number is set again. It returns false, and sets nothing, if:
 * - `name` breaks the rules above.
 * - `text` is NULL or longer than 255 bytes.
 * - It would be a 257th probe.
 * - Core isn't running, or memory runs out.
 *
 * get_text returns the probe's text, borrowed until the probe next changes. It returns ""
 * for a probe that is a number or was never set. */
WGF_API bool wgf_probe_set_text(const char *name, const char *text);
WGF_API const char *wgf_probe_get_text(const char *name);

/* Returns how many probes there are, and each one's name, in the order they were first set.
 *
 * A name is borrowed, valid until core stops. get_name returns "" for an index out of
 * range. */
WGF_API int wgf_probe_get_count(void);
WGF_API const char *wgf_probe_get_name(int index);

#ifdef __cplusplus
}
#endif

#endif
