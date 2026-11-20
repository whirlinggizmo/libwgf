#ifndef WGF_GFX_CUBEMAP_PRIV_H
#define WGF_GFX_CUBEMAP_PRIV_H

#include <stdbool.h>

#include "wgf_cubemap.h"

/* Cubemaps (texture/wgf_gfx_cubemap.c), their CPU half: how an image in one of raylib's
 * layouts is cut into faces, for the loader and the tests. Faces are in sokol's (and GL's)
 * order, +X, -X, +Y, -Y, +Z, -Z. */

/* The layout an image of `width` by `height` pixels is read as, and its faces' size: `layout`
 * when the image is exactly that layout's shape, for AUTO the one its shape is (raylib's
 * order: a row or a 4 by 3 cross when it is wider, a column or a 3 by 4 cross when taller);
 * AUTO (none: it can't be read) otherwise, a square image among them. FACES is never one. */
wgf_cubemap_layout_t wgf_gfx_priv_cubemap_layout_of(int width, int height, wgf_cubemap_layout_t layout, int *size);

/* Where face `face` (0 to 5) is in a layout's image, in faces from its top-left: a column and
 * a row. False for a layout that isn't one of the four, or a face that isn't one. */
bool wgf_gfx_priv_cubemap_face_cell(wgf_cubemap_layout_t layout, int face, int *column, int *row);

/* The six faces of an RGBA image `width` pixels a row, read as `layout` (what layout_of gave)
 * with faces of `size`, into `out`: +X to -Z, each size * size * 4 bytes, rows from the top. */
void wgf_gfx_priv_cubemap_extract(const unsigned char *rgba, int width, wgf_cubemap_layout_t layout, int size,
                                  unsigned char *out);

#endif
