#ifndef WGF_STAGE3D_H
#define WGF_STAGE3D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_actor.h"
#include "wgf_environment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 3D stage: the root of a 3D tree, seen through a 3D camera (wgf_camera3d.h) and lit by
 * its lights (wgf_light.h) and its ambient light; libwgt's scene, named for what it is
 * since "scene" is the ecs's data file (wgf_scene.h). It draws when asked, in order with
 * the frame's other draws: a backdrop 2D stage, then this, then a HUD 2D stage over it,
 * into the presentation's visible area (wgf_presentation.h), under the clip. A 3D stage is
 * an actor (wgf_actor.h): its own transform moves everything in it, it is always a root,
 * and wgf_actor_destroy ends it. Space is right-handed, y up.
 *
 * Nothing is lit implicitly: a new stage has no lights and no ambient light, so its lit
 * models are black until it has some. Lighting happens in linear space and is tone
 * mapped to the screen, as wgrender's. */

/* How a stage's lit colors map to the screen. Lighting can exceed what a screen shows;
 * tone mapping rolls off highlights instead of clipping them. */
typedef enum wgf_stage3d_tonemap_t {
    WGF_STAGE3D_TONEMAP_NONE = 0,    /* clip */
    WGF_STAGE3D_TONEMAP_NEUTRAL = 1, /* Khronos PBR Neutral: colors kept until highlights roll off (the default) */
    WGF_STAGE3D_TONEMAP_ACES = 2     /* filmic: more contrast, highlights shift toward white */
} wgf_stage3d_tonemap_t;

/* A stage, with no camera, no ambient light, and NEUTRAL tone mapping at an exposure of
 * 0. 0 when there is no room for another actor. */
WGF_API wgf_actor_t wgf_stage3d_create(void);

/* Draw it into this frame, through its camera: its opaque models, then its 3D shapes
 * (wgf_shape3d.h), then its see-through models (a BLEND material, or a tint with alpha
 * below 255) back to front, depth tested against each other and against the frame's 3D
 * drawn before it. Each model is lit by
 * the up to 8 lights that reach it most. Outside a frame, or with no camera, nothing. */
WGF_API void wgf_stage3d_draw(wgf_actor_t stage);

/* The camera it is seen through: a 3D camera anywhere, on this stage or not (default 0,
 * none: nothing is drawn). False when `camera` isn't a 3D camera. A camera that is
 * destroyed leaves the stage with none. */
WGF_API bool wgf_stage3d_set_camera(wgf_actor_t stage, wgf_actor_t camera);
WGF_API wgf_actor_t wgf_stage3d_get_camera(wgf_actor_t stage);

/* The actor on this stage named `name`, as wgf_stage2d_find: by an index, never a walk;
 * 0 for none, for a stage that isn't a 3D stage, and for a shared name (warned). */
WGF_API wgf_actor_t wgf_stage3d_find(wgf_actor_t stage, const char *name);

/* The light that reaches everything evenly, from no direction: its color times
 * `intensity` (clamped to 0 or more). Default: none, an intensity of 0. Alpha is
 * ignored. */
WGF_API bool wgf_stage3d_set_ambient(wgf_actor_t stage, wgf_color_t color, float intensity);
WGF_API wgf_color_t wgf_stage3d_get_ambient_color(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_ambient_intensity(wgf_actor_t stage);

/* Its tone mapping, and its exposure in stops (EV): +1 doubles the brightness. Default:
 * NEUTRAL, 0. False for a tone mapping that isn't one. */
WGF_API bool wgf_stage3d_set_tonemap(wgf_actor_t stage, wgf_stage3d_tonemap_t tonemap, float exposure);
WGF_API wgf_stage3d_tonemap_t wgf_stage3d_get_tonemap(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_exposure(wgf_actor_t stage);

/* Light its PBR models with an environment (wgf_environment.h) as well as its lights and
 * ambient light: soft light from all around, and reflections sharp or blurred by each
 * surface's roughness, darkened by its occlusion. `intensity` scales it (1 as the image
 * is; clamped to 0 or more); `rotation` (radians) turns it about +y. 0 removes it.
 * Default: none. The stage holds a reference of its own; one not READY lights nothing
 * until it is. False when `environment` isn't one. */
WGF_API bool wgf_stage3d_set_environment(wgf_actor_t stage, wgf_environment_t environment, float intensity,
                                         float rotation);
WGF_API wgf_environment_t wgf_stage3d_get_environment(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_environment_intensity(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_environment_rotation(wgf_actor_t stage);

/* Draw an environment behind everything the stage draws (a sky), before its models,
 * where nothing drawn before it in the frame's 3D is nearer: with the stage's environment
 * intensity and rotation when it is the same environment, else at 1 and unturned, and
 * tone mapped as the stage is. `blur` 0..1: sharp to fully blurred (clamped). 0 removes
 * it. Default: none. The stage holds a reference of its own; one not READY draws nothing
 * until it is. False when `environment` isn't one. */
WGF_API bool wgf_stage3d_set_background(wgf_actor_t stage, wgf_environment_t environment, float blur);
WGF_API wgf_environment_t wgf_stage3d_get_background(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_background_blur(wgf_actor_t stage);

/* Skip models the camera can't see (on by default): each is tested by the box around it
 * as it is placed now, grown a little against rounding, against the camera's view
 * before it is drawn, which is far cheaper than drawing it. Turn it off to draw
 * everything, when checking whether a model's bounds are right, say. */
WGF_API bool wgf_stage3d_set_culling(wgf_actor_t stage, bool culling);
WGF_API bool wgf_stage3d_is_culling(wgf_actor_t stage);

#ifdef __cplusplus
}
#endif

#endif
