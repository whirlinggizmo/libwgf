/* The sokol implementations, compiled once for the whole library, as wgrender's
 * wgr_sokol_impl.c: gfx and gl (immediate mode drawing), and, with a window,
 * app, app_utils (window size, style, visibility, and focus, from
 * deps/sokol_utils), and glue. The backend is the build's (CMakeLists.txt):
 * OpenGL core natively, WebGL2 or WebGPU on the web, and the dummy backend, with no
 * GPU, in a headless build, which has no window either. Compiled with warnings off:
 * they aren't libwgf's to fix. fontstash and stb are gfx's (wgf_gfx_*_impl.c), and
 * sokol_time core's (wgf_core_sokol_impl.c). */
#define _POSIX_C_SOURCE 200809L /* clock_gettime, under strict C11 */
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY /* the program has its own main, which calls wgf_app_run */
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#if !defined(SOKOL_DUMMY_BACKEND)
#include "sokol_app.h"
#include "sokol_app_utils.h"
#include "sokol_glue.h"
#endif
