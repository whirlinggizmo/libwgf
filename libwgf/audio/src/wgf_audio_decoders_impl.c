/* The WAV and MP3 decoders' implementations, compiled once, in the layer that owns
 * them, with warnings off: their warnings are not libwgf's to fix (deps/README.md).
 * dr_wav and dr_mp3 from dr_libs master at the commit in deps/dr/VERSION; Ogg Vorbis is
 * Xiph's libvorbis and libogg (deps/xiph/), compiled from their own files. Native
 * only: on the web the browser decodes. Files are untrusted input: the sanitizer
 * presets' tests load broken and cut-short ones of each format
 * (libwgf/audio/tests/wgf_audio_decoders_test.c). */
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
