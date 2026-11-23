#ifndef WGF_SPRITE_H
#define WGF_SPRITE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_play_state.h"
#include "wgf_sheet.h"
#include "wgf_actor.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sprite: an actor that shows a texture, or a region of one, on a 2D or 3D stage.
 *
 * Sprites are actors (wgf_actor.h): place, turn, scale, parent, and destroy them with the
 * actor calls. A sprite holds a reference to its texture. It drops that reference when it is
 * given another texture or is destroyed.
 *
 * - While its texture is PENDING, a sprite draws nothing.
 * - Once its texture has FAILED, it draws the placeholder checker.
 *
 * On a 2D stage, a sprite is drawn in the stage's units through immediate mode.
 *
 * On a 3D stage, a sprite is a quad in the stage's units. Its size defaults to 1 by 1 (a size
 * of 0, the default). It faces as its facing says, and is scaled by its actor. It is culled
 * by the box its quad can reach. Sprites are drawn instanced:
 * - OPAQUE and MASK sprites are drawn with the opaque models. Sprites that are alike go up
 *   as one draw.
 * - BLEND sprites are drawn among the see-through models, back to front.
 * - ADD sprites are drawn after those.
 *
 * A sprite on a 3D stage casts no shadow, and is unlit. */

/* Creates a sprite showing `texture`. With 0 for no texture, it draws nothing.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_sprite_create(wgf_texture_t texture);

/* Sets or gets the sprite's texture (0 is none). The setter returns false for a handle that
 * isn't a sprite, or a `texture` that isn't a texture. */
WGF_API bool wgf_sprite_set_texture(wgf_actor_t sprite, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_sprite_get_texture(wgf_actor_t sprite);

/* Sets or gets the region of the texture the sprite shows, in texture pixels from the
 * top-left. A width or height of 0 or less shows the whole texture (default). The getter
 * returns x, y, width, and height. The setter returns false for a handle that isn't a
 * sprite. */
WGF_API bool wgf_sprite_set_source(wgf_actor_t sprite, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_sprite_get_source(wgf_actor_t sprite);

/* Sets or gets the sprite's size, in its own units.
 *
 * A width or height of 0 or less (the default) means no size is set. The sprite then draws
 * at its region's size in pixels on a 2D stage, and at 1 by 1 on a 3D stage.
 *
 * get_size returns the size set, or else the region's size in pixels, on either stage. It
 * returns 0, 0 while the texture isn't READY and no size is set. The setter returns false
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_size(wgf_actor_t sprite, float width, float height);
WGF_API wgf_vec2_t wgf_sprite_get_size(wgf_actor_t sprite);

/* Sets or gets the sprite's pivot: the point placed at its actor's position, which the
 * sprite turns and scales about. It is a fraction of the sprite's size.
 *
 * - 0.5, 0.5 is its center (default).
 * - 0, 0 is its top-left.
 *
 * The setter returns false for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_pivot(wgf_actor_t sprite, float x, float y);
WGF_API wgf_vec2_t wgf_sprite_get_pivot(wgf_actor_t sprite);

/* Sets or gets the sprite's nine-slice borders, in texels in from the region's edges. A
 * nine-slice draws a panel or a health bar's frame at any size from one small image.
 *
 * - The corners, inside `left`, `top`, `right`, and `bottom`, keep their size, one texel a
 *   unit. If the sprite is smaller than its borders, they are scaled down together.
 * - The edges stretch along their length.
 * - The middle stretches both ways.
 *
 * All 0 means no nine-slice (default), and a border below 0 counts as 0. A nine-slice draws
 * only on a 2D stage. On a 3D stage the sprite draws whole. The getter returns left, top,
 * right, and bottom. The setter returns false for a handle that isn't a sprite, or for
 * numbers that aren't finite. */
WGF_API bool wgf_sprite_set_nine_slice(wgf_actor_t sprite, float left, float top, float right, float bottom);
WGF_API wgf_vec4_t wgf_sprite_get_nine_slice(wgf_actor_t sprite);

/* Sets or gets the color the sprite's texels are multiplied by (default: white, which leaves
 * them unchanged). The setter returns false for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_tint(wgf_actor_t sprite, wgf_color_t tint);
WGF_API wgf_color_t wgf_sprite_get_tint(wgf_actor_t sprite);

/* Sets or gets how the sprite uses its texels' alpha, one of wgf_material.h's modes (default:
 * BLEND), and the cutoff MASK cuts out below (default: 0.5, clamped to 0..1).
 *
 * - OPAQUE draws without blending.
 * - ADD adds its color.
 * - MASK draws blended on a 2D stage, since immediate mode cuts nothing out.
 *
 * The setter returns false for a handle that isn't a sprite, or a `mode` that isn't an alpha
 * mode. */
WGF_API bool wgf_sprite_set_alpha_mode(wgf_actor_t sprite, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_sprite_get_alpha_mode(wgf_actor_t sprite);
WGF_API float wgf_sprite_get_alpha_cutoff(wgf_actor_t sprite);

/* Sets or gets the material (wgf_material.h) a sprite is drawn with on a 3D stage, as a
 * model's part is. 0 (the default) goes back to the texture times the tint.
 *
 * - A PBR material is lit by the stage's lights (the up to 8 reaching it most) and its
 *   environment, and shadows fall on it.
 * - An UNLIT material draws its base color.
 * - A custom material (wgf_material_create_custom) draws by its shader, with the same lights.
 *
 * How the sprite and the material combine:
 * - The sprite's texture is the material's base color texture. The material's own base color
 *   texture isn't used, but a custom shader samples the textures its material was given.
 * - The sprite's tint is the placement's tint.
 * - Every map of the material is read over the sprite's region of the texture. So a normal
 *   map laid out like the sprite's sheet lights each frame of it.
 * - The quad is lit from the side it is seen from, so normal maps work on billboards.
 * - The sprite's own alpha mode still says how it draws. ADD draws as BLEND, and MASK uses
 *   the sprite's cutoff.
 * - The sprite's facing, size, pivot, and depth offset still say where it draws.
 *
 * A sprite with a material casts no shadow, and is drawn whole (no nine-slice). Sprites with
 * the same texture, material, and lights go up as one draw. A 2D stage has no lights, so
 * there the material isn't used and the sprite draws as it would without one.
 *
 * The sprite holds a reference to the material. The setter returns false for a handle that
 * isn't a sprite, or a `material` that isn't a material. */
WGF_API bool wgf_sprite_set_material(wgf_actor_t sprite, wgf_material_t material);
WGF_API wgf_material_t wgf_sprite_get_material(wgf_actor_t sprite);

/* Sets or gets the alpha below which a pick passes through the sprite's texels, so a round
 * button or a character is hit only where it shows. This applies to picks on a 2D stage
 * (wgf_stage2d_pick, and the pointer, wgf_pointer.h).
 *
 * `threshold` is 0..1. At 0 (the default), the whole quad is hit.
 *
 * The texture's texels are read from its file on the CPU the first time a pick tests the
 * sprite, and kept while the texture is. The whole quad is hit until they arrive, and always
 * for a texture made in code, a nine-slice, or the placeholder.
 *
 * The setter returns false for a handle that isn't a sprite, or a `threshold` outside 0..1. */
WGF_API bool wgf_sprite_set_pick_alpha(wgf_actor_t sprite, float threshold);
WGF_API float wgf_sprite_get_pick_alpha(wgf_actor_t sprite);

/* How a sprite on a 3D stage faces. On a 2D stage, a sprite lies in the stage, turned by its
 * actor. */
typedef enum wgf_sprite_facing_t {
    WGF_SPRITE_FACING_CAMERA = 0,         /* faces the camera, parallel to its view: a coin, a glow */
    WGF_SPRITE_FACING_CAMERA_FIXED_Y = 1, /* faces the camera, turning about Y and staying upright: a tree */
    WGF_SPRITE_FACING_Y_UP = 2,           /* lies flat on the ground, facing up: a decal, a shadow */
    WGF_SPRITE_FACING_FREE = 3            /* lies in its actor's x-y plane, turned as the actor is: a sign */
} wgf_sprite_facing_t;
/* Sets or gets how the sprite faces on a 3D stage (default: CAMERA). The setter returns false
 * for a handle that isn't a sprite, or a `facing` that isn't a facing. */
WGF_API bool wgf_sprite_set_facing(wgf_actor_t sprite, wgf_sprite_facing_t facing);
WGF_API wgf_sprite_facing_t wgf_sprite_get_facing(wgf_actor_t sprite);

/* Sets or gets how far the sprite is drawn nearer the camera on a 3D stage, along its line of
 * sight, in stage units (default: 0, where it is).
 *
 * The sprite stays in the same place on the screen, but wins the depth test over whatever
 * shares its place: a bar's fill over its back, or a label over its sprite. A negative
 * `offset` draws it farther away.
 *
 * For example, a health bar over a head, facing the camera, is two tinted sprites of one
 * white texture (wgf_texture_create_from_image of wgf_image_create_color):
 * - its back;
 * - its fill, pivoted at its left edge (wgf_sprite_set_pivot(fill, 0, 0.5)) and placed at the
 *   back's left, its width the share left, with an offset of 0.01.
 *
 * The setter returns false for a handle that isn't a sprite, or an `offset` that isn't
 * finite. */
WGF_API bool wgf_sprite_set_depth_offset(wgf_actor_t sprite, float offset);
WGF_API float wgf_sprite_get_depth_offset(wgf_actor_t sprite);

/* Plays a sprite sheet's clips (wgf_sheet.h) on a sprite.
 *
 * - set_sheet sets the sheet, and makes the sheet's texture the sprite's.
 * - play_clip plays the clip named `name` from its start, looping or once. If that clip is
 *   already playing, it goes on.
 * - The clip steps each frame by the frame's time times its speed. The speed defaults to 1,
 *   and 0 holds the clip still.
 *
 * The clip's state is one of these:
 * - PLAYING.
 * - PAUSED.
 * - COMPLETE, once a clip played once ends. It holds its last frame.
 * - STOPPED.
 *
 * The calls return false for a handle that isn't a sprite, a `sheet` that isn't a sheet, or
 * a clip the sheet doesn't have. */
WGF_API bool wgf_sprite_set_sheet(wgf_actor_t sprite, wgf_sheet_t sheet);
WGF_API wgf_sheet_t wgf_sprite_get_sheet(wgf_actor_t sprite);
WGF_API bool wgf_sprite_play_clip(wgf_actor_t sprite, const char *name, bool loop);
WGF_API bool wgf_sprite_pause_clip(wgf_actor_t sprite);
WGF_API bool wgf_sprite_resume_clip(wgf_actor_t sprite);
WGF_API bool wgf_sprite_stop_clip(wgf_actor_t sprite);
WGF_API wgf_play_state_t wgf_sprite_get_clip_state(wgf_actor_t sprite);
/* Returns the name of the clip playing ("" for none), and which of its frames shows (0 is its
 * first, -1 for none). */
WGF_API const char *wgf_sprite_get_clip(wgf_actor_t sprite);
WGF_API int wgf_sprite_get_clip_frame(wgf_actor_t sprite);
WGF_API bool wgf_sprite_set_clip_speed(wgf_actor_t sprite, float speed);
WGF_API float wgf_sprite_get_clip_speed(wgf_actor_t sprite);

/* Takes the events the sprite's clip raised as its frames came (wgf_sheet_set_clip_event).
 *
 * The events are written into `out`, oldest first, as many as fit in `count`. Taken events
 * are gone, so a second call doesn't return them again. At most the last 16 are kept.
 *
 * Returns how many events it wrote, or 0 for a sprite with no sheet. */
WGF_API int wgf_sprite_take_clip_events(wgf_actor_t sprite, int *out, int count);

/* Sets or gets a facing for the sprite's clips: a direction a character faces, such as "up"
 * or "left".
 *
 * A clip played, or playing, by name plays the sheet's clip named name_facing if it has one,
 * or else the clip named name. For example, "walk" with the facing "left" plays "walk_left".
 * If the facing changes while a clip plays, the clip for the new facing goes on from the same
 * place and time.
 *
 * "" or NULL means no facing. The setter returns false for a sprite with no sheet, or a
 * `facing` over 15 bytes. */
WGF_API bool wgf_sprite_set_clip_facing(wgf_actor_t sprite, const char *facing);
WGF_API const char *wgf_sprite_get_clip_facing(wgf_actor_t sprite);

#ifdef __cplusplus
}
#endif

#endif
