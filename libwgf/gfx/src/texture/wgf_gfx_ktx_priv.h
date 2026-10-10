#ifndef WGF_GFX_KTX_PRIV_H
#define WGF_GFX_KTX_PRIV_H

#include <stdbool.h>
#include <stddef.h>

#include "sokol_gfx.h"

/* KTX (version 1) files holding GPU-compressed textures: BC7, ASTC 4x4, or ETC2 RGBA,
 * 16 bytes a 4x4 block, with their mipmaps; what tools/compress_textures.py writes.
 * Parsing only points into the bytes. libwgt's wgt_gfx_ktx, as it was. */
typedef struct wgf_gfx_priv_ktx_t {
    sg_pixel_format format;
    int width, height;
    int mip_count;
    const unsigned char *levels[SG_MAX_MIPMAPS];
    size_t sizes[SG_MAX_MIPMAPS];
} wgf_gfx_priv_ktx_t;

/* False (with *error saying why) when the bytes aren't a KTX file gfx can use. */
bool wgf_gfx_priv_ktx_parse(const unsigned char *bytes, size_t size, wgf_gfx_priv_ktx_t *out, const char **error);

#endif
