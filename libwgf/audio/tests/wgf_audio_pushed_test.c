#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "wgf_audio.h"
#include "wgf_audio_priv.h"
#include "wgf_bus.h"
#include "wgf_core_priv.h"
#include "wgf_voice.h"

/* Pushed voices natively, headless, the mixer pulled by the test (wgf_voice.h, raylib's
 * AudioStream): the queue's room and fill, a 22050 Hz sine read back at 44.1 kHz,
 * stereo and mono, 16-bit samples, pitch, underruns counted once a time the queue runs
 * out, bus and voice volume heard, pause, stop, audio paused, its position, and what is
 * refused. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near(float a, float b, float within)
{
    return fabsf(a - b) <= within;
}

static float out[4096 * 2];

/* `frames` frames from the mixer at `rate`, into `out`. */
static void pull(int frames, int rate)
{
    wgf_audio_priv_mix(out, frames, rate);
}

/* Whether out's first `frames` frames are silent. */
static bool silent(int from, int frames)
{
    for (int i = from; i < from + frames; i++) {
        if (out[i * 2] != 0.0f || out[i * 2 + 1] != 0.0f) return false;
    }
    return true;
}

/* `frames` frames of a constant, `channels` of them, pushed: how many it took. */
static int push_constant(wgf_voice_t voice, int channels, int frames, float left, float right)
{
    static float samples[8192 * 2];
    for (int i = 0; i < frames; i++) {
        samples[i * channels] = left;
        if (channels == 2) samples[i * 2 + 1] = right;
    }
    return wgf_voice_push(voice, samples, frames * channels);
}

static void refusals(void)
{
    const wgf_voice_t plain = wgf_voice_create(0);
    const wgf_voice_t voice = wgf_voice_create_pushed(48000, 2, 1024);
    const float three[3] = {0.0f, 0.0f, 0.0f};
    const int ints[2] = {0, 0};
    expect(wgf_voice_create_pushed(999, 1, 1024) == 0 && wgf_voice_create_pushed(384001, 1, 1024) == 0,
           "a rate outside 1000 to 384000 refused");
    expect(wgf_voice_create_pushed(48000, 0, 1024) == 0 && wgf_voice_create_pushed(48000, 3, 1024) == 0,
           "channels other than 1 or 2 refused");
    expect(wgf_voice_create_pushed(48000, 1, 0) == 0 && wgf_voice_create_pushed(48000, 1, 1048577) == 0,
           "a capacity outside 1 to 1048576 refused");
    expect(voice != 0 && wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED && wgf_voice_get_sound(voice) == 0,
           "a pushed voice: STOPPED, with no sound");
    expect(wgf_voice_push(plain, three, 2) == 0 && wgf_voice_push_int16(plain, ints, 2) == 0 &&
               wgf_voice_get_room(plain) == 0 && wgf_voice_get_queued(plain) == 0 &&
               wgf_voice_get_underruns(plain) == 0,
           "a voice of a sound isn't a pushed one: 0");
    expect(wgf_voice_push(voice, three, 3) == 0 && wgf_voice_get_queued(voice) == 0,
           "a count that isn't a whole number of frames refused");
    expect(wgf_voice_push(voice, three, -2) == 0 && wgf_voice_push(voice, NULL, 2) == 0,
           "a count below 0, or NULL, refused");
    expect(wgf_voice_push(voice, three, 0) == 0, "nothing pushed: 0 taken");
    expect(!wgf_voice_set_sound(voice, 0) && !wgf_voice_set_position(voice, 0.0f) &&
               !wgf_voice_set_segment(voice, "") && !wgf_voice_set_pitch(voice, -1.0f) &&
               near(wgf_voice_get_pitch(voice), 1.0f, 0.0f),
           "no sound, position, segment, or negative pitch for a pushed voice");
    expect(wgf_voice_set_pitch(voice, 0.5f) && wgf_voice_set_pitch(voice, 1.0f), "a positive pitch is a pitch");
    wgf_voice_destroy(voice);
    expect(wgf_voice_get_room(voice) == 0 && wgf_voice_push(voice, three, 2) == 0, "destroyed: refused");
    wgf_voice_destroy(plain);
}

static void room_and_fill(void)
{
    const wgf_voice_t voice = wgf_voice_create_pushed(44100, 2, 1000);
    expect(wgf_voice_get_room(voice) == 1000 && wgf_voice_get_queued(voice) == 0, "empty: room for its capacity");
    expect(push_constant(voice, 2, 600, 0.1f, 0.2f) == 600 && wgf_voice_get_queued(voice) == 600 &&
               wgf_voice_get_room(voice) == 400,
           "600 frames pushed: 600 queued, room for 400");
    expect(push_constant(voice, 2, 600, 0.1f, 0.2f) == 400 && wgf_voice_get_room(voice) == 0 &&
               wgf_voice_get_queued(voice) == 1000,
           "past its capacity: as many as fit taken");
    expect(push_constant(voice, 2, 10, 0.1f, 0.2f) == 0, "full: none taken");
    pull(256, 44100);
    expect(wgf_voice_get_queued(voice) == 1000 && silent(0, 256), "STOPPED: nothing taken, nothing heard");
    wgf_voice_play(voice);
    pull(256, 44100);
    expect(wgf_voice_get_queued(voice) == 744 && near(out[0], 0.1f, 1e-6f) && near(out[1], 0.2f, 1e-6f) &&
               near(out[255 * 2 + 1], 0.2f, 1e-6f),
           "PLAYING at the mixer's rate: a frame taken a frame, as pushed, left and right");
    expect(near(wgf_voice_get_position(voice), 256.0f / 44100.0f, 1e-5f), "its position: the seconds it has played");
    expect(push_constant(voice, 2, 256, 0.1f, 0.2f) == 256 && wgf_voice_get_room(voice) == 0,
           "taken from: room again, and the ring wraps");
    pull(1000, 44100);
    expect(wgf_voice_get_queued(voice) == 0 && near(out[999 * 2], 0.1f, 1e-6f) && wgf_voice_get_underruns(voice) == 0,
           "all of it played, around the ring's end, without running out");
    wgf_voice_destroy(voice);
}

static void resampling(void)
{
    /* a 441 Hz sine at 22050 Hz, 50 samples a cycle, read back at 44100: each output frame
       the sine at its own time, linearly between two of the pushed */
    static float sine[22050];
    const wgf_voice_t voice = wgf_voice_create_pushed(22050, 1, 22050);
    float worst = 0.0f;
    for (int i = 0; i < 22050; i++) sine[i] = 0.5f * sinf(2.0f * 3.14159265f * 441.0f * (float)i / 22050.0f);
    expect(wgf_voice_push(voice, sine, 22050) == 22050, "a second of a sine pushed");
    wgf_voice_play(voice);
    for (int block = 0; block < 4; block++) {
        pull(4096, 44100);
        for (int i = 0; i < 4096; i++) {
            const int n = block * 4096 + i;
            const float want = 0.5f * sinf(2.0f * 3.14159265f * 441.0f * (float)n / 44100.0f);
            if (fabsf(out[i * 2] - want) > worst) worst = fabsf(out[i * 2] - want);
            if (out[i * 2] != out[i * 2 + 1]) worst = 1.0f; /* mono: both sides alike */
        }
    }
    printf("wgf_audio_pushed_test: a 22050 Hz sine at 44100, the most it is off %.5f\n", worst);
    expect(worst < 0.005f, "22050 Hz read back at 44100: the sine, within linear interpolation's error");
    expect(abs(wgf_voice_get_queued(voice) - (22050 - 4 * 4096 / 2)) <= 1, "half as many frames taken as made");
    expect(wgf_voice_get_underruns(voice) == 0, "no underrun while it had samples");

    /* pitch 2: twice as fast, so as many taken as made at a doubled rate's step */
    wgf_voice_stop(voice);
    expect(wgf_voice_get_queued(voice) == 0 && wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED,
           "stopped: its queue emptied");
    wgf_voice_push(voice, sine, 22050);
    wgf_voice_set_pitch(voice, 2.0f);
    wgf_voice_play(voice);
    pull(4096, 44100);
    expect(abs(wgf_voice_get_queued(voice) - (22050 - 4096)) <= 1, "pitch 2: a frame taken for each made at 44100");
    {
        float worst2 = 0.0f;
        for (int n = 0; n < 4096; n++) {
            const float want = 0.5f * sinf(2.0f * 3.14159265f * 441.0f * (float)n / 22050.0f);
            if (fabsf(out[n * 2] - want) > worst2) worst2 = fabsf(out[n * 2] - want);
        }
        expect(worst2 < 1e-5f, "an octave up: the pushed samples themselves");
    }
    wgf_voice_destroy(voice);
}

static void stereo_mono_and_int16(void)
{
    const wgf_voice_t stereo = wgf_voice_create_pushed(48000, 2, 4096);
    const wgf_voice_t mono = wgf_voice_create_pushed(48000, 1, 4096);
    int ints[8] = {16384, -16384, 40000, -40000, 0, 32767, -32768, 8192};
    push_constant(stereo, 2, 64, 0.25f, -0.5f);
    wgf_voice_play(stereo);
    pull(64, 48000);
    expect(near(out[0], 0.25f, 1e-6f) && near(out[1], -0.5f, 1e-6f) && near(out[63 * 2 + 1], -0.5f, 1e-6f),
           "stereo: left and right as pushed");
    wgf_voice_stop(stereo);
    push_constant(mono, 1, 64, 0.3f, 0.0f);
    wgf_voice_play(mono);
    pull(64, 48000);
    expect(near(out[0], 0.3f, 1e-6f) && near(out[1], 0.3f, 1e-6f), "mono: heard on both sides");
    wgf_voice_stop(mono);

    expect(wgf_voice_push_int16(stereo, ints, 8) == 4 && wgf_voice_push_int16(stereo, ints, 3) == 0,
           "16-bit: four stereo frames taken; a part of a frame refused");
    wgf_voice_play(stereo);
    pull(4, 48000);
    expect(near(out[0], 0.5f, 1e-6f) && near(out[1], -0.5f, 1e-6f) && near(out[2], 32767.0f / 32768.0f, 1e-6f) &&
               near(out[3], -1.0f, 1e-6f) && near(out[5], 32767.0f / 32768.0f, 1e-6f) && near(out[6], -1.0f, 1e-6f),
           "16-bit: over 32768, clamped to -32768 to 32767");
    wgf_voice_destroy(stereo);
    wgf_voice_destroy(mono);
}

static void underruns(void)
{
    const wgf_voice_t voice = wgf_voice_create_pushed(44100, 1, 4096);
    wgf_voice_play(voice);
    pull(256, 44100);
    expect(wgf_voice_get_underruns(voice) == 0 && silent(0, 256) &&
               wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING,
           "played with nothing pushed yet: silent, PLAYING, not an underrun");
    push_constant(voice, 1, 100, 0.4f, 0.0f);
    pull(256, 44100);
    expect(near(out[99 * 2], 0.4f, 1e-6f) && silent(100, 156) && wgf_voice_get_underruns(voice) == 1,
           "100 frames for 256: played, then silence, one underrun");
    pull(256, 44100);
    expect(wgf_voice_get_underruns(voice) == 1 && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING,
           "still empty: the same gap, not another; still PLAYING");
    push_constant(voice, 1, 10, 0.4f, 0.0f);
    pull(256, 44100);
    expect(wgf_voice_get_underruns(voice) == 2 && near(out[0], 0.4f, 1e-6f), "more, and out again: a second");
    push_constant(voice, 1, 512, 0.4f, 0.0f);
    pull(256, 44100);
    expect(wgf_voice_get_underruns(voice) == 2 && !silent(0, 256), "enough: none");
    wgf_voice_stop(voice);
    wgf_voice_play(voice);
    pull(256, 44100);
    expect(wgf_voice_get_underruns(voice) == 2, "stopped and played empty: not an underrun");
    wgf_voice_destroy(voice);
}

static void heard_and_paused(void)
{
    const wgf_voice_t voice = wgf_voice_create_pushed(44100, 1, 8192);
    wgf_bus_t bus = wgf_bus_create(wgf_bus_get_music());
    push_constant(voice, 1, 8192, 0.8f, 0.0f);
    wgf_voice_play(voice);
    pull(16, 44100);
    expect(near(out[0], 0.8f, 1e-6f), "heard as pushed");
    expect(wgf_voice_set_volume(voice, 0.5f) && (pull(16, 44100), near(out[0], 0.4f, 1e-6f)), "its volume heard");
    expect(wgf_voice_set_bus(voice, bus) && wgf_bus_set_volume(bus, 0.5f) &&
               (pull(16, 44100), near(out[0], 0.2f, 1e-6f)),
           "its bus's volume heard");
    wgf_bus_set_muted(wgf_bus_get_master(), true);
    pull(16, 44100);
    expect(silent(0, 16), "under a muted bus: silent");
    wgf_bus_set_muted(wgf_bus_get_master(), false);
    expect(wgf_voice_set_pan(voice, 1.0f) && (pull(16, 44100), out[0] == 0.0f && near(out[1], 0.2f, 1e-6f)),
           "panned right: the right only");
    wgf_voice_set_pan(voice, 0.0f);
    {
        const int queued = wgf_voice_get_queued(voice);
        expect(wgf_voice_pause(voice) && (pull(256, 44100), silent(0, 256)) && wgf_voice_get_queued(voice) == queued,
               "paused: silent, nothing taken");
        expect(wgf_voice_resume(voice) && (pull(16, 44100), near(out[0], 0.2f, 1e-6f)) &&
                   wgf_voice_get_queued(voice) == queued - 16,
               "resumed: on from where it was");
        wgf_audio_set_paused(true);
        pull(256, 44100);
        expect(silent(0, 256) && wgf_voice_get_queued(voice) == queued - 16, "audio paused: silent, nothing taken");
        wgf_audio_set_paused(false);
    }
    wgf_voice_set_spatial(voice, true);
    wgf_voice_set_place(voice, 0.0f, 0.0f, -100.0f);
    pull(16, 44100);
    expect(silent(0, 16), "spatial, past its far distance: silent");
    wgf_voice_set_spatial(voice, false);
    expect(wgf_voice_stop(voice) && wgf_voice_get_queued(voice) == 0 && (pull(16, 44100), silent(0, 16)) &&
               near(wgf_voice_get_position(voice), 0.0f, 0.0f),
           "stopped: emptied, silent, at 0");
    push_constant(voice, 1, 32, 0.8f, 0.0f);
    expect(wgf_voice_play(voice) && (pull(16, 44100), near(out[0], 0.2f, 1e-6f)), "pushed while STOPPED, then played");
    wgf_voice_destroy(voice);
    wgf_bus_destroy(bus);
}

int main(void)
{
    wgf_core_priv_init();
    refusals();
    room_and_fill();
    resampling();
    stereo_mono_and_int16();
    underruns();
    heard_and_paused();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_audio_pushed_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
