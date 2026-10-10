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
 *   directional light).
 * - Shadows (below) are a directional or spot light's, set on per light. */
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

/* Shadows, libwgt's (wgrender's). A casting light draws what it can see into a depth map
 * once a frame, and surfaces behind something are darkened: models that cast and receive
 * (wgf_model_set_shadow_casting, _receiving; both on by default), their see-through parts
 * casting nothing. Off by default: a map costs a pass and its memory. Directional and spot
 * lights cast; a point light doesn't: turning it on is refused (false, warned), as it would
 * need six maps, one each way. Up to 4 lights cast at once, the first a stage's draw finds
 * in its tree; past that a light lights the stage without shadowing it. Shadows fall on the
 * frame's first stage drawn with a casting light; a stage drawn after it (a HUD's) is lit
 * without. A model out of the camera's view still casts when its shadow reaches the view. */
WGF_API bool wgf_light_set_shadow_casting(wgf_actor_t light, bool casting);
WGF_API bool wgf_light_is_shadow_casting(wgf_actor_t light);

/* How far its shadows reach, in units (default 50): a directional light covers that much
 * of what the camera sees, in one map fitted around the view and snapped to its texels (no
 * cascades), so less distance is a sharper shadow; a spot light covers its cone out to this
 * or its range, whichever is nearer. False, unchanged, for 0 or less. */
WGF_API bool wgf_light_set_shadow_distance(wgf_actor_t light, float distance);
WGF_API float wgf_light_get_shadow_distance(wgf_actor_t light);

/* Pixels each way of its shadow map, clamped to 256..4096 and rounded down to a power of
 * two (default 2048); false for a size below 1. Bigger is sharper and slower. The casting
 * lights share one map, so they all get the largest size any of them asked for. get is
 * the size the GPU gets. */
WGF_API bool wgf_light_set_shadow_map_size(wgf_actor_t light, int size);
WGF_API int wgf_light_get_shadow_map_size(wgf_actor_t light);

/* How much of the light a shadow blocks, 0..1 (clamped; default 1, all of it). */
WGF_API bool wgf_light_set_shadow_strength(wgf_actor_t light, float strength);
WGF_API float wgf_light_get_shadow_strength(wgf_actor_t light);

/* A color mixed into what a shadow leaves behind (default black: nothing added), scaled by
 * how deep the shadow is: the stylized knob for a blue or a warm shadow. */
WGF_API bool wgf_light_set_shadow_color(wgf_actor_t light, wgf_color_t color);
WGF_API wgf_color_t wgf_light_get_shadow_color(wgf_actor_t light);

/* Depth offsets that keep a surface from shadowing itself, in shadow-map texels: `constant`
 * always, `slope` scaled by how steeply the surface faces away from the light (defaults 1,
 * 4). Too little and lit surfaces stripe ("shadow acne"); too much and a shadow creeps
 * away from its caster. False, unchanged, for a negative one. */
WGF_API bool wgf_light_set_shadow_bias(wgf_actor_t light, float constant, float slope);
WGF_API float wgf_light_get_shadow_bias_constant(wgf_actor_t light);
WGF_API float wgf_light_get_shadow_bias_slope(wgf_actor_t light);

#ifdef __cplusplus
}
#endif

#endif
