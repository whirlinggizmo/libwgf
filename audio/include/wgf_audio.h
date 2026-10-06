#ifndef WGF_AUDIO_H
#define WGF_AUDIO_H

#include <stdbool.h>

#include "wgf_api.h"

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

#ifdef __cplusplus
}
#endif

#endif /* WGF_AUDIO_H */
