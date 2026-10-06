#include <math.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_presentation.h"

/* The presentation modes (wgf_presentation.h): the fitting each makes of a design over
 * a framebuffer, installed by wgf_presentation_set in platform's transform (which the
 * pointer and touches go through) and, for the bars, in gfx's frame. In gfx, above
 * platform, so one call installs both, and a program that sets no mode links neither. */

static struct {
    wgf_presentation_mode_t mode;
    int width, height;
} presentation;

/* The fitting itself, for `mode` and a design of width by height, over a framebuffer
 * of fb_width by fb_height pixels: a uniform scale (but STRETCH's), the design centered,
 * and the visible area what of the logical plane the framebuffer shows, cut to the
 * design but under EXPAND (and FILL, whose design is past the framebuffer anyway). */
static void fit(wgf_presentation_mode_t mode, int width, int height, int fb_width, int fb_height,
                wgf_platform_priv_presentation_t *out)
{
    const float w = (float)width, h = (float)height, fw = (float)fb_width, fh = (float)fb_height;
    const float across = fw / w, down = fh / h;
    float scale = across < down ? across : down;
    float x0, y0, x1, y1;
    if (mode == WGF_PRESENTATION_MODE_FILL) scale = across > down ? across : down;
    if (mode == WGF_PRESENTATION_MODE_INTEGER) scale = scale >= 1.0f ? floorf(scale) : 1.0f;
    out->scale_x = out->scale_y = scale;
    if (mode == WGF_PRESENTATION_MODE_STRETCH) {
        out->scale_x = across;
        out->scale_y = down;
    }
    out->offset_x = (fw - w * out->scale_x) * 0.5f;
    out->offset_y = (fh - h * out->scale_y) * 0.5f;
    if (mode == WGF_PRESENTATION_MODE_INTEGER) { /* on whole pixels, or every edge is blurred */
        out->offset_x = floorf(out->offset_x);
        out->offset_y = floorf(out->offset_y);
    }
    /* the framebuffer's edges, in logical coordinates */
    x0 = -out->offset_x / out->scale_x;
    y0 = -out->offset_y / out->scale_y;
    x1 = (fw - out->offset_x) / out->scale_x;
    y1 = (fh - out->offset_y) / out->scale_y;
    if (mode != WGF_PRESENTATION_MODE_EXPAND) { /* the design's edges, inside the framebuffer's */
        x0 = x0 > 0.0f ? x0 : 0.0f;
        y0 = y0 > 0.0f ? y0 : 0.0f;
        x1 = x1 < w ? x1 : w;
        y1 = y1 < h ? y1 : h;
    }
    out->visible_x = x0;
    out->visible_y = y0;
    out->visible_width = x1 - x0;
    out->visible_height = y1 - y0;
    out->bars = mode == WGF_PRESENTATION_MODE_FIT || mode == WGF_PRESENTATION_MODE_INTEGER;
}

static void fill_bars(void)
{
    wgf_gfx_priv_render_fill_visible();
}

static void fitting(int fb_width, int fb_height, float dpi_scale, wgf_platform_priv_presentation_t *out)
{
    (void)dpi_scale; /* a design is fitted to the framebuffer's pixels, whatever their density */
    fit(presentation.mode, presentation.width, presentation.height, fb_width > 0 ? fb_width : 1,
        fb_height > 0 ? fb_height : 1, out);
}

bool wgf_presentation_set(wgf_presentation_mode_t mode, int width, int height)
{
    if ((int)mode < WGF_PRESENTATION_MODE_NONE || mode > WGF_PRESENTATION_MODE_INTEGER) return false;
    if (mode == WGF_PRESENTATION_MODE_NONE) {
        presentation.mode = mode;
        presentation.width = presentation.height = 0;
        wgf_platform_priv_set_fitting(NULL, 0, 0);
        return true;
    }
    if (width < 1 || height < 1) return false;
    presentation.mode = mode;
    presentation.width = width;
    presentation.height = height;
    wgf_platform_priv_set_fitting(fitting, width, height);
    wgf_gfx_priv_render_set_bars(fill_bars);
    return true;
}

wgf_presentation_mode_t wgf_presentation_get_mode(void)
{
    return presentation.mode;
}

int wgf_presentation_get_width(void)
{
    wgf_platform_priv_presentation_t now;
    if (presentation.mode != WGF_PRESENTATION_MODE_NONE) return presentation.width;
    wgf_platform_priv_get_presentation(&now);
    return (int)(now.visible_width + 0.5f);
}

int wgf_presentation_get_height(void)
{
    wgf_platform_priv_presentation_t now;
    if (presentation.mode != WGF_PRESENTATION_MODE_NONE) return presentation.height;
    wgf_platform_priv_get_presentation(&now);
    return (int)(now.visible_height + 0.5f);
}

wgf_vec4_t wgf_presentation_get_visible(void)
{
    wgf_platform_priv_presentation_t now;
    wgf_platform_priv_get_presentation(&now);
    return wgf_vec4_make(now.visible_x, now.visible_y, now.visible_width, now.visible_height);
}

float wgf_presentation_get_scale(void)
{
    wgf_platform_priv_presentation_t now;
    wgf_platform_priv_get_presentation(&now);
    return now.scale_x;
}
