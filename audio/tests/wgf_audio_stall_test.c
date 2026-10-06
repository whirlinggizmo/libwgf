#include <math.h>
#include <stdio.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include "wgf_app.h"
#include "wgf_core_fs_priv.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* A slow frame doesn't stop the sound: a voice plays the examples' music, streamed and
 * silent, through app's runtime; once it has started, one frame stalls for 300 ms, and
 * by the next the voice has moved on by about as long. Natively the mixer runs on the
 * device's thread (in a window, tools/run_in_xvfb.py); on the web the browser mixes on
 * its audio thread (tools/run_in_browser.py, wgf_audio_stall_web_test). Natively, with no
 * audio device the voice never moves: skipped, loudly (77). */

#define MUSIC "music/a_hero_is_born.mp3" /* under the examples' assets natively, bundled on the web */

static int failures, frames, stage;
static wgf_handle_t voice;
static double started, position_before, time_before;

static void finish(int code)
{
#if defined(__EMSCRIPTEN__)
    emscripten_force_exit(code);
#else
    if (code == 77) printf("wgf_audio_stall_test: SKIPPING (no audio device: the voice never moved)\n");
    failures = code;
    wgf_app_quit();
#endif
}

static void on_frame(void *user)
{
    (void)user;
    frames++;
    if (stage == 0) {
        wgf_handle_t sound;
#if !defined(__EMSCRIPTEN__)
        wgf_core_priv_fs_set_root(WGF_TEST_ASSETS); /* after app's start, which sets the default */
#endif
        sound = wgf_sound_create_streamed(MUSIC);
        voice = wgf_voice_create(sound);
        wgf_resource_release(sound);
        wgf_voice_set_volume(voice, 0.0f); /* heard by no one */
        wgf_voice_set_loop(voice, true);
        wgf_voice_play(voice);
        started = wgf_time_get_seconds();
        stage++;
    } else if (stage == 1) {
        if (wgf_voice_get_position(voice) > 0.2f) {
            position_before = wgf_voice_get_position(voice);
            time_before = wgf_time_get_seconds();
            while (wgf_time_get_seconds() < time_before + 0.3) {
            } /* the slow frame */
            stage++;
        } else if (wgf_time_get_seconds() - started > 10.0) {
            printf(wgf_resource_get_status(wgf_voice_get_sound(voice)) == WGF_RESOURCE_STATUS_READY
                       ? "the voice never moved\n"
                       : "the music never loaded\n");
#if defined(__EMSCRIPTEN__)
            finish(1);
#else
            finish(wgf_resource_get_status(wgf_voice_get_sound(voice)) == WGF_RESOURCE_STATUS_READY ? 77 : 1);
#endif
        }
    } else if (stage == 2) {
        const double moved = wgf_voice_get_position(voice) - position_before;
        const double passed = wgf_time_get_seconds() - time_before;
        printf("wgf_audio_stall_test: through a 300 ms frame the voice moved %.3f s in %.3f s\n", moved, passed);
        if (moved < passed - 0.1) {
            printf("FAIL: the sound stopped through the slow frame\n");
            finish(1);
        } else {
            finish(0);
        }
        stage++;
    }
}

int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return failures;
}
