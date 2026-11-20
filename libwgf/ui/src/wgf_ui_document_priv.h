#ifndef WGF_UI_DOCUMENT_PRIV_H
#define WGF_UI_DOCUMENT_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "sokol_gfx.h"
#include "wgf_texture.h"

/* Between documents (wgf_ui_document.c) and their drawing (wgf_ui_document_draw.c),
 * which answers wgf_ui_rmlui_priv.h's drawing calls: triangles, the scissor, the
 * transform, clip masks, layers, filters, and shaders. */

/* A frame's drawing of the documents, from the visible area's top-left (logical pixels,
 * `width` by `height`), at the frame's pixel density: begun before RmlUi renders and
 * ended after, the frame then as it was (the screen's pass, no clip, no transform). */
void wgf_ui_priv_doc_draw_begin(float origin_x, float origin_y, float width, float height);
void wgf_ui_priv_doc_draw_end(void);
/* Each update, outside a frame: what the frames before let go of (buffers, textures)
 * freed, once nothing recorded draws it; and everything, as the part stops. */
void wgf_ui_priv_doc_draw_update(void);
void wgf_ui_priv_doc_draw_stop(void);
/* The GPU's context given back: the gradients' triangles let go of, sent again as each is
 * next drawn from RmlUi's copy. */
void wgf_ui_priv_doc_draw_restore(void);

/* A texture RmlUi holds (wgf_ui_priv_doc_texture_load and _generate, or a layer saved):
 * what to bind to draw it, and whether its colors are premultiplied (a layer's are);
 * false for none. */
bool wgf_ui_priv_doc_texture_binding(uintptr_t texture, sg_view *view, sg_sampler *sampler, bool *premultiplied);
/* A render target given to RmlUi as a texture of premultiplied colors, its reference
 * taken; 0 when there is no room (its reference let go of). */
uintptr_t wgf_ui_priv_doc_texture_adopt(wgf_texture_t target, int width, int height);

#endif
