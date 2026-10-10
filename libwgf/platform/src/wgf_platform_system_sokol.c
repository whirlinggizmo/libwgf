#include "wgf_platform_priv.h"

#include <string.h>

#include "sokol_app.h"
#include "sokol_app_utils.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "wgf_log.h"

/* The window, through sokol_app: what is the same natively and on the web. The
 * rest is in wgf_platform_system_native.c and wgf_platform_system_web.c. Cribbed from wgrender's
 * wgr_platform. */

/* a copy: on the web, wgf_app_run returns while sokol_app still calls these */
static wgf_platform_priv_desc_t run_desc;
static bool style_resizable = true;
static bool style_decorated = true;

/* sokol_app's log lines, into core's log. */
static void sokol_log(const char *tag, uint32_t level, uint32_t item, const char *message, uint32_t line,
                      const char *file, void *user)
{
    static const wgf_log_level_t levels[] = {WGF_LOG_LEVEL_FATAL, WGF_LOG_LEVEL_ERROR,
                                                 WGF_LOG_LEVEL_WARN, WGF_LOG_LEVEL_INFO};
    const wgf_log_level_t wgf_level = level < 4 ? levels[level] : WGF_LOG_LEVEL_INFO;
    (void)user;
    if (wgf_level < wgf_log_get_level()) return;
    wgf_log_format(wgf_level, file, (int)line, "%s: %s (item %u)", tag, message != NULL ? message : "",
                       (unsigned)item);
}

static void on_init(void)
{
    /* sokol_app opens the window shown and styled its own way: the program's style
       now, as soon as there is one */
    wgf_platform_priv_set_style(run_desc.resizable, run_desc.decorated);
    if (!run_desc.visible) sapp_set_window_visible(false);
    wgf_platform_priv_sokol_open(run_desc.width, run_desc.height, run_desc.size_set);
    run_desc.init();
}

static void on_shutdown(void)
{
    /* let go first: the program may end itself in its shutdown, as the web's exit() does */
    wgf_platform_priv_sokol_hold(false);
    run_desc.shutdown();
}

static void on_event(const sapp_event *event)
{
    run_desc.event(event);
}

void wgf_platform_priv_run(const wgf_platform_priv_desc_t *desc)
{
    sapp_desc app;
    run_desc = *desc;
    memset(&app, 0, sizeof(app));
    app.init_cb = on_init;
    app.frame_cb = desc->frame;
    app.event_cb = on_event;
    app.cleanup_cb = on_shutdown;
    app.width = desc->width;
    app.height = desc->height;
    app.window_title = desc->title;
    app.fullscreen = desc->fullscreen;
    app.high_dpi = desc->high_dpi;
    app.sample_count = desc->msaa ? 4 : 1; /* a hint: the platform may give fewer */
    app.swap_interval = 1;
    app.disable_vsync = !desc->vsync;
    app.composite_mode = desc->transparent ? SAPP_COMPOSITEMODE_PREMULTIPLIED : SAPP_COMPOSITEMODE_OPAQUE;
    app.logger.func = sokol_log;
    wgf_platform_priv_sokol_hold(true);
    sapp_run(&app);
}

void wgf_platform_priv_request_quit(void)
{
    sapp_request_quit();
}

int wgf_platform_priv_get_framebuffer_width(void)
{
    return sapp_width();
}

int wgf_platform_priv_get_framebuffer_height(void)
{
    return sapp_height();
}

float wgf_platform_priv_get_dpi_scale(void)
{
    const float scale = sapp_dpi_scale();
    return scale >= 1.0f ? scale : 1.0f;
}

int wgf_platform_priv_get_sample_count(void)
{
    const int samples = sapp_sample_count();
    return samples > 1 ? samples : 1;
}

double wgf_platform_priv_get_frame_duration(void)
{
    return sapp_frame_duration_unfiltered();
}

sg_environment wgf_platform_priv_get_environment(void)
{
    return sglue_environment();
}

sg_swapchain wgf_platform_priv_get_swapchain(void)
{
    return sglue_swapchain();
}

void wgf_platform_priv_set_title(const char *title)
{
    sapp_set_window_title(title);
}

void wgf_platform_priv_set_fullscreen(bool fullscreen)
{
    if (sapp_is_fullscreen() == fullscreen) return;
    /* a fixed size can keep a window manager from filling the screen; the style
       comes back on leaving */
    if (fullscreen) sapp_set_window_resizable(true);
    sapp_toggle_fullscreen();
    if (!fullscreen) wgf_platform_priv_set_style(style_resizable, style_decorated);
}

bool wgf_platform_priv_is_fullscreen(void)
{
    return sapp_is_fullscreen();
}

void wgf_platform_priv_set_visible(bool visible)
{
    sapp_set_window_visible(visible);
}

bool wgf_platform_priv_is_visible(void)
{
    return sapp_window_visible();
}

void wgf_platform_priv_set_style(bool resizable, bool decorated)
{
    style_resizable = resizable;
    style_decorated = decorated;
    if (sapp_is_fullscreen()) return; /* applied on leaving fullscreen */
    sapp_set_window_resizable(resizable);
    sapp_set_window_decorated(decorated);
}

void wgf_platform_priv_lock_mouse(bool locked)
{
    sapp_lock_mouse(locked);
}

bool wgf_platform_priv_is_mouse_locked(void)
{
    return sapp_mouse_locked();
}

void wgf_platform_priv_show_mouse(bool shown)
{
    sapp_show_mouse(shown);
}

void wgf_platform_priv_set_paced(bool paced)
{
    (void)paced; /* a window's frames are the display's */
}
