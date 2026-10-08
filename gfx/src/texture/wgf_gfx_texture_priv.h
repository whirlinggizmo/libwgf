#ifndef WGF_GFX_TEXTURE_PRIV_H
#define WGF_GFX_TEXTURE_PRIV_H

#include <stdbool.h>

#include "sokol_gfx.h"
#include "wgf_texture.h"

/* Textures, for gfx's own drawing code. */

/* The sampler for a wrap on each axis, a filter, and whether to sample the mipmaps a
 * texture has (else its base level only), made the first time it's asked for and
 * shared. */
sg_sampler wgf_gfx_priv_texture_sampler(wgf_texture_wrap_t wrap_u, wgf_texture_wrap_t wrap_v,
                                        wgf_texture_filter_t filter, bool mipmaps);

/* What to bind to draw `texture`: its view, sampler, and size; the placeholder
 * checker's once it has FAILED (*is_placeholder set). False -- draw nothing -- while it
 * is PENDING, for a handle that isn't a texture, or before gfx is set up. */
bool wgf_gfx_priv_texture_get_binding(wgf_texture_t texture, sg_view *view, sg_sampler *sampler, int *width,
                                      int *height, bool *is_placeholder);

/* One more reference to `texture`, as a sprite takes on the texture it shows; dropped
 * with wgf_resource_release. Nothing for a handle that isn't a texture. */
void wgf_gfx_priv_texture_retain(wgf_texture_t texture);

/* An image in memory (a glTF's), decoded to RGBA with its mipmaps, on any thread: NULL
 * (warned, naming `what`) when it isn't one stb_image reads; freed with decoded_free, or
 * made a texture, READY, on the main thread with create_decoded, which takes it (0 when
 * the GPU didn't). */
void *wgf_gfx_priv_texture_decode(const unsigned char *bytes, int size, const char *what);
void wgf_gfx_priv_texture_decoded_free(void *decoded);
wgf_texture_t wgf_gfx_priv_texture_create_decoded(void *decoded);

/* The placeholder checker, gfx's own (0 before gfx is set up): what a color texture a file
 * names but couldn't give is drawn with. */
wgf_texture_t wgf_gfx_priv_texture_get_placeholder(void);

/* With the surface: the placeholder made at setup; every texture's GPU objects freed
 * at shutdown, loads still pending cancelled, and every handle gone. */
void wgf_gfx_priv_texture_setup(void);
void wgf_gfx_priv_texture_shutdown(void);

#endif
