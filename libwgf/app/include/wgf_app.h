#ifndef WGF_APP_H
#define WGF_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Marks a public function. The library is built with hidden visibility, so only the
 * functions marked WGF_API are exported. */
#include "wgf_api.h"

/* The runtime. app opens the window, starts and drives the layers below it, and runs the
 * frame loop. A program runs through it. See docs/ARCHITECTURE.md, "app". */

/* A function wgf_app_run calls. `user` is the pointer given to wgf_app_run, passed back
 * unchanged. */
typedef void (*wgf_app_callback_t)(void *user);

/* Opens the window and runs the program until it quits, calling:
 *
 *   init      once, when the window is open and core and gfx have started
 *   tick      0 or more times before each frame, at the fixed tick rate
 *             (wgf_loop_set_tick_rate). For simulation, with a step of
 *             wgf_loop_get_tick_delta seconds. Never draw here
 *   frame     once per displayed frame. For drawing, and whatever runs at the frame
 *             rate (wgf_loop_get_frame_delta)
 *   shutdown  once, as the program quits, before gfx and core stop
 *
 * Any of them may be NULL.
 *
 * Each frame, app does these in order:
 * - Takes the window's events.
 * - Updates core, so tasks and loads move on.
 * - Runs the ticks that are due.
 * - Runs the frame between gfx's begin and end, and presents it.
 *
 * Input read in a tick sees what changed since the previous tick. Input read in a frame
 * sees what changed since the previous frame.
 *
 * Make this the last call in main. Put everything after the program's start in init, and
 * everything before its end in shutdown. On the desktop it returns when the program has
 * quit, but on the web it returns at once and the browser runs the frames.
 *
 * Returns false, doing nothing, if the program is already running.
 *
 * This is the one public call that takes callbacks, since the window system owns the
 * loop. */
WGF_API bool wgf_app_run(wgf_app_callback_t init, wgf_app_callback_t tick, wgf_app_callback_t frame,
                           wgf_app_callback_t shutdown, void *user);

/* Lets an autopilot fly this run, or returns whether that is enabled. Call it before
 * wgf_app_run, as a build that is tested or checked does.
 *
 * - Natively the autopilot is the file LIBWGF_AUTOPILOT names.
 * - On the web it is the one the page gives the module.
 * - See BUILDING.md, "Autopilot files".
 *
 * This call is what links the autopilot's runner. A program that never calls it, such as
 * a game's web export by default, carries none of it. Given an autopilot all the same, such
 * a program logs that it has no runner and quits.
 *
 * Returns false once the program is running. */
WGF_API bool wgf_app_enable_autopilot(void);
WGF_API bool wgf_app_is_autopilot_enabled(void);

/* Asks the program to quit. The frame under way finishes, then shutdown runs. */
WGF_API void wgf_app_quit(void);

/* Returns whether quitting means anything on this platform.
 *
 * Returns false on the web, where the page stays when the program would end, and the
 * browser owns Escape. Offer a quit key or button only where this is true. */
WGF_API bool wgf_app_can_quit(void);

/* Returns whether the program is running: from wgf_app_run starting it until shutdown
 * finishes. */
WGF_API bool wgf_app_is_running(void);

/* Reads the program's options by name, the same way on both platforms.
 *
 * - Natively an option is `--name=value`, or `--name`, among the program's arguments.
 * - On the web it is `name=value`, or `name`, in the page's address after the `?`. For
 *   example, Wardens' `enemies=200`, or a benchmark's `count=10000`.
 * - If a name is given more than once, the last one wins.
 *
 * The calls:
 * - has_option returns whether the option was given.
 * - get_option returns its value, valid for the program's life. It returns "" for an
 *   option given without a value, or not given.
 * - get_option_int and get_option_float return the value as a number. They return
 *   `otherwise` if the option wasn't given or isn't a number.
 *
 * For a NULL `name` they return false, "", or `otherwise`. */
WGF_API bool wgf_app_has_option(const char *name);
WGF_API const char *wgf_app_get_option(const char *name);
WGF_API int wgf_app_get_option_int(const char *name, int otherwise);
WGF_API float wgf_app_get_option_float(const char *name, float otherwise);

#ifdef __cplusplus
}
#endif

#endif
