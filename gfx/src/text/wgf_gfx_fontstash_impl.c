/* fontstash, with stb_truetype for reading fonts, and sokol_fontstash, which draws
 * fontstash's glyphs through sokol_gl: compiled once for gfx. Vendored code,
 * compiled as it comes. fontstash's implementation uses libc without including it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h> /* fontstash opens files through MultiByteToWideChar there */
#endif

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define SOKOL_FONTSTASH_IMPL
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"
