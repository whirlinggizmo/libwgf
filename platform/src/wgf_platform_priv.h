#ifndef WGF_PLATFORM_PRIV_H
#define WGF_PLATFORM_PRIV_H

#include <stdbool.h>

#include "sokol_app.h" /* sapp_event: input reads the window's events in sokol's form */
#include "sokol_gfx.h" /* sg_environment, sg_swapchain: what gfx draws with and into */

/* The window system, under gfx and app, as wgrender's wgr_platform: sokol_app
 * (wgf_platform_system_sokol.c, with _native.c or _web.c for what differs), or none
 * in a headless build (wgf_platform_system_headless.c), whose frames run as fast as
 * asked with no window. Sizes here are framebuffer pixels unless named logical. */

typedef struct wgf_platform_priv_desc_t {
    void (*init)(void);
    void (*frame)(void);
    void (*event)(const sapp_event *event);
    void (*shutdown)(void);
    int width, height; /* logical */
    bool size_set;     /* the program set the size (on the web, else the page lays out the canvas) */
    const char *title;
    bool fullscreen;
    bool high_dpi;
    bool vsync;
    bool transparent;
    bool resizable;
    bool decorated;
    bool visible;
    bool msaa; /* ask for 4 samples a pixel */
} wgf_platform_priv_desc_t;

/* Open the window and run: returns once the program has quit (desktop,
 * headless), or at once with the browser running frames (web). */
void wgf_platform_priv_run(const wgf_platform_priv_desc_t *desc);
void wgf_platform_priv_request_quit(void);

int wgf_platform_priv_get_framebuffer_width(void);
int wgf_platform_priv_get_framebuffer_height(void);
float wgf_platform_priv_get_dpi_scale(void); /* 1 or more */
/* Samples a pixel the window's framebuffer has: 1, or what MSAA got. */
int wgf_platform_priv_get_sample_count(void);
/* Whether quitting means anything: false on the web, where the page stays. */
bool wgf_platform_priv_can_quit(void);
/* What gfx draws with and into, as sokol_glue gives sokol_gfx them: the device, as
 * sokol's environment, once the window exists, and the frame's swapchain, each
 * frame. A headless build gives the dummy backend's, at the window's size. As
 * wgrender's wgri_platform_environment and _swapchain. */
sg_environment wgf_platform_priv_get_environment(void);
sg_swapchain wgf_platform_priv_get_swapchain(void);
/* Seconds the display shows a frame for, measured. */
double wgf_platform_priv_get_frame_duration(void);

/* sokol_app only: keep the program alive while the loop runs (true), and let it
 * end (false). On the web, main returns while the browser runs the frames, which
 * would otherwise end the program before the first; natively, nothing to do. */
void wgf_platform_priv_sokol_hold(bool hold);
/* sokol_app only: the window has opened, `width` by `height` (logical), which the
 * program set if `size_set`. On the web the canvas is laid out by the page, so it
 * takes the size only when the program set one; natively, sokol_app opened it so. */
void wgf_platform_priv_sokol_open(int width, int height, bool size_set);

/* Frame pacing, for a target fps: the next frame is due in `wait` seconds (0 or
 * less: now). Natively this sleeps until then and says to run the frame; on the
 * web, where the browser offers frames, it says whether to run this one. */
bool wgf_platform_priv_pace(double wait);

/* Whether frames wait for a display: true at start. A headless build runs unpaced as
   fast as it can (an autopilot run's, whose time is its own); a window's frames are the
   display's either way. */
void wgf_platform_priv_set_paced(bool paced);

void wgf_platform_priv_set_title(const char *title);
bool wgf_platform_priv_set_size(int width, int height); /* logical */
bool wgf_platform_priv_has_fullscreen(void);
void wgf_platform_priv_set_fullscreen(bool fullscreen);
bool wgf_platform_priv_is_fullscreen(void);
void wgf_platform_priv_set_visible(bool visible);
bool wgf_platform_priv_is_visible(void);
void wgf_platform_priv_set_style(bool resizable, bool decorated);
bool wgf_platform_priv_is_focused(void);

/* Where the window is on the desktop, its top-left in the desktop's coordinates, and
 * the monitors: false, or one monitor the size of the screen, where the platform
 * doesn't place windows (the web; a Wayland desktop). Monitor sizes are logical. */
bool wgf_platform_priv_set_position(int x, int y);
bool wgf_platform_priv_get_position(int *x, int *y);
int wgf_platform_priv_get_monitor_count(void);
int wgf_platform_priv_get_monitor(void);
bool wgf_platform_priv_set_monitor(int monitor);
bool wgf_platform_priv_get_monitor_rect(int monitor, int *x, int *y, int *width, int *height);
const char *wgf_platform_priv_get_monitor_name(int monitor);
void wgf_platform_priv_lock_mouse(bool locked);
bool wgf_platform_priv_is_mouse_locked(void);
void wgf_platform_priv_show_mouse(bool shown);

/* Headless builds only, for tests: the DPI scale the window reports (1 by default). */
void wgf_platform_priv_headless_set_dpi_scale(float scale);

/* Headless builds only, for tests: queue an event, delivered before the next
 * frame as sokol_app delivers the window's. False when the queue is full. */
bool wgf_platform_priv_headless_push_event(const sapp_event *event);

/* A named point in startup, for measuring it: on the web a performance mark
 * (performance.mark, which DevTools and tools/measure_example_startup.py read);
 * natively nothing. wgrender's wgri_platform_mark. */
void wgf_platform_priv_mark(const char *name);

#endif
