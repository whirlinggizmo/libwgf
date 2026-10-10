#ifndef WGF_GFX_TEXTURE_PRIV_H
#define WGF_GFX_TEXTURE_PRIV_H

#include <stdbool.h>
#include <stddef.h>

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
/* A decoded image's first level, RGBA, 8 bits a channel, and its size. */
const unsigned char *wgf_gfx_priv_texture_decoded_pixels(const void *decoded, int *width, int *height);
wgf_texture_t wgf_gfx_priv_texture_create_decoded(void *decoded);

struct wgf_gfx_priv_ktx_t; /* texture/wgf_gfx_ktx_priv.h */

/* Compressed textures (KTX). The file to read for `path`, a texture's "name.ktx": this
 * GPU's variant (name.bc7.ktx, name.astc.ktx, name.etc2.ktx, the first it can sample),
 * or name.png when it can sample none; a variant named outright is itself. False for a
 * path that isn't a .ktx, or too long for `out`. Before gfx runs: name.png. The main
 * thread asks the GPU the first time; a worker may call it only after the main thread
 * has, once gfx runs (a glTF's prepare, after its lister). */
bool wgf_gfx_priv_texture_ktx_path(const char *path, char *out, size_t out_size);
/* The variants this GPU is taken to sample in place of asking it, bit i the i-th above
 * (0: none, so the PNG); -1 asks it again. For tests. */
void wgf_gfx_priv_texture_set_ktx_support(int mask);
/* A KTX file at `path` read and parsed, on any thread: the file, which *ktx points into,
 * freed with ktx_free; NULL (warned) when it can't be read or isn't one gfx can use. */
void *wgf_gfx_priv_texture_read_ktx(const char *path, const struct wgf_gfx_priv_ktx_t **ktx);
void wgf_gfx_priv_texture_ktx_free(void *file);
/* A texture of its own, READY, from a parsed KTX file, on the main thread: 0 (warned,
 * naming `what`) when this GPU can't sample its format or refused it. */
wgf_texture_t wgf_gfx_priv_texture_create_ktx(const struct wgf_gfx_priv_ktx_t *ktx, const char *what);

/* The placeholder checker, gfx's own (0 before gfx is set up): what a color texture a file
 * names but couldn't give is drawn with. */
wgf_texture_t wgf_gfx_priv_texture_get_placeholder(void);

/* With the surface: the placeholder made at setup; every texture's GPU objects freed
 * at shutdown, loads still pending cancelled, and every handle gone. */
void wgf_gfx_priv_texture_setup(void);
void wgf_gfx_priv_texture_shutdown(void);

#endif
