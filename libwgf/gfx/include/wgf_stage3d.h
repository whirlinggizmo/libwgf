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

/* A 3D stage: the root of a 3D tree, seen through a 3D camera (wgf_camera3d.h).
 *
 * It is lit by its lights (wgf_light.h) and its ambient light. It isn't called a scene,
 * since a scene is the ecs's data file (wgf_scene.h).
 *
 * It draws when asked, in order with the frame's other draws. For example: a backdrop 2D
 * stage, then this stage, then a HUD 2D stage over it. It draws into the presentation's
 * visible area (wgf_presentation.h), under the clip.
 *
 * A 3D stage is an actor (wgf_actor.h). Its own transform moves every actor in it. It is
 * always a root, and wgf_actor_destroy ends it. Space is right-handed, with y up.
 *
 * Nothing is lit implicitly. A new stage has no lights and no ambient light, so its lit
 * models are black until it has some. Lighting happens in linear space, and is tone mapped
 * to the screen. */

/* How a stage's lit colors map to the screen.
 *
 * Lighting can exceed what a screen shows. Tone mapping rolls off highlights instead of
 * clipping them. */
typedef enum wgf_stage3d_tonemap_t {
    WGF_STAGE3D_TONEMAP_NONE = 0,    /* clips */
    WGF_STAGE3D_TONEMAP_NEUTRAL = 1, /* Khronos PBR Neutral: keeps colors until highlights roll off (the default) */
    WGF_STAGE3D_TONEMAP_ACES = 2     /* filmic: more contrast, and highlights shift toward white */
} wgf_stage3d_tonemap_t;

/* Creates a 3D stage, with no camera, no ambient light, and NEUTRAL tone mapping at an
 * exposure of 0.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_stage3d_create(void);

/* Draws the stage into this frame, through its camera.
 *
 * It draws, in order:
 * - Its opaque models.
 * - Its 3D shapes (wgf_shape3d.h).
 * - Its see-through models, back to front. A model is see-through if it has a BLEND material,
 *   or a tint with alpha below 255.
 *
 * All are depth tested against each other, and against the frame's 3D drawn before them. Each
 * model is lit by the up to 8 lights that reach it most.
 *
 * Outside a frame, or with no camera, it does nothing. */
WGF_API void wgf_stage3d_draw(wgf_actor_t stage);

/* Draws the stage as wgf_stage3d_draw does, but through `camera` in place of its own.
 *
 * The stage's own camera is left as it is. Use it for a second view of the same stage, such as
 * a mirror or a minimap drawn into a render target (wgf_render_begin_target).
 *
 * The frame's shadows are fit around one view: the first drawn on the screen, or else the
 * first drawn. The stage's other views are shaded with those shadows where they reach.
 *
 * Does nothing if `camera` isn't a 3D camera, or outside a frame. */
WGF_API void wgf_stage3d_draw_with(wgf_actor_t stage, wgf_actor_t camera);

/* Sets or gets the camera the stage is seen through (default: 0, none, so nothing is drawn).
 *
 * It can be a 3D camera anywhere, on this stage or not. A camera that is destroyed leaves the
 * stage with none.
 *
 * set_camera returns false if `camera` isn't a 3D camera. */
WGF_API bool wgf_stage3d_set_camera(wgf_actor_t stage, wgf_actor_t camera);
WGF_API wgf_actor_t wgf_stage3d_get_camera(wgf_actor_t stage);

/* Returns the actor on this stage named `name`, as wgf_stage2d_find does.
 *
 * It looks the name up in an index, never by walking the tree.
 *
 * Returns 0 if no actor has that name, if `stage` isn't a 3D stage, or if the name is shared
 * (warned). */
WGF_API wgf_actor_t wgf_stage3d_find(wgf_actor_t stage, const char *name);

/* Returns the nearest actor on this stage that a ray meets.
 *
 * - pick casts the ray from the screen's point (x, y), in logical pixels (where the pointer
 *   is, wgf_mouse.h), through the stage's camera. It makes the ray as
 *   wgf_camera3d_screen_to_origin and wgf_camera3d_screen_to_direction do.
 * - pick_ray casts any ray, from its origin along its direction. The direction need not be a
 *   unit vector.
 *
 * What can be hit:
 * - A model, by its triangles as it is drawn now. Its box is tested first. A skinned model is
 *   tested in its pose this frame, with no box test first.
 * - A glTF file's node, which is reported as its file's root (the model wgf_model_create
 *   made).
 * - A 3D shape's cube or sphere.
 *
 * An actor that isn't enabled isn't hit, and neither is any actor under it. An actor that
 * isn't visible, or isn't pickable (wgf_actor_set_pickable), lets the ray through. Its
 * children can still be hit. A MASK or BLEND part is hit wherever it is drawn at all,
 * including its see-through texels.
 *
 * The hit goes into `hit`, your array, as many values as fit in `hit_count`. Pass NULL and 0
 * for none. The layout is wgf_collide3d.h's ray hit:
 * - 0, 1, 2: the surface's normal there, a unit vector facing the ray.
 * - 3: zero.
 * - 4, 5, 6: the point, in the stage's space.
 * - 7: how far along the ray, in lengths of its direction. For a point on the screen, it is in
 *   units from the camera's near plane.
 *
 * All values are 0 after a miss.
 *
 * Returns the actor hit. Returns 0 if:
 * - Nothing is hit.
 * - `stage` isn't a 3D stage.
 * - The stage has no camera (pick only).
 * - The direction is 0, or not finite (pick_ray only).
 *
 * It works outside a frame, with the stage as it is placed now. */
WGF_API wgf_actor_t wgf_stage3d_pick(wgf_actor_t stage, float x, float y, float *hit, int hit_count);
WGF_API wgf_actor_t wgf_stage3d_pick_ray(wgf_actor_t stage, float ox, float oy, float oz, float dx, float dy, float dz,
                                         float *hit, int hit_count);

/* Sets or gets the ambient light: light that reaches everything evenly, from no direction
 * (default: none, an intensity of 0).
 *
 * It is `color` times `intensity`. The intensity is clamped to 0 or more. Alpha is ignored. */
WGF_API bool wgf_stage3d_set_ambient(wgf_actor_t stage, wgf_color_t color, float intensity);
WGF_API wgf_color_t wgf_stage3d_get_ambient_color(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_ambient_intensity(wgf_actor_t stage);

/* Sets or gets the stage's tone mapping, and its exposure in stops (EV) (default: NEUTRAL,
 * 0).
 *
 * An exposure of +1 doubles the brightness.
 *
 * set_tonemap returns false if `tonemap` isn't a tone mapping. */
WGF_API bool wgf_stage3d_set_tonemap(wgf_actor_t stage, wgf_stage3d_tonemap_t tonemap, float exposure);
WGF_API wgf_stage3d_tonemap_t wgf_stage3d_get_tonemap(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_exposure(wgf_actor_t stage);

/* Sets or gets an environment (wgf_environment.h) that lights the stage's PBR models (default:
 * none).
 *
 * It lights them as well as the stage's lights and ambient light. It gives soft light from all
 * around, and reflections that are sharp or blurred by each surface's roughness, darkened by
 * its occlusion.
 *
 * - `intensity` scales it. 1 is as the image is. It is clamped to 0 or more.
 * - `rotation` turns it about +y, in radians.
 * - An `environment` of 0 removes it.
 *
 * The stage holds a reference of its own. An environment that isn't READY lights nothing
 * until it is.
 *
 * set_environment returns false if `environment` isn't an environment. */
WGF_API bool wgf_stage3d_set_environment(wgf_actor_t stage, wgf_environment_t environment, float intensity,
                                         float rotation);
WGF_API wgf_environment_t wgf_stage3d_get_environment(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_environment_intensity(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_environment_rotation(wgf_actor_t stage);

/* Sets or gets an environment drawn behind everything the stage draws, as a sky (default:
 * none).
 *
 * It is drawn before the stage's models, where nothing drawn before it in the frame's 3D is
 * nearer. It is tone mapped as the stage is.
 *
 * - If it is the same environment as the stage's, it uses the stage's environment intensity
 *   and rotation. Otherwise it is drawn at intensity 1, unturned.
 * - `blur` runs from 0, sharp, to 1, fully blurred (clamped).
 * - An `environment` of 0 removes it.
 *
 * The stage holds a reference of its own. An environment that isn't READY draws nothing until
 * it is.
 *
 * set_background returns false if `environment` isn't an environment. */
WGF_API bool wgf_stage3d_set_background(wgf_actor_t stage, wgf_environment_t environment, float blur);
WGF_API wgf_environment_t wgf_stage3d_get_background(wgf_actor_t stage);
WGF_API float wgf_stage3d_get_background_blur(wgf_actor_t stage);

/* Sets or gets whether the stage skips models the camera can't see (default: on).
 *
 * Before a model is drawn, the box around it, as it is placed now, is tested against the
 * camera's view. The box is grown a little against rounding. The test is far cheaper than
 * drawing the model.
 *
 * Turn it off to draw every model, for example when checking whether a model's bounds are
 * right. */
WGF_API bool wgf_stage3d_set_culling(wgf_actor_t stage, bool culling);
WGF_API bool wgf_stage3d_is_culling(wgf_actor_t stage);

#ifdef __cplusplus
}
#endif

#endif
