#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"
#include "texture/wgf_gfx_png_priv.h"

/* gfx's PNG writer (wgf_texture_read_save's), its files decoded by stb_image, which reads
 * every texture: a frame's kinds of pixels (flat color, a gradient, noise, which matches
 * little), each back exactly; a flat one far smaller than its pixels; a 1 by 1 and a long
 * row (matches past a row's end); and what it refuses. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* `rgba` encoded and decoded: the same pixels. The file's size in *size. */
static int round_trip(const unsigned char *rgba, int width, int height, int *size)
{
    int w = 0, h = 0, channels = 0, same;
    unsigned char *png = wgf_gfx_priv_png_encode(rgba, width, height, size), *back;
    if (png == NULL) return 0;
    back = stbi_load_from_memory(png, *size, &w, &h, &channels, 4);
    same = back != NULL && w == width && h == height && channels == 4 &&
           memcmp(back, rgba, (size_t)width * (size_t)height * 4u) == 0;
    stbi_image_free(back);
    free(png);
    return same;
}

int main(void)
{
    enum { W = 320, H = 200 };
    unsigned char *rgba = malloc((size_t)W * H * 4);
    unsigned int seed = 1;
    int x, y, size = 0;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            unsigned char *p = rgba + ((size_t)y * W + (size_t)x) * 4;
            p[0] = 40, p[1] = 60, p[2] = 90, p[3] = 255;
        }
    }
    expect(round_trip(rgba, W, H, &size), "a flat color back exactly");
    expect(size > 0 && size < W * H * 4 / 20, "and far smaller than its pixels");

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            unsigned char *p = rgba + ((size_t)y * W + (size_t)x) * 4;
            p[0] = (unsigned char)x, p[1] = (unsigned char)y,
                p[2] = (unsigned char)(x + y), p[3] = (unsigned char)(255 - y);
        }
    }
    expect(round_trip(rgba, W, H, &size), "a gradient, with alpha, back exactly");

    for (x = 0; x < W * H * 4; x++) {
        seed = seed * 1103515245u + 12345u;
        rgba[x] = (unsigned char)(seed >> 16);
    }
    expect(round_trip(rgba, W, H, &size), "noise back exactly");
    expect(round_trip(rgba, 1, 1, &size), "1 by 1");
    expect(round_trip(rgba, 16384, 1, &size), "one long row");
    memset(rgba, 7, (size_t)W * H * 4);
    expect(round_trip(rgba, 1, 16384, &size), "one tall column, every byte alike: matches of the longest");

    expect(wgf_gfx_priv_png_encode(NULL, 4, 4, &size) == NULL && size == 0, "no pixels: refused");
    expect(wgf_gfx_priv_png_encode(rgba, 0, 4, &size) == NULL && wgf_gfx_priv_png_encode(rgba, 4, 16385, &size) == NULL,
           "a side of 0 or past 16384: refused");
    free(rgba);
    if (failures == 0) printf("wgf_gfx_png_test: passed\n");
    return failures == 0 ? 0 : 1;
}
