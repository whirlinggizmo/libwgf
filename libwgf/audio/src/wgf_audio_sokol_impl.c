/* sokol_audio's implementation, compiled once, in the layer that owns it, natively and
 * with a window: the device the mixer plays through (wgf_audio_mix.c). A headless build
 * has no device; the web plays through the browser, not sokol_audio. Compiled with
 * warnings off: they aren't libwgf's to fix. */
#define _POSIX_C_SOURCE 200809L /* ALSA's headers, under strict C11, as the platform's sokol file has it */
/* Linux: ALSA, as before sokol_audio made PulseAudio its default there (2026-10-29). A
 * program built with libwgf then needs libasound alone at run time, which every Linux
 * desktop has, where PulseAudio's client library is one more to find or fail to start. */
#define SAUDIO_USE_ALSA (1)
#define SOKOL_AUDIO_IMPL
#include "sokol_audio.h"
