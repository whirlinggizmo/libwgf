#include <stdio.h>
#include <stdlib.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_material.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_window.h"

/* Screen effects, in a browser (tools/run_in_browser.py) on a WebGL2 canvas, their
 * pixels read back: a 64 by 32 frame, a yellow square at its top-left and the rest black,
 * through the tests' invert effect (libwgf/gfx/tests/shaders/invert.glsl). The frame as it
 * is while the shader loads; then one effect, inverted, the square still at the top-left;
 * two (one between textures, the last onto the screen), inverted back and nothing moved;
 * three, inverted again; the last's parameter 0, the frame as it is; and cleared, the
 * frame alone. */

static int failures;
static wgf_material_t invert[3];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The pixel at (x, y) from the top-left of the 32-pixel-high canvas, its channels within
 * `slack` of the color's. */
static void expect_pixel(int x, int y, wgf_color_t color, int slack, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, 31 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (abs(rgba[0] - (int)wgf_color_get_red(color)) > slack ||
        abs(rgba[1] - (int)wgf_color_get_green(color)) > slack ||
        abs(rgba[2] - (int)wgf_color_get_blue(color)) > slack) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

/* One frame: the square, and the frame drawn through whatever effects there are. */
static void draw(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_draw_rectangle(0, 0, 8, 8, wgf_color_make(255, 255, 0, 255));
    wgf_gfx_priv_end_frame();
}

static void expect_frame(bool inverted, const char *what)
{
    char line[160];
    snprintf(line, sizeof(line), "%s: the square at the top-left", what);
    expect_pixel(2, 2, inverted ? wgf_color_make(0, 0, 255, 255) : wgf_color_make(255, 255, 0, 255), 1, line);
    snprintf(line, sizeof(line), "%s: the rest", what);
    expect_pixel(2, 28, inverted ? WGF_COLOR_WHITE : WGF_COLOR_BLACK, 1, line);
    snprintf(line, sizeof(line), "%s: the far corner", what);
    expect_pixel(60, 28, inverted ? WGF_COLOR_WHITE : WGF_COLOR_BLACK, 1, line);
}

static int frames;

static void on_frame(void *user)
{
    wgf_shader_t shader;
    (void)user;
    frames++;
    if (wgf_resource_get_status(wgf_material_get_shader(invert[0])) == WGF_RESOURCE_STATUS_PENDING && frames < 600) {
        draw();
        expect_frame(false, "its shader loading: the frame as it is");
        return;
    }
    shader = wgf_material_get_shader(invert[0]);
    expect(wgf_resource_get_status(shader) == WGF_RESOURCE_STATUS_READY, "loaded");
    draw();
    expect_frame(true, "one effect");

    expect(wgf_render_add_effect(invert[1]), "a second");
    draw();
    expect_frame(false, "two in a chain: inverted back, nothing moved");

    expect(wgf_render_add_effect(invert[2]), "a third");
    expect(wgf_render_get_effect_count() == 3, "three");
    draw();
    expect_frame(true, "three in a chain");

    wgf_material_set_float(invert[2], "amount", 0.0f);
    draw();
    expect_frame(false, "the last by none: two in a chain");

    wgf_render_clear_effects();
    expect(wgf_render_get_effect_count() == 0, "cleared");
    draw();
    expect_frame(false, "cleared: the frame alone");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

/* What loads from a file is made once core runs (its file preloaded at /wgf/shaders). */
static void init(void *user)
{
    wgf_shader_t shader;
    int i;
    (void)user;
    shader = wgf_shader_create("shaders/invert.wgfshader");
    for (i = 0; i < 3; i++) {
        invert[i] = wgf_material_create_custom(shader);
        wgf_material_set_float(invert[i], "amount", 1.0f);
    }
    wgf_resource_release(shader); /* the materials hold it */
    expect(wgf_render_add_effect(invert[0]), "an effect added while its shader loads");
    expect(!wgf_render_add_effect(0), "not a material");
}

int main(void)
{
    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
