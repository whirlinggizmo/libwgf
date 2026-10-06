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
    wgf_resource_release(sound);
    wgf_resource_release(streamed);
    wgf_core_priv_fs_rmdir("sounds");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
