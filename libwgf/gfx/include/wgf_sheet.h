#ifndef WGF_SHEET_H
#define WGF_SHEET_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_texture.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sprite sheet, an object: a handle of kind "gfx.sheet". */
typedef wgf_handle_t wgf_sheet_t;

/* Sprite sheets and 2D animation, helix-legacy's SpriteSheet2D and SpriteAnimator2D: a
 * texture cut into frames, and clips (runs of its frames, each shown for a while, in a
 * direction) a sprite (wgf_sprite.h) plays by name, its region set to the frame showing as
 * the frames go by. A sheet holds a reference to its texture; a sprite playing holds none to
 * the sheet, so destroying a sheet stops what plays it. */

/* A sheet over `texture` cut into cells of frame_width by frame_height texels, left to right
 * then down, `margin` texels in from its edges and `spacing` between cells: `count` frames
 * (0: every cell that fits), cut once the texture is READY. 0 for a texture that isn't one, a
 * frame size not above 0, or a count, margin, or spacing below 0. */
WGF_API wgf_sheet_t wgf_sheet_create_grid(wgf_texture_t texture, int frame_width, int frame_height, int count,
                                          int margin, int spacing);
WGF_API void wgf_sheet_destroy(wgf_sheet_t sheet);
WGF_API wgf_texture_t wgf_sheet_get_texture(wgf_sheet_t sheet);
/* Its frames, and one's region of the texture (x, y, width, height in texels); none while its
 * texture isn't READY. 0 for a handle that isn't a sheet, or an index outside it. */
WGF_API int wgf_sheet_get_frame_count(wgf_sheet_t sheet);
WGF_API wgf_vec4_t wgf_sheet_get_frame(wgf_sheet_t sheet, int index);

/* How a clip runs through its frames. */
typedef enum wgf_sheet_direction_t {
    WGF_SHEET_DIRECTION_FORWARD = 0,
    WGF_SHEET_DIRECTION_BACKWARD = 1,
    WGF_SHEET_DIRECTION_PING_PONG = 2 /* forward, then back, the ends shown once */
} wgf_sheet_direction_t;

/* A clip named `name` over the sheet's frames by index (`count` of them), each shown for
 * `seconds`, run in `direction`, in place of one of that name. At most 64 clips a sheet;
 * false for a name empty or over 31 bytes, no frames, seconds not above 0, or a direction
 * that isn't one. */
WGF_API bool wgf_sheet_add_clip(wgf_sheet_t sheet, const char *name, const int *frames, int count, float seconds,
                                wgf_sheet_direction_t direction);
WGF_API int wgf_sheet_get_clip_count(wgf_sheet_t sheet);

#ifdef __cplusplus
}
#endif

#endif /* WGF_SHEET_H */
