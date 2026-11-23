#ifndef WGF_TIME_H
#define WGF_TIME_H

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the seconds since the runtime started.
 *
 * The clock is monotonic: it never goes back, and changes to the wall clock don't move it.
 * Returns 0 if core isn't running. */
WGF_API double wgf_time_get_seconds(void);

#ifdef __cplusplus
}
#endif

#endif
