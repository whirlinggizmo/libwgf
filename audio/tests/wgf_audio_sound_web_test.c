#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emscripten.h>

#include "wgf_app.h"
#include "wgf_sound.h"
#include "wgf_fs.h"
#include "wgf_resource.h"
#include "wgf_window.h"

/* Sounds in a browser (tools/run_in_browser.py), which decodes them: WAV, MP3, and Ogg,
 * each decoded (decodeAudioData) and streamed (an <audio> element), the examples'
 * music and click bundled at their paths and a WAV written here. The checks are
 * wgf_audio_sound_test's, natively: READY with the length the file has, a streamed
 * sound's length a decoded one's (within what the browser's MP3 decoder trims), a path
 * made both ways two sounds, a missing or undecodable file FAILED, a released sound gone.
 * A frame loop, since the browser decodes between frames. */

static int failures, frames, step;
static wgf_handle_t task_wav, task_garbage;
static wgf_handle_t sounds[3][2]; /* [wav, mp3, ogg][streamed] */
static wgf_handle_t missing, garbage[2];
static const char *const paths[3] = {"sounds/ramp.wav", "music/a_hero_is_born.mp3", "sounds/click_004.ogg"};

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }

/* A 16-bit stereo WAV of 2000 frames at 44.1 kHz. */
static unsigned char wav[44 + 2000 * 4];
static void make_wav(void)
{
    memcpy(wav, "RIFF", 4);
    put32(wav + 4, 36u + 2000u * 4u);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    put16(wav + 20, 1);
    put16(wav + 22, 2);
    put32(wav + 24, 44100);
    put32(wav + 28, 44100u * 4u);
    put16(wav + 32, 4);
    put16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    put32(wav + 40, 2000u * 4u);
    for (int i = 0; i < 2000 * 2; i++) put16(wav + 44 + i * 2, (unsigned)(i * 37) & 0xFFFFu);
}

static bool any_pending(void)
{
    bool pending = wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_PENDING ||
                   wgf_resource_get_status(garbage[0]) == WGF_RESOURCE_STATUS_PENDING ||
                   wgf_resource_get_status(garbage[1]) == WGF_RESOURCE_STATUS_PENDING;
    for (int f = 0; f < 3; f++) {
        for (int s = 0; s < 2; s++) pending = pending || wgf_resource_get_status(sounds[f][s]) == WGF_RESOURCE_STATUS_PENDING;
    }
    return pending;
}

static void check(void)
{
    /* the click is 10.02 ms natively (442 frames at 44.1 kHz); Chromium decodes it as
       8.7 ms and its <audio> reads 12.9 ms, each decoder treating a file's edges its own
       way: a few milliseconds, which the header says */
    static const float least[3] = {2000.0f / 44100.0f - 0.002f, 85.0f, 0.005f};
    static const float most[3] = {2000.0f / 44100.0f + 0.002f, 95.0f, 0.02f};
    static const float alike[3] = {0.002f, 0.1f, 0.006f}; /* streamed against decoded */
    for (int f = 0; f < 3; f++) {
        char what[160];
        const float decoded = wgf_sound_get_duration(sounds[f][0]), streamed = wgf_sound_get_duration(sounds[f][1]);
        snprintf(what, sizeof(what), "%s: READY, decoded and streamed", paths[f]);
        expect(wgf_resource_get_status(sounds[f][0]) == WGF_RESOURCE_STATUS_READY &&
                   wgf_resource_get_status(sounds[f][1]) == WGF_RESOURCE_STATUS_READY,
               what);
        snprintf(what, sizeof(what), "%s: its length, %.4f s decoded and %.4f s streamed", paths[f], decoded, streamed);
        expect(decoded >= least[f] && decoded <= most[f] && fabsf(streamed - decoded) <= alike[f], what);
        expect(sounds[f][0] != sounds[f][1], "a path made both ways: two sounds");
    }
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "a missing file: FAILED");
    expect(wgf_resource_get_status(garbage[0]) == WGF_RESOURCE_STATUS_FAILED &&
               wgf_resource_get_status(garbage[1]) == WGF_RESOURCE_STATUS_FAILED,
           "a file the browser can't decode: FAILED, decoded or streamed");
    for (int f = 0; f < 3; f++) {
        for (int s = 0; s < 2; s++) wgf_resource_release(sounds[f][s]);
    }
    expect(wgf_resource_get_status(sounds[1][0]) == WGF_RESOURCE_STATUS_NONE && wgf_sound_get_duration(sounds[1][0]) == 0.0f,
           "released: gone");
}

static void on_frame(void *user)
{
    (void)user;
    frames++;
    if (frames > 3600) {
        printf("FAIL: still waiting at step %d\n", step);
        failures++;
        emscripten_force_exit(1);
        return;
    }
    if (step == 0) {
        make_wav();
        task_wav = wgf_fs_write("sounds/ramp.wav", wav, (int)sizeof(wav));
        task_garbage = wgf_fs_write("sounds/garbage.ogg", (const unsigned char *)"not a sound", 11);
        step++;
    } else if (step == 1 && wgf_fs_task_get_status(task_wav) != WGF_FS_TASK_STATUS_PENDING &&
               wgf_fs_task_get_status(task_garbage) != WGF_FS_TASK_STATUS_PENDING) {
        expect(wgf_fs_task_get_status(task_wav) == WGF_FS_TASK_STATUS_DONE, "the WAV written");
        wgf_fs_task_destroy(task_wav);
        wgf_fs_task_destroy(task_garbage);
        for (int f = 0; f < 3; f++) {
            sounds[f][0] = wgf_sound_create(paths[f]);
            sounds[f][1] = wgf_sound_create_streamed(paths[f]);
        }
        missing = wgf_sound_create("sounds/missing.wav");
        garbage[0] = wgf_sound_create("sounds/garbage.ogg");
        garbage[1] = wgf_sound_create_streamed("sounds/garbage.ogg");
        step++;
    } else if (step == 2 && !any_pending()) {
        check();
        emscripten_force_exit(failures == 0 ? 0 : 1);
    }
}

int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
