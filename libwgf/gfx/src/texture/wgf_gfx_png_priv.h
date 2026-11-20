#ifndef WGF_GFX_PNG_PRIV_H
#define WGF_GFX_PNG_PRIV_H

/* RGBA pixels (8 bits a channel, the top row first) as a PNG file: malloc'd, its length in
 * *out_size; NULL for no pixels, a side of 0 or over 16384, or no memory. */
unsigned char *wgf_gfx_priv_png_encode(const unsigned char *rgba, int width, int height, int *out_size);

#endif
