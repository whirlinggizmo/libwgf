#ifndef WGF_APP_H
#define WGF_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Marks a public function. The library is built with hidden visibility, so only
 * functions marked WGF_API are exported. */
#include "wgf_api.h"

/* The runtime, as wgrender's wgr_run: app opens the window, starts and drives the
 * layers below it, and runs the frame loop. A program runs through it; see
 * docs/ARCHITECTURE.md, "app". */

/* What wgf_app_run calls. `user` is wgf_app_run's, passed back unchanged. */
typedef void (*wgf_app_callback_t)(void *user);

/* Open the window and run the program until it quits:
 *
 *   init      once, when the window is open and core and gfx have started
 *   tick      0 or more times before each frame, at the fixed tick rate
 *             (wgf_loop_set_tick_rate): simulation, with a step of
 *             wgf_loop_get_tick_delta seconds. Never draw here
 *   frame     once per displayed frame: drawing, and whatever runs at the frame
 *             rate (wgf_loop_get_frame_delta)
 *   shutdown  once, as the program quits, before gfx and core stop
 *
 * Any of them may be NULL. Each frame, app takes the window's events, updates core
 * (tasks and loads move on), runs the ticks that are due, then the frame between
 * gfx's begin and end, and presents it. Input read in a tick sees what changed since
 * the previous tick; read in a frame, since the previous frame.
 *
 * Make this the last call in main, with everything after the program's start in
 * init and everything before its end in shutdown: on the desktop it returns when
 * the program has quit, but on the web it returns at once and the browser runs
 * the frames. False, doing nothing, when the program is already running. This is
 * the one public call that takes callbacks, since the window system owns the
 * loop. */
WGF_API bool wgf_app_run(wgf_app_callback_t init, wgf_app_callback_t tick, wgf_app_callback_t frame,
                           wgf_app_callback_t shutdown, void *user);

/* Let an autopilot fly this run (BUILDING.md, "Autopilot files": natively the file
 * LIBWGF_AUTOPILOT names, on the web the one the page gives the module), as a build that is
 * tested or checked does, before wgf_app_run. It is what links the autopilot's runner, so a
 * program that never calls it (a game's web export, by default) carries none of it; given an
 * autopilot all the same, it logs that it has none and quits. False once the program is
 * running. */
WGF_API bool wgf_app_enable_autopilot(void);
WGF_API bool wgf_app_is_autopilot_enabled(void);

/* Ask the program to quit: the frame under way finishes, then shutdown runs. */
WGF_API void wgf_app_quit(void);

/* Whether quitting means anything here: false on the web, where the page stays when
 * the program would end, and the browser owns Escape. A game offers a quit key or
 * button only where this is true. */
WGF_API bool wgf_app_can_quit(void);

/* Between wgf_app_run starting the program and shutdown finishing. */
WGF_API bool wgf_app_is_running(void);

/* The program's options by name, read the same way on both platforms: natively `--name=value`
 * (or `--name`) among its arguments, on the web `name=value` (or `name`) in its page's
 * address after the `?` (Wardens' `enemies=200`, a benchmark's `count=10000`). has: whether it
 * was given; get: its value, "" for one given without a value or not given (valid for the
 * program's life); get_int and get_float: the value as a number, `otherwise` when it wasn't
 * given or isn't one. The last of a name given wins. False, "", or `otherwise` for NULL. */
WGF_API bool wgf_app_has_option(const char *name);
WGF_API const char *wgf_app_get_option(const char *name);
WGF_API int wgf_app_get_option_int(const char *name, int otherwise);
WGF_API float wgf_app_get_option_float(const char *name, float otherwise);

#ifdef __cplusplus
}
#endif

#endif
