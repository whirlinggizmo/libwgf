#ifndef WGF_ENVIRONMENT_H
#define WGF_ENVIRONMENT_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An environment: the light all around a 3D stage, from a panorama image.
 *
 * The panorama is an equirectangular (2:1 latitude-longitude) image, either:
 * - a .hdr file (Radiance RGBE), at its full brightness, or
 * - a PNG or JPEG, taken as sRGB.
 *
 * An environment lights a stage's PBR models with soft light from every direction, and with
 * reflections sharp or blurred by each surface's roughness. It can also be drawn behind them
 * (wgf_stage3d_set_environment, wgf_stage3d_set_background).
 *
 * An environment is a resource (wgf_resource.h): shared, reference counted, and loaded on
 * create, as a texture is. It is then uploaded during the runtime's update.
 *
 * Its preparation takes about 0.2 seconds of a desktop CPU for a 1024 by 512 image.
 * - Natively, it runs on a worker thread.
 * - On the web, which has no threads, it runs a step of about a millisecond at a time,
 *   within the load budget (wgf_resource_set_load_budget). It lengthens no frame, and takes
 *   a few hundred of them.
 * - A PNG or JPEG's decoding is one step, as a texture's is.
 *
 * While it is PENDING, or once it has FAILED (logged once, naming the file), a stage it is
 * set on is lit and drawn as if it had none.
 *
 * A backend that can't filter half-float textures has no environments: each one FAILS, with
 * a warning. */
typedef wgf_handle_t wgf_environment_t;

/* Creates an environment from a panorama image file.
 *
 * Returns 0 only if there is no room for another environment. A path or file that can't be
 * loaded gives an environment that FAILED. */
WGF_API wgf_environment_t wgf_environment_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif
