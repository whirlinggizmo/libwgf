#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* Textures from image files, loaded as they are created: each drawn large with smooth
 * (linear) and sharp (nearest) sampling side by side, with its size and how long it
 * took from asking to having it. Below, the logo and the flame again under their
 * "name.ktx" names, as the PNG each falls back to.
 *
 *   Esc   quit, where quitting means anything
 *
 * libwgt's gfx-textures (wgrender's textures) done 1:1, so the two compare in the size
 * table: the same window, files, layout, and text. Where it differs, and why:
 *   - "libwgt" reads "libwgf" in the window's title and the heading: the library's name.
 *   - The compressed half loads the PNGs, not "name.ktx": libwgf has no compressed (KTX)
 *     textures, and examples/assets/ has no .ktx files. libwgt picks "else name.png" on
 *     a GPU with no variant, so this is what libwgt shows there: each captioned
 *     "name.ktx: name.png", sampled smooth, its GPU memory counted as RGBA. The PNG is
 *     the texture already made above (the same path gives the same texture), so its
 *     time is from its own asking to that texture being ready.
 *   - A texture not ready yet draws nothing, where libwgt's draws the magenta and black
 *     checker: libwgf draws the checker only for one that FAILED (wgf_draw.h). */

enum { TEXTURES = 3 };
static const char *PATHS[TEXTURES] = {"sprites/logo/wg-logo-bw-alpha.png", "textures/flame.png",
                                      "textures/tiles.png"};

enum { TILES = 2 };

/* The compressed half: the logo and the flame, named as name.ktx, loaded as the PNG
 * libwgt falls back to. */
enum { COMPRESSED = 2 };
static const char *KTX_PATHS[COMPRESSED] = {"sprites/logo/wg-logo-bw-alpha.ktx", "textures/flame.ktx"};
static const char *FALLBACK_PATHS[COMPRESSED] = {"sprites/logo/wg-logo-bw-alpha.png", "textures/flame.png"};

/* The tile sheet's cells, in its pixels: x, y, width, height (wgrender's
 * tools/gen_tiles.py). Each has a 2 pixel gutter around it repeating its edge, so
 * filtering and mipmaps never reach a neighbour; the gutters aren't drawn. */
static const int CELLS[8][4] = {
    {2, 2, 16, 16},  {22, 2, 16, 16},  {42, 2, 16, 16},  {62, 2, 16, 16}, /* grass, sand, water, stone */
    {2, 22, 16, 32}, {22, 22, 16, 32}, {42, 22, 16, 16}, {62, 22, 16, 16}, /* tree, flag, coin, rock */
};

typedef struct slot_t {
    wgf_texture_t texture;
    double asked, took; /* when it was asked for, and how long it took */
} slot_t;

static slot_t slots[TEXTURES], compressed[COMPRESSED];

/* Under a texture: its file's name (and the file chosen for a .ktx), then its size,
 * the GPU memory it takes, and how long it took, or how it is coming along. */
static void draw_caption(slot_t *slot, const char *path, float x, float y)
{
    char line[160];
    const wgf_resource_status_t status = wgf_resource_get_status(slot->texture);
    if (status == WGF_RESOURCE_STATUS_READY && slot->took == 0.0) slot->took = wgf_time_get_seconds() - slot->asked;
    const char *loaded = wgf_resource_get_path(slot->texture); /* "" until it is READY */
    const bool is_compressed = strstr(loaded, ".ktx") != NULL;
    if (strrchr(loaded, '/') == NULL || strcmp(strrchr(path, '/') + 1, strrchr(loaded, '/') + 1) == 0) {
        wgf_draw_text(0, strrchr(path, '/') + 1, x, y, 16, WGF_COLOR_LIGHTGRAY);
    } else {
        snprintf(line, sizeof(line), "%s: %s", strrchr(path, '/') + 1, strrchr(loaded, '/') + 1); /* the file chosen */
        wgf_draw_text(0, line, x, y, 16, WGF_COLOR_LIGHTGRAY);
    }
    if (status == WGF_RESOURCE_STATUS_READY) {
        /* GPU memory with its mipmaps (a third more): 4 bytes a pixel as RGBA, 1 compressed. */
        const double kb = (double)wgf_texture_get_width(slot->texture) * wgf_texture_get_height(slot->texture) *
                          (is_compressed ? 1.0 : 4.0) * 4.0 / 3.0 / 1024.0;
        snprintf(line, sizeof(line), "%d x %d, GPU %.0f KB, ready in %.0f ms", wgf_texture_get_width(slot->texture),
                 wgf_texture_get_height(slot->texture), kb, slot->took * 1000.0);
    } else {
        snprintf(line, sizeof(line), "%s", status == WGF_RESOURCE_STATUS_FAILED ? "failed" : "loading...");
    }
    wgf_draw_text(0, line, x, y + 22, 14, WGF_COLOR_LIGHTGRAY);
}

/* The sheet's cells, cut out of their gutters and set edge to edge as they were
 * drawn, `width` wide. */
static void draw_cells(wgf_texture_t texture, float x, float y, float width)
{
    const float scale = width / 64.0f; /* the cells span 64 by 48 pixels */
    int c;
    for (c = 0; c < 8; c++) {
        const int *cell = CELLS[c];
        const int column = c % 4, top = c < 4 ? 0 : 16;
        wgf_draw_texture_region(texture, (float)cell[0], (float)cell[1], (float)cell[2], (float)cell[3],
                                x + (float)(column * 16) * scale, y + (float)top * scale, (float)cell[2] * scale,
                                (float)cell[3] * scale, WGF_COLOR_WHITE);
    }
}

/* One texture at `width`: the tile sheet as its cells, anything else whole. */
static void draw_one(int t, float x, float y, float width)
{
    if (t == TILES) draw_cells(slots[t].texture, x, y, width);
    else wgf_draw_texture(slots[t].texture, x, y, width, width, WGF_COLOR_WHITE);
}

static void frame(void *user)
{
    int t;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    wgf_draw_text(0, "libwgf textures: smooth (linear) and sharp (nearest) sampling", 12, 20, 22, WGF_COLOR_RAYWHITE);
    for (t = 0; t < TEXTURES; t++) {
        const float x = 20.0f + (float)t * 330.0f, y = 70.0f;
        const wgf_texture_t texture = slots[t].texture;
        /* Sampling is the texture's, read as each draw is recorded: one texture, drawn
         * smooth, then sharp. */
        wgf_texture_set_sampling(texture, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_LINEAR);
        draw_one(t, x, y, 150);
        wgf_texture_set_sampling(texture, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_NEAREST);
        draw_one(t, x + 160, y, 150);
        draw_caption(&slots[t], PATHS[t], x, y + 166);
    }
    wgf_draw_text(0, "compressed: as name.ktx, this GPU's variant or else the PNG", 12, 300, 22, WGF_COLOR_RAYWHITE);
    for (t = 0; t < COMPRESSED; t++) {
        const float x = 20.0f + (float)t * 330.0f, y = 350.0f;
        /* The PNG is the texture drawn sharp above: smooth, as libwgt's own .ktx texture is. */
        wgf_texture_set_sampling(compressed[t].texture, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                                 WGF_TEXTURE_FILTER_LINEAR);
        wgf_draw_texture(compressed[t].texture, x, y, 150, 150, WGF_COLOR_WHITE);
        draw_caption(&compressed[t], KTX_PATHS[t], x, y + 166);
    }
}

static void init(void *user)
{
    int t;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(38, 42, 54, 255));
    for (t = 0; t < TEXTURES; t++) {
        slots[t].asked = wgf_time_get_seconds();
        slots[t].texture = wgf_texture_create(PATHS[t]);
    }
    for (t = 0; t < COMPRESSED; t++) {
        compressed[t].asked = wgf_time_get_seconds();
        compressed[t].texture = wgf_texture_create(FALLBACK_PATHS[t]);
    }
}

static void on_shutdown(void *user)
{
    int t;
    (void)user;
    for (t = 0; t < TEXTURES; t++) wgf_resource_release(slots[t].texture);
    for (t = 0; t < COMPRESSED; t++) wgf_resource_release(compressed[t].texture);
}

int main(void)
{
    wgf_window_set_title("libwgf textures");
    wgf_window_set_size(1010, 580);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, on_shutdown, NULL);
    return 0;
}
