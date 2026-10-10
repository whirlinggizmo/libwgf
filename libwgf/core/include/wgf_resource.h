#ifndef WGF_RESOURCE_H
#define WGF_RESOURCE_H

#include <stdbool.h>

#include "wgf.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resources: the shared, reference counted data an object uses -- a texture, a mesh, a
 * material, a font, an environment, a shader. What every resource has in common is
 * here, as wgrender's wgr_resource.h; each kind's own calls are in its header
 * (wgf_texture.h, ...). docs/ARCHITECTURE.md, "Resources and objects".
 *
 * A resource loads on create: wgf_<kind>_create(path) takes a path under fs's root and
 * returns the handle at once, PENDING; a worker prepares the file and the main thread
 * finishes it, and the handle turns READY or FAILED in a later update. Nothing is
 * called back. Creating the same path again gives the same handle, with one more
 * reference, whatever its status; releasing the last reference of one still loading
 * stops the load. A resource made from nothing but numbers (a generated mesh, a
 * material) is READY from the start. Objects take a resource in any status and do the
 * right thing until it is READY (each kind's header says what), so a program can
 * create everything at once and never wait; the status is for what a program wants to
 * show, such as a loading screen. */

typedef enum wgf_resource_status_t {
    WGF_RESOURCE_STATUS_NONE = 0,    /* not a resource: an object, a released handle, 0 */
    WGF_RESOURCE_STATUS_PENDING = 1, /* its file is being read, prepared, or finished */
    WGF_RESOURCE_STATUS_READY = 2,
    WGF_RESOURCE_STATUS_FAILED = 3   /* the path, the file, or its decoding failed (logged why) */
} wgf_resource_status_t;

/* PENDING, READY, or FAILED for a resource of any kind; NONE for anything else. */
WGF_API wgf_resource_status_t wgf_resource_get_status(wgf_handle_t resource);

/* The file it was read from, as a path under fs's root: for "name.ktx" the variant
 * this GPU got (or the PNG). "" until it is READY, for one made from numbers, and for
 * anything that isn't a resource. Borrowed: valid while the resource is. */
WGF_API const char *wgf_resource_get_path(wgf_handle_t resource);

/* Drop this handle's reference. Resources are shared and reference counted, so one is
 * freed when its last reference goes, not when this is called; one still loading then
 * stops loading. Objects hold their own references, so handing a resource to one and
 * releasing it right away is the normal pattern. A built-in (the placeholder texture)
 * is never freed. False for a handle that isn't a resource: 0, an object, or one
 * already freed. */
WGF_API bool wgf_resource_release(wgf_handle_t resource);

/* Milliseconds a frame spent finishing loads on the main thread, such as GPU uploads
 * (default 4), and on the web, where there are no worker threads, preparing those that
 * prepare in steps (an environment), so loading doesn't stall the frames it runs beside.
 * At least one step runs each frame however small it is, so one large file can exceed it:
 * a 4096x4096 texture is one upload of about 45 ms (wgrender's measurement). 0 finishes
 * one step a frame. Loading takes the longer the less of each frame it has, the more so
 * where frames are slow: a loading screen, with nothing else to draw, can give it more
 * (16 ms, say) and set it back once loaded. It applies to every load, from a bundled,
 * cached, or fetched file alike. False, and nothing changed, for a negative or
 * non-finite one. */
WGF_API bool wgf_resource_set_load_budget(float milliseconds);
WGF_API float wgf_resource_get_load_budget(void);

#ifdef __cplusplus
}
#endif

#endif
