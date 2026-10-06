/* Voices' checks, one source for both platforms (included by wgf_audio_voice_test.c,
 * natively, and wgf_audio_voice_web_test.c, in a browser): every state, transition, and
 * refusal (wgf_play_state.h, wgf_voice.h), loop, reverse, pitch 0, positions wrapping
 * and clamping, a streamed sound's refusals, and audio's pause. A sequence of steps, each
 * acting and checking, then saying how long to wait before the next: the driver waits
 * by mixing natively and by the browser's audio clock on the web, so positions are
 * checked within TOLERANCE, the driver's. The sound is a second of mono WAV the driver
 * wrote, "sounds/second.wav", loaded as `sound` (decoded) and `streamed`. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_audio.h"
#include "wgf_play_state.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_voice.h"

static int failures;
static wgf_handle_t sound, streamed, voice, other;
static float held; /* a position to compare against later */

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void near(float got, float want, const char *what)
{
    if (fabsf(got - want) > TOLERANCE) {
        printf("FAIL: %s: %.3f s, not %.3f\n", what, got, want);
        failures++;
    }
}

/* A WAV of one second of mono at 44.1 kHz, a ramp, for the driver to write. */
static int make_second(unsigned char *out)
{
    enum { FRAMES = 44100 };
    static const unsigned char header[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                                             16,  0,   0,   0,   1, 0, 1, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0,
                                             2,   0,   16,  0,   'd', 'a', 't', 'a', 0, 0, 0, 0};
    const unsigned data = FRAMES * 2;
    for (int i = 0; i < 44; i++) out[i] = header[i];
    out[4] = (unsigned char)(36 + data);
    out[5] = (unsigned char)((36 + data) >> 8);
    out[6] = (unsigned char)((36 + data) >> 16);
    out[40] = (unsigned char)data;
    out[41] = (unsigned char)(data >> 8);
    out[42] = (unsigned char)(data >> 16);
    for (int i = 0; i < FRAMES; i++) {
        const int value = (i % 400) * 80 - 16000; /* a 110 Hz saw, audible if anyone listens */
        out[44 + i * 2] = (unsigned char)value;
        out[45 + i * 2] = (unsigned char)(value >> 8);
    }
    return 44 + (int)data;
}

/* Step `index`'s actions and checks; the seconds to wait before the next, or -1 when
 * every step has run. */
static double step(int index)
{
    switch (index) {
    case 0:
        expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY &&
                   wgf_resource_get_status(streamed) == WGF_RESOURCE_STATUS_READY,
               "the sounds are READY");
        voice = wgf_voice_create(sound);
        expect(voice != 0 && wgf_voice_get_sound(voice) == sound, "a voice of the sound");
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED && wgf_voice_get_volume(voice) == 1.0f &&
                   wgf_voice_get_pitch(voice) == 1.0f && wgf_voice_get_pan(voice) == 0.0f && !wgf_voice_is_loop(voice),
               "made STOPPED, volume 1, pitch 1, pan 0, not looping");
        expect(!wgf_voice_pause(voice) && !wgf_voice_resume(voice), "pause and resume refused while STOPPED");
        expect(wgf_voice_play(voice) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "play: PLAYING");
        return 0.3;
    case 1:
        near(wgf_voice_get_position(voice), 0.3f, "playing advances");
        expect(!wgf_voice_resume(voice), "resume refused while PLAYING");
        expect(wgf_voice_pause(voice) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PAUSED, "pause: PAUSED");
        held = wgf_voice_get_position(voice);
        return 0.2;
    case 2:
        expect(fabsf(wgf_voice_get_position(voice) - held) < 0.001f, "paused: held where it was");
        expect(!wgf_voice_pause(voice), "pause refused while PAUSED");
        expect(wgf_voice_resume(voice) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "resume: PLAYING");
        return 0.2;
    case 3:
        near(wgf_voice_get_position(voice), held + 0.2f, "resumed from where it was");
        expect(wgf_voice_stop(voice) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED &&
                   wgf_voice_get_position(voice) == 0.0f,
               "stop: STOPPED, rewound");
        expect(wgf_voice_play(voice), "play again");
        return 1.25;
    case 4:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE, "ran out: COMPLETE");
        near(wgf_voice_get_position(voice), 1.0f, "COMPLETE holds its end");
        expect(!wgf_voice_pause(voice) && !wgf_voice_resume(voice), "pause and resume refused while COMPLETE");
        expect(wgf_voice_set_loop(voice, true) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE,
               "loop turned on while COMPLETE: still COMPLETE");
        expect(wgf_voice_play(voice) && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "play from COMPLETE");
        return 1.3;
    case 5:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "looping never COMPLETE");
        near(wgf_voice_get_position(voice), 0.3f, "a loop goes on from the start");
        wgf_voice_set_loop(voice, false);
        wgf_voice_stop(voice);
        expect(wgf_voice_set_pitch(voice, -1.0f) && wgf_voice_get_pitch(voice) == -1.0f, "a negative pitch");
        expect(wgf_voice_play(voice), "play backwards");
        near(wgf_voice_get_position(voice), 1.0f, "backwards starts at the end");
        return 0.3;
    case 6:
        near(wgf_voice_get_position(voice), 0.7f, "backwards goes back");
        return 0.9;
    case 7:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE, "backwards runs out: COMPLETE");
        near(wgf_voice_get_position(voice), 0.0f, "COMPLETE at the start");
        wgf_voice_set_pitch(voice, 2.0f);
        wgf_voice_play(voice);
        return 0.25;
    case 8:
        near(wgf_voice_get_position(voice), 0.5f, "pitch 2: twice the speed");
        expect(wgf_voice_set_pitch(voice, 0.0f), "pitch 0");
        held = wgf_voice_get_position(voice);
        return 0.2;
    case 9:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "pitch 0: still PLAYING, not paused");
        near(wgf_voice_get_position(voice), held, "pitch 0 holds where it is");
        wgf_voice_set_pitch(voice, 1.0f);
        wgf_voice_set_loop(voice, true);
        expect(wgf_voice_set_position(voice, 2.25f), "a position past the end, looping");
        near(wgf_voice_get_position(voice), 0.25f, "looping: it wraps");
        wgf_voice_set_loop(voice, false);
        expect(wgf_voice_set_position(voice, 5.0f), "a position past the end, not looping");
        near(wgf_voice_get_position(voice), 1.0f, "not looping: clamped to the end");
        expect(wgf_voice_set_position(voice, -1.0f), "before the start");
        near(wgf_voice_get_position(voice), 0.0f, "clamped to the start");
        expect(!wgf_voice_set_position(voice, NAN) && !wgf_voice_set_volume(voice, -1.0f) &&
                   !wgf_voice_set_volume(voice, INFINITY) && !wgf_voice_set_pitch(voice, NAN) &&
                   !wgf_voice_set_pan(voice, NAN),
               "non-finite, and a negative volume: refused");
        expect(wgf_voice_set_volume(voice, 0.5f) && wgf_voice_get_volume(voice) == 0.5f, "volume reads back");
        expect(wgf_voice_set_pan(voice, 3.0f) && wgf_voice_get_pan(voice) == 1.0f, "pan clamped to 1");
        expect(wgf_voice_set_pitch(voice, 100.0f) && wgf_voice_get_pitch(voice) == 16.0f, "pitch clamped to 16");
        wgf_voice_stop(voice);
        wgf_voice_set_pitch(voice, -1.0f);
        wgf_voice_set_loop(voice, true);
        wgf_voice_play(voice);
        return 1.3;
    case 10:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "a reverse loop goes on");
        near(wgf_voice_get_position(voice), 0.7f, "a reverse loop wraps from the start to the end");
        wgf_audio_set_paused(true);
        expect(wgf_audio_is_paused(), "audio paused");
        held = wgf_voice_get_position(voice);
        return 0.3;
    case 11:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "audio's pause leaves the voice's state");
        near(wgf_voice_get_position(voice), held, "audio's pause holds every voice");
        wgf_audio_set_paused(false);
        expect(!wgf_audio_set_volume(-1.0f) && wgf_audio_set_volume(0.5f) && wgf_audio_get_volume() == 0.5f,
               "audio's volume: a negative one refused");
        wgf_audio_set_volume(1.0f);
        /* a streamed sound: one voice at a time, and forwards only */
        other = wgf_voice_create(streamed);
        expect(wgf_voice_play(other), "a voice plays the streamed sound");
        expect(!wgf_voice_set_pitch(other, -1.0f), "a negative pitch refused on a streamed sound");
        {
            const wgf_handle_t second = wgf_voice_create(streamed);
            expect(!wgf_voice_play(second), "a second voice can't play the streamed sound");
            wgf_voice_destroy(second);
        }
        return 0.3;
    case 12:
        near(wgf_voice_get_position(other), 0.3f, "the streamed sound plays");
        /* choosing what to play keeps the state (CONVENTIONS.md) */
        expect(wgf_voice_set_sound(other, sound) && wgf_voice_get_state(other) == WGF_PLAY_STATE_PLAYING &&
                   wgf_voice_get_sound(other) == sound,
               "another sound while PLAYING: still PLAYING");
        near(wgf_voice_get_position(other), 0.0f, "the new sound from its start");
        wgf_voice_pause(other);
        expect(wgf_voice_set_sound(other, streamed) && wgf_voice_get_state(other) == WGF_PLAY_STATE_PAUSED &&
                   wgf_voice_get_position(other) == 0.0f,
               "another sound while PAUSED: PAUSED at its start");
        expect(wgf_voice_set_sound(other, 0) && wgf_voice_get_state(other) == WGF_PLAY_STATE_STOPPED,
               "no sound: STOPPED");
        expect(!wgf_voice_set_sound(other, 12345), "a handle that isn't a sound: refused");
        wgf_voice_destroy(voice);
        wgf_voice_destroy(other);
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED && wgf_voice_get_sound(voice) == 0 &&
                   !wgf_voice_play(voice) && !wgf_voice_stop(voice) && wgf_voice_get_volume(voice) == 0.0f,
               "a destroyed voice: refused, and reads 0");
        expect(wgf_voice_create(12345) == 0, "a voice of a handle that isn't a sound: 0");
        return 0.0;
    case 13: /* segments: one sound of many effects */
        expect(!wgf_sound_add_segment(sound, "x", -0.1f, 0.5f) && !wgf_sound_add_segment(sound, "x", 0.5f, 0.5f) &&
                   !wgf_sound_add_segment(sound, "x", 0.6f, 0.5f) && !wgf_sound_add_segment(sound, "", 0.0f, 0.5f) &&
                   !wgf_sound_add_segment(sound, NULL, 0.0f, 0.5f) &&
                   !wgf_sound_add_segment(sound, "a name of thirty-two bytes, long", 0.0f, 0.5f) &&
                   !wgf_sound_add_segment(sound, "x", NAN, 0.5f) && !wgf_sound_add_segment(streamed, "x", 0.0f, 0.5f) &&
                   !wgf_sound_add_segment(sound, "past", 1.5f, 2.0f),
               "segments refused: a negative or empty range, a bad name, a streamed sound, past the end");
        expect(wgf_sound_add_segment(sound, "mid", 0.25f, 0.75f) && wgf_sound_add_segment(sound, "tail", 0.9f, 5.0f),
               "segments added, one ending past the sound");
        voice = wgf_voice_create(sound);
        expect(!wgf_voice_set_segment(voice, "nope") && !wgf_voice_set_segment(voice, NULL), "an unknown segment refused");
        expect(wgf_voice_set_segment(voice, "mid") && strcmp(wgf_voice_get_segment(voice), "mid") == 0 &&
                   wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED && wgf_voice_get_position(voice) == 0.0f,
               "a segment selected while STOPPED: STOPPED at its start");
        wgf_voice_play(voice);
        return 0.3;
    case 14:
        near(wgf_voice_get_position(voice), 0.3f, "a segment's position is from its start");
        return 0.3;
    case 15:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE, "a segment runs out: COMPLETE");
        near(wgf_voice_get_position(voice), 0.5f, "COMPLETE at the segment's end");
        wgf_voice_set_loop(voice, true);
        wgf_voice_play(voice);
        return 0.7;
    case 16:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "a looping segment goes on");
        near(wgf_voice_get_position(voice), 0.2f, "it loops within the segment");
        wgf_voice_set_loop(voice, false);
        wgf_voice_stop(voice);
        wgf_voice_set_pitch(voice, -1.0f);
        wgf_voice_play(voice);
        near(wgf_voice_get_position(voice), 0.5f, "backwards, a segment starts at its end");
        return 0.3;
    case 17:
        near(wgf_voice_get_position(voice), 0.2f, "backwards within the segment");
        wgf_voice_set_pitch(voice, 1.0f);
        expect(wgf_voice_set_segment(voice, "tail") && wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING,
               "another segment while PLAYING: still PLAYING, from its start");
        wgf_voice_play(voice);
        other = wgf_voice_create(sound);
        wgf_voice_set_segment(other, "tail");
        expect(wgf_voice_play(other), "a second voice plays the same segment");
        return 0.2;
    case 18:
        expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE &&
                   wgf_voice_get_state(other) == WGF_PLAY_STATE_COMPLETE,
               "a segment ending past the sound ends with it: both COMPLETE");
        near(wgf_voice_get_position(voice), 0.1f, "its end clamped to the sound's");
        expect(wgf_voice_set_segment(voice, "") && strcmp(wgf_voice_get_segment(voice), "") == 0 &&
                   wgf_voice_get_state(voice) == WGF_PLAY_STATE_STOPPED,
               "\"\" once COMPLETE: the whole sound again, STOPPED at its start");
        {
            const wgf_handle_t third = wgf_voice_create(streamed);
            expect(!wgf_voice_set_segment(third, "mid"), "a streamed sound has no segments");
            wgf_voice_destroy(third);
        }
        wgf_voice_destroy(voice);
        wgf_voice_destroy(other);
        return -1.0;
    default:
        return -1.0;
    }
}
