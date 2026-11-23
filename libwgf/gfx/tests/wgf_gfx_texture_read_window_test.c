#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "wgf_web_canvas.h"
#define finish emscripten_force_exit
#else
#define finish exit
#endif

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_fs.h"
#include "wgf_image.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Render targets read back (wgf_texture_read) over the runtime's own frames, in a window
 * (tools/run_in_xvfb.py, OpenGL on a virtual display) with MSAA, so what is read is the
 * image the samples resolve into; wgf_gfx_texture_read_web_test is this file on WebGL2,
 * without MSAA, so a target drawn into directly. A 32 by 24 target, dark gray, with a
 * yellow square at its top-left and a green
 * one at its bottom-right: a read is PENDING in its frame and DONE in the next, its rows
 * the top first; a read of another target released in its frame still reads it, the read
 * holding it; and one destroyed while PENDING is gone. The screen read (wgf_render_read_screen)
 * as its frame ends, a red square at its top-left, and saved (wgf_texture_read_save), the
 * file written. Images (wgf_image.h): a texture made from one reads back as its pixels, an
 * image made from that read is them again, and the image saved as a PNG and loaded as a
 * texture from the file reads back as them too. */

#define WIDTH 32
#define HEIGHT 24

static int failures;
static int frames;
static wgf_texture_t target, blue;
static wgf_texture_read_t read, blue_read, screen;
static wgf_fs_task_t saved;
static wgf_image_t image;                    /* checks, 16 by 8, one color half see-through, its last pixel lime */
static wgf_texture_t from_image, from_file;  /* made from it; loaded from its PNG */
static wgf_texture_read_t image_read, file_read;
static wgf_fs_task_t image_saved;
static bool file_checked;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A read's pixels are exactly the image's. */
static bool read_is_image(wgf_texture_read_t r)
{
    return wgf_texture_read_get_status(r) == WGF_TEXTURE_READ_STATUS_DONE &&
           wgf_texture_read_get_size(r) == wgf_image_get_size(image) &&
           memcmp(wgf_texture_read_get_data(r), wgf_image_get_data(image), (size_t)wgf_image_get_size(image)) == 0;
}

/* The texture loaded from the image's file, once it is written, then read once READY. */
static void step_file(void)
{
    if (from_file == 0 && wgf_fs_task_get_status(image_saved) == WGF_FS_TASK_STATUS_DONE) {
        from_file = wgf_texture_create("read-test/image.png");
    } else if (from_file != 0 && file_read == 0 && wgf_resource_get_status(from_file) == WGF_RESOURCE_STATUS_READY) {
        file_read = wgf_texture_read(from_file);
    } else if (file_read != 0 && wgf_texture_read_get_status(file_read) != WGF_TEXTURE_READ_STATUS_PENDING) {
        expect(read_is_image(file_read), "a texture loaded from the image's PNG reads back as its pixels");
        wgf_texture_read_destroy(file_read);
        wgf_resource_release(from_file);
        file_checked = true;
    }
}

/* The pixel at (x, y) from the top-left of a read, exactly the color's. */
static void expect_pixel(wgf_texture_read_t r, int x, int y, wgf_color_t color, const char *what)
{
    const unsigned char *rgba = wgf_texture_read_get_data(r) +
        ((size_t)y * (size_t)wgf_texture_read_get_width(r) + (size_t)x) * 4u;
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color) || rgba[3] != wgf_color_get_alpha(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d %d, expected %d %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               rgba[3], wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color),
               wgf_color_get_alpha(color));
        failures++;
    }
}

static void on_frame(void *user)
{
    wgf_texture_read_t dropped;
    (void)user;
    frames++;
    if (!file_checked) step_file();
    if (frames == 2) { /* the first frame's drawing may wait on the window */
        wgf_render_begin_target(target);
        wgf_draw_rectangle(0, 0, 8, 8, WGF_COLOR_YELLOW);
        wgf_draw_rectangle(WIDTH - 8, HEIGHT - 8, 8, 8, WGF_COLOR_GREEN);
        wgf_render_end_target();
        wgf_render_begin_target(blue);
        wgf_render_end_target(); /* cleared to its color */
        wgf_draw_rectangle(0, 0, 8, 8, WGF_COLOR_RED);
        screen = wgf_render_read_screen();
        read = wgf_texture_read(target);
        expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_PENDING, "PENDING in its frame");
        expect(wgf_texture_read_get_data(read) == NULL && wgf_texture_read_get_width(read) == 0,
               "no pixels while PENDING");
        blue_read = wgf_texture_read(blue);
        wgf_resource_release(blue); /* the read holds it */
        dropped = wgf_texture_read(target);
        expect(wgf_texture_read_destroy(dropped), "one destroyed PENDING");
        image_read = wgf_texture_read(from_image);
    } else if (frames == 3) {
        expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_DONE, "DONE in the next frame");
        expect(wgf_texture_read_get_width(read) == WIDTH && wgf_texture_read_get_height(read) == HEIGHT &&
                   wgf_texture_read_get_size(read) == WIDTH * HEIGHT * 4,
               "its size, and 4 bytes a pixel");
        if (wgf_texture_read_get_data(read) != NULL) {
            expect_pixel(read, 2, 2, WGF_COLOR_YELLOW, "the square at the top-left, in the first rows");
            expect_pixel(read, WIDTH - 2, HEIGHT - 2, WGF_COLOR_GREEN, "the square at the bottom-right, in the last");
            expect_pixel(read, WIDTH / 2, HEIGHT / 2, WGF_COLOR_DARKGRAY, "the clear color between");
        }
        expect(wgf_texture_read_get_status(blue_read) == WGF_TEXTURE_READ_STATUS_DONE,
               "a target released in its read's frame still read");
        if (wgf_texture_read_get_data(blue_read) != NULL) {
            expect_pixel(blue_read, 4, 4, WGF_COLOR_BLUE, "what it was cleared to");
        }
        expect(!wgf_handle_is_alive(blue), "and let go of once read");
        expect(wgf_texture_read_destroy(read) && wgf_texture_read_destroy(blue_read), "destroyed");
        expect(wgf_texture_read_get_status(screen) == WGF_TEXTURE_READ_STATUS_DONE &&
                   wgf_texture_read_get_width(screen) == 64 &&
                   wgf_texture_read_get_height(screen) == 32,
               "the screen read as its frame ended, the window's size");
        if (wgf_texture_read_get_data(screen) != NULL) {
            expect_pixel(screen, 2, 2, WGF_COLOR_RED, "the square at the screen's top-left, in the first rows");
            expect_pixel(screen, 60, 28, wgf_render_get_clear_color(), "the clear color at its bottom-right");
        }
        expect(read_is_image(image_read), "a texture made from an image reads back as its pixels");
        {
            const wgf_image_t copy = wgf_image_create_from_read(image_read);
            expect(copy != 0 && wgf_image_get_width(copy) == 16 &&
                       wgf_image_get_size(copy) == wgf_image_get_size(image) &&
                       memcmp(wgf_image_get_data(copy), wgf_image_get_data(image),
                              (size_t)wgf_image_get_size(image)) == 0,
                   "and an image made from the read is them again");
            wgf_resource_release(copy);
        }
        wgf_texture_read_destroy(image_read);
        saved = wgf_texture_read_save(screen, "read-test/shot.png");
        expect(saved != 0, "saved");
        wgf_texture_read_destroy(screen);
    } else if (frames > 3 && wgf_fs_task_get_status(saved) != WGF_FS_TASK_STATUS_PENDING &&
               (file_checked || frames > 600)) {
        expect(wgf_fs_task_get_status(saved) == WGF_FS_TASK_STATUS_DONE, "and written");
        expect(file_checked, "the image's PNG loaded and read in 600 frames");
        wgf_fs_task_destroy(image_saved);
        wgf_fs_task_destroy(saved);
        wgf_fs_task_destroy(wgf_fs_rmdir("read-test"));
        finish(failures == 0 ? 0 : 1);
    }
}

static void init(void *user)
{
    (void)user;
    target = wgf_texture_create_target(WIDTH, HEIGHT);
    wgf_render_set_target_clear_color(target, WGF_COLOR_DARKGRAY);
    blue = wgf_texture_create_target(8, 8);
    wgf_render_set_target_clear_color(blue, WGF_COLOR_BLUE);
    image = wgf_image_create_checked(16, 8, 4, 4, WGF_COLOR_ORANGE, 0x20406080u);
    wgf_image_set_pixel(image, 15, 7, WGF_COLOR_LIME); /* its last pixel: the bottom row last */
    from_image = wgf_texture_create_from_image(image);
    image_saved = wgf_image_save(image, "read-test/image.png");
}

int main(void)
{
    wgf_window_set_size(64, 32);
#ifdef __EMSCRIPTEN__
    web_canvas_set_size(64, 32); /* the page sizes the canvas */
#else
    wgf_window_set_msaa(true);
#endif
    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
