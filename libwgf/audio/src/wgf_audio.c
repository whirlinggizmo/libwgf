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
static float listener[3], facing[3] = {0.0f, 0.0f, -1.0f}; /* the listener: under the lock once audio runs */

void wgf_audio_priv_lock(void)
{
    wgf_core_priv_mutex_lock(&mutex);
}

void wgf_audio_priv_unlock(void)
{
    wgf_core_priv_mutex_unlock(&mutex);
}

/* ------------------------------------------------------------------ the part ---- */

static void each_spatial(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user)
{
    (void)user;
    if (voice_ptr->spatial) wgf_audio_priv_platform_apply(voice, voice_ptr);
}

static void update(float dt)
{
    wgf_audio_priv_bus_update(dt);
    /* spatial voices given what the listener hears of them now (the web's gains; the mixer
       works it out as it mixes) */
    wgf_audio_priv_lock();
    wgf_audio_priv_voice_each(each_spatial, NULL);
    wgf_audio_priv_unlock();
    wgf_audio_priv_sound_feed(); /* files arriving: before the voices, so a sound READY now plays */
    wgf_audio_priv_platform_update();
    wgf_audio_priv_voice_sweep();
}

static void stop(void)
{
    wgf_audio_priv_platform_stop(); /* nothing mixes from here on */
    wgf_audio_priv_voice_stop_all();
    wgf_audio_priv_sound_stop_all();
    wgf_audio_priv_bus_reset();
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

bool wgf_audio_priv_is_running(void)
{
    return part.installed;
}

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

/* --------------------------------------------------------------- the listener ---- */

void wgf_audio_priv_spatial(const wgf_audio_priv_voice_t *voice_ptr, float *gain, float *pan)
{
    float dx, dy, dz, d, rx, rz, len, side;
    *gain = 1.0f;
    *pan = voice_ptr->pan;
    if (!voice_ptr->spatial) return;
    dx = voice_ptr->place[0] - listener[0], dy = voice_ptr->place[1] - listener[1], dz = voice_ptr->place[2] - listener[2];
    d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d >= voice_ptr->range[1]) *gain = 0.0f;
    else if (d > voice_ptr->range[0]) *gain = 1.0f - (d - voice_ptr->range[0]) / (voice_ptr->range[1] - voice_ptr->range[0]);
    /* its side: along the listener's right, facing x up (y) */
    rx = -facing[2], rz = facing[0];
    len = sqrtf(rx * rx + rz * rz);
    side = d > 1e-4f && len > 1e-6f ? (dx * rx + dz * rz) / (len * d) : 0.0f;
    *pan = *pan + side;
    *pan = *pan < -1.0f ? -1.0f : *pan > 1.0f ? 1.0f : *pan;
}

static void set_listener(float *to, float x, float y, float z)
{
    if (part.installed) wgf_audio_priv_lock();
    to[0] = x, to[1] = y, to[2] = z;
    if (part.installed) wgf_audio_priv_unlock();
}

bool wgf_audio_set_listener_position(float x, float y, float z)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return false;
    set_listener(listener, x, y, z);
    return true;
}

wgf_vec3_t wgf_audio_get_listener_position(void)
{
    return wgf_vec3_make(listener[0], listener[1], listener[2]);
}

bool wgf_audio_set_listener_facing(float x, float y, float z)
{
    const float across = sqrtf(x * x + z * z), len = sqrtf(x * x + y * y + z * z);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || across < 1e-6f) return false;
    set_listener(facing, x / len, y / len, z / len);
    return true;
}

wgf_vec3_t wgf_audio_get_listener_facing(void)
{
    return wgf_vec3_make(facing[0], facing[1], facing[2]);
}
