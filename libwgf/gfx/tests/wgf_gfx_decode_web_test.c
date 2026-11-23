#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "texture/wgf_gfx_decode_priv.h"
#include "texture/wgf_gfx_png_priv.h"
#include "wgf_app.h"
#include "wgf_window.h"

/* The browser's image decode, in a page (tools/run_in_browser.py): a PNG written here, of opaque,
 * half-transparent, and clear pixels in colors, decoded by the browser a frame or more later, its
 * pixels the file's exactly (none premultiplied, none color-converted), its rows from the top;
 * bytes that aren't an image FAILED; a decode let go of before it is done. */

#define W 3
#define H 2

static int failures, frames;
static wgf_gfx_priv_decode_t *good, *bad, *dropped;
static unsigned char rgba[W * H * 4];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void on_frame(void *user)
{
    (void)user;
    if (++frames == 1) {
        static const unsigned char pixels[W * H * 4] = {
            255, 0,   0, 255, 0,   255, 0,   128, 200, 100, 50, 64, /* top: opaque red, half green, a quarter orange */
            /* bottom: blue, clear (its color kept), nearly clear */
            0,   0, 255, 255, 10,  20,  30,  0,   255, 255, 255, 1};
        int size = 0;
        unsigned char *png = wgf_gfx_priv_png_encode(pixels, W, H, &size);
        memcpy(rgba, pixels, sizeof(rgba));
        expect(png != NULL, "a PNG written");
        good = wgf_gfx_priv_decode_start(png, size, "test.png");
        dropped = wgf_gfx_priv_decode_start(png, size, "dropped.png");
        free(png);
        bad = wgf_gfx_priv_decode_start((const unsigned char *)"not an image", 12, "garbage.png");
        expect(good != NULL && wgf_gfx_priv_decode_poll(good) == WGF_GFX_PRIV_DECODE_PENDING, "pending as it starts");
        wgf_gfx_priv_decode_cancel(dropped);
        return;
    }
    if (frames > 600) {
        printf("FAIL: the decodes never ended\n");
        emscripten_force_exit(1);
        return;
    }
    if (wgf_gfx_priv_decode_poll(good) == WGF_GFX_PRIV_DECODE_PENDING ||
        wgf_gfx_priv_decode_poll(bad) == WGF_GFX_PRIV_DECODE_PENDING) {
        return;
    }
    {
        int width = 0, height = 0, i, exact = 1;
        unsigned char *pixels;
        expect(wgf_gfx_priv_decode_poll(good) == WGF_GFX_PRIV_DECODE_DONE, "decoded");
        pixels = wgf_gfx_priv_decode_take(good, &width, &height);
        expect(pixels != NULL && width == W && height == H, "its size");
        for (i = 0; pixels != NULL && i < W * H * 4; i++) {
            if (pixels[i] != rgba[i]) {
                printf("FAIL: byte %d read %d, the file's %d\n", i, pixels[i], rgba[i]);
                exact = 0;
            }
        }
        expect(exact, "its pixels the file's exactly: rows from the top, none premultiplied");
        free(pixels);
        expect(wgf_gfx_priv_decode_poll(bad) == WGF_GFX_PRIV_DECODE_FAILED, "bytes that aren't an image: FAILED");
        expect(wgf_gfx_priv_decode_take(bad, &width, &height) == NULL, "and no pixels");
    }
    if (failures == 0) printf("wgf_gfx_decode_web_test: passed\n");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    wgf_window_set_size(16, 16);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
