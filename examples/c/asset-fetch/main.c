#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The desktop pulls its files from the same site the browser does. On the web the
 * browser downloads a missing file and caches it; natively libwgf has no HTTP client
 * (and no TLS), so it asks the program: with a URL as the asset host and fetching on, a
 * cache miss becomes a request the program takes once a frame, writing the file it
 * names (wgf_asset.h). The downloader here shells out to curl, so the example needs
 * nothing built or linked; it is synchronous, fine for a few small files, where a real
 * one starts a download and answers wgf_asset_fetch_done from its own thread.
 *
 * Natively it downloads only when asked: WGF_ASSET_HOST names the host (python3
 * tools/server.py's), or "-" for libwgf's own assets on GitHub, over HTTPS; unasked, or
 * with no host answering, it reads the local files, and says so. Two buttons show the cache at work: Fetch asset
 * loads the logo again (releasing the texture, then creating it), and Clear cache
 * forgets what was downloaded, so the next fetch downloads it again. The line under
 * them says what happened: natively, whether the file came from the cache or was
 * downloaded, told by looking in the cache directory first; the web keeps its cache in
 * the browser, where the program can't look, so there it says loaded. Escape quits
 * where quitting means anything.
 *
 * wgrender's fetch example, done for the size table. Its differences:
 *   - Whether a host answers is asked with wgf_asset_ping_host, which natively comes
 *     back as a ping request this program answers with curl (a HEAD of the logo, as
 *     wgrender's host check did), not with a curl call in init: the asking is libwgf's.
 *   - The web is told from the desktop by wgf_asset_set_fetching refusing there, not by
 *     a platform #if, so this file has none; the curl code is in the web build, unused.
 *   - Natively it stays offline unless WGF_ASSET_HOST asks, in a window too, where
 *     wgrender's went online by default and its headless build stayed offline: a
 *     program can't tell it runs headless, and the checks mustn't need the network.
 *   - Downloads go in libwgf's default cache directory (the user's cache, by the
 *     program's identity), not build/asset-cache: libwgf's programs build in out/.
 *   - The buttons are libwgf's UI, in a row where wgrender's widgets were; Fetch asset
 *     is ignored while a fetch is under way rather than drawn disabled, which libwgf's
 *     UI has no call for.
 *   - The FPS counter is text drawn from wgf_loop_get_fps: libwgf has no debug overlay.
 *   - Text is ASCII ("--" for the em dash): the built-in font is not known to have it. */

#define TEXTURE_PATH "sprites/logo/wg-logo-white-alpha.png"
#define DEFAULT_HOST "https://raw.githubusercontent.com/whirlinggizmo/libwgf/main/examples/assets"
#define LOCAL_HOST "../assets" /* the examples' files, one level above every program */

/* Where the files come from: the browser's own fetching, a host being asked whether it
 * answers, a URL host this program downloads from, or the local files. */
enum { BROWSER, ASKING, REMOTE, LOCAL };

static int source = BROWSER;
static wgf_asset_task_t ping;
static wgf_texture_t texture; /* the logo; watched until it's READY or FAILED */
static bool waiting, was_cached;
static char host[256];
static char state[512] = "";

/* Download a request's URL to its destination, then say how it went; a ping asks for
 * the logo there, so a host without it counts as not answering. A real one wouldn't
 * block. */
static void fetch_with_curl(wgf_asset_task_t request)
{
    char command[2200];
    if (wgf_asset_fetch_is_ping(request)) {
        snprintf(command, sizeof command, "curl -fsS -I --max-time 2 -o /dev/null \"%s%s\"",
                 wgf_asset_fetch_get_url(request), TEXTURE_PATH);
    } else {
        snprintf(command, sizeof command, "curl -fsS --max-time 30 -o \"%s\" \"%s\"",
                 wgf_asset_fetch_get_dest(request), wgf_asset_fetch_get_url(request));
    }
    wgf_asset_fetch_done(request, system(command) == 0);
}

/* The logo finished loading: say where it came from. */
static void report_loaded(void)
{
    if (source == BROWSER) {
        snprintf(state, sizeof state, "loaded %s", TEXTURE_PATH);
    } else if (source == LOCAL) {
        snprintf(state, sizeof state, "read %s from %s", TEXTURE_PATH, LOCAL_HOST);
    } else {
        snprintf(state, sizeof state, was_cached ? "loaded %s from the cache" : "downloaded %s into the cache",
                 TEXTURE_PATH);
    }
}

/* Load the logo again, having noted whether the cache has it already. */
static void fetch(void)
{
    was_cached = false;
    if (source == REMOTE) {
        char cached[1024];
        FILE *file;
        snprintf(cached, sizeof cached, "%s/%s", wgf_asset_get_cache_dir(), TEXTURE_PATH);
        file = fopen(cached, "rb");
        was_cached = file != NULL;
        if (file != NULL) fclose(file);
    }
    /* nothing may hold the old texture, or creating the path again finds it loaded */
    wgf_resource_release(texture);
    texture = wgf_texture_create(TEXTURE_PATH); /* PENDING: made local (from the cache, or downloaded), then loaded */
    waiting = true;
    snprintf(state, sizeof state, "fetching %s...", TEXTURE_PATH);
}

static void init(void *user)
{
    const char *wanted = getenv("WGF_ASSET_HOST");
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(28, 30, 38, 255));
    if (wanted != NULL && wanted[0] == '\0') wanted = NULL;
    if (wanted != NULL && wgf_asset_set_fetching(true)) { /* natively, asked: this program downloads */
        snprintf(host, sizeof host, "%s", wanted[0] == '-' ? DEFAULT_HOST : wanted);
        ping = wgf_asset_ping_host(host, 2000);
        source = ASKING;
        snprintf(state, sizeof state, "asking %s...", host);
    } else if (wanted != NULL || !wgf_asset_set_fetching(true)) { /* the web: the browser fetches, beside the page */
        snprintf(host, sizeof host, "%s", LOCAL_HOST);
        wgf_asset_set_host(host);
        fetch();
    } else { /* natively, not asked: the local files, so the checks never need the network */
        wgf_asset_set_fetching(false);
        source = LOCAL;
        snprintf(host, sizeof host, "%s", LOCAL_HOST);
        wgf_asset_set_host(host);
        fetch();
    }
}

/* The ping answered or didn't: fetch from the host, or read the local files. */
static void poll_ping(void)
{
    const wgf_asset_task_status_t status = wgf_asset_task_get_status(ping);
    if (status == WGF_ASSET_TASK_STATUS_PENDING) return;
    wgf_asset_task_destroy(ping);
    ping = 0;
    if (status == WGF_ASSET_TASK_STATUS_DONE) {
        source = REMOTE;
    } else {
        source = LOCAL;
        wgf_asset_set_fetching(false);
        snprintf(host, sizeof host, "%s", LOCAL_HOST);
    }
    wgf_asset_set_host(host);
    fetch();
}

static void buttons(void)
{
    if (!wgf_ui_begin()) return;
    wgf_ui_begin_box("page", WGF_UI_DIRECTION_COLUMN); /* the screen, its buttons at the top left */
    wgf_ui_set_width(WGF_UI_SIZING_GROW, 0);
    wgf_ui_set_height(WGF_UI_SIZING_GROW, 0);
    wgf_ui_set_padding(12, 150);
    wgf_ui_set_align(WGF_UI_ALIGN_START, WGF_UI_ALIGN_START);
    wgf_ui_begin_box("buttons", WGF_UI_DIRECTION_ROW);
    wgf_ui_set_gap(12);
    wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, 180);
    wgf_ui_set_style_value(WGF_UI_VALUE_TEXT_SIZE, 17);
    if (wgf_ui_button("fetch", "Fetch asset") && !waiting && source != ASKING) fetch();
    if (wgf_ui_button("clear", "Clear cache")) {
        wgf_asset_clear_cache();
        snprintf(state, sizeof state, "cache cleared: the next fetch downloads");
    }
    wgf_ui_end_box();
    wgf_ui_end_box();
    wgf_ui_end();
}

static void frame(void *user)
{
    char line[512];
    wgf_asset_task_t request;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    while ((request = wgf_asset_fetch_next()) != 0) { /* none on the web, where the browser downloads */
        fetch_with_curl(request);
    }
    if (source == ASKING) poll_ping(); /* a ping of 0 (no room) is NONE: the local files */
    if (waiting && wgf_resource_get_status(texture) != WGF_RESOURCE_STATUS_PENDING) {
        waiting = false;
        if (wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY) {
            report_loaded();
        } else {
            snprintf(state, sizeof state, "failed to get %s", TEXTURE_PATH); /* the log says why */
        }
    }

    /* drawn once it's READY, centered where wgrender's sprite was */
    wgf_draw_texture(texture, 512 - wgf_texture_get_width(texture) * 0.5f,
                     380 - wgf_texture_get_height(texture) * 0.5f, 0, 0, WGF_COLOR_WHITE);
    snprintf(line, sizeof line, "%.0f FPS", wgf_loop_get_fps());
    wgf_draw_text(0, line, 12, 10, 16, WGF_COLOR_LIME);
    wgf_draw_text(0, "libwgf fetch: the desktop build downloads what the browser downloads", 12, 36, 20,
                  WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof line, "host: %s", host);
    wgf_draw_text(0, line, 12, 64, 16, WGF_COLOR_LIGHTGRAY);
    if (source == REMOTE) {
        snprintf(line, sizeof line, "cache: %s", wgf_asset_get_cache_dir());
        wgf_draw_text(0, line, 12, 86, 16, WGF_COLOR_LIGHTGRAY);
    } else if (source == LOCAL) {
        snprintf(line, sizeof line, "no host reachable -- reading %s locally instead", LOCAL_HOST);
        wgf_draw_text(0, line, 12, 86, 16, WGF_COLOR_LIGHTGRAY);
    }
    wgf_draw_text(0, state, 12, 120, 18, WGF_COLOR_SKYBLUE);
    buttons();
}

int main(void)
{
    wgf_window_set_title("libwgf fetch");
    wgf_window_set_size(1024, 640);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
