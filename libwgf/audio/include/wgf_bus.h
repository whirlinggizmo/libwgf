#ifndef WGF_BUS_H
#define WGF_BUS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A bus, an object: a handle of kind "audio.bus". */
typedef wgf_handle_t wgf_bus_t;

/* Buses: what voices (wgf_voice.h) and plays (wgf_sound_play) are heard through, helix's
 * Audio.master, music, effects, and a game's own. Each has a volume, a mute, and a fade,
 * and a parent; a voice is heard at its own volume times its bus's heard volume: the
 * bus's volume times its parents' up to the master, 0 when it or a parent is muted (and
 * all of it under wgf_audio_set_volume). The master, music, and effects buses always
 * exist, music and effects under the master; a voice is on effects until set, and so is
 * a play, unless its sound says another (wgf_sound_set_bus). At most 64 buses. */

/* The three that always exist. */
WGF_API wgf_bus_t wgf_bus_get_master(void);
WGF_API wgf_bus_t wgf_bus_get_music(void);
WGF_API wgf_bus_t wgf_bus_get_effects(void);

/* A game's own bus under `parent`, at volume 1, unmuted. 0 for a handle that isn't a bus,
 * or when there are 64. */
WGF_API wgf_bus_t wgf_bus_create(wgf_bus_t parent);
/* The bus ended: its voices, and the buses under it, move to its parent. Nothing for the
 * three that always exist, or a handle that isn't a bus. */
WGF_API void wgf_bus_destroy(wgf_bus_t bus);
/* Its parent; 0 for the master, and for a handle that isn't a bus. */
WGF_API wgf_bus_t wgf_bus_get_parent(wgf_bus_t bus);

/* Its volume: 1 as its voices are, 0 silent, above 1 louder (and may clip). Setting it
 * ends a fade. Refuses a negative or non-finite one, and a handle that isn't a bus. */
WGF_API bool wgf_bus_set_volume(wgf_bus_t bus, float volume);
WGF_API float wgf_bus_get_volume(wgf_bus_t bus);
/* Muted: its heard volume, and everything heard through it or a bus under it, is 0, its
 * volume kept for unmuting. False only for a handle that isn't a bus. */
WGF_API bool wgf_bus_set_muted(wgf_bus_t bus, bool muted);
WGF_API bool wgf_bus_is_muted(wgf_bus_t bus);
/* What it is heard at now: its volume times its parents', 0 when it or one is muted. 0
 * for a handle that isn't a bus. */
WGF_API float wgf_bus_get_heard_volume(wgf_bus_t bus);

/* Its volume moved to `volume` over `seconds` of the runtime's frames, even in decibels
 * (as an ear hears it; silence taken as -60 dB), helix's; 0 seconds or less sets it at
 * once. A fade over another, or a volume set, ends the one before. Refuses a negative or
 * non-finite volume, non-finite seconds, and a handle that isn't a bus. is_fading: a fade
 * not yet ended. */
WGF_API bool wgf_bus_fade(wgf_bus_t bus, float volume, float seconds);
WGF_API bool wgf_bus_is_fading(wgf_bus_t bus);

#ifdef __cplusplus
}
#endif

#endif /* WGF_BUS_H */
