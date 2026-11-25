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

/* sokol_fontstash's own 2D pipeline (blended, its shader reading the atlas's coverage as
 * alpha): what glyph quads drawn by another's code (the ui module's documents) draw with. */
bool wgf_gfx_priv_fontstash_pipeline(struct FONScontext *context, sgl_pipeline *pipeline)
{
    const _sfons_t *sfons = context != NULL ? (const _sfons_t *)context->params.userPtr : NULL;
    if (sfons == NULL || sfons->pip.id == SG_INVALID_ID) return false;
    *pipeline = sfons->pip;
    return true;
}

void wgf_gfx_priv_fontstash_restore(struct FONScontext *context)
{
    _sfons_t *sfons = context != NULL ? (_sfons_t *)context->params.userPtr : NULL;
    if (sfons == NULL) return;
    sfons->img_dirty = true; /* the whole atlas, sent again at the next flush: it sends its dirty rows */
    sfons->img_dirty_rect.x0 = sfons->img_dirty_rect.y0 = 0;
    sfons->img_dirty_rect.x1 = sfons->cur_width;
    sfons->img_dirty_rect.y1 = sfons->cur_height;
}

/* Snapping glyphs to the framebuffer's pixels (wgf_gfx_font_priv.h): sokol_fontstash's draw,
 * wrapped. fontstash hands over six vertices a glyph, two triangles of its quad. */
static struct {
    bool on;
    float ax, bx, ay, by;
    void (*draw)(void *user, const float *verts, const float *tcoords, const unsigned int *colors, int nverts);
    float verts[FONS_VERTEX_COUNT * 2];
} snapping;

static void snapped_draw(void *user, const float *verts, const float *tcoords, const unsigned int *colors,
                         int nverts)
{
    int i, j;
    if (!snapping.on || nverts > FONS_VERTEX_COUNT) {
        snapping.draw(user, verts, tcoords, colors, nverts);
        return;
    }
    memcpy(snapping.verts, verts, sizeof(float) * 2 * (size_t)nverts);
    for (i = 0; i + 6 <= nverts; i += 6) {
        float *quad = snapping.verts + 2 * i, left = quad[0], top = quad[1], x, y, dx, dy;
        for (j = 1; j < 6; j++) {
            if (quad[2 * j] < left) left = quad[2 * j];
            if (quad[2 * j + 1] < top) top = quad[2 * j + 1];
        }
        x = left * snapping.ax + snapping.bx;
        y = top * snapping.ay + snapping.by;
        dx = (floorf(x + 0.5f) - x) / snapping.ax;
        dy = (floorf(y + 0.5f) - y) / snapping.ay;
        for (j = 0; j < 6; j++) {
            quad[2 * j] += dx;
            quad[2 * j + 1] += dy;
        }
    }
    snapping.draw(user, snapping.verts, tcoords, colors, nverts);
}

void wgf_gfx_priv_fontstash_hook_snapping(struct FONScontext *context)
{
    if (context == NULL || context->params.renderDraw == snapped_draw) return;
    snapping.draw = context->params.renderDraw;
    context->params.renderDraw = snapped_draw;
}

void wgf_gfx_priv_fontstash_snap(bool on, float ax, float bx, float ay, float by)
{
    const bool usable = fabsf(ax) > 1e-6f && fabsf(ay) > 1e-6f && snapping.draw != NULL;
    snapping.on = on && usable;
    snapping.ax = ax;
    snapping.bx = bx;
    snapping.ay = ay;
    snapping.by = by;
}
