#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "stb_image.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_image.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_time.h"

/* Images on the CPU (wgf_image.h), headless: made in code, READY at once, each generator's
 * pixels -- a color everywhere, a linear gradient's ends exact at every angle, the radial
 * and square gradients' middle and edges, checks -- and noise the same for a seed (to a
 * hash every platform must give), different for another, in range, and tiling by its
 * offset; a pixel read and written, and its bytes; loaded from a file, PENDING then READY
 * with the file's pixels, the same path the same image, FAILED for junk or nothing, loaded
 * again in place with a new file's and kept when the new file is broken;
 * saved as a PNG that stb_image reads back to the same pixels; made a texture and written
 * to one; and every misuse refused. Pixels on the GPU: wgf_gfx_texture_read_window_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A 2x2 PNG, one pixel see-through (the texture test's rock). */
static const unsigned char rock_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x13, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x08, 0x41, 0xe0, 0x3f, 0x18, 0x00, 0x00, 0x3f,
    0xd2, 0x08, 0xf8, 0x65, 0x89, 0xa5, 0xdd, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

/* A task updated until it isn't PENDING; its status, the task kept. */
static wgf_fs_task_status_t wait_task(wgf_fs_task_t task)
{
    const double start = wgf_time_get_seconds();
    while (wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
    return wgf_fs_task_get_status(task);
}

static void write_file(const char *path, const unsigned char *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    wait_task(task);
    wgf_fs_task_destroy(task);
}

/* Updated until none of `images` is PENDING; a worker decodes, so on time, not a count. */
static void settle(const wgf_image_t *images, int count)
{
    const double start = wgf_time_get_seconds();
    for (;;) {
        int i, pending = 0;
        for (i = 0; i < count; i++) pending += wgf_resource_get_status(images[i]) == WGF_RESOURCE_STATUS_PENDING;
        if (pending == 0 || wgf_time_get_seconds() - start > 30.0) return;
        wgf_core_priv_update();
    }
}

/* Update until `image` has loaded again (wgf_asset_reload). */
static void settle_reload(wgf_image_t image)
{
    const double start = wgf_time_get_seconds();
    while (wgf_core_priv_resource_is_reloading(image) && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
}

/* FNV-1a over an image's bytes. */
static uint32_t hash_of(wgf_image_t image)
{
    const unsigned char *data = wgf_image_get_data(image);
    const int size = wgf_image_get_size(image);
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < size; i++) h = (h ^ data[i]) * 16777619u;
    return h;
}

/* Every pixel opaque gray: the lowest and highest gray in *low and *high. */
static bool all_gray(wgf_image_t image, int *low, int *high)
{
    const int w = wgf_image_get_width(image), h = wgf_image_get_height(image);
    int x, y;
    *low = 255;
    *high = 0;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const wgf_color_t c = wgf_image_get_pixel(image, x, y);
            const int r = wgf_color_get_red(c);
            if (r != wgf_color_get_green(c) || r != wgf_color_get_blue(c) || wgf_color_get_alpha(c) != 255)
                return false;
            if (r < *low) *low = r;
            if (r > *high) *high = r;
        }
    }
    return w > 0;
}

static bool column_is(wgf_image_t image, int x, wgf_color_t color)
{
    int y;
    for (y = 0; y < wgf_image_get_height(image); y++) {
        if (wgf_image_get_pixel(image, x, y) != color) return false;
    }
    return true;
}

static bool row_is(wgf_image_t image, int y, wgf_color_t color)
{
    int x;
    for (x = 0; x < wgf_image_get_width(image); x++) {
        if (wgf_image_get_pixel(image, x, y) != color) return false;
    }
    return true;
}

static void generated(void)
{
    const wgf_color_t red = 0xFF0000FFu, blue = 0x0000FFFFu, black = 0x000000FFu, white = 0xFFFFFFFFu;
    wgf_image_t image;
    int x, y, low, high;

    image = wgf_image_create_color(3, 2, red);
    expect(image != 0 && wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_READY, "a color: READY at once");
    expect(strcmp(wgf_handle_get_kind_name(image), "gfx.image") == 0, "a handle of kind gfx.image");
    expect(wgf_image_get_width(image) == 3 && wgf_image_get_height(image) == 2 && wgf_image_get_size(image) == 24,
           "its size, 4 bytes a pixel");
    expect(row_is(image, 0, red) && row_is(image, 1, red), "every pixel the color");
    expect(memcmp(wgf_image_get_data(image), "\xFF\x00\x00\xFF", 4) == 0, "its bytes RGBA");
    expect(strcmp(wgf_resource_get_path(image), "") == 0, "no path");
    wgf_resource_release(image);
    expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_NONE && wgf_image_get_width(image) == 0 &&
               wgf_image_get_data(image) == NULL,
           "released: gone");

    /* linear gradients: their ends exact, every angle */
    image = wgf_image_create_gradient_linear(5, 3, 0.0f, black, white);
    expect(column_is(image, 0, black) && column_is(image, 4, white), "0 degrees: left to right");
    expect(wgf_image_get_pixel(image, 2, 1) == 0x808080FFu, "halfway, half (rounded)");
    wgf_resource_release(image);
    image = wgf_image_create_gradient_linear(5, 3, 90.0f, black, white);
    expect(row_is(image, 0, black) && row_is(image, 2, white), "90 degrees: top to bottom");
    wgf_resource_release(image);
    image = wgf_image_create_gradient_linear(5, 3, 180.0f, red, blue);
    expect(column_is(image, 0, blue) && column_is(image, 4, red), "180 degrees: right to left");
    wgf_resource_release(image);
    image = wgf_image_create_gradient_linear(6, 4, 45.0f, red, blue);
    expect(wgf_image_get_pixel(image, 0, 0) == red && wgf_image_get_pixel(image, 5, 3) == blue,
           "45 degrees: top-left to bottom-right");
    wgf_resource_release(image);
    image = wgf_image_create_gradient_linear(1, 1, 30.0f, red, blue);
    expect(wgf_image_get_pixel(image, 0, 0) == red, "one pixel: the start");
    wgf_resource_release(image);

    /* radial and square: the middle inner, the edges outer */
    image = wgf_image_create_gradient_radial(9, 9, 0.0f, white, black);
    expect(wgf_image_get_pixel(image, 4, 4) == white, "radial: the middle inner");
    expect(wgf_image_get_pixel(image, 0, 0) == black && wgf_image_get_pixel(image, 8, 8) == black &&
               wgf_image_get_pixel(image, 4, 0) == black && wgf_image_get_pixel(image, 0, 4) == black,
           "the corners and the sides' middles outer");
    x = wgf_color_get_red(wgf_image_get_pixel(image, 6, 4));
    expect(x == 128, "halfway out, half");
    wgf_resource_release(image);
    image = wgf_image_create_gradient_radial(9, 9, 0.5f, white, black);
    expect(wgf_image_get_pixel(image, 6, 4) == white && wgf_image_get_pixel(image, 7, 4) != white,
           "radial: solid within its density");
    wgf_resource_release(image);
    image = wgf_image_create_gradient_square(9, 7, 0.0f, white, black);
    expect(wgf_image_get_pixel(image, 4, 3) == white, "square: the middle inner");
    expect(row_is(image, 0, black) && row_is(image, 6, black) && column_is(image, 0, black) &&
               column_is(image, 8, black),
           "every edge pixel outer");
    wgf_resource_release(image);

    /* checks */
    image = wgf_image_create_checked(6, 4, 2, 2, red, blue);
    expect(wgf_image_get_pixel(image, 0, 0) == red && wgf_image_get_pixel(image, 1, 1) == red &&
               wgf_image_get_pixel(image, 2, 0) == blue && wgf_image_get_pixel(image, 0, 2) == blue &&
               wgf_image_get_pixel(image, 2, 2) == red && wgf_image_get_pixel(image, 5, 3) == blue,
           "checks of 2 by 2, the first at the top-left");
    wgf_resource_release(image);

    /* noise: the same for a seed, on every platform */
    {
        const wgf_image_t a = wgf_image_create_white_noise(64, 64, 0.5f, 7),
                          b = wgf_image_create_white_noise(64, 64, 0.5f, 7);
        const wgf_image_t c = wgf_image_create_white_noise(64, 64, 0.5f, 8);
        int whites = 0;
        expect(memcmp(wgf_image_get_data(a), wgf_image_get_data(b), 64 * 64 * 4) == 0 &&
                   memcmp(wgf_image_get_data(a), wgf_image_get_data(c), 64 * 64 * 4) != 0,
               "white noise: the same for a seed, another for another");
        for (y = 0; y < 64; y++) {
            for (x = 0; x < 64; x++) {
                const wgf_color_t p = wgf_image_get_pixel(a, x, y);
                expect(p == black || p == white, "white noise: black or white");
                whites += p == white;
            }
        }
        expect(whites > 64 * 64 * 4 / 10 && whites < 64 * 64 * 6 / 10, "about half white at 0.5");
        expect(hash_of(a) == 0x46c0024cu, "white noise: the same pixels as every platform's");
        wgf_resource_release(a);
        wgf_resource_release(b);
        wgf_resource_release(c);
        image = wgf_image_create_white_noise(8, 8, 0.0f, 1);
        expect(all_gray(image, &low, &high) && high == 0, "factor 0: all black");
        wgf_resource_release(image);
        image = wgf_image_create_white_noise(8, 8, 2.0f, 1);
        expect(all_gray(image, &low, &high) && low == 255, "factor over 1, clamped: all white");
        wgf_resource_release(image);
    }
    {
        const wgf_image_t a = wgf_image_create_perlin_noise(64, 48, 0, 0, 4.0f, 3);
        const wgf_image_t b = wgf_image_create_perlin_noise(64, 48, 0, 0, 4.0f, 3);
        const wgf_image_t c = wgf_image_create_perlin_noise(64, 48, 0, 0, 4.0f, 4);
        const wgf_image_t shifted = wgf_image_create_perlin_noise(64, 48, 32, -16, 4.0f, 3);
        bool tiles = true;
        expect(hash_of(a) == hash_of(b) && hash_of(a) != hash_of(c),
               "Perlin noise: the same for a seed, another for another");
        expect(all_gray(a, &low, &high) && low < 96 && high > 160, "opaque gray, spread over the range");
        for (y = 16; y < 48; y++) {
            for (x = 0; x < 32; x++) {
                if (wgf_image_get_pixel(shifted, x, y) != wgf_image_get_pixel(a, x + 32, y - 16)) tiles = false;
            }
        }
        expect(tiles, "offset by pixels: the same noise further along");
        expect(hash_of(a) == 0x6680a81du, "Perlin noise: the same pixels as every platform's");
        wgf_resource_release(a);
        wgf_resource_release(b);
        wgf_resource_release(c);
        wgf_resource_release(shifted);
    }
    {
        const wgf_image_t a = wgf_image_create_cellular(64, 64, 16, 5), b = wgf_image_create_cellular(64, 64, 16, 5);
        const wgf_image_t c = wgf_image_create_cellular(64, 64, 16, 6);
        expect(hash_of(a) == hash_of(b) && hash_of(a) != hash_of(c), "cells: the same for a seed, another for another");
        expect(all_gray(a, &low, &high) && low == 0 && high > 128, "opaque gray, black at its points");
        expect(hash_of(a) == 0xa41f3f45u, "cells: the same pixels as every platform's");
        wgf_resource_release(a);
        wgf_resource_release(b);
        wgf_resource_release(c);
    }

    /* refused */
    expect(wgf_image_create_color(0, 4, red) == 0 && wgf_image_create_color(4, -1, red) == 0 &&
               wgf_image_create_color(16385, 1, red) == 0 &&
               wgf_image_create_gradient_linear(0, 1, 0.0f, red, blue) == 0 &&
               wgf_image_create_gradient_radial(1, 0, 0.0f, red, blue) == 0 &&
               wgf_image_create_gradient_square(-3, 3, 0.0f, red, blue) == 0 &&
               wgf_image_create_white_noise(0, 0, 0.5f, 1) == 0 && wgf_image_create_cellular(4, 0, 2, 1) == 0,
           "a side of 0 or less, or over 16384: refused");
    expect(wgf_image_create_checked(4, 4, 0, 1, red, blue) == 0 &&
               wgf_image_create_checked(4, 4, 1, -1, red, blue) == 0,
           "checks of 0 or less: refused");
    expect(wgf_image_create_perlin_noise(4, 4, 0, 0, 0.0f, 1) == 0 &&
               wgf_image_create_perlin_noise(4, 4, 0, 0, NAN, 1) == 0 &&
               wgf_image_create_perlin_noise(4, 4, 0, 0, 5000.0f, 1) == 0,
           "a scale of 0, NaN, or over 4096: refused");
    expect(wgf_image_create_cellular(4, 4, 0, 1) == 0, "a tile of 0: refused");
}

static void pixels(void)
{
    const wgf_image_t image = wgf_image_create_color(4, 3, 0x000000FFu);
    const wgf_color_t green = 0x00FF00FFu;
    const unsigned char *data = wgf_image_get_data(image);
    expect(wgf_image_set_pixel(image, 1, 2, green) && wgf_image_get_pixel(image, 1, 2) == green, "a pixel set, read");
    expect(memcmp(data + (2 * 4 + 1) * 4, "\x00\xFF\x00\xFF", 4) == 0, "in its bytes, rows from the top");
    expect(wgf_image_set_pixel(image, 3, 0, 0x12345678u) && wgf_image_get_pixel(image, 3, 0) == 0x12345678u,
           "any color, its alpha too");
    expect(!wgf_image_set_pixel(image, -1, 0, green) && !wgf_image_set_pixel(image, 4, 0, green) &&
               !wgf_image_set_pixel(image, 0, 3, green) && !wgf_image_set_pixel(0, 0, 0, green) &&
               !wgf_image_set_pixel(12345, 0, 0, green),
           "outside it, or not an image: refused");
    expect(wgf_image_get_pixel(image, 4, 0) == 0 && wgf_image_get_pixel(image, 0, -1) == 0 &&
               wgf_image_get_pixel(0, 0, 0) == 0,
           "read outside it: 0");
    expect(wgf_image_get_width(0) == 0 && wgf_image_get_size(12345) == 0 && wgf_image_get_data(0) == NULL,
           "getters of what isn't an image: 0");
    wgf_resource_release(image);
}

static void files(void)
{
    const unsigned char junk[] = "this is not an image";
    const wgf_image_t made = wgf_image_create_checked(5, 3, 1, 1, 0xFF8000FFu, 0x10204080u);
    wgf_image_t rock, again, broken, missing, saved;
    wgf_fs_task_t task;

    write_file("image-test/rock.png", rock_png, (int)sizeof(rock_png));
    write_file("image-test/junk.png", junk, (int)sizeof(junk));

    rock = wgf_image_create("image-test/rock.png");
    expect(rock != 0 && wgf_resource_get_status(rock) == WGF_RESOURCE_STATUS_PENDING, "loading: PENDING at once");
    expect(wgf_image_get_width(rock) == 0 && wgf_image_get_data(rock) == NULL && wgf_image_get_pixel(rock, 0, 0) == 0 &&
               !wgf_image_set_pixel(rock, 0, 0, 0xFFFFFFFFu) && wgf_image_save(rock, "image-test/no.png") == 0 &&
               wgf_texture_create_from_image(rock) == 0,
           "PENDING: no pixels, nothing set, saved, or made a texture");
    again = wgf_image_create("./image-test/rock.png");
    expect(again == rock, "the same path is the same image");
    wgf_resource_release(again);
    settle(&rock, 1);
    expect(wgf_resource_get_status(rock) == WGF_RESOURCE_STATUS_READY && wgf_image_get_width(rock) == 2 &&
               wgf_image_get_height(rock) == 2,
           "then READY, at its size");
    expect(wgf_image_get_pixel(rock, 0, 0) == 0xFF0000FFu &&
               wgf_color_get_alpha(wgf_image_get_pixel(rock, 1, 0)) == 0 &&
               wgf_image_get_pixel(rock, 0, 1) == 0x0000FFFFu && wgf_image_get_pixel(rock, 1, 1) == 0xFFFFFFFFu,
           "its pixels, rows from the top, one see-through");
    expect(strcmp(wgf_resource_get_path(rock), "image-test/rock.png") == 0, "the file it read");

    broken = wgf_image_create("image-test/junk.png");
    missing = wgf_image_create("image-test/missing.png");
    {
        const wgf_image_t both[2] = {broken, missing};
        settle(both, 2);
    }
    expect(wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_FAILED, "a file that isn't an image FAILED");
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "a missing file FAILED");
    expect(wgf_image_get_width(broken) == 0 && wgf_image_get_data(missing) == NULL, "with no pixels");

    /* saved: a PNG stb_image reads back as it was, and an image loads from it */
    task = wgf_image_save(made, "image-test/made.png");
    expect(task != 0 && wait_task(task) == WGF_FS_TASK_STATUS_DONE, "saved as a PNG");
    wgf_fs_task_destroy(task);
    expect(wgf_image_save(made, NULL) == 0 && wgf_image_save(0, "image-test/none.png") == 0,
           "no path, or no image: not saved");
    task = wgf_fs_read("image-test/made.png");
    if (wait_task(task) == WGF_FS_TASK_STATUS_DONE) {
        int w = 0, h = 0, comp = 0;
        unsigned char *decoded =
            stbi_load_from_memory(wgf_fs_task_get_data(task), wgf_fs_task_get_size(task), &w, &h, &comp, 4);
        expect(decoded != NULL && w == 5 && h == 3 && memcmp(decoded, wgf_image_get_data(made), 5 * 3 * 4) == 0,
               "stb_image reads the same pixels back");
        stbi_image_free(decoded);
    } else {
        expect(false, "the saved file read");
    }
    wgf_fs_task_destroy(task);
    saved = wgf_image_create("image-test/made.png");
    settle(&saved, 1);
    expect(wgf_image_get_size(saved) == 5 * 3 * 4 &&
               memcmp(wgf_image_get_data(saved), wgf_image_get_data(made), 60) == 0,
           "and an image loaded from it the same");

    /* loaded again in place: the file's new pixels and size; a broken file keeps them */
    task = wgf_image_save(made, "image-test/rock.png");
    expect(task != 0 && wait_task(task) == WGF_FS_TASK_STATUS_DONE, "another picture saved over the file");
    wgf_fs_task_destroy(task);
    expect(wgf_asset_can_reload("image-test/rock.png") && wgf_asset_reload("image-test/rock.png") == 1,
           "an image loads again");
    settle_reload(rock);
    expect(wgf_resource_get_status(rock) == WGF_RESOURCE_STATUS_READY && wgf_image_get_width(rock) == 5 &&
               wgf_image_get_height(rock) == 3 &&
               memcmp(wgf_image_get_data(rock), wgf_image_get_data(made), 60) == 0,
           "in place: the same handle, the new file's size and pixels");
    write_file("image-test/rock.png", junk, (int)sizeof(junk));
    expect(wgf_asset_reload("image-test/rock.png") == 1, "a broken file, loading again");
    settle_reload(rock);
    expect(wgf_resource_get_status(rock) == WGF_RESOURCE_STATUS_READY && wgf_image_get_width(rock) == 5,
           "what it had is kept");

    wgf_resource_release(saved);
    wgf_resource_release(made);
    wgf_resource_release(broken);
    wgf_resource_release(missing);
    wgf_resource_release(rock);
    {
        static const char *const paths[] = {"image-test/rock.png", "image-test/junk.png", "image-test/made.png"};
        int i;
        for (i = 0; i < 3; i++) {
            task = wgf_fs_remove(paths[i]);
            wait_task(task);
            wgf_fs_task_destroy(task);
        }
        task = wgf_fs_rmdir("image-test");
        wait_task(task);
        wgf_fs_task_destroy(task);
    }
}

static void textures(void)
{
    const wgf_image_t image = wgf_image_create_gradient_linear(8, 4, 0.0f, 0x000000FFu, 0xFFFFFFFFu);
    const wgf_image_t other = wgf_image_create_color(4, 4, 0xFFFFFFFFu);
    wgf_texture_t texture, target;
    wgf_texture_read_t read;

    expect(wgf_texture_create_from_image(image) == 0, "no texture before gfx runs");
    expect(wgf_gfx_priv_start(), "gfx starts");
    texture = wgf_texture_create_from_image(image);
    expect(texture != 0 && wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY &&
               wgf_texture_get_width(texture) == 8 && wgf_texture_get_height(texture) == 4,
           "a texture from an image: READY, at its size");
    expect(!wgf_texture_write_image(texture, image), "written once in the frame it is made");
    wgf_platform_priv_set_size(64, 64);
    wgf_gfx_priv_begin_frame();
    wgf_gfx_priv_end_frame();
    expect(wgf_texture_write_image(texture, image), "written again the next frame");
    wgf_gfx_priv_begin_frame();
    wgf_gfx_priv_end_frame();
    expect(!wgf_texture_write_image(texture, other) && !wgf_texture_write_image(texture, 0) &&
               !wgf_texture_write_image(0, image),
           "an image not its size, no image, or no texture: refused");
    target = wgf_texture_create_target(8, 4);
    expect(!wgf_texture_write_image(target, image), "a texture not made in code: refused");
    expect(wgf_texture_create_from_image(0) == 0 && wgf_texture_create_from_image(texture) == 0,
           "a texture from what isn't an image: none");

    read = wgf_texture_read(texture);
    expect(read != 0 && wgf_image_create_from_read(read) == 0, "an image of a read not DONE: none");
    wgf_texture_read_destroy(read);
    expect(wgf_image_create_from_read(0) == 0, "nor of no read");

    wgf_resource_release(target);
    wgf_resource_release(texture);
    wgf_resource_release(other);
    /* `image` is left for gfx's stop, which frees every image */
}

int main(void)
{
    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_FATAL); /* the refusals below log, on purpose */
    generated();
    pixels();
    files();
    textures();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
