#ifndef WGF_TIME_H
#define WGF_TIME_H

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Seconds since the runtime started, from a monotonic clock: it never goes back, and
 * isn't moved by changes to the wall clock. 0 when core isn't running. */
WGF_API double wgf_time_get_seconds(void);

#ifdef __cplusplus
}
#endif

#endif
