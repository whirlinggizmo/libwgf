#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "wgf_web_canvas.h"
#define finish emscripten_force_exit
#else
#define finish exit
#endif

#include "sprite/wgf_gfx_sprite_run_priv.h"
#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_image.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* 2D sprites drawn in runs (wgf_gfx_sprite_run.c) blend as drawn one by one: overlapping
 * see-through sprites over 4 textures and 4 alpha modes, drawn by a stage into a render
 * target and read back, each pixel what the blend gives drawing them in the stage's order,
 * worked out here, within 2 of 255. The first three, of three textures, are one run, so
 * their overlaps show a run keeps its sprites' order; an ADD, a BLEND of a see-through
 * texture, a MULTIPLY, and an OPAQUE each begin a run of their own. In a window
 * (tools/run_in_xvfb.py, OpenGL on a virtual display, where a target is drawn flipped);
 * wgf_gfx_sprite_run_web_test is this file on WebGL2. */

#define WIDTH 64
#define HEIGHT 16
#define CLEAR 0x404040FFu

typedef struct sprite_t {
    int texture; /* of textures[] */
    float x, y, w, h;
    wgf_color_t tint;
    wgf_alpha_mode_t mode;
} sprite_t;

static const wgf_color_t colors[4] = {0xC82828FFu, 0x28C828FFu, 0x2828C8FFu, 0xF0F02880u}; /* the last see-through */
static const sprite_t sprites[] = {
    {0, 0, 0, 24, 8, 0xFFFFFF80u, WGF_ALPHA_MODE_BLEND},
    {2, 8, 0, 24, 8, 0xFFFFFFA0u, WGF_ALPHA_MODE_BLEND},
    {1, 16, 0, 24, 8, 0xFFFFFF60u, WGF_ALPHA_MODE_BLEND},
    {0, 36, 0, 12, 8, 0xFFFFFF80u, WGF_ALPHA_MODE_ADD},
    {3, 44, 0, 12, 8, 0xFFFFFFFFu, WGF_ALPHA_MODE_BLEND},
    {2, 52, 0, 12, 8, 0xC0C0C0FFu, WGF_ALPHA_MODE_MULTIPLY},
    {1, 0, 8, 8, 8, 0xFFFFFF40u, WGF_ALPHA_MODE_OPAQUE},
};
#define SPRITES ((int)(sizeof(sprites) / sizeof(sprites[0])))

static int failures, frames;
static wgf_texture_t textures[4], target;
static wgf_actor_t stage;
static wgf_texture_read_t read;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static float channel(wgf_color_t color, int c)
{
    return (float)((color >> (24 - 8 * c)) & 0xFFu) / 255.0f;
}

/* A framebuffer's 8 bits a channel. */
static float stored(float value)
{
    value = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    return floorf(value * 255.0f + 0.5f) / 255.0f;
}

/* The pixel at (x, y), worked out: the clear color, then each sprite covering it in turn,
 * texture x tint blended as its mode's pipeline does, stored each time. */
static void expected(int x, int y, float out[4])
{
    int i, c;
    for (c = 0; c < 4; c++) out[c] = channel(CLEAR, c);
    for (i = 0; i < SPRITES; i++) {
        const sprite_t *s = &sprites[i];
        float src[4], a;
        if ((float)x + 0.5f < s->x || (float)x + 0.5f > s->x + s->w || (float)y + 0.5f < s->y ||
            (float)y + 0.5f > s->y + s->h) {
            continue;
        }
        for (c = 0; c < 4; c++) src[c] = channel(colors[s->texture], c) * channel(s->tint, c);
        a = src[3];
        for (c = 0; c < 3; c++) {
            switch (s->mode) {
            case WGF_ALPHA_MODE_ADD: out[c] = src[c] * a + out[c]; break;
            case WGF_ALPHA_MODE_MULTIPLY: out[c] = src[c] * out[c] + out[c] * (1.0f - a); break;
            case WGF_ALPHA_MODE_OPAQUE: out[c] = src[c]; break;
            default: out[c] = src[c] * a + out[c] * (1.0f - a); break;
            }
        }
        out[3] = s->mode == WGF_ALPHA_MODE_OPAQUE ? a
                 : s->mode == WGF_ALPHA_MODE_ADD  ? a + out[3]
                                                  : a + out[3] * (1.0f - a);
        for (c = 0; c < 4; c++) out[c] = stored(out[c]);
    }
}

static void expect_pixel(int x, int y, const char *what)
{
    const unsigned char *rgba = wgf_texture_read_get_data(read) + ((size_t)y * WIDTH + (size_t)x) * 4u;
    float want[4];
    int c, off = 0;
    expected(x, y, want);
    for (c = 0; c < 4; c++) {
        if (abs((int)rgba[c] - (int)(want[c] * 255.0f + 0.5f)) > 2) off = 1;
    }
    if (off) {
        printf("FAIL: %s: at %d, %d read %d %d %d %d, expected %.0f %.0f %.0f %.0f\n", what, x, y, rgba[0], rgba[1],
               rgba[2], rgba[3], want[0] * 255.0f, want[1] * 255.0f, want[2] * 255.0f, want[3] * 255.0f);
        failures++;
    }
}

static void on_frame(void *user)
{
    (void)user;
    frames++;
    if (frames == 2) { /* the first frame's drawing may wait on the window */
        wgf_render_begin_target(target);
        wgf_stage2d_draw(stage);
        wgf_render_end_target();
        read = wgf_texture_read(target);
    } else if (frames == 3) {
        expect(wgf_gfx_priv_sprite_run_get_drawn() == SPRITES, "every sprite drawn in a run");
        expect(wgf_gfx_priv_sprite_run_get_draw_calls() == 5,
               "5 draws: three see-through sprites of three textures, then an ADD, a BLEND, a MULTIPLY, an OPAQUE");
        expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_DONE &&
                   wgf_texture_read_get_width(read) == WIDTH && wgf_texture_read_get_height(read) == HEIGHT,
               "the target read");
        if (wgf_texture_read_get_data(read) != NULL) {
            expect_pixel(4, 4, "the first alone");
            expect_pixel(12, 4, "the second over the first");
            expect_pixel(20, 4, "the third over both, in the same run");
            expect_pixel(28, 4, "the third over the second");
            expect_pixel(34, 4, "the third alone");
            expect_pixel(38, 4, "an ADD over the third");
            expect_pixel(42, 4, "the ADD alone");
            expect_pixel(46, 4, "a see-through texture over the ADD");
            expect_pixel(50, 4, "the see-through texture alone");
            expect_pixel(54, 4, "a MULTIPLY over it");
            expect_pixel(60, 4, "the MULTIPLY alone");
            expect_pixel(4, 12, "an OPAQUE: its color and alpha as they are");
            expect_pixel(30, 12, "the clear color, where none is");
        }
        wgf_texture_read_destroy(read);
        finish(failures == 0 ? 0 : 1);
    }
}

static void init(void *user)
{
    int i;
    (void)user;
    target = wgf_texture_create_target(WIDTH, HEIGHT);
    wgf_render_set_target_clear_color(target, CLEAR);
    for (i = 0; i < 4; i++) {
        const wgf_image_t image = wgf_image_create_color(4, 4, colors[i]);
        textures[i] = wgf_texture_create_from_image(image);
        wgf_resource_release(image);
    }
    stage = wgf_stage2d_create();
    for (i = 0; i < SPRITES; i++) {
        const sprite_t *s = &sprites[i];
        const wgf_actor_t sprite = wgf_sprite_create(textures[s->texture]);
        wgf_sprite_set_pivot(sprite, 0, 0);
        wgf_sprite_set_size(sprite, s->w, s->h);
        wgf_sprite_set_tint(sprite, s->tint);
        wgf_sprite_set_alpha_mode(sprite, s->mode, 0.5f);
        wgf_actor_set_position(sprite, s->x, s->y, 0);
        wgf_actor_set_parent(sprite, stage);
    }
}

int main(void)
{
    wgf_window_set_size(64, 32);
#ifdef __EMSCRIPTEN__
    web_canvas_set_size(64, 32); /* the page sizes the canvas */
#endif
    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
