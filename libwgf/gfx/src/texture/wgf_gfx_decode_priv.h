#ifndef WGF_GFX_DECODE_PRIV_H
#define WGF_GFX_DECODE_PRIV_H

/* An image file's decode to RGBA8, rows from the top, unpremultiplied: PNG, JPEG, BMP, TGA, and
 * GIF (its first frame). Natively, and in a headless web build, stb_image's, done as it starts
 * (wgf_gfx_decode_stb.c), on whatever thread starts it; in a page the browser's, done in a later
 * update (wgf_gfx_decode_web.c: createImageBitmap, its pixels read back through WebGL so none
 * is premultiplied), started and polled on the main thread. So a loader starts one in its
 * preparation and, when it isn't done, waits for it in its finish. */

typedef struct wgf_gfx_priv_decode_t wgf_gfx_priv_decode_t;

typedef enum wgf_gfx_priv_decode_state_t {
    WGF_GFX_PRIV_DECODE_PENDING = 0,
    WGF_GFX_PRIV_DECODE_DONE = 1,
    WGF_GFX_PRIV_DECODE_FAILED = 2
} wgf_gfx_priv_decode_state_t;

/* A decode of `size` bytes (copied: the caller's are its own again), `what` naming it in the
 * log; NULL without the memory. */
wgf_gfx_priv_decode_t *wgf_gfx_priv_decode_start(const unsigned char *bytes, int size, const char *what);
wgf_gfx_priv_decode_state_t wgf_gfx_priv_decode_poll(wgf_gfx_priv_decode_t *decode);
/* A done decode's pixels (malloc'd: free()), its size in `width` and `height`, and the decode
 * let go of; NULL, logged, for one that FAILED. */
unsigned char *wgf_gfx_priv_decode_take(wgf_gfx_priv_decode_t *decode, int *width, int *height);
/* A decode let go of, done or not. */
void wgf_gfx_priv_decode_cancel(wgf_gfx_priv_decode_t *decode);

#endif
