#include <stdio.h>
#include <stdlib.h>

#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_thread_priv.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"

/* Voices with the mixer on a thread of its own, as a device's: while it mixes without
 * a break, the game's thread creates and destroys voices, plays, pauses, stops, moves,
 * and retargets them between a decoded sound and a streamed one, and sweeps, as wgrender's
 * concurrency test. Nothing may crash, and under the tsan preset nothing may race; the
 * longest a game call took (waiting on the lock, behind the mixer's copy or write-back,
 * never its mixing) is printed. The examples' music is the streamed sound, so the mixer
 * decodes MP3 outside the lock throughout. */

static volatile int running = 1; /* the main thread's word to stop; read by the mixer */
static wgf_core_priv_mutex_t flag_lock;
static int mixes;

static bool still_running(void)
{
    bool on;
    wgf_core_priv_mutex_lock(&flag_lock);
    on = running != 0;
    wgf_core_priv_mutex_unlock(&flag_lock);
    return on;
}

static void mixer(void *arg)
{
    static float out[512 * 2];
    (void)arg;
    while (still_running()) {
        wgf_audio_priv_mix(out, 512, 48000);
        mixes++;
    }
}

int main(void)
{
    wgf_core_priv_thread_t thread;
    wgf_handle_t sounds[2], voices[8] = {0};
    double longest = 0.0, start;
    unsigned seed = 12345u;
    int failures = 0;

    if (!wgf_core_priv_thread_is_available()) {
        printf("wgf_audio_voice_thread_test: SKIPPING (no threads here)\n");
        return 77;
    }
    wgf_core_priv_init();
    wgf_core_priv_mutex_init(&flag_lock);
    wgf_core_priv_fs_set_root(WGF_TEST_ASSETS);
    /* the examples' click, decoded, and their music, streamed, read where they are */
    sounds[0] = wgf_sound_create("sounds/click_004.ogg");
    sounds[1] = wgf_sound_create_streamed("music/a_hero_is_born.mp3");
    start = wgf_time_get_seconds();
    while (wgf_core_priv_load_get_pending_count() > 0 && wgf_time_get_seconds() - start < 60.0) wgf_core_priv_update();
    if (wgf_resource_get_status(sounds[0]) != WGF_RESOURCE_STATUS_READY ||
        wgf_resource_get_status(sounds[1]) != WGF_RESOURCE_STATUS_READY) {
        printf("FAIL: the sounds didn't load\n");
        return 1;
    }
    if (!wgf_core_priv_thread_create(&thread, mixer, NULL)) {
        printf("FAIL: no mixer thread\n");
        return 1;
    }
    start = wgf_time_get_seconds();
    for (int i = 0; i < 20000 || wgf_time_get_seconds() - start < 0.5; i++) { /* long enough to interleave */
        const int slot = (int)((seed = seed * 1664525u + 1013904223u) >> 8) % 8;
        const int action = (int)(seed >> 20) % 10;
        const double before = wgf_time_get_seconds();
        switch (action) {
        case 0: if (voices[slot] == 0) voices[slot] = wgf_voice_create(sounds[slot % 2]); break;
        case 1: wgf_voice_destroy(voices[slot]); voices[slot] = 0; break;
        case 2: wgf_voice_play(voices[slot]); break;
        case 3: wgf_voice_pause(voices[slot]); break;
        case 4: wgf_voice_resume(voices[slot]); break;
        case 5: wgf_voice_stop(voices[slot]); break;
        case 6: wgf_voice_set_sound(voices[slot], sounds[(seed >> 4) % 2]); break;
        case 7: wgf_voice_set_position(voices[slot], (float)((seed >> 3) % 100) * 0.05f); break;
        case 8: wgf_voice_set_pitch(voices[slot], (float)((seed >> 5) % 5) * 0.5f); break;
        default: wgf_core_priv_part_update(0.016f); break; /* the sweep */
        }
        if (wgf_time_get_seconds() - before > longest) longest = wgf_time_get_seconds() - before;
    }
    wgf_core_priv_mutex_lock(&flag_lock);
    running = 0;
    wgf_core_priv_mutex_unlock(&flag_lock);
    wgf_core_priv_thread_join(&thread);
    printf("wgf_audio_voice_thread_test: game calls beside %d mixes for %.2f s; the longest call %.3f ms\n", mixes,
           wgf_time_get_seconds() - start, longest * 1000.0);
    if (mixes == 0) {
        printf("FAIL: the mixer never ran\n");
        failures++;
    }
    for (int i = 0; i < 8; i++) wgf_voice_destroy(voices[i]);
    wgf_core_priv_part_update(0.0f);
    wgf_resource_release(sounds[0]);
    wgf_resource_release(sounds[1]);
    wgf_core_priv_shutdown();
    wgf_core_priv_mutex_destroy(&flag_lock);
    return failures == 0 ? 0 : 1;
}
