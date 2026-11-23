#ifndef WGF_LIGHT_H
#define WGF_LIGHT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A light: an actor that lights the stage it is on (wgf_stage3d.h).
 *
 * Place and aim it with the actor calls (wgf_actor_look_at). A light shines down its -z.
 *
 * - Nothing is lit implicitly. A stage with no lights and no ambient light is black.
 * - Each model is lit by the up to 8 lights that reach it most, by their brightness and their
 *   falloff at its box. Point and spot lights whose range doesn't reach a model are skipped
 *   for it.
 * - A light shines while it is enabled (with every actor above it) and visible (wgf_actor.h).
 *   Hiding it turns it off, and leaves its children as they are, such as a lamp's bulb model
 *   under it.
 * - Parameters follow glTF's KHR_lights_punctual, and shading follows glTF's materials
 *   (wgf_material.h). So lights from glTF tools look the same here.
 * - Light colors are sRGB. Lighting happens in linear space.
 * - A white directional light of intensity pi (about 3) fully lights a white, rough,
 *   non-metal surface facing it.
 * - Point and spot lights fall off with the inverse square of distance. They fade smoothly to
 *   nothing at their range (0: no limit).
 * - Setters store values even where they don't apply to the light's type, such as a range on
 *   a directional light.
 * - Directional and spot lights can cast shadows (below), turned on for each light. */
typedef enum wgf_light_type_t {
    WGF_LIGHT_TYPE_DIRECTIONAL = 0, /* direction only, infinitely far: the sun, the moon */
    WGF_LIGHT_TYPE_POINT = 1,       /* from a point, every way: a lamp, a torch */
    WGF_LIGHT_TYPE_SPOT = 2         /* from a point, in a cone down its -z: a flashlight */
} wgf_light_type_t;

/* Creates a white light of intensity 1, with no range limit, and a spot cone of pi/6 to pi/4.
 *
 * Returns 0 if `type` isn't a light type, or if there is no room for another actor. */
WGF_API wgf_actor_t wgf_light_create(wgf_light_type_t type);
WGF_API wgf_light_type_t wgf_light_get_type(wgf_actor_t light);

/* Sets or gets the light's color (default: white). Alpha is ignored. */
WGF_API bool wgf_light_set_color(wgf_actor_t light, wgf_color_t color);
WGF_API wgf_color_t wgf_light_get_color(wgf_actor_t light);

/* Sets or gets how strong the light is: its color times this (default: 1).
 *
 * It is clamped to 0 or more. */
WGF_API bool wgf_light_set_intensity(wgf_actor_t light, float intensity);
WGF_API float wgf_light_get_intensity(wgf_actor_t light);

/* Sets or gets how far a point or spot light reaches, in units (default: 0, unlimited).
 *
 * The light fades to nothing at its range. A range below 0 is clamped to 0. */
WGF_API bool wgf_light_set_range(wgf_actor_t light, float range);
WGF_API float wgf_light_get_range(wgf_actor_t light);

/* Sets or gets a spot light's cone, in radians from its direction.
 *
 * - Inside `inner`, the light is full.
 * - Past `outer`, there is none.
 * - Between them, it fades smoothly.
 *
 * These are glTF's innerConeAngle and outerConeAngle. `outer` is clamped to 0..pi/2, and
 * `inner` to 0..outer. */
WGF_API bool wgf_light_set_spot_cone(wgf_actor_t light, float inner, float outer);
WGF_API float wgf_light_get_spot_inner_angle(wgf_actor_t light);
WGF_API float wgf_light_get_spot_outer_angle(wgf_actor_t light);

/* Sets or gets whether the light casts shadows (default: off).
 *
 * A casting light draws what it can see into a depth map once a frame. Surfaces behind another
 * surface are then darkened. Shadows are off by default because a map costs a pass and its
 * memory.
 *
 * - Models cast and receive shadows (wgf_model_set_shadow_casting and
 *   wgf_model_set_shadow_receiving; both on by default). Their see-through parts cast nothing.
 * - Directional and spot lights cast. A point light doesn't: turning it on is refused (returns
 *   false, warned), since it would need six maps, one each way.
 * - Up to 4 lights cast at once: the first ones a stage's draw finds in its tree. Past that, a
 *   light lights the stage without shadowing it.
 * - Shadows fall on the frame's first stage drawn with a casting light. A stage drawn after it
 *   (a HUD's) is lit without shadows.
 * - A model out of the camera's view still casts when its shadow reaches the view. */
WGF_API bool wgf_light_set_shadow_casting(wgf_actor_t light, bool casting);
WGF_API bool wgf_light_is_shadow_casting(wgf_actor_t light);

/* Sets or gets how far the light's shadows reach, in units (default: 50).
 *
 * - A directional light covers that much of what the camera sees. It uses one map, fitted
 *   around the view and snapped to its texels (no cascades). So a shorter distance gives a
 *   sharper shadow.
 * - A spot light covers its cone out to this distance or its range, whichever is nearer.
 *
 * set_shadow_distance returns false, and changes nothing, for a distance of 0 or less. */
WGF_API bool wgf_light_set_shadow_distance(wgf_actor_t light, float distance);
WGF_API float wgf_light_get_shadow_distance(wgf_actor_t light);

/* Sets or gets the size of the light's shadow map, in pixels each way (default: 2048).
 *
 * The size is clamped to 256..4096 and rounded down to a power of two. Bigger is sharper and
 * slower. The casting lights share one map, so they all get the largest size any of them asked
 * for. get_shadow_map_size returns the size the GPU gets.
 *
 * set_shadow_map_size returns false for a size below 1. */
WGF_API bool wgf_light_set_shadow_map_size(wgf_actor_t light, int size);
WGF_API int wgf_light_get_shadow_map_size(wgf_actor_t light);

/* Sets or gets how much of the light a shadow blocks, 0..1 (default: 1, all of it).
 *
 * It is clamped to 0..1. */
WGF_API bool wgf_light_set_shadow_strength(wgf_actor_t light, float strength);
WGF_API float wgf_light_get_shadow_strength(wgf_actor_t light);

/* Sets or gets a color mixed into what a shadow leaves behind (default: black, nothing added).
 *
 * It is scaled by how deep the shadow is. Use it to stylize shadows, as blue or warm ones. */
WGF_API bool wgf_light_set_shadow_color(wgf_actor_t light, wgf_color_t color);
WGF_API wgf_color_t wgf_light_get_shadow_color(wgf_actor_t light);

/* Sets or gets the depth offsets that keep a surface from shadowing itself, in shadow-map
 * texels (default: 1 and 4).
 *
 * - `constant` always applies.
 * - `slope` is scaled by how steeply the surface faces away from the light.
 *
 * Too little bias and lit surfaces stripe ("shadow acne"). Too much, and a shadow creeps away
 * from its caster.
 *
 * set_shadow_bias returns false, and changes nothing, if either is negative. */
WGF_API bool wgf_light_set_shadow_bias(wgf_actor_t light, float constant, float slope);
WGF_API float wgf_light_get_shadow_bias_constant(wgf_actor_t light);
WGF_API float wgf_light_get_shadow_bias_slope(wgf_actor_t light);

#ifdef __cplusplus
}
#endif

#endif
