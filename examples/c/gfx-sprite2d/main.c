#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Screen-space sprites in a canvas:
 *   - "sheet": the 256x256 logo treated as a 2x2 sprite sheet; its source steps
 *     through the four quadrants like animation frames
 *   - "spin": turns about its center (the default pivot)
 *   - "swing": turns about its top-left corner (pivot 0, 0)
 *   - "flip": mirrored by a negative x scale
 *   - "tint": a white copy of the logo cycling through tint colors (a tint multiplies
 *     the texture's color, so it can't show on the black logo)
 *   - a one-off wgf_draw_texture in the corner (no node)
 * Hovering enlarges the sprite under the pointer, the topmost (the last drawn) first.
 * Positions are logical pixels.
 *
 *   Esc   quit, where quitting means anything
 *
 * libwgt's gfx-sprite2d (wgrender's sprite2d) done 1:1, so the two compare in the size
 * table: the same window, sprites, motion, and text. Where it differs, and why:
 *   - "libwgt" reads "libwgf" in the window's title and the heading: the library's name.
 *   - No 3D scene behind the sprites (the animated character, its camera, sun, and
 *     ambient light), and so no "model" to hover: libwgf is 2D only. The sprites are
 *     over the clear color.
 *   - Hovering is the example's own test of the pointer against each sprite, where
 *     libwgt's pointer picking (wgt_pointer.h) finds it: libwgf has no picking. Nor can
 *     it read a texture's alpha (libwgt's pick alpha threshold), so a whole logo is hit
 *     inside its gear's disc, and the sheet's quarter inside its rectangle: close to
 *     what the threshold lets through.
 *   - The default font draws the text; the frame rate isn't drawn, as in libwgt's. */

#define LOGO_PATH "sprites/logo/wg-logo-bw-alpha.png"
#define WHITE_LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"

enum { SPRITE_COUNT = 5, SHEET_SPRITE = 0, FLIP_SPRITE = 3, TINT_SPRITE = 4, PALETTE_SIZE = 24 };

static const char *names[SPRITE_COUNT] = {"sheet", "spin", "swing", "flip", "tint"};

static struct {
    wgf_node_t canvas;
    wgf_texture_t logo;
    wgf_color_t palette[PALETTE_SIZE];
    wgf_node_t sprites[SPRITE_COUNT];
    float angle[SPRITE_COUNT]; /* each one's rotation about z, as set */
    float grow[SPRITE_COUNT];  /* each one's scale, its mirror aside, as set */
    float time;
    int frame;
} g;

static void init(void *user)
{
    wgf_texture_t white;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(28, 30, 38, 255));
    for (i = 0; i < PALETTE_SIZE; i++) { /* colors are values, so cycle a palette */
        const float a = (float)i / PALETTE_SIZE * 6.2831853f;
        g.palette[i] = wgf_color_make((int)(127 + 127 * sinf(a)), (int)(127 + 127 * sinf(a + 2.1f)),
                                      (int)(127 + 127 * sinf(a + 4.2f)), 255);
    }

    g.canvas = wgf_canvas_create();
    g.logo = wgf_texture_create(LOGO_PATH); /* kept for the one-off draw */
    white = wgf_texture_create(WHITE_LOGO_PATH);
    for (i = 0; i < SPRITE_COUNT; i++) {
        g.sprites[i] = wgf_sprite_create(i == TINT_SPRITE ? white : g.logo);
        wgf_sprite_set_size(g.sprites[i], 128, 128);
        wgf_node_set_parent(g.sprites[i], g.canvas);
        g.grow[i] = 1.0f;
    }
    wgf_resource_release(white); /* the sprite holds its own reference */
    wgf_node_set_position(g.sprites[0], 140, 170, 0);
    wgf_node_set_position(g.sprites[1], 140, 380, 0);
    wgf_sprite_set_pivot(g.sprites[2], 0, 0);
    wgf_node_set_position(g.sprites[2], 700, 110, 0);
    wgf_sprite_set_size(g.sprites[2], 96, 96);
    wgf_node_set_position(g.sprites[FLIP_SPRITE], 760, 400, 0);
    wgf_node_set_scale(g.sprites[FLIP_SPRITE], -1, 1, 1);
    wgf_node_set_position(g.sprites[TINT_SPRITE], 450, 110, 0);
    wgf_sprite_set_size(g.sprites[TINT_SPRITE], 96, 96);
}

/* Whether (x, y) is on sprite `i`, as it was last placed: the point taken into the
 * sprite's own units, then inside its gear's disc, or the sheet's quarter inside its
 * rectangle. A mirror doesn't change what it covers. */
static bool is_over(int i, float x, float y)
{
    const wgf_vec3_t position = wgf_node_get_position(g.sprites[i]);
    const wgf_vec2_t size = wgf_sprite_get_size(g.sprites[i]);
    const wgf_vec2_t pivot = wgf_sprite_get_pivot(g.sprites[i]);
    const float c = cosf(g.angle[i]), s = sinf(g.angle[i]);
    const float dx = x - position.x, dy = y - position.y;
    /* Undo the turn, then the scale; then from the pivot to the sprite's center. */
    const float lx = (c * dx + s * dy) / g.grow[i] - (0.5f - pivot.x) * size.x;
    const float ly = (-s * dx + c * dy) / g.grow[i] - (0.5f - pivot.y) * size.y;
    if (size.x <= 0.0f || size.y <= 0.0f) return false;
    if (i == SHEET_SPRITE) return fabsf(lx) <= size.x * 0.5f && fabsf(ly) <= size.y * 0.5f;
    return lx * lx + ly * ly <= size.x * size.x * 0.25f;
}

static void frame(void *user)
{
    const wgf_vec2_t mouse = wgf_mouse_get_position();
    const char *hover_name = "nothing";
    char line[128];
    int hovered = -1, i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    g.time += wgf_loop_get_frame_delta();

    /* Sprite sheet: one quadrant of the 256x256 texture per animation frame. */
    g.frame = (int)(g.time * 2.0f) % 4;
    wgf_sprite_set_source(g.sprites[0], (float)(g.frame % 2) * 128, (float)(g.frame / 2) * 128, 128, 128);
    g.angle[1] = g.time;
    g.angle[2] = sinf(g.time * 1.5f) * 0.8f;
    wgf_node_set_rotation(g.sprites[1], 0, 0, g.angle[1]);
    wgf_node_set_rotation(g.sprites[2], 0, 0, g.angle[2]);
    wgf_sprite_set_tint(g.sprites[TINT_SPRITE], g.palette[(int)(g.time * 6.0f) % PALETTE_SIZE]);

    /* Hover: the topmost sprite under the pointer, the last drawn first. */
    for (i = SPRITE_COUNT - 1; i >= 0 && hovered < 0; i--) {
        if (is_over(i, mouse.x, mouse.y)) hovered = i;
    }
    for (i = 0; i < SPRITE_COUNT; i++) {
        g.grow[i] = i == hovered ? 1.15f : 1.0f;
        /* "flip" keeps its mirror. */
        wgf_node_set_scale(g.sprites[i], i == FLIP_SPRITE ? -g.grow[i] : g.grow[i], g.grow[i], 1);
        if (i == hovered) hover_name = names[i];
    }

    wgf_canvas_draw(g.canvas);
    wgf_draw_texture(g.logo, (float)wgf_window_get_width() - 74, 10, 64, 64, WGF_COLOR_WHITE); /* one-off, no node */

    wgf_draw_text(0, "libwgf sprite2d: source rect, pivot, rotation, flip, picking", 12, 12, 16, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "mouse (%d, %d)  hover: %s  sheet frame %d", (int)mouse.x, (int)mouse.y, hover_name,
             g.frame);
    wgf_draw_text(0, line, 12, 36, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf sprite2d");
    wgf_window_set_size(900, 520);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
