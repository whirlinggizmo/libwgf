#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_sound.h"
#include "wgf_asset.h"
#include "wgf_audio.h"
#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_play_state.h"
#include "wgf_resource.h"
#include "wgf_time.h"
#include "wgf_voice.h"

/* Sounds natively (wgf_sound.h): WAV, MP3, and Ogg, each decoded and streamed, the
 * examples' music and click and a WAV made here, in a root of the test's own. A streamed
 * sound's length is a decoded one's; a path made both ways is two sounds; a missing or
 * undecodable file fails; a released sound is gone; audio stops with core and comes
 * back with the next sound; and a sound loads again in place under the voices playing it.
 * The browser's half is wgf_audio_sound_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Update until nothing is pending, as a frame loop would: a long file decodes on a
 * worker for a while (90 s of MP3, in a debug build), so up to a minute. */
static void settle(void)
{
    const double start = wgf_time_get_seconds();
    while (wgf_core_priv_load_get_pending_count() > 0 && wgf_time_get_seconds() - start < 60.0) wgf_core_priv_update();
    expect(wgf_core_priv_load_get_pending_count() == 0, "everything settled");
}

/* The examples' file `name` into the test's root, at the same path. */
static void copy_asset(const char *name)
{
    char path[512];
    FILE *file;
    long size;
    unsigned char *data;
    snprintf(path, sizeof(path), "%s/%s", WGF_TEST_ASSETS, name);
    file = fopen(path, "rb");
    expect(file != NULL, name);
    if (file == NULL) return;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)size);
    if (data != NULL && fread(data, 1, (size_t)size, file) == (size_t)size) {
        expect(wgf_core_priv_fs_write(name, data, (int)size), "the file copied in");
    }
    free(data);
    fclose(file);
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }

/* A 16-bit stereo WAV of `frames` frames (4000 at most) at 44.1 kHz. */
static void write_wav(const char *path, unsigned frames)
{
    enum { MOST = 4000 };
    const unsigned FRAMES = frames < MOST ? frames : MOST;
    static unsigned char wav[44 + MOST * 4];
    memcpy(wav, "RIFF", 4);
    put32(wav + 4, 36u + FRAMES * 4u);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    put16(wav + 20, 1);
    put16(wav + 22, 2);
    put32(wav + 24, 44100);
    put32(wav + 28, 44100u * 4u);
    put16(wav + 32, 4);
    put16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    put32(wav + 40, FRAMES * 4u);
    for (unsigned i = 0; i < FRAMES * 2; i++) put16(wav + 44 + i * 2, (i * 37u) & 0xFFFFu);
    expect(wgf_core_priv_fs_write(path, wav, (int)(44 + FRAMES * 4)), "the WAV written");
}

static void test_pair(const char *path, float least, float most)
{
    const wgf_handle_t decoded = wgf_sound_create(path), streamed = wgf_sound_create_streamed(path);
    const wgf_audio_priv_sound_t *d, *s;
    expect(decoded != 0 && streamed != 0 && decoded != streamed, "a path made both ways: two sounds");
    expect(wgf_sound_create(path) == decoded, "the same path again: the same sound");
    wgf_resource_release(decoded);
    expect(wgf_resource_get_status(decoded) == WGF_RESOURCE_STATUS_PENDING &&
               wgf_sound_get_duration(decoded) == 0.0f,
           "PENDING at once, no length yet");
    settle();
    expect(wgf_resource_get_status(decoded) == WGF_RESOURCE_STATUS_READY, path);
    expect(wgf_resource_get_status(streamed) == WGF_RESOURCE_STATUS_READY, "and streamed");
    expect(wgf_sound_get_duration(decoded) >= least && wgf_sound_get_duration(decoded) <= most,
           "its length, decoded");
    expect(fabsf(wgf_sound_get_duration(streamed) - wgf_sound_get_duration(decoded)) < 0.001f,
           "streamed, the same length");
    d = wgf_audio_priv_sound_of(decoded);
    s = wgf_audio_priv_sound_of(streamed);
    expect(d != NULL && d->pcm != NULL && !d->streamed && d->channels >= 1 && d->channels <= 2,
           "decoded: its samples");
    expect(s != NULL && s->pcm == NULL && s->streamed && s->reader.bytes != NULL &&
               s->reader.available == s->reader.size && s->frames == d->frames,
           "streamed: its file, all of it there, the same frames");
    wgf_resource_release(decoded);
    wgf_resource_release(streamed);
    expect(wgf_resource_get_status(decoded) == WGF_RESOURCE_STATUS_NONE &&
               wgf_resource_get_status(streamed) == WGF_RESOURCE_STATUS_NONE && wgf_sound_get_duration(decoded) == 0.0f,
           "released: gone");
}

/* Update until neither sound is loading again (wgf_asset_reload). */
static void settle_reload(wgf_handle_t a, wgf_handle_t b)
{
    const double start = wgf_time_get_seconds();
    while ((wgf_core_priv_resource_is_reloading(a) || wgf_core_priv_resource_is_reloading(b)) &&
           wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

/* A sound loaded again in place, decoded and streamed, while a voice plays each: the new
 * file's length, the voices at its start again and still playing; a paused voice held at
 * the start; a segment past the new end dropped; and a broken file keeps what it had. */
static void test_reload(void)
{
    static float out[512 * 2];
    const wgf_handle_t decoded = wgf_sound_create("sounds/ramp.wav");
    const wgf_handle_t streamed = wgf_sound_create_streamed("sounds/ramp.wav");
    wgf_handle_t one, two, held;
    settle();
    expect(wgf_sound_add_segment(decoded, "late", 0.03f, 0.04f), "a segment near the end of 2000 frames");
    one = wgf_voice_create(decoded);
    two = wgf_voice_create(streamed);
    held = wgf_voice_create(decoded);
    wgf_voice_set_loop(one, true);
    wgf_voice_set_loop(two, true);
    wgf_voice_play(one);
    wgf_voice_play(two);
    wgf_voice_play(held);
    wgf_audio_priv_mix(out, 512, 44100);
    wgf_voice_pause(held);
    expect(wgf_voice_get_position(one) > 0.0f && wgf_voice_get_position(two) > 0.0f, "both playing on");

    write_wav("sounds/ramp.wav", 4000);
    expect(wgf_asset_can_reload("sounds/ramp.wav") && wgf_asset_reload("sounds/ramp.wav") == 2,
           "a path made both ways: both load again");
    settle_reload(decoded, streamed);
    expect(wgf_resource_get_status(decoded) == WGF_RESOURCE_STATUS_READY &&
               wgf_resource_get_status(streamed) == WGF_RESOURCE_STATUS_READY &&
               fabsf(wgf_sound_get_duration(decoded) - 4000.0f / 44100.0f) < 1e-6f &&
               fabsf(wgf_sound_get_duration(streamed) - 4000.0f / 44100.0f) < 1e-6f,
           "in place: the same handles, the new file's length");
    expect(wgf_voice_get_state(one) == WGF_PLAY_STATE_PLAYING && wgf_voice_get_position(one) == 0.0f &&
               wgf_voice_get_state(two) == WGF_PLAY_STATE_PLAYING && wgf_voice_get_position(two) == 0.0f,
           "a voice playing it: at its start again, still playing");
    expect(wgf_voice_get_state(held) == WGF_PLAY_STATE_PAUSED && wgf_voice_get_position(held) == 0.0f,
           "a paused one: held at the start");
    wgf_audio_priv_mix(out, 512, 44100);
    expect(wgf_voice_get_position(one) > 0.0f && wgf_voice_get_position(two) > 0.0f, "and they play the new one");
    expect(wgf_voice_set_segment(one, "late"), "its segment kept");

    write_wav("sounds/ramp.wav", 1000); /* shorter than the segment's start */
    expect(wgf_asset_reload("sounds/ramp.wav") == 2, "saved shorter");
    settle_reload(decoded, streamed);
    expect(fabsf(wgf_sound_get_duration(decoded) - 1000.0f / 44100.0f) < 1e-6f &&
               !wgf_voice_set_segment(held, "late"),
           "the segment past the new end dropped");
    wgf_audio_priv_mix(out, 512, 44100);

    expect(wgf_core_priv_fs_write("sounds/ramp.wav", (const unsigned char *)"not a sound", 11), "saved broken");
    expect(wgf_asset_reload("sounds/ramp.wav") == 2, "a broken file, loading again");
    settle_reload(decoded, streamed);
    expect(wgf_resource_get_status(decoded) == WGF_RESOURCE_STATUS_READY &&
               wgf_resource_get_status(streamed) == WGF_RESOURCE_STATUS_READY &&
               fabsf(wgf_sound_get_duration(streamed) - 1000.0f / 44100.0f) < 1e-6f,
           "what they had is kept");
    wgf_audio_priv_mix(out, 512, 44100);
    wgf_voice_destroy(one);
    wgf_voice_destroy(two);
    wgf_voice_destroy(held);
    wgf_resource_release(decoded);
    wgf_resource_release(streamed);
}

int main(void)
{
    wgf_handle_t missing, garbage, garbage_streamed;
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("audio_sound_test_root");
    copy_asset("music/a_hero_is_born.mp3");
    copy_asset("sounds/click_004.ogg");
    write_wav("sounds/ramp.wav", 2000);
    expect(wgf_core_priv_fs_write("sounds/garbage.ogg", (const unsigned char *)"not a sound", 11), "garbage written");

    test_pair("sounds/ramp.wav", 2000.0f / 44100.0f - 1e-6f, 2000.0f / 44100.0f + 1e-6f);
    test_pair("music/a_hero_is_born.mp3", 85.0f, 95.0f);
    test_pair("sounds/click_004.ogg", 0.01f, 2.0f);

    missing = wgf_sound_create("sounds/missing.wav");
    garbage = wgf_sound_create("sounds/garbage.ogg");
    garbage_streamed = wgf_sound_create_streamed("sounds/garbage.ogg");
    settle();
    expect(!wgf_audio_is_blocked(), "outside a browser: sound never held back");
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "a missing file: FAILED");
    expect(wgf_resource_get_status(garbage) == WGF_RESOURCE_STATUS_FAILED &&
               wgf_resource_get_status(garbage_streamed) == WGF_RESOURCE_STATUS_FAILED,
           "a file no decoder takes: FAILED, decoded or streamed");
    expect(wgf_sound_get_duration(0) == 0.0f && wgf_sound_get_duration(12345) == 0.0f,
           "a handle that isn't a sound: no length");

    /* audio stops with core, holding sounds (they're freed), and the next run's first
       sound brings it back */
    wgf_core_priv_shutdown();
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_NONE, "core's shutdown frees every sound");
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("audio_sound_test_root");
    {
        const wgf_handle_t again = wgf_sound_create("sounds/ramp.wav");
        settle();
        expect(wgf_resource_get_status(again) == WGF_RESOURCE_STATUS_READY, "a second run's sound loads");
    }
    test_reload();
    wgf_core_priv_fs_rmdir("sounds");
    wgf_core_priv_fs_rmdir("music");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
