/* sokol_time's implementation, compiled once, in core, which owns the clock: core has no
 * window, so it can't take time from the platform layer above it. Compiled with
 * warnings off: they aren't libwgf's to fix. */
#define _POSIX_C_SOURCE 200809L /* clock_gettime, under strict C11 */
#define SOKOL_TIME_IMPL
#include "sokol_time.h"
