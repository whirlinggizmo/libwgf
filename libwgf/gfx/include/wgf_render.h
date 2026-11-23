#ifndef WGF_RENDER_H
#define WGF_RENDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame gfx draws into: the window.
 *
 * A frame is recorded while the program's frame callback runs, and drawn when it returns. */

/* Sets or gets the color each frame starts cleared to (default: black). */
WGF_API void wgf_render_set_clear_color(wgf_color_t color);
WGF_API wgf_color_t wgf_render_get_clear_color(void);


/* Returns the frame's size in pixels, and how many pixels make one logical pixel.
 *
 * - The DPI scale is 2 on most high-density displays.
 * - Drawing is in logical pixels: the frame is get_width() / get_dpi_scale() of them wide.
 * - While there is no surface, these return 0, 0, and 1.
 * - Inside a render target (wgf_render_begin_target), they return the target's size, and 1.
 *   Drawing there is in the target's own pixels, with no presentation (wgf_presentation.h).
 *   A stage's camera sees the target at its aspect ratio. */
WGF_API int wgf_render_get_width(void);
WGF_API int wgf_render_get_height(void);
WGF_API float wgf_render_get_dpi_scale(void);

/* Returns frame stats: the last frame's draw calls and triangles, and the bytes the GPU holds.
 *
 * - Draw calls and triangles count every pass: the stages', the shadows', the render
 *   targets', and immediate mode's.
 * - GPU bytes count the buffers and images alive now, at their sizes as made, with each mip
 *   level and slice. A GPU's own padding isn't counted.
 * - The stats are counted as the graphics backend is called, so they cover every draw.
 *
 * Each returns 0 before the first frame. */
WGF_API int wgf_render_get_draw_calls(void);
WGF_API double wgf_render_get_triangle_count(void);
WGF_API double wgf_render_get_gpu_bytes(void);

/* Returns whether the GPU context is lost, and whether it was restored this frame.
 *
 * On the web, the browser drops WebGL when a phone backgrounds the page or the GPU resets,
 * and gives it back later. Natively the context is never lost, and both calls always return
 * false.
 *
 * While the context is lost (is_context_lost), nothing is drawn and nothing is sent to the
 * GPU. Frames, ticks, input, and sound go on.
 * - A draw, a stage's draw, a document's draw, or a target begun does nothing, silently.
 * - A UI is laid out and its buttons answer, but nothing is drawn.
 * - A texture, mesh, shader, font, or other resource that is loading finishes once the
 *   context is back.
 * - A target or blank texture made meanwhile gets its GPU side then.
 *
 * When the context comes back, everything is made again under the same handles, before the
 * next frame is drawn:
 * - shaders and pipelines;
 * - a texture, cubemap, environment, bitmap font's pages, or glTF file's images, read again
 *   from its file as this run has it, with nothing fetched. Each is blank until it is in,
 *   which takes a few frames;
 * - meshes, from their vertices;
 * - a blank texture, with its last write (wgf_texture.h);
 * - fonts' glyphs;
 * - RmlUi's documents' textures.
 *
 * Render targets come back blank. is_context_restored is true once, in the frame the
 * context is back in. A game that draws a target only when its content changes should draw
 * it again in that frame. */
WGF_API bool wgf_render_is_context_lost(void);
WGF_API bool wgf_render_is_context_restored(void);

/* Begins drawing into a render target (wgf_texture_create_target) instead of the screen,
 * until wgf_render_end_target.
 *
 * - Call it inside a frame. Everything then draws into the target: stages, sprites,
 *   particles, text, the UI, and immediate mode.
 * - Targets are drawn before the screen, in the order begun, so the screen shows what they
 *   drew this frame.
 * - A target begun again in the same frame keeps what it drew before.
 * - Targets don't nest.
 * - A target isn't read while it is drawn into. Used inside its own pass, it draws nothing
 *   there (warned once).
 * - A frame that ends inside a target ends the target (warned).
 *
 * Returns false (logged) outside a frame, inside another target, for a texture that isn't
 * a target, or past 256 target passes in a frame. */
WGF_API bool wgf_render_begin_target(wgf_texture_t target);
WGF_API void wgf_render_end_target(void);

/* Reads the screen back as this frame ends, after its last pass and before it is shown.
 *
 * - Outside a frame, it reads the next frame.
 * - The read (wgf_texture.h) is DONE then, holding the window's framebuffer at its pixels'
 *   size (wgf_render_get_width times the DPI scale), with the top row first.
 * - In a headless build, the read FAILED.
 * - With wgf_texture_read_save, this takes a screenshot.
 *
 * Returns 0 if there is no room for another read (logged). */
WGF_API wgf_texture_read_t wgf_render_read_screen(void);

/* Sets or gets the color a pass into `target` starts cleared to (default: transparent black).
 *
 * The color is kept with the target until changed. For a texture that isn't a render
 * target, the setter returns false, and the getter returns transparent black. */
WGF_API bool wgf_render_set_target_clear_color(wgf_texture_t target, wgf_color_t color);
WGF_API wgf_color_t wgf_render_get_target_clear_color(wgf_texture_t target);

/* Adds, clears, or counts screen effects: post-processing of the finished frame.
 *
 * The finished frame (stages, sprites, text, and the UI) is redrawn by each effect in the
 * order added. The last effect draws onto the screen.
 *
 * - An effect is a custom material (wgf_material_create_custom) whose shader is a screen
 *   effect (wgf.glsl's wgf_screen, wgf_shader.h).
 * - Its parameters are the material's, and can change any frame.
 * - An effect whose shader is still loading is skipped until it is READY. One whose shader
 *   FAILED is skipped.
 * - The chain is taken as each frame begins. Adding or clearing inside a frame shows from
 *   the next frame.
 * - The effects hold their own references to their materials, and let go of them when
 *   cleared.
 * - There can be up to 8 effects.
 *
 * add returns false (logged) past 8 effects, for anything but a custom material, for a
 * surface shader's material, or before gfx runs (it works from init on). */
WGF_API bool wgf_render_add_effect(wgf_material_t material);
WGF_API void wgf_render_clear_effects(void);
WGF_API int wgf_render_get_effect_count(void);

/* Clips drawing to a rectangle until the matching pop.
 *
 * - The rectangle is in logical pixels from the top-left. Inside a render target, it is in
 *   the target's own pixels.
 * - Everything drawn between push and pop is clipped: immediate mode, and the 2D stages
 *   drawn there.
 * - Clips nest. Each push intersects with the clip it's pushed inside, so a scroll area
 *   inside a panel stays inside the panel.
 * - A width or height of 0 clips everything away.
 * - A render target's pass starts unclipped. The screen's clips apply again when it ends.
 * - Clips go up to 32 deep. A push past that clips nothing more (warned once).
 *
 * Pop every push within the frame, and a target's within its pass. Unmatched pushes are
 * dropped at the end, with a warning, as is a pop without a push. Outside a frame, both
 * calls do nothing (warned). */
WGF_API void wgf_render_push_clip(float x, float y, float width, float height);
WGF_API void wgf_render_pop_clip(void);

#ifdef __cplusplus
}
#endif

#endif
