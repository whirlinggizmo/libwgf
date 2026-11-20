/* Buses (wgf_bus.h): helix-legacy's Bus, Audio.master, music, and effects, as handles. A
 * bus is a small record in a fixed table of 64 (they never move, so the mixer may read
 * them); what voices are heard at is read from it under the audio lock (natively by the
 * mixer, on the web carried into each voice's gain). Fades step once a frame in audio's
 * update, even in decibels as helix's Fade is. The table is made by the first bus call or
 * the first voice, so buses may be set before audio starts. */
#include "wgf_bus.h"

#include <math.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_core_handle_priv.h"

#define MAX_BUSES 64
#define FLOOR_DB -60.0f /* a silent end, as helix's Fade takes it */

typedef struct bus_t {
    wgf_handle_t parent;
    float volume;
    bool muted;
    bool fading;
    float from, to, seconds, elapsed;
} bus_t;

static struct {
    bool ready;
    wgf_core_priv_handle_pool_t pool;
    bus_t *buses;
    wgf_bus_t master, music, effects;
} bp;

static void ensure(void);

static bus_t *bus_of_at(wgf_handle_t bus, const char *caller)
{
    uint16_t index;
    ensure();
    if (!bp.ready || !wgf_core_priv_handle_pool_resolve_at(&bp.pool, bus, &index, caller)) return NULL;
    return &bp.buses[index];
}
#define bus_of(bus) bus_of_at((bus), WGF_CORE_PRIV_CALLER)

static wgf_bus_t make(wgf_handle_t parent)
{
    const wgf_handle_t bus = wgf_core_priv_handle_pool_alloc(&bp.pool);
    uint16_t index;
    if (bus == 0 || !wgf_core_priv_handle_pool_resolve(&bp.pool, bus, &index)) return 0;
    memset(&bp.buses[index], 0, sizeof(bus_t));
    bp.buses[index].parent = parent;
    bp.buses[index].volume = 1.0f;
    return bus;
}

static void ensure(void)
{
    if (bp.ready) return;
    bp.ready = wgf_core_priv_handle_pool_init(&bp.pool, WGF_CORE_PRIV_HANDLE_KIND_AUDIO_BUS, (void **)&bp.buses,
                                              sizeof(bus_t), MAX_BUSES + 1, MAX_BUSES + 1); /* slot 0 is none */
    if (!bp.ready) return;
    bp.master = make(0);
    bp.music = make(bp.master);
    bp.effects = make(bp.master);
}

/* Changes the mixer reads go under the lock, once audio runs. */
static void lock(void)
{
    if (wgf_audio_priv_is_running()) wgf_audio_priv_lock();
}

static void each_apply(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user)
{
    (void)user;
    wgf_audio_priv_platform_apply(voice, voice_ptr);
}

/* After a heard volume changed: the web's voices given it; then the lock let go of. */
static void unlock(void)
{
    if (!wgf_audio_priv_is_running()) return;
    wgf_audio_priv_voice_each(each_apply, NULL);
    wgf_audio_priv_unlock();
}

float wgf_audio_priv_bus_heard(wgf_handle_t bus)
{
    float heard = 1.0f;
    int depth = 0;
    uint16_t index;
    if (!bp.ready) return 1.0f; /* the mixer's thread: never makes the table */
    if (bus == 0 || !wgf_core_priv_handle_pool_resolve(&bp.pool, bus, &index)) bus = bp.effects;
    while (bus != 0 && depth++ < MAX_BUSES && wgf_core_priv_handle_pool_resolve(&bp.pool, bus, &index)) {
        const bus_t *b = &bp.buses[index];
        if (b->muted) return 0.0f;
        heard *= b->volume;
        bus = b->parent;
    }
    return heard;
}

bool wgf_audio_priv_bus_is(wgf_handle_t bus)
{
    uint16_t index;
    ensure();
    return bp.ready && wgf_core_priv_handle_pool_resolve(&bp.pool, bus, &index);
}

/* The volume `fraction` of the way from `from` to `to`, even in decibels. */
static float fade_at(float from, float to, float fraction)
{
    float a, b, db;
    if (fraction >= 1.0f) return to;
    a = from > 0.0f ? 20.0f * log10f(from) : FLOOR_DB;
    b = to > 0.0f ? 20.0f * log10f(to) : FLOOR_DB;
    if (a < FLOOR_DB) a = FLOOR_DB;
    if (b < FLOOR_DB) b = FLOOR_DB;
    db = a + (b - a) * fraction;
    if (db <= FLOOR_DB) return to <= 0.0f || from <= 0.0f ? 0.0f : powf(10.0f, FLOOR_DB / 20.0f);
    return powf(10.0f, db / 20.0f);
}

void wgf_audio_priv_bus_update(float dt)
{
    bool any = false;
    uint16_t i;
    if (!bp.ready) return;
    for (i = 1; i < bp.pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&bp.pool, i) != 0 && bp.buses[i].fading) any = true;
    }
    if (!any) return;
    lock();
    for (i = 1; i < bp.pool.capacity; i++) {
        bus_t *b = &bp.buses[i];
        if (wgf_core_priv_handle_pool_handle_from_index(&bp.pool, i) == 0 || !b->fading) continue;
        b->elapsed += dt > 0.0f ? dt : 0.0f;
        b->volume = fade_at(b->from, b->to, b->elapsed / b->seconds);
        if (b->elapsed >= b->seconds) {
            b->volume = b->to;
            b->fading = false;
        }
    }
    unlock();
}

void wgf_audio_priv_bus_reset(void)
{
    if (!bp.ready) return;
    wgf_core_priv_handle_pool_destroy(&bp.pool);
    memset(&bp, 0, sizeof(bp));
}

/* --------------------------------------------------------------- the public API ---- */

wgf_bus_t wgf_bus_get_master(void)
{
    ensure();
    return bp.master;
}

wgf_bus_t wgf_bus_get_music(void)
{
    ensure();
    return bp.music;
}

wgf_bus_t wgf_bus_get_effects(void)
{
    ensure();
    return bp.effects;
}

wgf_bus_t wgf_bus_create(wgf_bus_t parent)
{
    wgf_bus_t bus;
    if (bus_of(parent) == NULL) return 0;
    lock();
    bus = make(parent);
    unlock();
    return bus;
}

static void each_move(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user)
{
    const wgf_handle_t *moves = (const wgf_handle_t *)user;
    (void)voice;
    if (voice_ptr->bus == moves[0]) voice_ptr->bus = moves[1];
}

void wgf_bus_destroy(wgf_bus_t bus)
{
    bus_t *b = bus_of(bus);
    wgf_handle_t moves[2];
    uint16_t i;
    if (b == NULL || bus == bp.master || bus == bp.music || bus == bp.effects) return;
    lock();
    moves[0] = bus;
    moves[1] = b->parent;
    for (i = 1; i < bp.pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&bp.pool, i) != 0 && bp.buses[i].parent == bus) {
            bp.buses[i].parent = b->parent;
        }
    }
    if (wgf_audio_priv_is_running()) wgf_audio_priv_voice_each(each_move, moves);
    wgf_core_priv_handle_pool_free(&bp.pool, bus);
    unlock();
}

wgf_bus_t wgf_bus_get_parent(wgf_bus_t bus)
{
    const bus_t *b = bus_of(bus);
    return b != NULL ? b->parent : 0;
}

bool wgf_bus_set_volume(wgf_bus_t bus, float volume)
{
    bus_t *b = bus_of(bus);
    if (b == NULL || !isfinite(volume) || volume < 0.0f) return false;
    lock();
    b->volume = volume;
    b->fading = false;
    unlock();
    return true;
}

float wgf_bus_get_volume(wgf_bus_t bus)
{
    const bus_t *b = bus_of(bus);
    return b != NULL ? b->volume : 0.0f;
}

bool wgf_bus_set_muted(wgf_bus_t bus, bool muted)
{
    bus_t *b = bus_of(bus);
    if (b == NULL) return false;
    lock();
    b->muted = muted;
    unlock();
    return true;
}

bool wgf_bus_is_muted(wgf_bus_t bus)
{
    const bus_t *b = bus_of(bus);
    return b != NULL && b->muted;
}

float wgf_bus_get_heard_volume(wgf_bus_t bus)
{
    return bus_of(bus) != NULL ? wgf_audio_priv_bus_heard(bus) : 0.0f;
}

bool wgf_bus_fade(wgf_bus_t bus, float volume, float seconds)
{
    bus_t *b = bus_of(bus);
    if (b == NULL || !isfinite(volume) || volume < 0.0f || !isfinite(seconds)) return false;
    lock();
    if (seconds <= 0.0f) {
        b->volume = volume;
        b->fading = false;
    } else {
        b->from = b->volume;
        b->to = volume;
        b->seconds = seconds;
        b->elapsed = 0.0f;
        b->fading = true;
    }
    unlock();
    return true;
}

bool wgf_bus_is_fading(wgf_bus_t bus)
{
    const bus_t *b = bus_of(bus);
    return b != NULL && b->fading;
}
