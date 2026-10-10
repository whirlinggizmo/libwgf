#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Compressed textures on a real GPU, in a window (tools/run_in_xvfb.py, OpenGL on a
 * virtual display), their pixels read back: the examples' flame from its PNG and from
 * each variant this GL samples (tools/compress_textures.py's, beside it), drawn alike,
 * within a little of each other as compression leaves them; and "textures/flame.ktx" is
 * the first of them, or the PNG where it samples none. libwgt's GL test's BC7 against its
 * PNG, for every variant. */

#define SIZE 64

static const struct {
    const char *path;
    sg_pixel_format format;
} VARIANTS[] = {
    {"textures/flame.bc7.ktx", SG_PIXELFORMAT_BC7_RGBA},
    {"textures/flame.astc.ktx", SG_PIXELFORMAT_ASTC_4x4_RGBA},
    {"textures/flame.etc2.ktx", SG_PIXELFORMAT_ETC2_RGBA8},
};
enum { VARIANT_COUNT = sizeof(VARIANTS) / sizeof(VARIANTS[0]) };

static int failures;
static wgf_texture_t png, named, variants[VARIANT_COUNT];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* `texture` drawn over the window, its pixels read into `rgba`. */
static void draw_texture(wgf_texture_t texture, unsigned char *rgba)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_draw_texture(texture, 0, 0, SIZE, SIZE, WGF_COLOR_WHITE);
    wgf_gfx_priv_end_frame();
    glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static int pending(wgf_texture_t texture)
{
    return texture != 0 && wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_PENDING;
}

static void check(void)
{
    static unsigned char a[SIZE * SIZE * 4], b[SIZE * SIZE * 4];
    int v, sampled = 0;
    expect(wgf_resource_get_status(png) == WGF_RESOURCE_STATUS_READY, "the PNG is READY");
    draw_texture(png, a);
    for (v = 0; v < VARIANT_COUNT; v++) {
        int i, worst = 0;
        long total = 0;
        char what[160];
        if (variants[v] == 0) continue;
        sampled++;
        snprintf(what, sizeof(what), "%s is READY, at the PNG's size", VARIANTS[v].path);
        expect(wgf_resource_get_status(variants[v]) == WGF_RESOURCE_STATUS_READY &&
                   wgf_texture_get_width(variants[v]) == wgf_texture_get_width(png) &&
                   wgf_texture_get_height(variants[v]) == wgf_texture_get_height(png),
               what);
        draw_texture(variants[v], b);
        for (i = 0; i < SIZE * SIZE * 4; i++) {
            const int d = abs(a[i] - b[i]);
            worst = d > worst ? d : worst;
            total += d;
        }
        printf("%s against its PNG: worst %d, mean %.2f\n", VARIANTS[v].path, worst,
               (double)total / (SIZE * SIZE * 4));
        snprintf(what, sizeof(what), "%s drawn as its PNG is", VARIANTS[v].path);
        expect(total / (SIZE * SIZE * 4) <= 3 && worst <= 48, what);
    }
    if (sampled == 0) printf("this GL samples none of the compressed formats: only the PNG fallback is checked\n");
    {
        const char *expected = "textures/flame.png";
        for (v = 0; v < VARIANT_COUNT; v++) {
            if (variants[v] != 0) {
                expected = VARIANTS[v].path;
                break;
            }
        }
        expect(wgf_resource_get_status(named) == WGF_RESOURCE_STATUS_READY &&
                   strcmp(wgf_resource_get_path(named), expected) == 0,
               "textures/flame.ktx is the first variant this GL samples, else the PNG");
    }
}

static int frames;

static void on_frame(void *user)
{
    int v, waiting;
    (void)user;
    if (++frames == 1) {
        wgf_asset_set_host(WGF_TEST_ASSETS);
        png = wgf_texture_create("textures/flame.png");
        named = wgf_texture_create("textures/flame.ktx");
        for (v = 0; v < VARIANT_COUNT; v++) {
            variants[v] = sg_query_pixelformat(VARIANTS[v].format).sample ? wgf_texture_create(VARIANTS[v].path) : 0;
        }
        return;
    }
    waiting = pending(png) || pending(named);
    for (v = 0; v < VARIANT_COUNT; v++) waiting = waiting || pending(variants[v]);
    if (waiting && frames < 600) return;
    check();
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    wgf_window_set_size(SIZE, SIZE);
    wgf_window_set_high_dpi(false); /* one pixel a logical pixel, whatever the display */
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
