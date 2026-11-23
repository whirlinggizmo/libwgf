#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_time.h"

/* Voices natively, headless: the mixer pulled by the test (wgf_audio_priv_mix) in place
 * of a device, at 48 kHz, 10 ms at a time, the part's update between, as a frame's. The
 * checks are wgf_audio_voice_checks.c's, the browser's too; and here, what only the
 * mixer shows: the samples it mixes, and its pan. */

#define TOLERANCE 0.012f /* a mixing step, and a little */
#define RATE 48000
#define STEP_FRAMES 480

#include "wgf_audio_voice_checks.c"

static float mixed[STEP_FRAMES * 2];

static void advance(double seconds)
{
    for (double done = 0.0; done < seconds - 1e-9; done += (double)STEP_FRAMES / RATE) {
        wgf_audio_priv_mix(mixed, STEP_FRAMES, RATE);
        wgf_core_priv_part_update((float)STEP_FRAMES / RATE);
    }
}

static void settle(void)
{
    const double start = wgf_time_get_seconds();
    while (wgf_core_priv_load_get_pending_count() > 0 && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
    wgf_core_priv_part_update(0.0f);
}

/* The mixed samples: the sound's own, at its rate, at volume 1 and pan 0 on both sides,
 * then panned right, the left fades. */
static void check_samples(void)
{
    static float out[64 * 2];
    const wgf_handle_t v = wgf_voice_create(sound);
    bool exact = true;
    wgf_voice_play(v);
    wgf_audio_priv_mix(out, 64, 44100);
    for (int i = 0; i < 64; i++) {
        const float want = (float)((i % 400) * 80 - 16000) / 32768.0f;
        exact = exact && fabsf(out[i * 2] - want) < 1e-4f && fabsf(out[i * 2 + 1] - want) < 1e-4f;
    }
    expect(exact, "at its own rate, the sound's own samples, on both sides");
    wgf_voice_set_pan(v, 1.0f);
    wgf_audio_priv_mix(out, 64, 44100);
    expect(out[0] == 0.0f && out[1] != 0.0f, "panned right: the left silent");
    wgf_voice_destroy(v);
}

/* A sound made from samples: READY at once, its length its frames', played as a file's is, the
 * caller's samples free to change; stereo kept apart; what it refuses. */
static void check_from_samples(void)
{
    static float samples[200], out[64 * 2];
    bool exact = true;
    wgf_handle_t made, stereo, v;
    for (int i = 0; i < 200; i++) samples[i] = (float)i / 400.0f;
    made = wgf_sound_create_from_samples(samples, 100, 1, 44100);
    expect(made != 0 && wgf_resource_get_status(made) == WGF_RESOURCE_STATUS_READY, "made from samples: READY at once");
    expect(fabsf(wgf_sound_get_duration(made) - 100.0f / 44100.0f) < 1e-6f && wgf_resource_get_path(made)[0] == '\0',
           "its length its frames', no path");
    samples[0] = 1.0f; /* copied: the caller's are its own again */
    v = wgf_voice_create(made);
    wgf_voice_play(v);
    wgf_audio_priv_mix(out, 64, 44100);
    for (int i = 0; i < 64; i++) {
        const float want = (float)i / 400.0f;
        exact = exact && fabsf(out[i * 2] - want) < 1e-6f && fabsf(out[i * 2 + 1] - want) < 1e-6f;
    }
    expect(exact, "played: the samples given, on both sides");
    wgf_voice_destroy(v);
    expect(wgf_sound_play(made, 1.0f, 1.0f, 0.0f) && wgf_sound_add_segment(made, "start", 0.0f, 0.001f),
           "played and forgotten, a segment added: a decoded sound's");
    for (int i = 0; i < 200; i++) samples[i] = i % 2 == 0 ? 0.5f : -0.25f;
    stereo = wgf_sound_create_from_samples(samples, 200, 2, 44100);
    v = wgf_voice_create(stereo);
    wgf_voice_play(v);
    wgf_audio_priv_mix(out, 64, 44100);
    expect(stereo != 0 && fabsf(wgf_sound_get_duration(stereo) - 100.0f / 44100.0f) < 1e-6f &&
               fabsf(out[0] - 0.5f) < 1e-6f && fabsf(out[1] + 0.25f) < 1e-6f,
           "stereo: left then right, a frame each pair");
    wgf_voice_destroy(v);
    expect(wgf_sound_create_from_samples(NULL, 10, 1, 44100) == 0 &&
               wgf_sound_create_from_samples(samples, 0, 1, 44100) == 0 &&
               wgf_sound_create_from_samples(samples, 3, 2, 44100) == 0 &&
               wgf_sound_create_from_samples(samples, 10, 3, 44100) == 0 &&
               wgf_sound_create_from_samples(samples, 10, 1, 7999) == 0 &&
               wgf_sound_create_from_samples(samples, 10, 1, 192001) == 0,
           "refused: no samples, none, half a frame, three channels, a rate out of range");
    expect(wgf_resource_release(made) && wgf_resource_release(stereo), "released");
}

int main(void)
{
    static unsigned char wav[44 + 44100 * 2];
    const int size = make_second(wav);
    double wait;
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("audio_voice_test_root");
    expect(wgf_core_priv_fs_write("sounds/second.wav", wav, size), "the WAV written");
    sound = wgf_sound_create("sounds/second.wav");
    streamed = wgf_sound_create_streamed("sounds/second.wav");
    settle();
    for (int i = 0; (wait = step(i)) >= 0.0; i++) advance(wait);
    check_samples();
    check_from_samples();
    wgf_resource_release(sound);
    wgf_resource_release(streamed);
    wgf_core_priv_fs_rmdir("sounds");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
