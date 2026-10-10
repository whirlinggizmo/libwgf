#ifndef WGF_ENVIRONMENT_H
#define WGF_ENVIRONMENT_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An environment: the light all around a 3D stage, from a panorama -- an equirectangular
 * (2:1 latitude-longitude) image, a .hdr (Radiance RGBE) at its full brightness, or a
 * PNG or JPEG taken as sRGB -- which lights its PBR models (soft light from every
 * direction, and reflections sharp or blurred by each surface's roughness) and can be
 * drawn behind them (wgf_stage3d_set_environment, wgf_stage3d_set_background). It is a
 * resource (wgf_resource.h): shared, reference counted, and loaded on create, as a
 * texture is, then uploaded during the runtime's update. Its preparation is about 0.2 s of
 * a desktop CPU for a 1024 by 512 image: on a worker thread natively, and on the web, which
 * has no threads, a step of about a millisecond at a time within the load budget
 * (wgf_resource_set_load_budget), so it lengthens no frame and takes a few hundred of them
 * (a PNG or JPEG's decoding is one step, as a texture's is). While it is PENDING, or once
 * it has FAILED (logged once, naming the file), a stage it is set on is lit and drawn as
 * if it had none. A backend that can't filter half-float textures has no environments: each
 * FAILS, warned. */
typedef wgf_handle_t wgf_environment_t;

/* An environment from a panorama image file. 0 only when there is no room for another;
 * a path or file that can't be loaded gives one that FAILED. */
WGF_API wgf_environment_t wgf_environment_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif
