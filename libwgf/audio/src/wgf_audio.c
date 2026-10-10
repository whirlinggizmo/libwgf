/* audio's own section (CONVENTIONS.md: a section named after its layer lives in
 * <layer>/src/wgf_<layer>.c): the lock, the layer-wide calls (wgf_audio.h), and audio as
 * a part on core's list, installed by the first sound: its update sweeps the voices
 * once a frame, and its stop ends the platform's playing, then frees every voice and
 * sound, then the lock. */
#include "wgf_audio.h"

#include <math.h>

#include "wgf_audio_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_thread_priv.h"

static wgf_core_priv_mutex_t mutex;
static float volume = 1.0f; /* the mixer's settings: under the lock once audio runs */
static bool paused;

void wgf_audio_priv_lock(void)
{
    wgf_core_priv_mutex_lock(&mutex);
}

void wgf_audio_priv_unlock(void)
{
    wgf_core_priv_mutex_unlock(&mutex);
}

/* ------------------------------------------------------------------ the part ---- */

static void update(float dt)
{
    (void)dt;
    wgf_audio_priv_sound_feed(); /* files arriving: before the voices, so a sound READY now plays */
    wgf_audio_priv_platform_update();
    wgf_audio_priv_voice_sweep();
}

static void stop(void)
{
    wgf_audio_priv_platform_stop(); /* nothing mixes from here on */
    wgf_audio_priv_voice_stop_all();
    wgf_audio_priv_sound_stop_all();
    wgf_core_priv_mutex_destroy(&mutex);
}

static wgf_core_priv_part_t part = {.name = "audio",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_AUDIO,
                                    .order = WGF_CORE_PRIV_PART_AUDIO,
                                    .update = update,
                                    .stop = stop};

void wgf_audio_priv_install(void)
{
    if (part.installed) return;
    wgf_core_priv_mutex_init(&mutex);
    wgf_core_priv_part_install(&part);
    wgf_audio_priv_platform_start();
    wgf_audio_priv_platform_master(volume, paused);
}

/* ---------------------------------------------------------------- the mixer ---- */

float wgf_audio_priv_get_volume(void)
{
    return volume;
}

bool wgf_audio_priv_is_paused(void)
{
    return paused;
}

bool wgf_audio_set_volume(float to)
{
    if (!isfinite(to) || to < 0.0f) return false;
    if (part.installed) wgf_audio_priv_lock();
    volume = to;
    if (part.installed) {
        wgf_audio_priv_platform_master(volume, paused);
        wgf_audio_priv_unlock();
    }
    return true;
}

float wgf_audio_get_volume(void)
{
    return volume;
}

void wgf_audio_set_paused(bool to)
{
    if (part.installed) wgf_audio_priv_lock();
    paused = to;
    if (part.installed) {
        wgf_audio_priv_platform_master(volume, paused);
        wgf_audio_priv_unlock();
    }
}

bool wgf_audio_is_paused(void)
{
    return paused;
}
