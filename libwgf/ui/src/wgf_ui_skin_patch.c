#include <math.h>

#include "wgf_draw.h"
#include "wgf_ui.h"
#include "wgf_ui_skin_priv.h"

/* The Clay UI's skins' tiled patches (wgf_ui.h's wgf_ui_patch_t): their drawing, a file of
 * its own that a skin's drawing reaches through the hook the first tiled fill sets from
 * here, so skins that only stretch link none of it. */

#define TILES_MOST 256 /* along an edge: art a texel wide over a screen isn't drawn a quad a texel */

/* A patch's piece, the source rectangle repeated over the destination: at its own size, the
 * last cut where the room ends, or (`fit`) a whole number of times, each stretched a little
 * to fill it exactly. */
static void tile(wgf_texture_t texture, float sx, float sy, float sw, float sh, float x, float y, float width, float height,
                 bool fit, wgf_color_t tint)
{
    int nx, ny, i, j;
    if (sw <= 0.0f || sh <= 0.0f || width <= 0.0f || height <= 0.0f) return;
    if (!fit && width / sw <= (float)TILES_MOST && height / sh <= (float)TILES_MOST) {
        wgf_draw_texture_tiled(texture, sx, sy, sw, sh, x, y, width, height, 1.0f, tint);
        return;
    }
    nx = (int)fminf(fmaxf(roundf(width / sw), 1.0f), (float)TILES_MOST);
    ny = (int)fminf(fmaxf(roundf(height / sh), 1.0f), (float)TILES_MOST);
    for (j = 0; j < ny; j++) {
        for (i = 0; i < nx; i++) {
            wgf_draw_texture_region(texture, sx, sy, sw, sh, x + width * (float)i / (float)nx,
                                    y + height * (float)j / (float)ny, width / (float)nx, height / (float)ny, tint);
        }
    }
}

/* A skin's nine patches with its edges and middle tiled, not stretched: the corners at
 * their size, each edge repeated along it, the middle both ways. False, drawing nothing,
 * for a rectangle smaller than its borders (drawn stretched, its corners scaled down). */
static bool draw_tiled(wgf_texture_t texture, const float source[4], const float borders[4], float x, float y,
                       float width, float height, bool fit, wgf_color_t tint)
{
    const float sw = source[2] > 0.0f && source[3] > 0.0f ? source[2] : (float)wgf_texture_get_width(texture);
    const float sh = source[2] > 0.0f && source[3] > 0.0f ? source[3] : (float)wgf_texture_get_height(texture);
    const float l = borders[0], t = borders[1], r = borders[2], b = borders[3];
    /* the source's and the destination's columns and rows: an edge, the middle, an edge */
    const float sx[3] = {source[0], source[0] + l, source[0] + sw - r}, sws[3] = {l, sw - l - r, r};
    const float sy[3] = {source[1], source[1] + t, source[1] + sh - b}, shs[3] = {t, sh - t - b, b};
    const float dx[3] = {x, x + l, x + width - r}, dws[3] = {l, width - l - r, r};
    const float dy[3] = {y, y + t, y + height - b}, dhs[3] = {t, height - t - b, b};
    int row, column;
    if (width < l + r || height < t + b || sws[1] <= 0.0f || shs[1] <= 0.0f) return false;
    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            if (row != 1 && column != 1) { /* a corner */
                if (sws[column] > 0.0f && shs[row] > 0.0f) {
                    wgf_draw_texture_region(texture, sx[column], sy[row], sws[column], shs[row], dx[column],
                                            dy[row], dws[column], dhs[row], tint);
                }
            } else {
                tile(texture, sx[column], sy[row], sws[column], shs[row], dx[column], dy[row], dws[column], dhs[row], fit,
                     tint);
            }
        }
    }
    return true;
}

bool wgf_ui_set_skin_patch(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_ui_patch_t patch)
{
    unsigned char *kept = wgf_ui_priv_skin_patch((int)widget, (int)part, (int)state);
    if (kept == NULL || (int)patch < 0 || (int)patch > 2) return false;
    if (patch != WGF_UI_PATCH_STRETCHED) wgf_ui_priv_set_patch_draw(draw_tiled);
    *kept = (unsigned char)patch;
    return true;
}
