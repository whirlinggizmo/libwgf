#ifndef WGF_AUDIO_H
#define WGF_AUDIO_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio as a whole: what every voice (wgf_voice.h) is played through. As the layer's own
 * section, these calls don't start audio: the first sound does (wgf_sound.h). */

/* Every voice's loudness, times its own: 1 as they are, 0 silent, above 1 louder (and
 * may clip). Refuses a negative or non-finite one. */
WGF_API bool wgf_audio_set_volume(float volume);
WGF_API float wgf_audio_get_volume(void);

/* Every voice held where it is, its state unchanged, until unpaused: for a game's pause
 * screen. Paused or not, voices are told to play, pause, and stop as usual. */
WGF_API void wgf_audio_set_paused(bool paused);
WGF_API bool wgf_audio_is_paused(void);

/* The listener, what a spatial voice (wgf_voice_set_spatial) is heard from: where it is,
 * and the way it faces (y up; normalized when set), at the origin facing -z by default. A
 * spatial voice is quieter with distance (wgf_voice_set_range) and panned by which side of
 * the listener it is on. The ecs sets them from an actor (wgf_world_set_listener). Refuses
 * a non-finite one, and a facing of no length or straight up or down. */
WGF_API bool wgf_audio_set_listener_position(float x, float y, float z);
WGF_API wgf_vec3_t wgf_audio_get_listener_position(void);
WGF_API bool wgf_audio_set_listener_facing(float x, float y, float z);
WGF_API wgf_vec3_t wgf_audio_get_listener_facing(void);

#ifdef __cplusplus
}
#endif

#endif /* WGF_AUDIO_H */
