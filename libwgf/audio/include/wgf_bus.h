#ifndef WGF_BUS_H
#define WGF_BUS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A bus is an object: a handle of kind "audio.bus". */
typedef wgf_handle_t wgf_bus_t;

/* Buses: what voices (wgf_voice.h) and plays (wgf_sound_play) are heard through.
 *
 * Each bus has a volume, a mute, a fade, and a parent.
 * - A voice is heard at its own volume times its bus's heard volume.
 * - A bus's heard volume is its volume times its parents', up to the master. It is 0 when it
 *   or a parent is muted.
 * - All of it is under wgf_audio_set_volume.
 *
 * - The master, music, and effects buses always exist. Music and effects are under the master.
 *   A game can add its own.
 * - A voice is on effects until set. So is a play, unless its sound names another bus
 *   (wgf_sound_set_bus).
 * - There are at most 64 buses. */

/* Returns one of the three buses that always exist. */
WGF_API wgf_bus_t wgf_bus_get_master(void);
WGF_API wgf_bus_t wgf_bus_get_music(void);
WGF_API wgf_bus_t wgf_bus_get_effects(void);

/* Creates a game's own bus under `parent`, at volume 1, unmuted.
 *
 * Returns 0 if `parent` isn't a bus, or if there are already 64 buses. */
WGF_API wgf_bus_t wgf_bus_create(wgf_bus_t parent);
/* Destroys `bus`. Its voices, and the buses under it, move to its parent.
 *
 * Does nothing for the three buses that always exist, or a handle that isn't a bus. */
WGF_API void wgf_bus_destroy(wgf_bus_t bus);
/* Returns the bus's parent. Returns 0 for the master, and for a handle that isn't a bus. */
WGF_API wgf_bus_t wgf_bus_get_parent(wgf_bus_t bus);

/* Sets or gets the bus's volume.
 *
 * - 1 plays its voices as they are.
 * - 0 is silent.
 * - Above 1 is louder, and may clip.
 *
 * Setting it ends a fade. Returns false for a negative or non-finite volume, and for a handle
 * that isn't a bus. */
WGF_API bool wgf_bus_set_volume(wgf_bus_t bus, float volume);
WGF_API float wgf_bus_get_volume(wgf_bus_t bus);
/* Sets or gets whether the bus is muted.
 *
 * While muted, its heard volume is 0, and so is everything heard through it or a bus under it.
 * Its volume is kept for unmuting. Returns false only for a handle that isn't a bus. */
WGF_API bool wgf_bus_set_muted(wgf_bus_t bus, bool muted);
WGF_API bool wgf_bus_is_muted(wgf_bus_t bus);
/* Returns the volume the bus is heard at now: its volume times its parents'. It is 0 when the
 * bus or a parent is muted.
 *
 * Returns 0 for a handle that isn't a bus. */
WGF_API float wgf_bus_get_heard_volume(wgf_bus_t bus);

/* Fades the bus's volume to `volume` over `seconds` of the runtime's frames.
 *
 * - The fade is even in decibels, as an ear hears it. Silence is taken as -60 dB.
 * - 0 seconds or less sets the volume at once.
 * - A new fade, or setting the volume, ends the fade before it.
 * - wgf_bus_is_fading returns whether a fade has not yet ended.
 *
 * Returns false for a negative or non-finite volume, non-finite seconds, or a handle that isn't
 * a bus. */
WGF_API bool wgf_bus_fade(wgf_bus_t bus, float volume, float seconds);
WGF_API bool wgf_bus_is_fading(wgf_bus_t bus);

#ifdef __cplusplus
}
#endif

#endif /* WGF_BUS_H */
