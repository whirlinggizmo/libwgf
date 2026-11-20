#include "wgf_platform_priv.h"

#include "sokol_app.h"
#include "sokol_app_utils.h"
#include "wgf_core_os_priv.h"
#include "wgf_time.h"

/* The window on the desktop: what differs from the web. */

/* Window sizes are the OS's pixels, except on macOS, where they are already
 * logical (points). */
static float os_pixels_per_logical(void)
{
#if defined(__APPLE__)
    return 1.0f;
#else
    return wgf_platform_priv_get_dpi_scale();
#endif
}

void wgf_platform_priv_sokol_hold(bool hold)
{
    (void)hold; /* sapp_run returns when the program has quit */
}

void wgf_platform_priv_sokol_open(int width, int height, bool size_set)
{
    (void)width, (void)height, (void)size_set; /* sapp_run opened it at that size */
}

bool wgf_platform_priv_set_size(int width, int height)
{
    const float scale = os_pixels_per_logical();
    sapp_set_window_size((int)((float)width * scale + 0.5f), (int)((float)height * scale + 0.5f));
    return true;
}

bool wgf_platform_priv_can_quit(void)
{
    return true;
}

/* Under XWayland the compositor places windows: a program's moves are ignored, and
 * the position X11 reports isn't where the window is. */
static bool can_move(void)
{
    return sapp_can_move_window();
}

bool wgf_platform_priv_set_position(int x, int y)
{
    if (!can_move()) return false;
    sapp_set_window_position(x, y);
    return true;
}

bool wgf_platform_priv_get_position(int *x, int *y)
{
    *x = *y = 0;
    if (!can_move()) return false;
    sapp_get_window_position(x, y);
    return true;
}

int wgf_platform_priv_get_monitor_count(void)
{
    const int count = sapp_num_displays();
    return count > 0 ? count : 1;
}

int wgf_platform_priv_get_monitor(void)
{
    const int monitor = sapp_current_display();
    return monitor >= 0 ? monitor : 0;
}

bool wgf_platform_priv_set_monitor(int monitor)
{
    if (monitor < 0 || monitor >= sapp_num_displays() || !can_move()) return false;
    sapp_set_display(monitor);
    return true;
}

bool wgf_platform_priv_get_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    const float scale = os_pixels_per_logical();
    *x = *y = *width = *height = 0;
    if (monitor < 0 || monitor >= sapp_num_displays()) return false;
    sapp_display_position(monitor, x, y);
    *width = (int)((float)sapp_display_width(monitor) / scale + 0.5f);
    *height = (int)((float)sapp_display_height(monitor) / scale + 0.5f);
    return true;
}

const char *wgf_platform_priv_get_monitor_name(int monitor)
{
    const char *name = monitor >= 0 && monitor < sapp_num_displays() ? sapp_display_name(monitor) : NULL;
    return name != NULL ? name : "";
}

bool wgf_platform_priv_has_fullscreen(void)
{
    return true;
}

bool wgf_platform_priv_is_focused(void)
{
    return sapp_window_focused();
}

/* Sleep until the frame is due, yielding for the last moment, since OS sleeps
 * overshoot by up to a millisecond or two. */
bool wgf_platform_priv_pace(double wait)
{
    const double spin = 0.002;
    const double deadline = wgf_time_get_seconds() + wait;
    for (;;) {
        const double remaining = deadline - wgf_time_get_seconds();
        if (remaining <= 0.0) return true;
        wgf_core_priv_os_sleep(remaining > spin ? remaining - spin : 0.0);
    }
}

void wgf_platform_priv_mark(const char *name)
{
    (void)name;
}

/* ---- the system's browser, and the device ------------------------------------------- */

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

bool wgf_platform_priv_open_url(const char *url)
{
#if defined(_WIN32)
    return (INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
#else
    /* the opener run with the URL as one argument, never through a shell */
#if defined(__APPLE__)
    char *argv[] = {"open", (char *)url, NULL};
#else
    char *argv[] = {"xdg-open", (char *)url, NULL};
#endif
    pid_t pid;
    return posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0;
#endif
}

static bool keyboard_shown;

void wgf_platform_priv_show_keyboard(bool shown, float x, float y, float height)
{
    (void)x;
    (void)y;
    (void)height;
    if (shown != keyboard_shown) sapp_show_keyboard(shown); /* a phone's; a desktop has its own */
    keyboard_shown = shown;
}

bool wgf_platform_priv_is_keyboard_shown(void)
{
    return keyboard_shown;
}

void wgf_platform_priv_sokol_poll(void (*deliver)(const sapp_event *event))
{
    (void)deliver; /* sokol_app delivers every event itself */
}

bool wgf_platform_priv_has_touch(void)
{
    return false; /* a desktop's window: sokol_app gives touches on phones only */
}

bool wgf_platform_priv_has_fine_pointer(void)
{
    return true;
}

int wgf_platform_priv_get_os(void)
{
#if defined(_WIN32)
    return 2;
#elif defined(__APPLE__)
    return 3;
#else
    return 1;
#endif
}

/* Nothing covers a desktop's window, and nothing turns it: its shape is its orientation. */
void wgf_platform_priv_get_safe_insets(int insets[4])
{
    insets[0] = insets[1] = insets[2] = insets[3] = 0;
}

bool wgf_platform_priv_request_orientation(int orientation)
{
    (void)orientation;
    return false;
}

int wgf_platform_priv_get_orientation_lock(void)
{
    return 0;
}

bool wgf_platform_priv_can_lock_orientation(void)
{
    return false;
}

/* Natively the GPU's context isn't lost. */
bool wgf_platform_priv_gpu_is_lost(void)
{
    return false;
}

unsigned wgf_platform_priv_gpu_get_restores(void)
{
    return 0;
}
