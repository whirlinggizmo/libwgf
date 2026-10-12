#ifndef WGF_GFX_RENDER_PRIV_H
#define WGF_GFX_RENDER_PRIV_H

#include <stdbool.h>

#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "wgf_handle.h"
#include "wgf_mat4.h"

/* The frame, for gfx's own drawing code and app's runtime. */

/* sokol's pools, as wgrender and libwgt size them (sokol's defaults are 128 buffers and
 * images, 256 views): room for every texture a 2D game loads. */
#define WGF_GFX_PRIV_BUFFER_POOL_SIZE 4096
#define WGF_GFX_PRIV_IMAGE_POOL_SIZE 2048
#define WGF_GFX_PRIV_VIEW_POOL_SIZE 4096
#define WGF_GFX_PRIV_PIPELINE_POOL_SIZE 256

/* gfx's start and stop, run by app's runtime once the window exists: sokol_gfx on the
 * window's device (wgf_platform_priv_get_environment). Stopping frees everything gfx
 * holds: its actors, textures, and fonts. False when sokol_gfx couldn't start. */
bool wgf_gfx_priv_start(void);
void wgf_gfx_priv_stop(void);

/* A frame, at the window's size and DPI scale: recorded between begin and end, and
 * drawn at end into the window's swapchain. in_frame: between the two. */
void wgf_gfx_priv_begin_frame(void);
void wgf_gfx_priv_end_frame(void);
bool wgf_gfx_priv_is_in_frame(void);

/* Between wgf_gfx_priv_start and _stop: the GPU can be used. Drawing is recorded only
 * inside a frame, so a draw outside one can't land in the next. */
bool wgf_gfx_priv_render_is_running(void);

/* The frame's immediate mode recording (sokol_gl), for gfx's drawing code: made
 * current, with the 2D projection -- logical pixels from the top-left, y down,
 * blended, no depth -- and an identity model-view. Call before recording, since
 * other code (fontstash) may have changed sokol_gl's state. */
void wgf_gfx_priv_render_set_2d(void);
/* That 2D projection, as a matrix (OpenGL's clip space): what a sokol_gfx draw among the
 * 2D (particles) is seen through. */
wgf_mat4_t wgf_gfx_priv_render_projection_2d(void);
/* The 2D pipeline for an alpha mode (wgf_alpha_mode_t): blended (BLEND and MASK, which
 * immediate mode can't cut out), added, or opaque. */
sgl_pipeline wgf_gfx_priv_render_pipeline_2d(int mode);

/* Immediate mode in 3D (wgf_draw_begin_3d): through `view_proj` (column by column,
 * OpenGL's clip space), into the visible area, depth tested and written; and back to 2D,
 * the whole framebuffer again. */
void wgf_gfx_priv_render_set_3d(const float view_proj[16]);
void wgf_gfx_priv_render_end_3d(void);

/* The frame's commands (wgf_gfx_render_commands.c): `replay(index)` draws with
 * sokol_gfx at this point among the frame's immediate mode, when the frame is drawn,
 * under the clip it was made under, in the open pass. False outside a frame, or out of
 * memory. */
bool wgf_gfx_priv_render_add_command(void (*replay)(int index), int index);
/* A target's pass, its layers and commands, into the open sokol pass (the targets'
 * drawing; the screen's is the frame's own). */
void wgf_gfx_priv_render_draw_pass(int pass);

/* A command's own immediate mode: from begin to end, immediate mode records into a
 * layer of its own through `view_proj` (depth tested, 3D), which the command draws when
 * it is replayed (draw_side_layer), and the frame's state is as it was after it. */
int wgf_gfx_priv_render_begin_side_layer(const float view_proj[16]);
void wgf_gfx_priv_render_end_side_layer(void);
void wgf_gfx_priv_render_draw_side_layer(int layer);

/* The 3D pipeline and `view_proj` loaded into immediate mode, an identity model-view;
 * nothing else changed (set_3d's viewport is its own). */
void wgf_gfx_priv_render_load_3d(const float view_proj[16]);

/* For the commands: the immediate mode recording's context; the frame's drawing of a
 * pass's layers and commands (NULL: its one layer); the clip now, and immediate mode's
 * viewport now (the visible area's in 3D, the pass's otherwise), each x, y, width,
 * height in the pass's pixels from its top-left (mirrored in a flipped pass). */
sgl_context wgf_gfx_priv_render_get_context(void);
void wgf_gfx_priv_render_set_drawing(void (*draw)(int pass));
void wgf_gfx_priv_render_get_scissor(int rect[4]);
void wgf_gfx_priv_render_get_viewport(int rect[4]);
/* The visible area, where a 3D view is drawn, in the pass's pixels as above. */
void wgf_gfx_priv_render_get_visible_viewport(int rect[4]);

/* The frame's passes: 0 the screen, then each render target begun
 * (wgf_render_begin_target, wgf_gfx_render_target.c), drawn before the screen. The pass
 * being recorded; and the one entered: `pass` 0 the screen again, its size, presentation,
 * and clips as they were, else the target `texture`'s, `width` by `height` of its own
 * pixels (a DPI scale of 1, no presentation), unclipped. Set by the targets alone. */
int wgf_gfx_priv_render_get_pass(void);
void wgf_gfx_priv_render_enter_pass(int pass, wgf_handle_t texture, int width, int height);
/* The targets' hook, set by every target begun: at the frame's end, before anything is
 * drawn (`draw` false: a target left open is ended), and after the uploads (true: each
 * target's pass drawn, before the screen's). */
void wgf_gfx_priv_render_set_targets(void (*hook)(bool draw));
/* The screen effects' hook (wgf_gfx_render_effect.c), set by the first effect added:
 * BEGIN as a frame starts (true: its screen's pass goes through the effects and is
 * recorded flipped, as a target's is on GL), SCREEN before the screen's pass begins
 * (`pass` turned into the frame's texture), and END after it ends, before the commit
 * (the effects drawn, the last onto the screen); the last two do nothing in a frame
 * without effects. */
typedef enum wgf_gfx_priv_render_effects_t {
    WGF_GFX_PRIV_RENDER_EFFECTS_BEGIN,
    WGF_GFX_PRIV_RENDER_EFFECTS_SCREEN,
    WGF_GFX_PRIV_RENDER_EFFECTS_END
} wgf_gfx_priv_render_effects_t;
void wgf_gfx_priv_render_set_effects(bool (*hook)(wgf_gfx_priv_render_effects_t when, sg_pass *pass));
/* The render target drawn into now, while recording or drawing its pass; 0 the screen. A
 * texture can't be read in the pass that draws into it. The pass draws into `texture`
 * from now until 0 (the targets' draw). */
wgf_handle_t wgf_gfx_priv_render_get_target(void);
void wgf_gfx_priv_render_set_drawing_into(wgf_handle_t texture);

/* GL stores a render target's rows bottom-up (its origin is the bottom-left corner);
 * drawing into one, libwgf flips clip space's y, as libwgt does, so a target is stored
 * top-down and whatever samples it -- a sprite, a material, a custom shader -- need not
 * know. True while recording such a pass: the triangles' winding turns with it, and the
 * pipelines that cull are chosen for the other winding. `clip` is `m` (to clip space)
 * flipped so, when it is. */
bool wgf_gfx_priv_render_is_flipped(void);
wgf_mat4_t wgf_gfx_priv_render_clip(wgf_mat4_t m);

/* What immediate mode keeps between frames (a polyline's scratch), freed at gfx's stop. */
void wgf_gfx_priv_draw_shutdown(void);

/* From wgf_presentation_set: how the bars are filled from here on, when a mode has
 * them; and the fill itself, which only that hook reaches. */
void wgf_gfx_priv_render_set_bars(void (*fill)(void));
void wgf_gfx_priv_render_fill_visible(void);

/* What the frame shows, in logical pixels (the presentation's visible area,
 * wgf_presentation.h): where a 2D stage centers its view and the UI lays out. */
void wgf_gfx_priv_render_get_visible(float *x, float *y, float *width, float *height);

/* The clip drawing is under now (wgf_render_push_clip), in logical pixels; the whole
 * frame when none is pushed, which is when this is false. */
bool wgf_gfx_priv_render_get_clip(float *x, float *y, float *width, float *height);

/* The immediate mode recording's room, for tests: it starts at 65536 vertices and
 * 16384 commands, and a frame that runs out has the draws past it dropped, and
 * doubles the room for the frames after it. */
int wgf_gfx_priv_render_get_vertex_capacity(void);
int wgf_gfx_priv_render_get_command_capacity(void);

/* Frames numbered from 1 as they begin: the frame under way, and the last one that
 * ended (0 before any has). */
unsigned wgf_gfx_priv_render_get_frame(void);
unsigned wgf_gfx_priv_render_get_last_frame(void);

#endif
