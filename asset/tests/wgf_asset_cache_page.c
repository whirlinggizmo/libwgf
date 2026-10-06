#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* The page tools/check_asset_cache.py visits again and again: one texture, made from a
 * relative path, so the asset part fetches it from beside the page (nothing bundled)
 * and keeps it in the browser's cache, drawn over the whole window. It asks for a
 * manifest, as a game would, so the same page serves the visits with manifests and
 * without. While it loads, or
 * once its load has failed, the placeholder checker is drawn instead. */

static wgf_handle_t tiles;

static void init(void *user)
{
    (void)user;
    wgf_asset_set_manifest("manifest.json"); /* a host without one answers 404, and nothing is listed */
    tiles = wgf_texture_create("textures/tiles.png");
}

static void frame(void *user)
{
    (void)user;
    wgf_draw_texture(tiles, 0.0f, 0.0f, 4096.0f, 4096.0f, WGF_COLOR_WHITE);
}

int main(void)
{
    wgf_window_set_size(256, 256);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
