#include <stdbool.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_log.h"
#include "wgf_loop.h"
#include "wgf_play_state.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* Both of an ensure's overrides at once: a fetch_url (where this call's bytes come
 * from) and WGF_ASSET_ENSURE_FORCE_FETCH (past the cache). The asset's KEY is a bogus
 * path, nothing at host + key, so a plain ensure would fail; the fetch_url names where
 * the bytes really are. It is relative, read against the host as a browser reads a URL
 * against a directory, so it is the same call on every platform: on the web the bytes
 * are downloaded and cached under the key; natively, whose host is a local directory,
 * the file is read where it is, under the key's name. Once the ensure is DONE the key
 * is created as a sound and played as looping music. M pauses and resumes it; Escape
 * quits where quitting means anything.
 *
 * wgrender's force_fetch example, done for the size table. Its differences:
 *   - The music is a streamed sound played by a looping voice: libwgf's sound is the
 *     data and its voice the playing (wgrender's audio and sound), and libwgf streams
 *     only when asked, where wgrender streamed a file over 1 MB by itself, as this is.
 *   - The FPS counter is text drawn from wgf_loop_get_fps: libwgf has no FPS call.
 *   - The quit key's hint is shown where wgf_app_can_quit, not by a platform #if. */

#define INVALID_MUSIC_PATH "music/invalid.mp3" /* intentionally invalid, to show the fetch_url is honored */
/* Where the bytes are, relative to the asset host: under it wherever the site is
 * served, GitHub Pages' /<repo>/ included. An absolute https://cdn.example/... URL is
 * used as it is. */
#define MUSIC_FORCE_FETCH_PATH "music/a_hero_is_born.mp3"

static wgf_asset_task_t fetch; /* the ensure, until it has finished */
static wgf_voice_t music;

/* Once the ensure is DONE the file is local, under the key: creating the key loads it
 * from wherever the ensure found it. */
static void poll_fetch(void)
{
    const wgf_asset_task_status_t status = wgf_asset_task_get_status(fetch);
    if (status == WGF_ASSET_TASK_STATUS_PENDING) return;
    if (status == WGF_ASSET_TASK_STATUS_DONE) {
        const wgf_sound_t sound = wgf_sound_create_streamed(INVALID_MUSIC_PATH);
        music = wgf_voice_create(sound);
        wgf_resource_release(sound); /* the voice holds its own reference */
        wgf_voice_set_volume(music, 0.5f);
        wgf_voice_set_loop(music, true); /* music is a looping voice */
        wgf_voice_play(music);
    } else {
        wgf_log_error("fetch failed: %s from %s", INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH);
    }
    wgf_asset_task_destroy(fetch);
    fetch = 0;
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_asset_set_manifest("manifest.json");
    wgf_render_set_clear_color(wgf_color_make(18, 20, 28, 255));

    /* The key is a bogus path, so the bytes can only come from the explicit source,
     * showing the override is honored. */
    fetch = wgf_asset_ensure(INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH, WGF_ASSET_ENSURE_FORCE_FETCH);
    wgf_log_info("force_fetch: %s from %s", INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH);
}

static void frame(void *user)
{
    const wgf_play_state_t state = wgf_voice_get_state(music);
    char line[32];
    (void)user;
    if (fetch != 0) poll_fetch();
    if (wgf_keyboard_is_pressed(WGF_KEY_M) && music != 0 && !wgf_voice_pause(music)) wgf_voice_resume(music);
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    wgf_draw_text(0, "libwgf + audio + force_fetch", 24, 30, 28, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0,
                  music == 0                       ? "music: loading..."
                  : state == WGF_PLAY_STATE_PAUSED ? "music: paused"
                                                   : "music: playing (mp3, looping)",
                  24, 80, 18, WGF_COLOR_SKYBLUE);
    wgf_draw_text(0, wgf_app_can_quit() ? "[M] toggle music   [ESC] quit" : "[M] toggle music", 24, 150, 16,
                  WGF_COLOR_LIGHTGRAY);
    snprintf(line, sizeof line, "%.0f FPS", wgf_loop_get_fps());
    wgf_draw_text(0, line, 24, 12, 16, WGF_COLOR_LIME);
}

int main(void)
{
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    wgf_window_set_title("libwgf audio + force_fetch");
    wgf_window_set_size(720, 240);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
