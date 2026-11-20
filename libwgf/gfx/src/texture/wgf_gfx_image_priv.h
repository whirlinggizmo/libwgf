#ifndef WGF_GFX_IMAGE_PRIV_H
#define WGF_GFX_IMAGE_PRIV_H

#include "wgf_image.h"

/* The largest side an image may have: a texture's (wgf_texture_create_blank's), and small
 * enough that its bytes' count fits an int. */
#define WGF_GFX_PRIV_IMAGE_MAX_SIDE 16384

/* A new image made in code, READY, `width` by `height` pixels, transparent black, its
 * pixels (width * height * 4 bytes, the top row first) in *pixels for the maker to fill;
 * 0 (logged, as `caller`) for a side of 0 or less or over the largest, or no memory. */
wgf_image_t wgf_gfx_priv_image_add(int width, int height, unsigned char **pixels, const char *caller);

#endif
