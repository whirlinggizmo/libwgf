#include "texture/wgf_gfx_decode_priv.h"

#include <stdlib.h>

#include "stb_image.h"
#include "wgf_log.h"

/* Images decoded by stb_image (wgf_gfx_decode_priv.h): natively, and in a headless web build,
 * which runs where a browser's decoder isn't. Done as it starts. */

struct wgf_gfx_priv_decode_t {
    unsigned char *pixels; /* NULL: it FAILED */
    int width, height;
};

wgf_gfx_priv_decode_t *wgf_gfx_priv_decode_start(const unsigned char *bytes, int size, const char *what)
{
    wgf_gfx_priv_decode_t *decode = (wgf_gfx_priv_decode_t *)calloc(1, sizeof(*decode));
    int comp;
    if (decode == NULL) return NULL;
    decode->pixels = stbi_load_from_memory(bytes, size, &decode->width, &decode->height, &comp, 4);
    if (decode->pixels == NULL) wgf_log_warn("wgf_gfx: %s: not an image it can read (%s)", what, stbi_failure_reason());
    return decode;
}

wgf_gfx_priv_decode_state_t wgf_gfx_priv_decode_poll(wgf_gfx_priv_decode_t *decode)
{
    return decode != NULL && decode->pixels != NULL ? WGF_GFX_PRIV_DECODE_DONE : WGF_GFX_PRIV_DECODE_FAILED;
}

unsigned char *wgf_gfx_priv_decode_take(wgf_gfx_priv_decode_t *decode, int *width, int *height)
{
    unsigned char *pixels;
    if (decode == NULL) return NULL;
    pixels = decode->pixels; /* stb_image's own allocator: malloc */
    *width = decode->width;
    *height = decode->height;
    free(decode);
    return pixels;
}

void wgf_gfx_priv_decode_cancel(wgf_gfx_priv_decode_t *decode)
{
    if (decode == NULL) return;
    stbi_image_free(decode->pixels);
    free(decode);
}
