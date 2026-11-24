#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emscripten.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_play_state.h"
#include "wgf_sound.h"
#include "wgf_voice.h"
#include "wgf_fs.h"
#include "wgf_resource.h"
#include "wgf_window.h"

/* Sounds in a browser (tools/run_in_browser.py), which decodes them: WAV, MP3, and Ogg,
 * each decoded (decodeAudioData) and streamed (an <audio> element), the examples'
 * music and click bundled at their paths and a WAV written here. The checks are
 * wgf_audio_sound_test's, natively: READY with the length the file has, a streamed
 * sound's length a decoded one's (within what the browser's MP3 decoder trims), a path
 * made both ways two sounds, a missing or undecodable file FAILED, a released sound gone;
 * a sound made from samples, an AudioBuffer holding them a channel apart, played; and a
 * sound loaded again in place, from the page's server, under a voice playing it.
 * A frame loop, since the browser decodes between frames. */

static int failures, frames, step;
static wgf_handle_t task_wav, task_garbage;
static wgf_handle_t sounds[3][2]; /* [wav, mp3, ogg][streamed] */
static wgf_handle_t missing, garbage[2];
static wgf_handle_t again[2], playing[2]; /* [streamed]: loaded again in place, and the voice playing each */
static float length;
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

/* A 16-bit stereo WAV of `count` frames (4000 at most) at 44.1 kHz: its size in bytes. */
static unsigned char wav[44 + 4000 * 4];
static int make_wav(unsigned count)
{
    memcpy(wav, "RIFF", 4);
    put32(wav + 4, 36u + count * 4u);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    put16(wav + 20, 1);
    put16(wav + 22, 2);
    put32(wav + 24, 44100);
    put32(wav + 28, 44100u * 4u);
    put16(wav + 32, 4);
    put16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    put32(wav + 40, count * 4u);
    for (unsigned i = 0; i < count * 2; i++) put16(wav + 44 + i * 2, (i * 37u) & 0xFFFFu);
    return (int)(44 + count * 4);
}

static bool any_pending(void)
{
    bool pending = wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_PENDING ||
                   wgf_resource_get_status(garbage[0]) == WGF_RESOURCE_STATUS_PENDING ||
                   wgf_resource_get_status(garbage[1]) == WGF_RESOURCE_STATUS_PENDING;
    for (int f = 0; f < 3; f++) {
        for (int s = 0; s < 2; s++)
            pending = pending || wgf_resource_get_status(sounds[f][s]) == WGF_RESOURCE_STATUS_PENDING;
    }
    return pending;
}

/* A sample of a sound's AudioBuffer, its `channel`'s `frame`th. */
EM_JS(double, buffer_sample, (int handle, int channel, int frame), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(handle);
    return entry && entry.buffer ? entry.buffer.getChannelData(channel)[frame] : -9;
})

/* A sound's entry in the browser marked, and whether it still is: one loaded again is a
 * new entry, with no mark. */
EM_JS(void, mark, (int handle), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(handle);
    if (entry) entry.marked = true;
})

EM_JS(int, is_marked, (int handle), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(handle);
    return entry && entry.marked ? 1 : 0;
})

static void check_from_samples(void)
{
    static float samples[2 * 4800];
    wgf_handle_t made, voice;
    for (int i = 0; i < 4800; i++) {
        samples[i * 2] = 0.5f * sinf((float)i * 0.0626f);
        samples[i * 2 + 1] = -0.25f;
    }
    made = wgf_sound_create_from_samples(samples, 2 * 4800, 2, 48000);
    expect(made != 0 && wgf_resource_get_status(made) == WGF_RESOURCE_STATUS_READY &&
               fabsf(wgf_sound_get_duration(made) - 0.1f) < 1e-6f,
           "made from samples: READY at once, a tenth of a second");
    /* near, not exact: Chromium's buffer, read back, differed from what was given by about 2e-4 near its start */
    expect(fabs(buffer_sample((int)made, 0, 1000) - samples[2000]) < 0.01 &&
               fabs(buffer_sample((int)made, 1, 1000) + 0.25) < 0.01,
           "its buffer holds them, a channel apart");
    voice = wgf_voice_create(made);
    expect(voice != 0 && wgf_voice_play(voice) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "and plays");
    wgf_voice_destroy(voice);
    expect(wgf_sound_create_from_samples(samples, 10, 1, 7999) == 0, "a rate out of range refused");
    expect(wgf_resource_release(made), "released");
}

static void check(void)
{
    check_from_samples();
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
    expect(wgf_resource_get_status(sounds[1][0]) == WGF_RESOURCE_STATUS_NONE &&
               wgf_sound_get_duration(sounds[1][0]) == 0.0f,
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
        task_wav = wgf_fs_write("sounds/ramp.wav", wav, make_wav(2000));
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
        /* loaded again in place, under a voice playing each: a file the page's server has,
           since a reload fetches anew */
        again[0] = wgf_sound_create("served/click.ogg");
        again[1] = wgf_sound_create_streamed("served/click.ogg");
        step++;
    } else if (step == 3 && wgf_resource_get_status(again[0]) != WGF_RESOURCE_STATUS_PENDING &&
               wgf_resource_get_status(again[1]) != WGF_RESOURCE_STATUS_PENDING) {
        expect(wgf_resource_get_status(again[0]) == WGF_RESOURCE_STATUS_READY &&
                   wgf_resource_get_status(again[1]) == WGF_RESOURCE_STATUS_READY,
               "a served file: READY, decoded and streamed");
        length = wgf_sound_get_duration(again[0]);
        for (int s = 0; s < 2; s++) {
            playing[s] = wgf_voice_create(again[s]);
            wgf_voice_set_loop(playing[s], true);
            wgf_voice_play(playing[s]);
            mark((int)again[s]);
        }
        expect(wgf_asset_can_reload("served/click.ogg") && wgf_asset_reload("served/click.ogg") == 2,
               "a path made both ways: both load again");
        expect(is_marked((int)again[0]) && is_marked((int)again[1]) &&
                   wgf_voice_get_state(playing[0]) == WGF_PLAY_STATE_PLAYING,
               "as they were, and playing, until the new file is decoded");
        step++;
    } else if (step == 4 && !is_marked((int)again[0]) && !is_marked((int)again[1])) {
        expect(wgf_resource_get_status(again[0]) == WGF_RESOURCE_STATUS_READY &&
                   wgf_resource_get_status(again[1]) == WGF_RESOURCE_STATUS_READY &&
                   fabsf(wgf_sound_get_duration(again[0]) - length) < 1e-4f,
               "in place: the same handles, a new buffer and element, the file's length");
        expect(wgf_voice_get_state(playing[0]) == WGF_PLAY_STATE_PLAYING &&
                   wgf_voice_get_state(playing[1]) == WGF_PLAY_STATE_PLAYING,
               "a voice playing it: still playing, in the new one");
        emscripten_force_exit(failures == 0 ? 0 : 1);
    }
}

int main(void)
{
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
