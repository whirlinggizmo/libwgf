#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_asset.h"
#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_thread_priv.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"

/* A streamed sound played while its file arrives, natively (ROADMAP.md, phase 3): the
 * program's downloads (wgf_asset_set_fetching) answered by the test, which writes each
 * file under .part/ a piece at a time, as a downloader does. Headless: the mixer pulled
 * by the test at 48 kHz, 10 ms at a time, the updates between. Checked: an Ogg READY
 * with its start, its length 0 until whole, its samples a whole file's, a voice waiting
 * where the file runs out and paused across the moment it is whole; a short WAV that
 * can't start until whole; an MP3's length from its Info frame, and a download failing
 * partway; one a manifest lists that isn't what it lists; and the mixer on a thread of
 * its own while a file arrives, for the tsan preset. Files under
 * audio_arrive_test_cache/. */

#define RATE 48000
#define STEP_FRAMES 480

static int failures;
static float mixed[STEP_FRAMES * 2];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data = NULL;
    long length;
    *size = 0;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length > 0 && (data = (unsigned char *)malloc((size_t)length)) != NULL &&
        fread(data, 1, (size_t)length, f) == (size_t)length) {
        *size = (size_t)length;
    }
    fclose(f);
    return data;
}

/* A mono 16-bit WAV of `frames` at 44.1 kHz, a 110 Hz saw, into `out` (44 + 2 * frames). */
static size_t make_wav(unsigned char *out, unsigned frames)
{
    static const unsigned char header[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                                             16,  0,   0,   0,   1, 0, 1, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0,
                                             2,   0,   16,  0,   'd', 'a', 't', 'a', 0, 0, 0, 0};
    const unsigned data = frames * 2;
    memcpy(out, header, sizeof(header));
    for (int b = 0; b < 4; b++) {
        out[4 + b] = (unsigned char)((36 + data) >> (8 * b));
        out[40 + b] = (unsigned char)(data >> (8 * b));
    }
    for (unsigned i = 0; i < frames; i++) {
        const int value = (int)(i % 400) * 80 - 16000;
        out[44 + i * 2] = (unsigned char)value;
        out[45 + i * 2] = (unsigned char)(value >> 8);
    }
    return 44 + data;
}

/* An update, as a frame's: core's (the downloads' answers), then the parts' (audio's,
 * which reads what arrived). */
static void tick(void)
{
    wgf_core_priv_update();
    wgf_core_priv_part_update((float)STEP_FRAMES / RATE);
}

static void advance(double seconds)
{
    for (double done = 0.0; done < seconds - 1e-9; done += (double)STEP_FRAMES / RATE) {
        wgf_audio_priv_mix(mixed, STEP_FRAMES, RATE);
        tick();
    }
}

/* The next download asked for, its destination in `dest`; 0 when none comes. */
static wgf_handle_t take(char *dest, size_t dest_size)
{
    for (int i = 0; i < 100; i++) {
        const wgf_handle_t request = wgf_asset_fetch_next();
        if (request != 0) {
            snprintf(dest, dest_size, "%s", wgf_asset_fetch_get_dest(request));
            return request;
        }
        tick();
    }
    return 0;
}

/* Bytes [from, to) of `data` added to the download at `dest`. */
static void append(const char *dest, const unsigned char *data, size_t from, size_t to)
{
    FILE *f = fopen(dest, "ab");
    if (f == NULL) return;
    fwrite(data + from, 1, to - from, f);
    fclose(f);
}

/* A whole download: requested, written, answered, and READY. */
static wgf_handle_t download(const char *path, const unsigned char *data, size_t size)
{
    char dest[1024];
    const wgf_handle_t sound = wgf_sound_create_streamed(path);
    const wgf_handle_t request = take(dest, sizeof(dest));
    append(dest, data, 0, size);
    wgf_asset_fetch_done(request, true);
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        tick(); /* whole: an ordinary load, prepared on a worker */
    }
    return sound;
}

/* The first `frames` a voice of `sound` mixes, alone, from its start. */
static void capture(wgf_handle_t sound, float *out, int frames)
{
    const wgf_handle_t voice = wgf_voice_create(sound);
    wgf_voice_play(voice);
    tick(); /* the sweep starts it */
    for (int done = 0; done < frames; done += STEP_FRAMES) wgf_audio_priv_mix(out + done * 2, STEP_FRAMES, RATE);
    wgf_voice_destroy(voice);
    tick();
}

/* An Ogg says nothing of its length until whole; it plays from what has come, the same
 * samples as a whole file's, waits where the file runs out, and holds its place paused
 * across the moment the file is whole, where its decoder could seek from then on. */
static void test_ogg(const unsigned char *ogg, size_t size)
{
    enum { FRAMES = RATE / 2 };
    static float whole[FRAMES * 2], arriving[FRAMES * 2];
    char dest[1024];
    wgf_handle_t reference, sound, request, voice;
    float held, before;
    bool same = true;

    reference = download("music/reference.ogg", ogg, size);
    expect(wgf_resource_get_status(reference) == WGF_RESOURCE_STATUS_READY, "ogg: the whole file READY");
    capture(reference, whole, FRAMES);

    sound = wgf_sound_create_streamed("music/long.ogg");
    request = take(dest, sizeof(dest));
    expect(request != 0, "ogg: its download asked for");
    append(dest, ogg, 0, 40 * 1024);
    for (int i = 0; i < 5; i++) tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_PENDING, "ogg: 40 KB in: PENDING still");
    append(dest, ogg, 40 * 1024, 200 * 1024);
    tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY, "ogg: 200 KB in: READY");
    expect(wgf_sound_get_duration(sound) == 0.0f, "ogg: its length 0 until whole");

    capture(sound, arriving, FRAMES);
    for (int i = 0; i < FRAMES * 2; i++) same = same && arriving[i] == whole[i];
    expect(same, "ogg: while it arrives, the same samples as the whole file");

    voice = wgf_voice_create(sound);
    wgf_voice_play(voice);
    advance(30.0); /* far past what 200 KB holds, less the margin */
    held = wgf_voice_get_position(voice);
    advance(1.0);
    expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING, "ogg: caught up with the file: PLAYING");
    expect(held > 1.0f && held < 20.0f && wgf_voice_get_position(voice) == held,
           "ogg: caught up with the file: waiting where it runs out");

    wgf_voice_pause(voice);
    before = wgf_voice_get_position(voice);
    append(dest, ogg, 200 * 1024, size);
    wgf_asset_fetch_done(request, true);
    for (int i = 0; i < 5; i++) tick();
    expect(fabsf(wgf_sound_get_duration(sound) - wgf_sound_get_duration(reference)) < 1e-4f &&
               wgf_sound_get_duration(sound) > 39.0f,
           "ogg: whole: its length, as the whole file's");
    wgf_voice_resume(voice);
    expect(wgf_voice_get_position(voice) == before, "ogg: paused across whole: where it was");
    advance(1.0);
    expect(fabsf(wgf_voice_get_position(voice) - (before + 1.0f)) < 0.02f, "ogg: resumed: on from there");
    advance(45.0);
    expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE, "ogg: played to its end: COMPLETE");

    wgf_voice_destroy(voice);
    wgf_resource_release(sound);
    wgf_resource_release(reference);
    tick();
}

/* A file shorter than the margin a decoder keeps ahead of one arriving: it starts only
 * once whole, and then plays to its end. */
static void test_short_wav(void)
{
    static unsigned char wav[44 + 8820 * 2];
    const size_t size = make_wav(wav, 8820); /* 0.2 s */
    char dest[1024];
    const wgf_handle_t sound = wgf_sound_create_streamed("sounds/short.wav");
    const wgf_handle_t request = take(dest, sizeof(dest));
    wgf_handle_t voice;
    append(dest, wav, 0, size / 2);
    for (int i = 0; i < 5; i++) tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_PENDING, "short wav: half of it: PENDING");
    voice = wgf_voice_create(sound);
    wgf_voice_play(voice); /* waits for its sound */
    advance(0.5);
    expect(wgf_voice_get_position(voice) == 0.0f, "short wav: nothing played before it is whole");
    append(dest, wav, size / 2, size);
    wgf_asset_fetch_done(request, true);
    tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY &&
               fabsf(wgf_sound_get_duration(sound) - 0.2f) < 1e-4f,
           "short wav: whole: READY, 0.2 s");
    advance(1.0);
    expect(wgf_voice_get_state(voice) == WGF_PLAY_STATE_COMPLETE, "short wav: played to its end");
    wgf_voice_destroy(voice);
    wgf_resource_release(sound);
    tick();
}

/* A WAV's header and an MP3's Info frame say their length before the rest has come; a
 * download that fails partway fails its sound, and its voice stops. */
static void test_known_lengths(const unsigned char *mp3)
{
    static unsigned char wav[44 + 88200 * 2];
    const size_t wav_size = make_wav(wav, 88200); /* 2 s */
    char dest[1024];
    wgf_handle_t sound, request, voice;

    sound = wgf_sound_create_streamed("sounds/two.wav");
    request = take(dest, sizeof(dest));
    append(dest, wav, 0, 100 * 1024);
    tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY && wgf_sound_get_duration(sound) == 2.0f,
           "wav: by its header: READY and 2 s, the rest still to come");
    append(dest, wav, 100 * 1024, wav_size);
    wgf_asset_fetch_done(request, true);
    tick();
    wgf_resource_release(sound);

    sound = wgf_sound_create_streamed("music/hero.mp3");
    request = take(dest, sizeof(dest));
    append(dest, mp3, 0, 300 * 1024);
    tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY && wgf_sound_get_duration(sound) > 85.0f &&
               wgf_sound_get_duration(sound) < 95.0f,
           "mp3: by its Info frame: READY with its length, the rest still to come");
    voice = wgf_voice_create(sound);
    wgf_voice_play(voice);
    advance(1.0);
    expect(wgf_voice_get_position(voice) > 0.9f, "mp3: playing while it arrives");
    wgf_asset_fetch_done(request, false); /* the connection lost */
    advance(0.1);
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_FAILED, "mp3: its download failed: FAILED");
    expect(wgf_voice_get_state(voice) != WGF_PLAY_STATE_PLAYING, "mp3: its voice stopped");
    wgf_voice_destroy(voice);
    wgf_resource_release(sound);
    tick();
}

/* A path rule's candidate is a download of its own under a URL host: one missing (a
 * mod without this file) falls through to the file's own path, the sound streaming from
 * there; it is the file only once its bytes come. */
static void test_redirect(const unsigned char *ogg, size_t size)
{
    char dest[1024];
    wgf_handle_t sound, request;
    wgf_asset_add_redirect("modded/", "mods/loud/modded/");
    sound = wgf_sound_create_streamed("modded/long.ogg");
    request = take(dest, sizeof(dest));
    expect(request != 0 && strstr(wgf_asset_fetch_get_url(request), "mods/loud/modded/long.ogg") != NULL,
           "redirect: the mod's file asked for first");
    tick();
    wgf_asset_fetch_done(request, false); /* a 404: nothing written */
    request = take(dest, sizeof(dest));
    expect(request != 0 && strstr(wgf_asset_fetch_get_url(request), "game/modded/long.ogg") != NULL,
           "redirect: the mod hasn't it: the file's own asked for");
    append(dest, ogg, 0, 200 * 1024);
    tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY, "redirect: streaming from its own path");
    append(dest, ogg, 200 * 1024, size);
    wgf_asset_fetch_done(request, true);
    for (int i = 0; i < 5; i++) tick();
    expect(wgf_sound_get_duration(sound) > 39.0f, "redirect: whole, from its own path");
    wgf_resource_release(sound);
    tick();
}

/* A file a manifest lists plays while it arrives, and fails once whole if it isn't
 * what the manifest says. */
static void test_manifest(void)
{
    static unsigned char wav[44 + 88200 * 2];
    const size_t size = make_wav(wav, 88200);
    static const char manifest[] = "{\"wgf_manifest\": 1, \"files\": {\"listed.wav\": "
                                   "\"sha256:0000000000000000000000000000000000000000000000000000000000000000\"}}";
    char dest[1024];
    wgf_handle_t sound, request;
    wgf_asset_set_manifest("manifest.json");
    sound = wgf_sound_create_streamed("listed.wav");
    request = take(dest, sizeof(dest)); /* the manifest first */
    expect(strstr(wgf_asset_fetch_get_url(request), "manifest.json") != NULL, "manifest: asked for first");
    append(dest, (const unsigned char *)manifest, 0, sizeof(manifest) - 1);
    wgf_asset_fetch_done(request, true);
    request = take(dest, sizeof(dest));
    append(dest, wav, 0, 100 * 1024);
    tick();
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY, "manifest: listed: READY while it arrives");
    append(dest, wav, 100 * 1024, size);
    wgf_asset_fetch_done(request, true);
    for (int i = 0; i < 20; i++) tick(); /* hashed a slice an update */
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_FAILED, "manifest: not what it lists: FAILED");
    wgf_resource_release(sound);
    tick();
}

/* The mixer on a thread of its own, as a device's, decoding a file as it arrives a
 * piece an update: for the tsan preset, nothing may race. */
static wgf_core_priv_mutex_t flag_lock;
static int mixing = 1;

static bool still_mixing(void)
{
    bool on;
    wgf_core_priv_mutex_lock(&flag_lock);
    on = mixing != 0;
    wgf_core_priv_mutex_unlock(&flag_lock);
    return on;
}

static void mixer(void *arg)
{
    static float out[STEP_FRAMES * 2];
    (void)arg;
    while (still_mixing()) wgf_audio_priv_mix(out, STEP_FRAMES, RATE);
}

static void test_threaded(const unsigned char *ogg, size_t size)
{
    wgf_core_priv_thread_t thread;
    char dest[1024];
    const wgf_handle_t sound = wgf_sound_create_streamed("music/raced.ogg");
    const wgf_handle_t request = take(dest, sizeof(dest));
    const wgf_handle_t voice = wgf_voice_create(sound);
    if (!wgf_core_priv_thread_is_available()) return;
    wgf_core_priv_mutex_init(&flag_lock);
    wgf_voice_play(voice);
    if (!wgf_core_priv_thread_create(&thread, mixer, NULL)) return;
    for (size_t at = 0; at < size; at += 16 * 1024) {
        append(dest, ogg, at, at + 16 * 1024 < size ? at + 16 * 1024 : size);
        tick();
    }
    wgf_asset_fetch_done(request, true);
    for (int i = 0; i < 10; i++) tick();
    wgf_core_priv_mutex_lock(&flag_lock);
    mixing = 0;
    wgf_core_priv_mutex_unlock(&flag_lock);
    wgf_core_priv_thread_join(&thread);
    expect(wgf_resource_get_status(sound) == WGF_RESOURCE_STATUS_READY && wgf_sound_get_duration(sound) > 39.0f,
           "threaded: whole while the mixer played it");
    wgf_voice_destroy(voice);
    wgf_resource_release(sound);
    tick();
    wgf_core_priv_mutex_destroy(&flag_lock);
}

int main(void)
{
    size_t ogg_size, mp3_size;
    unsigned char *ogg = read_file(WGF_TEST_DATA "/long.ogg", &ogg_size);
    unsigned char *mp3 = read_file(WGF_TEST_ASSETS "/music/a_hero_is_born.mp3", &mp3_size);
    expect(ogg != NULL && mp3 != NULL, "the test's files read");
    if (ogg == NULL || mp3 == NULL) return 1;

    wgf_core_priv_init();
    wgf_asset_set_cache_dir("audio_arrive_test_cache");
    wgf_asset_clear_cache();
    wgf_asset_set_host("https://example.com/game");
    wgf_asset_set_fetching(true);

    test_ogg(ogg, ogg_size);
    test_short_wav();
    test_known_lengths(mp3);
    test_threaded(ogg, ogg_size);
    test_redirect(ogg, ogg_size);
    test_manifest(); /* last: the manifest stays set */

    wgf_asset_clear_cache();
    wgf_core_priv_shutdown();
    free(ogg);
    free(mp3);
    return failures == 0 ? 0 : 1;
}
