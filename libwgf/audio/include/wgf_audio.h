#ifndef WGF_AUDIO_H
#define WGF_AUDIO_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio as a whole: what every voice (wgf_voice.h) is played through.
 *
 * These calls don't start audio. The first sound does (wgf_sound.h). */

/* Sets or gets the volume of every voice, multiplied into each voice's own.
 *
 * - 1 plays the voices as they are.
 * - 0 is silent.
 * - Above 1 is louder, and may clip.
 *
 * Returns false for a negative or non-finite volume. */
WGF_API bool wgf_audio_set_volume(float volume);
WGF_API float wgf_audio_get_volume(void);

/* Sets or gets whether every voice is held where it is, its state unchanged, until unpaused.
 *
 * Use it for a game's pause screen. Paused or not, voices are told to play, pause, and stop as
 * usual. */
WGF_API void wgf_audio_set_paused(bool paused);
WGF_API bool wgf_audio_is_paused(void);

/* Returns whether the browser is holding sound back until the page's first key, click, or
 * touch. While it is, a voice told to play reads PLAYING, is silent, and its position stays.
 *
 * Use it to show "click for sound" only while it is true. Returns false outside a browser,
 * and after that first input. Before any sound is made it is what the browser says of the
 * page's input, and true in a browser that doesn't say. */
WGF_API bool wgf_audio_is_blocked(void);

/* Sets or gets the listener's position and facing. A spatial voice (wgf_voice_set_spatial) is
 * heard from the listener.
 *
 * - The facing has y up, and is normalized when set.
 * - By default the listener is at the origin, facing -z.
 * - A spatial voice is quieter with distance (wgf_voice_set_range), and panned by which side
 *   of the listener it is on.
 * - The ecs sets both from an actor (wgf_world_set_listener).
 *
 * Returns false for a non-finite value, and for a facing of no length or straight up or
 * down. */
WGF_API bool wgf_audio_set_listener_position(float x, float y, float z);
WGF_API wgf_vec3_t wgf_audio_get_listener_position(void);
WGF_API bool wgf_audio_set_listener_facing(float x, float y, float z);
WGF_API wgf_vec3_t wgf_audio_get_listener_facing(void);

#ifdef __cplusplus
}
#endif

#endif /* WGF_AUDIO_H */
