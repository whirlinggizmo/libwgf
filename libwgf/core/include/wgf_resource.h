#ifndef WGF_RESOURCE_H
#define WGF_RESOURCE_H

#include <stdbool.h>

#include "wgf.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resources: the shared, reference-counted data that objects use, such as a texture, mesh,
 * material, font, environment, or shader.
 *
 * This header holds what every resource has in common. Each kind's own calls are in its
 * own header (wgf_texture.h, and so on). See docs/ARCHITECTURE.md, "Resources and
 * objects".
 *
 * A resource loads when it is created:
 * - wgf_<kind>_create(path) takes a path under fs's root, and returns the handle at once,
 *   PENDING.
 * - A worker prepares the file, and the main thread finishes it. The handle turns READY
 *   or FAILED in a later update. Nothing is called back.
 * - Creating the same path again gives the same handle, with one more reference, whatever
 *   its status.
 * - Releasing the last reference of a resource that is still loading stops the load.
 * - A resource made from numbers alone, such as a generated mesh or a material, is READY
 *   from the start.
 *
 * An object, such as an actor or a voice, takes a resource in any status. Until the
 * resource is READY it draws or plays a stand-in, or nothing, as each kind's header says. So a
 * program can create everything at once and never wait. The status is for what a program
 * wants to show, such as a loading screen. */

typedef enum wgf_resource_status_t {
    WGF_RESOURCE_STATUS_NONE = 0,    /* not a resource: an object, a released handle, or 0 */
    WGF_RESOURCE_STATUS_PENDING = 1, /* its file is being read, prepared, or finished */
    WGF_RESOURCE_STATUS_READY = 2,
    WGF_RESOURCE_STATUS_FAILED = 3   /* the path, the file, or its decoding failed (logged) */
} wgf_resource_status_t;

/* Returns a resource's status: PENDING, READY, or FAILED, for a resource of any kind.
 * Returns NONE for a handle that isn't a resource. */
WGF_API wgf_resource_status_t wgf_resource_get_status(wgf_handle_t resource);

/* Returns the file a resource was read from, as a path under fs's root.
 *
 * For "name.ktx" it is the variant this GPU got, or the PNG. The path is borrowed, valid
 * while the resource is. Returns "" until the resource is READY, for a resource made from
 * numbers, and for a handle that isn't a resource. */
WGF_API const char *wgf_resource_get_path(wgf_handle_t resource);

/* Releases this handle's reference to a resource.
 *
 * Resources are shared and reference counted, so a resource is freed when its last
 * reference is released, not when this is called. A resource still loading then stops
 * loading. A built-in resource, such as the placeholder texture, is never freed.
 *
 * Objects, and resources such as a material, hold their own references. So handing a
 * resource to an object and releasing it right away is the normal pattern.
 *
 * Returns false for a handle that isn't a resource: 0, an object, or a resource already
 * freed. */
WGF_API bool wgf_resource_release(wgf_handle_t resource);

/* Sets or gets the load budget: the milliseconds each frame may spend on loading, so that
 * loading doesn't stall the frames it runs beside (default: 4).
 *
 * - The budget covers finishing loads on the main thread, such as GPU uploads.
 * - On the web, where there are no worker threads, it also covers preparing the
 *   resources that prepare in steps, such as an environment.
 * - At least one step runs each frame, however small the budget. So one large file can
 *   exceed it: a 4096x4096 texture is one upload of about 45 ms.
 * - 0 finishes one step a frame.
 * - It applies to every load, whether the file is bundled, cached, or fetched.
 *
 * The smaller the budget, the longer loading takes, and more so where frames are slow. A
 * loading screen, with nothing else to draw, can raise it (to 16 ms, say) and set it back
 * once loaded. A measurement of every frame of a run, such as tools/bench/measure_frames.py,
 * counts those as slow frames too.
 *
 * The setter returns false, and changes nothing, for a negative or non-finite value. */
WGF_API bool wgf_resource_set_load_budget(float milliseconds);
WGF_API float wgf_resource_get_load_budget(void);

#ifdef __cplusplus
}
#endif

#endif
