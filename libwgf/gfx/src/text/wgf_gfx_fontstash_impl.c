/* fontstash, with stb_truetype for reading fonts, and sokol_fontstash, which draws
 * fontstash's glyphs through sokol_gl: compiled once for gfx. Vendored code,
 * compiled as it comes. fontstash's implementation uses libc without including it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h> /* fontstash opens files through MultiByteToWideChar there */
#endif

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define SOKOL_FONTSTASH_IMPL
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"

#include "text/wgf_gfx_font_priv.h"

/* What sokol_fontstash renders with, for drawing glyph quads with another pipeline
 * (text in 3D: depth tested). The atlas view changes when fontstash grows the atlas,
 * so ask again for every draw. libwgt's (wgrender's wgri_fontstash_render_state). */
bool wgf_gfx_priv_fontstash_render_state(struct FONScontext *context, sg_view *atlas, sg_sampler *sampler,
                                         sg_shader *shader)
{
    const _sfons_t *sfons = context != NULL ? (const _sfons_t *)context->params.userPtr : NULL;
    if (sfons == NULL || sfons->shd.id == SG_INVALID_ID || sfons->tex_view.id == SG_INVALID_ID) return false;
    *atlas = sfons->tex_view;
    *sampler = sfons->smp;
    *shader = sfons->shd;
    return true;
}
