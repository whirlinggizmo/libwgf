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
 * the frames go by. A sheet is a resource (wgf_resource.h): a grid made from a texture, READY
 * at once, or a packer's atlas read from its file. It holds a reference to its texture; a
 * sprite playing holds none to the sheet, so releasing a sheet's last reference stops what
 * plays it.
 *
 * An atlas is TexturePacker's JSON (a hash or an array of frames) or Aseprite's, its image
 * the file its meta.image names beside it: each frame's region and name, trimmed frames
 * drawn in place in their whole picture (the sprite's size and pivot the whole picture's),
 * each frame's duration (Aseprite's milliseconds; 0.1 s for one without), its clips its
 * tags (Aseprite's frameTags: forward, reverse, ping-pong), and a clip for each run of
 * frames named alike but for a number ("run_01.png", "run_02.png": "run"), in their
 * numbers' order. A frame packed turned (TexturePacker's rotation) isn't read: pack without
 * rotation (logged). */

/* A sheet over `texture` cut into cells of frame_width by frame_height texels, left to right
 * then down, `margin` texels in from its edges and `spacing` between cells: `count` frames
 * (0: every cell that fits), cut once the texture is READY. 0 for a texture that isn't one, a
 * frame size not above 0, or a count, margin, or spacing below 0. */
WGF_API wgf_sheet_t wgf_sheet_create_grid(wgf_texture_t texture, int frame_width, int frame_height, int count,
                                          int margin, int spacing);
/* The atlas at `path`: loaded as a resource is (PENDING at once, READY or FAILED in a later
 * update), its image too. 0 only when there is no room for another. */
WGF_API wgf_sheet_t wgf_sheet_create(const char *path);
/* One reference let go of (wgf_resource_release): a grid's only one frees it. */
WGF_API void wgf_sheet_destroy(wgf_sheet_t sheet);
WGF_API wgf_texture_t wgf_sheet_get_texture(wgf_sheet_t sheet);
/* Its frames, and one's region of the texture (x, y, width, height in texels); none while its
 * texture isn't READY. 0 for a handle that isn't a sheet, or an index outside it. */
WGF_API int wgf_sheet_get_frame_count(wgf_sheet_t sheet);
WGF_API wgf_vec4_t wgf_sheet_get_frame(wgf_sheet_t sheet, int index);
/* A frame's name ("" for a grid's, or an index outside it), and the frame named `name` (-1
 * for none). */
WGF_API const char *wgf_sheet_get_frame_name(wgf_sheet_t sheet, int index);
WGF_API int wgf_sheet_find_frame(wgf_sheet_t sheet, const char *name);

/* How a clip runs through its frames. */
typedef enum wgf_sheet_direction_t {
    WGF_SHEET_DIRECTION_FORWARD = 0,
    WGF_SHEET_DIRECTION_BACKWARD = 1,
    WGF_SHEET_DIRECTION_PING_PONG = 2, /* forward, then back, the ends shown once */
    WGF_SHEET_DIRECTION_PING_PONG_BACKWARD = 3 /* back, then forward (Aseprite's pingpong_reverse) */
} wgf_sheet_direction_t;

/* A clip named `name` over the sheet's frames by index (`count` of them), each shown for
 * `seconds` (0: each frame's own duration, an atlas's), run in `direction`, in place of one of
 * that name. At most 64 clips a sheet; false for a name empty or over 63 bytes, no frames,
 * seconds below 0, or a direction that isn't one. Its clips counted, one's name ("" for none),
 * and the one named `name` (-1 for none). */
WGF_API bool wgf_sheet_add_clip(wgf_sheet_t sheet, const char *name, const int *frames, int count, float seconds,
                                wgf_sheet_direction_t direction);
WGF_API int wgf_sheet_get_clip_count(wgf_sheet_t sheet);
WGF_API const char *wgf_sheet_get_clip_name(wgf_sheet_t sheet, int index);
WGF_API int wgf_sheet_find_clip(wgf_sheet_t sheet, const char *name);

/* An event on a clip's frame: `event` (above 0; 0 takes one off) raised each time the clip
 * comes to its `place`th frame (0 its first, by place in the clip as added), read from the
 * sprite playing it (wgf_sprite_take_clip_events): a footstep's sound, an attack's hit
 * frame. False for a clip it hasn't, a place outside it, or an event below 0. */
WGF_API bool wgf_sheet_set_clip_event(wgf_sheet_t sheet, const char *clip, int place, int event);
WGF_API int wgf_sheet_get_clip_event(wgf_sheet_t sheet, const char *clip, int place);

#ifdef __cplusplus
}
#endif

#endif /* WGF_SHEET_H */
