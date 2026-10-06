#include <stdbool.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* Looping music and a one-shot click. The music is streamed (decoded as it plays), the
 * click decoded as it loads; the voices playing them are made at once, and one told to
 * play a sound still loading starts when it has. The sound plays off the frame's thread
 * on both platforms (natively libwgf's mixer, on the web the browser's), so it plays on
 * through a slow frame: press S to stall one for 300 ms and hear it not care. SPACE
 * plays the click, M pauses and resumes the music. From wgrender's audio example, where
 * the web stuttered through the stall. */

static wgf_handle_t music, click;

/* A voice of `sound`, which holds the sound's reference: the program's own is let go of. */
static wgf_handle_t voice_of(wgf_handle_t sound)
{
    const wgf_handle_t voice = wgf_voice_create(sound);
    wgf_resource_release(sound);
    return voice;
}

static bool loaded(wgf_handle_t voice)
{
    return wgf_resource_get_status(wgf_voice_get_sound(voice)) == WGF_RESOURCE_STATUS_READY;
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(18, 20, 28, 255));
    music = voice_of(wgf_sound_create_streamed("music/a_hero_is_born.mp3"));
    wgf_voice_set_volume(music, 0.5f);
    wgf_voice_set_loop(music, true); /* music is a looping voice */
    wgf_voice_play(music);           /* plays once it has loaded */
    click = voice_of(wgf_sound_create("sounds/click_004.ogg"));
}

static void frame(void *user)
{
    const wgf_play_state_t state = wgf_voice_get_state(music);
    (void)user;
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) wgf_voice_play(click);
    if (wgf_keyboard_is_pressed(WGF_KEY_S)) {
        const double until = wgf_time_get_seconds() + 0.3; /* a deliberately slow frame */
        while (wgf_time_get_seconds() < until) {
        }
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_M) && !wgf_voice_pause(music)) wgf_voice_resume(music);
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    wgf_draw_text(0, "libwgf audio", 24, 30, 28, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0,
                  !loaded(music)                          ? "music: loading..."
                  : state == WGF_PLAY_STATE_PAUSED        ? "music: paused"
                                                          : "music: playing (mp3, streamed, looping)",
                  24, 80, 18, WGF_COLOR_SKYBLUE);
    wgf_draw_text(0, loaded(click) ? "click: ready (ogg)" : "click: loading...", 24, 110, 18, WGF_COLOR_LIME);
    wgf_draw_text(0,
                  wgf_app_can_quit() ? "[SPACE] play click   [M] pause music   [S] stall 300 ms   [ESC] quit"
                                     : "[SPACE] play click   [M] pause music   [S] stall 300 ms",
                  24, 150, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf audio");
    wgf_window_set_size(720, 240);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
