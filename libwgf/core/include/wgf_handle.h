#ifndef WGF_HANDLE_H
#define WGF_HANDLE_H

#include <stdbool.h>
#include <stdint.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An opaque number that refers to an object or a resource that libwgf owns, such as a
 * texture, a model, a voice, or a file task.
 *
 * 0 is never a valid handle. Once what a handle refers to is destroyed, the handle stops
 * working, even after libwgf reuses its slot.
 *
 * A handle is valid only in the running program. Never save one, or send one over the
 * network. */
typedef uint32_t wgf_handle_t;

/* Returns the kind of `handle` as "<layer>.<type>", such as "gfx.texture".
 *
 * The kind is read from the handle alone, so a handle whose object or resource was already
 * destroyed still names its kind. Returns "" for 0, or for a number that no kind has. */
WGF_API const char *wgf_handle_get_kind_name(wgf_handle_t handle);

/* Returns whether `handle` refers to an object or a resource that is still alive.
 *
 * Returns false for:
 * - 0, or a number that no kind has.
 * - A handle that was destroyed or released.
 * - A resource whose last reference was released.
 *
 * Ask this first wherever a handle may be dead, such as the other side of a trigger or an
 * actor kept across frames. Every other call refuses a dead handle, returning false, 0, or
 * nothing. In a debug build it also warns, naming the call and the handle. Each call
 * warns only the first time. */
WGF_API bool wgf_handle_is_alive(wgf_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
