#include "wgf_sound.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */

/* Sounds: the resource half of audio, the same on both platforms; what loading one
 * means is each platform's (wgf_audio_priv.h). Decoded and streamed sounds are two
 * handle kinds, each a pool of its own, so a path made both ways is two sounds. The
 * first sound installs audio (wgf_audio.c): until then nothing of it is linked or
 * running. */

/* --------------------------------------------------------------- the pools ---- */

static struct {
    bool ready;
    wgf_core_priv_handle_pool_t pool;
    wgf_audio_priv_sound_t *sounds;
} kinds[2]; /* [streamed] */

wgf_audio_priv_sound_t *wgf_audio_priv_sound_of_at(wgf_handle_t sound, const char *caller)
{
    uint16_t index;
    const int streamed = sound != 0 && WGF_CORE_PRIV_HANDLE_KIND(sound) == WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED;
    if (sound == 0 || (!streamed && WGF_CORE_PRIV_HANDLE_KIND(sound) != WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND) ||
        !kinds[streamed].ready || !wgf_core_priv_handle_pool_resolve_at(&kinds[streamed].pool, sound, &index, caller)) {
        return NULL;
    }
    return &kinds[streamed].sounds[index];
}

static void *prepare_decoded(const char *path)
{
    return wgf_audio_priv_sound_prepare(path, false);
}

static void *prepare_streamed(const char *path)
{
    return wgf_audio_priv_sound_prepare(path, true);
}

static void check_segments(wgf_audio_priv_sound_t *sound_ptr);

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t sound)
{
    wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(sound);
    if (sound_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    wgf_core_priv_load_step_t step;
    sound_ptr->streamed = WGF_CORE_PRIV_HANDLE_KIND(sound) == WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED;
    step = wgf_audio_priv_sound_finish(prepared, sound, sound_ptr);
    if (step == WGF_CORE_PRIV_LOAD_DONE) {
        wgf_audio_priv_lock();
        check_segments(wgf_audio_priv_sound_of(sound));
        wgf_audio_priv_unlock();
    }
    return step;
}

static void fail(wgf_handle_t sound)
{
    wgf_core_priv_resource_failed(sound);
}

/* A streamed sound's file still arriving: the platform reads it as it comes. */
static void arrive(wgf_handle_t sound, const char *path, wgf_handle_t arrival)
{
    wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(sound);
    if (sound_ptr == NULL) {
        if (wgf_core_priv_load_hooks.arrival_end != NULL) wgf_core_priv_load_hooks.arrival_end(arrival);
        return;
    }
    sound_ptr->streamed = true;
    wgf_audio_priv_sound_arrive(sound, sound_ptr, path, arrival);
}

static const wgf_core_priv_loader_t loaders[2] = {
    {"sound", prepare_decoded, finish, wgf_audio_priv_sound_discard, fail, NULL, false},
    {"streamed sound", prepare_streamed, finish, wgf_audio_priv_sound_discard, fail, arrive, false},
};

static const wgf_core_priv_loader_t *decoded_loader(const char *path)
{
    (void)path;
    return &loaders[0];
}

static const wgf_core_priv_loader_t *streamed_loader(const char *path)
{
    (void)path;
    return &loaders[1];
}

/* Under the lock (the resource core takes it): the mixer may be reading the record. No
 * voice points at its segments now: a voice holds a reference to its sound. */
static void free_sound(wgf_handle_t sound, void *record)
{
    wgf_audio_priv_sound_t *sound_ptr = (wgf_audio_priv_sound_t *)record;
    for (int i = 0; i < sound_ptr->segment_count; i++) free(sound_ptr->segments[i]);
    free(sound_ptr->segments);
    sound_ptr->segments = NULL;
    sound_ptr->segment_count = sound_ptr->segment_capacity = 0;
    wgf_audio_priv_sound_free(sound, sound_ptr);
}

wgf_audio_priv_segment_t *wgf_audio_priv_sound_segment(const wgf_audio_priv_sound_t *sound_ptr, const char *name)
{
    for (int i = 0; sound_ptr != NULL && i < sound_ptr->segment_count; i++) {
        if (strcmp(sound_ptr->segments[i]->name, name) == 0) return sound_ptr->segments[i];
    }
    return NULL;
}

static void forget_segment(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *segment)
{
    (void)voice;
    if (voice_ptr->segment == segment) voice_ptr->segment = NULL; /* the whole sound from now */
}

/* A sound just READY: a segment starting at or past its end is gone (logged), as
 * segments added before the file came are checked against it only now. */
static void check_segments(wgf_audio_priv_sound_t *sound_ptr)
{
    for (int i = 0; i < sound_ptr->segment_count;) {
        wgf_audio_priv_segment_t *segment = sound_ptr->segments[i];
        if (segment->start < sound_ptr->duration) {
            i++;
            continue;
        }
        wgf_log_warn("wgf_sound_add_segment: %s: segment \"%s\" starts at %.3f s, past its %.3f s: dropped",
                     sound_ptr->resource.path, segment->name, segment->start, sound_ptr->duration);
        wgf_audio_priv_voice_each(forget_segment, segment);
        free(segment);
        sound_ptr->segments[i] = sound_ptr->segments[--sound_ptr->segment_count];
    }
}

static const wgf_core_priv_resource_kind_t resource_kinds[2] = {
    {.create = "wgf_sound_create",
     .loader = decoded_loader,
     .free = free_sound,
     .lock = wgf_audio_priv_lock,
     .unlock = wgf_audio_priv_unlock},
    {.create = "wgf_sound_create_streamed",
     .loader = streamed_loader,
     .free = free_sound,
     .lock = wgf_audio_priv_lock,
     .unlock = wgf_audio_priv_unlock},
};

/* ----------------------------------------------------------- audio's stop ---- */

/* Every sound freed and the pools gone (audio's stop, wgf_audio.c). */
void wgf_audio_priv_sound_stop_all(void)
{
    for (int streamed = 0; streamed < 2; streamed++) {
        if (!kinds[streamed].ready) continue;
        wgf_core_priv_resource_unregister(&kinds[streamed].pool);
        wgf_core_priv_handle_pool_destroy(&kinds[streamed].pool);
        memset(&kinds[streamed], 0, sizeof(kinds[streamed]));
    }
    wgf_audio_priv_sound_stop();
}

/* The first sound: audio installs (wgf_audio.c), and the sound's pool is made. */
static bool install(int streamed)
{
    static const wgf_core_priv_handle_kind_t handle_kinds[2] = {WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND,
                                                                WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED};
    wgf_audio_priv_install();
    if (!kinds[streamed].ready) {
        kinds[streamed].ready =
            wgf_core_priv_handle_pool_init(&kinds[streamed].pool, handle_kinds[streamed],
                                           (void **)&kinds[streamed].sounds, sizeof(wgf_audio_priv_sound_t), 16, 4096);
        if (kinds[streamed].ready) wgf_core_priv_resource_register(&kinds[streamed].pool, &resource_kinds[streamed]);
    }
    return kinds[streamed].ready;
}

/* ------------------------------------------------------------- the public API ---- */

wgf_handle_t wgf_sound_create(const char *path)
{
    return install(0) ? wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND, path) : 0;
}

wgf_handle_t wgf_sound_create_streamed(const char *path)
{
    return install(1) ? wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED, path) : 0;
}

bool wgf_sound_add_segment(wgf_handle_t sound, const char *name, float start, float end)
{
    wgf_audio_priv_sound_t *sound_ptr;
    wgf_audio_priv_segment_t *segment;
    bool added = false;
    if (name == NULL || name[0] == '\0' || strlen(name) >= sizeof(segment->name) || !isfinite(start) ||
        !isfinite(end) || start < 0.0f || start >= end) {
        return false;
    }
    wgf_audio_priv_lock();
    sound_ptr = wgf_audio_priv_sound_of(sound);
    if (sound_ptr != NULL && WGF_CORE_PRIV_HANDLE_KIND(sound) == WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND &&
        sound_ptr->resource.status != WGF_RESOURCE_STATUS_FAILED &&
        !(sound_ptr->resource.status == WGF_RESOURCE_STATUS_READY && start >= sound_ptr->duration)) {
        segment = wgf_audio_priv_sound_segment(sound_ptr, name);
        if (segment == NULL && sound_ptr->segment_count == sound_ptr->segment_capacity) {
            const int capacity = sound_ptr->segment_capacity > 0 ? sound_ptr->segment_capacity * 2 : 8;
            wgf_audio_priv_segment_t **grown = (wgf_audio_priv_segment_t **)realloc(
                sound_ptr->segments, sizeof(wgf_audio_priv_segment_t *) * (size_t)capacity);
            if (grown != NULL) {
                sound_ptr->segments = grown;
                sound_ptr->segment_capacity = capacity;
            }
        }
        if (segment == NULL && sound_ptr->segment_count < sound_ptr->segment_capacity &&
            (segment = (wgf_audio_priv_segment_t *)calloc(1, sizeof(*segment))) != NULL) {
            strcpy(segment->name, name);
            sound_ptr->segments[sound_ptr->segment_count++] = segment;
        }
        if (segment != NULL) {
            segment->start = start;
            segment->end = end;
            added = true;
        }
    }
    wgf_audio_priv_unlock();
    return added;
}

float wgf_sound_get_duration(wgf_handle_t sound)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(sound);
    return sound_ptr != NULL && sound_ptr->resource.status == WGF_RESOURCE_STATUS_READY ? sound_ptr->duration : 0.0f;
}
