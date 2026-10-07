#ifndef WGF_LIGHT_H
#define WGF_LIGHT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A light: an actor that lights the stage it is on (wgf_stage3d.h), placed and aimed with
 * the actor calls (wgf_actor_look_at): a light shines down its -z. Lights work as
 * libwgt's (wgrender's):
 *
 * - Nothing is lit implicitly: a stage with no lights and no ambient is black.
 * - Each model is lit by the up to 8 lights reaching it most (their brightness, and
 *   their falloff at its box); point and spot lights whose range doesn't reach a model
 *   are skipped for it.
 * - A light shines while it is enabled (with everything above it) and visible
 *   (wgf_actor.h): hiding it turns it off and leaves its children as they are, a lamp's
 *   bulb model under it, say.
 * - Parameters follow glTF's KHR_lights_punctual, and shading glTF's materials
 *   (wgf_material.h), so lights from glTF tools look the same here. Light colors are
 *   sRGB; lighting happens in linear space. A white directional light of intensity pi
 *   (about 3) lights a white, rough, non-metal surface facing it fully. Point and spot
 *   lights fall off with the inverse square of distance and fade smoothly to nothing at
 *   their range (0: no limit).
 * - Setters store values even where they don't apply to the light's type (a range on a
 *   directional light). Shadows are milestone 2, step 8's. */
typedef enum wgf_light_type_t {
    WGF_LIGHT_TYPE_DIRECTIONAL = 0, /* direction only, infinitely far: the sun, the moon */
    WGF_LIGHT_TYPE_POINT = 1,       /* from a point, every way: a lamp, a torch */
    WGF_LIGHT_TYPE_SPOT = 2         /* from a point, in a cone down its -z: a flashlight */
} wgf_light_type_t;

/* A white light of intensity 1, no range limit, a spot's cone pi/6 to pi/4. 0 when
 * `type` isn't a type, or there is no room for another actor. */
WGF_API wgf_actor_t wgf_light_create(wgf_light_type_t type);
WGF_API wgf_light_type_t wgf_light_get_type(wgf_actor_t light);

/* Its color (default white); alpha is ignored. */
WGF_API bool wgf_light_set_color(wgf_actor_t light, wgf_color_t color);
WGF_API wgf_color_t wgf_light_get_color(wgf_actor_t light);

/* How strong it is, its color times this (default 1, clamped to 0 or more). */
WGF_API bool wgf_light_set_intensity(wgf_actor_t light, float intensity);
WGF_API float wgf_light_get_intensity(wgf_actor_t light);

/* How far a point or spot light reaches, in units: it fades to nothing there (default
 * 0, unlimited; below 0 is clamped to 0). */
WGF_API bool wgf_light_set_range(wgf_actor_t light, float range);
WGF_API float wgf_light_get_range(wgf_actor_t light);

/* A spot light's cone, radians from its direction: full inside `inner`, nothing past
 * `outer`, smooth between (glTF's innerConeAngle and outerConeAngle). outer is clamped
 * to 0..pi/2, inner to 0..outer. */
WGF_API bool wgf_light_set_spot_cone(wgf_actor_t light, float inner, float outer);
WGF_API float wgf_light_get_spot_inner_angle(wgf_actor_t light);
WGF_API float wgf_light_get_spot_outer_angle(wgf_actor_t light);

#ifdef __cplusplus
}
#endif

#endif
