#ifndef WGF_HANDLE_H
#define WGF_HANDLE_H

#include <stdint.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An opaque reference to something libwgf owns: a texture, a model, a task. 0 is
 * never a valid handle. A handle to something destroyed stops working, even
 * after its slot is reused.
 *
 * A handle is valid only in the running program: never save one or send one
 * over the network. */
typedef uint32_t wgf_handle_t;

/* The kind of thing `handle` refers to, as "<layer>.<type>" ("gfx.texture"); ""
 * for 0 or a number no kind has. Read from the handle alone: a handle to
 * something already destroyed still names its kind. */
WGF_API const char *wgf_handle_get_kind_name(wgf_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
