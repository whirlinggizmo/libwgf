#include "wgf_platform_priv.h"

#include <stdlib.h>
#include <string.h>

#include "wgf_core_os_priv.h"
#include "wgf_time.h"

/* No window, for a headless build: frames run until the program quits, into a
 * framebuffer of the window's size at a DPI scale of 1, at most 60 a second, as a
 * display would show them, so code that runs on time runs as it would in a window.
 * The window's settings are kept and read back; there is nothing to show them on.
 * Events come only from tests (wgf_platform_priv_headless_push_event).
 *
 * LIBWGF_HEADLESS_FRAMES=<n> in the environment quits after n frames, so a program
 * built headless can be run for a while and checked (tools/run_smoke.py). */

#define MAX_EVENTS 64
#define DISPLAY_FRAME (1.0 / 60.0)

static wgf_platform_priv_desc_t desc;
static bool quit;
static bool fullscreen;
static bool visible;
static bool mouse_locked;
static int position_x, position_y;
static double frame_duration;
static float dpi_scale = 1.0f;
static bool paced = true;
static sapp_event events[MAX_EVENTS];
static int event_count;

void wgf_platform_priv_run(const wgf_platform_priv_desc_t *run_desc)
{
    desc = *run_desc;
    quit = false;
    fullscreen = false;
    visible = desc.visible;
    mouse_locked = false;
    position_x = position_y = 0;
    frame_duration = 1.0 / 60.0;
    event_count = 0;
    const char *frames_env = getenv("LIBWGF_HEADLESS_FRAMES");
    const long max_frames = frames_env != NULL ? strtol(frames_env, NULL, 10) : 0;
    long frames = 0;
    double last;
    desc.init(); /* starts core, and its clock */
    last = wgf_time_get_seconds();
    while (!quit) {
        const double start = wgf_time_get_seconds();
        int i;
        /* the queue as it is now: events pushed while delivering wait a frame */
        const int count = event_count;
        sapp_event delivering[MAX_EVENTS];
        memcpy(delivering, events, sizeof(sapp_event) * (size_t)count);
        event_count = 0;
        for (i = 0; i < count; i++) desc.event(&delivering[i]);
        desc.frame();
        if (max_frames > 0 && ++frames >= max_frames) quit = true;
        /* as a display would: no faster than its rate */
        if (paced) wgf_core_priv_os_sleep(DISPLAY_FRAME - (wgf_time_get_seconds() - start));
        frame_duration = wgf_time_get_seconds() - last;
        last = wgf_time_get_seconds();
    }
    desc.shutdown();
}

bool wgf_platform_priv_headless_push_event(const sapp_event *event)
{
    if (event == NULL || event_count >= MAX_EVENTS) return false;
    events[event_count++] = *event;
    return true;
}

void wgf_platform_priv_request_quit(void)
{
    quit = true;
}

int wgf_platform_priv_get_framebuffer_width(void)
{
    return desc.width;
}

int wgf_platform_priv_get_framebuffer_height(void)
{
    return desc.height;
}

float wgf_platform_priv_get_dpi_scale(void)
{
    return dpi_scale;
}

int wgf_platform_priv_get_sample_count(void)
{
    return desc.msaa ? 4 : 1;
}

bool wgf_platform_priv_can_quit(void)
{
    return true;
}

/* One virtual monitor the size of the window, at the origin; the window can be
 * placed on it. */
bool wgf_platform_priv_set_position(int x, int y)
{
    position_x = x;
    position_y = y;
    return true;
}

bool wgf_platform_priv_get_position(int *x, int *y)
{
    *x = position_x;
    *y = position_y;
    return true;
}

int wgf_platform_priv_get_monitor_count(void)
{
    return 1;
}

int wgf_platform_priv_get_monitor(void)
{
    return 0;
}

bool wgf_platform_priv_set_monitor(int monitor)
{
    return monitor == 0;
}

bool wgf_platform_priv_get_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    *x = *y = *width = *height = 0;
    if (monitor != 0) return false;
    *width = desc.width;
    *height = desc.height;
    return true;
}

const char *wgf_platform_priv_get_monitor_name(int monitor)
{
    return monitor == 0 ? "headless" : "";
}

sg_environment wgf_platform_priv_get_environment(void)
{
    sg_environment environment;
    memset(&environment, 0, sizeof(environment));
    environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    environment.defaults.sample_count = wgf_platform_priv_get_sample_count();
    return environment;
}

sg_swapchain wgf_platform_priv_get_swapchain(void)
{
    sg_swapchain swapchain;
    memset(&swapchain, 0, sizeof(swapchain));
    swapchain.width = desc.width;
    swapchain.height = desc.height;
    swapchain.sample_count = wgf_platform_priv_get_sample_count();
    swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    return swapchain;
}

void wgf_platform_priv_headless_set_dpi_scale(float scale)
{
    dpi_scale = scale >= 1.0f ? scale : 1.0f;
}

void wgf_platform_priv_headless_set_framebuffer(int width, int height)
{
    desc.width = width;
    desc.height = height;
}

double wgf_platform_priv_get_frame_duration(void)
{
    return frame_duration;
}

bool wgf_platform_priv_pace(double wait)
{
    if (wait > 0.0) wgf_core_priv_os_sleep(wait);
    return true;
}

void wgf_platform_priv_set_title(const char *title)
{
    (void)title;
}

bool wgf_platform_priv_set_size(int width, int height)
{
    desc.width = width;
    desc.height = height;
    return true;
}

bool wgf_platform_priv_has_fullscreen(void)
{
    return false;
}

void wgf_platform_priv_set_fullscreen(bool on)
{
    fullscreen = on;
}

bool wgf_platform_priv_is_fullscreen(void)
{
    return fullscreen;
}

void wgf_platform_priv_set_visible(bool shown)
{
    visible = shown;
}

bool wgf_platform_priv_is_visible(void)
{
    return visible;
}

void wgf_platform_priv_set_style(bool resizable, bool decorated)
{
    (void)resizable;
    (void)decorated;
}

bool wgf_platform_priv_is_focused(void)
{
    return true;
}

void wgf_platform_priv_lock_mouse(bool locked)
{
    mouse_locked = locked;
}

bool wgf_platform_priv_is_mouse_locked(void)
{
    return mouse_locked;
}

void wgf_platform_priv_show_mouse(bool shown)
{
    (void)shown;
}

void wgf_platform_priv_mark(const char *name)
{
    (void)name;
}

void wgf_platform_priv_set_paced(bool paced_frames)
{
    paced = paced_frames;
}
