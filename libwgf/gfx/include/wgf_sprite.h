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

/* A sprite: an actor showing a texture, or a region of one, on a 2D stage or a 3D one. It
 * holds a reference to its texture, dropped when it is given another or destroyed. Sprites
 * are actors (wgf_actor.h): place, turn, scale, parent, and destroy them with the actor
 * calls. While its texture is PENDING it draws nothing; once that FAILED, the checker.
 *
 * On a 2D stage it is drawn in the stage's units through immediate mode. On a 3D stage it
 * is a quad in the stage's units (its size; 0, the default, is 1 by 1), facing as its
 * facing says, scaled by its actor, culled by the box its quad can reach, drawn as
 * libwgt's instanced sprites: OPAQUE and MASK ones with the opaque models, alike ones one
 * draw; BLEND ones among the see-through models, back to front; ADD ones after them. It
 * casts no shadow, and is unlit. */

/* A sprite of `texture` (0 for none: it draws nothing). 0 when there is no room for
 * another actor. */
WGF_API wgf_actor_t wgf_sprite_create(wgf_texture_t texture);

/* False for a handle that isn't a sprite, or a texture that isn't one (0 is none). */
WGF_API bool wgf_sprite_set_texture(wgf_actor_t sprite, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_sprite_get_texture(wgf_actor_t sprite);

/* The region of the texture shown, in its pixels from the top-left; a width or height
 * of 0 or less (the default) shows the whole texture. False for a handle that isn't a
 * sprite. Read back as x, y, width, height. */
WGF_API bool wgf_sprite_set_source(wgf_actor_t sprite, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_sprite_get_source(wgf_actor_t sprite);

/* Its size in its own units; a width or height of 0 or less (the default) is the
 * region's size in pixels on a 2D stage (1 by 1 on a 3D one). get_size is the size it draws at: what was set, or else
 * the region's (0, 0 while its texture isn't READY and no size is set). False for a
 * handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_size(wgf_actor_t sprite, float width, float height);
WGF_API wgf_vec2_t wgf_sprite_get_size(wgf_actor_t sprite);

/* The point of the sprite at its actor's position, which it turns and scales about, as
 * a fraction of its size: 0.5, 0.5 its center (the default), 0, 0 its top-left. False
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_pivot(wgf_actor_t sprite, float x, float y);
WGF_API wgf_vec2_t wgf_sprite_get_pivot(wgf_actor_t sprite);

/* A nine-slice: its region drawn with borders `left`, `top`, `right`, and `bottom` texels in
 * from its edges kept at their size (one texel a unit; scaled down together when its size is
 * smaller than its borders), the edges stretched along, the middle both ways: a panel or a
 * health bar's frame of any size from one small image. All 0 (the default, and anything below
 * 0) for none. On a 2D stage; on a 3D one it draws whole. False for a handle that isn't a
 * sprite, or numbers that aren't finite. Read back as left, top, right, bottom. */
WGF_API bool wgf_sprite_set_nine_slice(wgf_actor_t sprite, float left, float top, float right, float bottom);
WGF_API wgf_vec4_t wgf_sprite_get_nine_slice(wgf_actor_t sprite);

/* What its texels are multiplied by; white (the default) leaves them as they are. False
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_tint(wgf_actor_t sprite, wgf_color_t tint);
WGF_API wgf_color_t wgf_sprite_get_tint(wgf_actor_t sprite);

/* How it uses its texels' alpha (wgf_material.h's modes; default BLEND), and the cutoff
 * MASK cuts out below (0..1, clamped; default 0.5): OPAQUE draws no blending, ADD adds. On
 * a 2D stage MASK draws blended (immediate mode cuts out nothing). False for a handle that
 * isn't a sprite, or a mode that isn't one. */
WGF_API bool wgf_sprite_set_alpha_mode(wgf_actor_t sprite, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_sprite_get_alpha_mode(wgf_actor_t sprite);
WGF_API float wgf_sprite_get_alpha_cutoff(wgf_actor_t sprite);

/* Drawn with a material (wgf_material.h) on a 3D stage, as a model's part is: a PBR one lit
 * by the stage's lights (the up to 8 reaching it most) and its environment, shadows falling
 * on it; an UNLIT one its base color; a custom one (wgf_material_create_custom) by its
 * shader, with the same lights. The sprite's texture is the material's base color texture
 * (the material's own isn't used; a custom shader samples what its material was given), its
 * tint the placement's, and its region of the texture what every map of the material is
 * read over, so a normal map laid out as the sprite's sheet lights each frame of it. The
 * quad is lit from the side it is seen from, so normal maps work on billboards. The
 * sprite's own alpha mode still says how it draws (ADD as BLEND, and a MASK's cutoff its
 * own), and its facing, size, pivot, and depth offset where. It casts no shadow, and is
 * drawn whole (no nine-slice). Sprites alike in texture, material, and lights go up as one
 * draw. On a 2D stage, which has no lights, the material isn't used: the sprite draws as
 * without one. The sprite holds a reference to the material; 0 (the default) goes back to
 * its texture times its tint. False for a handle that isn't a sprite, or one that isn't a
 * material. */
WGF_API bool wgf_sprite_set_material(wgf_actor_t sprite, wgf_material_t material);
WGF_API wgf_material_t wgf_sprite_get_material(wgf_actor_t sprite);

/* A pick on a 2D stage (wgf_stage2d_pick, and the pointer, wgf_pointer.h) passes through
 * its texels whose alpha is below `threshold` (0..1; 0, the default, hits its whole quad),
 * so a round button or a character is hit where it shows. Its texture's texels are read
 * from its file on the CPU the first time a pick tests it, and kept while the texture is;
 * until they arrive, and for a texture made in code, a nine-slice, or the placeholder, its
 * quad is hit. False for a handle that isn't a sprite, or a threshold outside 0..1. */
WGF_API bool wgf_sprite_set_pick_alpha(wgf_actor_t sprite, float threshold);
WGF_API float wgf_sprite_get_pick_alpha(wgf_actor_t sprite);

/* How a sprite on a 3D stage faces, libwgt's and wgrender-c's (on a 2D stage it lies in
 * the stage, turned by its actor): */
typedef enum wgf_sprite_facing_t {
    WGF_SPRITE_FACING_CAMERA = 0,         /* the camera, parallel to its view: a coin, a glow */
    WGF_SPRITE_FACING_CAMERA_FIXED_Y = 1, /* the camera, turning about Y and staying upright: a tree */
    WGF_SPRITE_FACING_Y_UP = 2,           /* flat on the ground, facing up: a decal, a shadow */
    WGF_SPRITE_FACING_FREE = 3            /* as its actor is turned, in its x-y plane: a sign */
} wgf_sprite_facing_t;
/* Default CAMERA. False for a handle that isn't a sprite, or a facing that isn't one. */
WGF_API bool wgf_sprite_set_facing(wgf_actor_t sprite, wgf_sprite_facing_t facing);
WGF_API wgf_sprite_facing_t wgf_sprite_get_facing(wgf_actor_t sprite);

/* On a 3D stage, drawn `offset` stage units nearer the camera along its line of sight (0, the
 * default: where it is), so it stays where it is on the screen and wins the depth test over
 * what shares its place: a bar's fill over its back, a label over its sprite (a negative one
 * drawn farther). A bar over a head, facing the camera, is two sprites of one white texture
 * (wgf_texture_create_from_image of wgf_image_create_color) tinted: its back, and its fill
 * pivoted at its left edge (wgf_sprite_set_pivot(fill, 0, 0.5), placed at the back's left),
 * its width the share left, offset 0.01. False for a handle that isn't a sprite, or an offset
 * that isn't finite. */
WGF_API bool wgf_sprite_set_depth_offset(wgf_actor_t sprite, float offset);
WGF_API float wgf_sprite_get_depth_offset(wgf_actor_t sprite);

/* A sprite showing a sprite sheet's frames (wgf_sheet.h): its texture the sheet's (set_sheet), and the clip
 * named `name` played from its start, looping or once (a clip already playing goes on);
 * stepped each frame by the frame's time times its speed (1 by default; 0 holds it). Its
 * state: PLAYING, PAUSED, COMPLETE once a clip played once ends (holding its last frame),
 * STOPPED. False for a sprite that isn't one, a sheet that isn't one, or a clip it doesn't
 * have. */
WGF_API bool wgf_sprite_set_sheet(wgf_actor_t sprite, wgf_sheet_t sheet);
WGF_API wgf_sheet_t wgf_sprite_get_sheet(wgf_actor_t sprite);
WGF_API bool wgf_sprite_play_clip(wgf_actor_t sprite, const char *name, bool loop);
WGF_API bool wgf_sprite_pause_clip(wgf_actor_t sprite);
WGF_API bool wgf_sprite_resume_clip(wgf_actor_t sprite);
WGF_API bool wgf_sprite_stop_clip(wgf_actor_t sprite);
WGF_API wgf_play_state_t wgf_sprite_get_clip_state(wgf_actor_t sprite);
/* The clip playing's name ("" for none), and which of its frames shows (0 its first; -1 for
 * none). */
WGF_API const char *wgf_sprite_get_clip(wgf_actor_t sprite);
WGF_API int wgf_sprite_get_clip_frame(wgf_actor_t sprite);
WGF_API bool wgf_sprite_set_clip_speed(wgf_actor_t sprite, float speed);
WGF_API float wgf_sprite_get_clip_speed(wgf_actor_t sprite);

/* The events its clip raised as its frames came (wgf_sheet_set_clip_event), oldest first,
 * into `out` (as many as fit in `count`), returning how many; taken, so read again they are
 * gone. At most the last 16 are kept. 0 for a sprite with no sheet. */
WGF_API int wgf_sprite_take_clip_events(wgf_actor_t sprite, int *out, int count);

/* A facing for its clips (a direction a character faces: "up", "left"): a clip played, or
 * playing, by name plays the sheet's clip named name_facing when it has one ("walk" facing
 * "left": "walk_left"), else name. Changed while one plays, the clip for the new facing goes
 * on from the same place and time. "" or NULL for none. False for a sprite with no sheet, or a
 * facing over 15 bytes. */
WGF_API bool wgf_sprite_set_clip_facing(wgf_actor_t sprite, const char *facing);
WGF_API const char *wgf_sprite_get_clip_facing(wgf_actor_t sprite);

#ifdef __cplusplus
}
#endif

#endif
