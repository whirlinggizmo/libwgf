/* sokol_audio's implementation, compiled once, in the layer that owns it, natively and
 * with a window: the device the mixer plays through (wgf_audio_mix.c). A headless build
 * has no device; the web plays through the browser, not sokol_audio. Compiled with
 * warnings off: they aren't libwgf's to fix. */
#define _POSIX_C_SOURCE 200809L /* ALSA's headers, under strict C11, as the platform's sokol file has it */
#define SOKOL_AUDIO_IMPL
#include "sokol_audio.h"
