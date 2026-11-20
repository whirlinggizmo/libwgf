#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_image.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Images on the CPU (wgf_image.h), raylib's GenImage* and LoadTextureFromImage: a color,
 * three gradients, checks, and three kinds of noise generated, each made a texture and
 * drawn; Perlin noise scrolling, generated again each frame at a shifted offset and
 * written into its texture (wgf_texture_write_image); and an image loaded from a file with
 * a frame of pixels set around it before it is made a texture.
 *
 *   Space  a new seed for the noise
 *   Esc    quit, where quitting means anything */

#define SIDE 128
#define COUNT 8

static const char *const NAMES[COUNT] = {"color",    "linear",      "radial", "square",
                                         "checked",  "white noise", "Perlin", "cellular"};
static wgf_texture_t textures[COUNT];
static wgf_texture_t scrolling, from_file;
static wgf_image_t loaded;
static int seed = 1;
static int offset;

/* The generated images, made textures; each image let go of once its texture has it. */
static void generate(void)
{
    wgf_image_t images[COUNT];
    int i;
    images[0] = wgf_image_create_color(SIDE, SIDE, WGF_COLOR_SKYBLUE);
    images[1] = wgf_image_create_gradient_linear(SIDE, SIDE, 45.0f, WGF_COLOR_GOLD, WGF_COLOR_DARKPURPLE);
    images[2] = wgf_image_create_gradient_radial(SIDE, SIDE, 0.1f, WGF_COLOR_WHITE, WGF_COLOR_DARKBLUE);
    images[3] = wgf_image_create_gradient_square(SIDE, SIDE, 0.3f, WGF_COLOR_ORANGE, WGF_COLOR_MAROON);
    images[4] = wgf_image_create_checked(SIDE, SIDE, 16, 16, WGF_COLOR_LIGHTGRAY, WGF_COLOR_DARKGRAY);
    images[5] = wgf_image_create_white_noise(SIDE, SIDE, 0.3f, seed);
    images[6] = wgf_image_create_perlin_noise(SIDE, SIDE, 0, 0, 2.0f, seed);
    images[7] = wgf_image_create_cellular(SIDE, SIDE, 32, seed);
    for (i = 0; i < COUNT; i++) {
        if (textures[i] != 0) wgf_resource_release(textures[i]);
        textures[i] = wgf_texture_create_from_image(images[i]);
        wgf_resource_release(images[i]);
    }
}

static void frame(void *user)
{
    char label[64];
    int i;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) {
        seed++;
        generate();
    }

    for (i = 0; i < COUNT; i++) {
        const float x = 24.0f + (float)(i % 4) * (SIDE + 24), y = 24.0f + (float)(i / 4) * (SIDE + 40);
        wgf_draw_texture(textures[i], x, y, SIDE, SIDE, WGF_COLOR_WHITE);
        wgf_draw_text(0, NAMES[i], x, y + SIDE + 4, 16, WGF_COLOR_RAYWHITE);
    }

    /* Perlin noise scrolling: generated again a pixel along, written into its texture */
    {
        const wgf_image_t next = wgf_image_create_perlin_noise(SIDE, SIDE, offset++, 0, 2.0f, seed);
        wgf_texture_write_image(scrolling, next);
        wgf_resource_release(next);
        wgf_draw_texture(scrolling, 24, 360, SIDE, SIDE, WGF_COLOR_WHITE);
        wgf_draw_text(0, "Perlin, scrolling", 24, 360 + SIDE + 4, 16, WGF_COLOR_RAYWHITE);
    }

    /* the file's image, once loaded: framed in pixels set one at a time, then a texture */
    if (from_file == 0 && wgf_resource_get_status(loaded) == WGF_RESOURCE_STATUS_READY) {
        const int w = wgf_image_get_width(loaded), h = wgf_image_get_height(loaded);
        int x, y;
        for (x = 0; x < w; x++) {
            for (y = 0; y < 4; y++) {
                wgf_image_set_pixel(loaded, x, y, WGF_COLOR_RED);
                wgf_image_set_pixel(loaded, x, h - 1 - y, WGF_COLOR_RED);
            }
        }
        for (y = 0; y < h; y++) {
            for (x = 0; x < 4; x++) {
                wgf_image_set_pixel(loaded, x, y, WGF_COLOR_RED);
                wgf_image_set_pixel(loaded, w - 1 - x, y, WGF_COLOR_RED);
            }
        }
        from_file = wgf_texture_create_from_image(loaded);
    }
    if (from_file != 0) {
        snprintf(label, sizeof(label), "a file's image, %d x %d, framed", wgf_image_get_width(loaded),
                 wgf_image_get_height(loaded));
        wgf_draw_texture(from_file, 24 + SIDE + 24, 360, SIDE, SIDE, WGF_COLOR_WHITE);
        wgf_draw_text(0, label, 24 + SIDE + 24, 360 + SIDE + 4, 16, WGF_COLOR_RAYWHITE);
    } else {
        wgf_draw_text(0, wgf_resource_get_status(loaded) == WGF_RESOURCE_STATUS_FAILED ? "the image failed to load"
                                                                                       : "loading an image...",
                      24 + SIDE + 24, 360, 16, WGF_COLOR_GRAY);
    }
    snprintf(label, sizeof(label), "[Space] a new seed (%d)", seed);
    wgf_draw_text(0, label, 24 + 2 * (SIDE + 24), 360, 16, WGF_COLOR_LIGHTGRAY);
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(24, 24, 36, 255));
    generate();
    scrolling = wgf_texture_create_blank(SIDE, SIDE);
    loaded = wgf_image_create("textures/tiles.png");
}

static void on_shutdown(void *user)
{
    int i;
    (void)user;
    for (i = 0; i < COUNT; i++) wgf_resource_release(textures[i]);
    wgf_resource_release(scrolling);
    if (from_file != 0) wgf_resource_release(from_file);
    wgf_resource_release(loaded);
}

int main(void)
{
    wgf_window_set_title("libwgf images");
    wgf_window_set_size(640, 540);
    wgf_app_run(init, NULL, frame, on_shutdown, NULL);
    return 0;
}
