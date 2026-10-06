#include <stdio.h>

#include "wgf_app_priv.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_loop.h"

/* The frame-rate overlay (wgf_debug.h): libwgt's wgt_loop_draw_fps and wgrender-c's
 * wgr_debug_enable_fps, as a setting the runtime draws, with the frame's cost beside
 * the rate. */

static struct {
    wgf_font_t font; /* shown while size is above 0 */
    float x, y, size;
    wgf_color_t color;
} fps;

static void draw_fps(void)
{
    char text[48];
    static float cost; /* smoothed as the fps is, so the readout holds still */
    int tenths;
    cost = cost > 0.0f ? cost + 0.05f * (wgf_loop_get_frame_cost() - cost) : wgf_loop_get_frame_cost();
    tenths = (int)(cost * 10000.0f + 0.5f); /* integers: no float formatting linked */
    snprintf(text, sizeof(text), "%d FPS %d.%d ms", (int)(wgf_loop_get_fps() + 0.5f), tenths / 10, tenths % 10);
    wgf_draw_text(fps.font, text, fps.x, fps.y, fps.size, fps.color);
}

void wgf_debug_show_fps(wgf_font_t font, float x, float y, float size, wgf_color_t color)
{
    fps.font = font;
    fps.x = x;
    fps.y = y;
    fps.size = size;
    fps.color = color;
    wgf_app_priv_set_overlay(draw_fps);
}

void wgf_debug_hide_fps(void)
{
    fps.size = 0.0f;
    wgf_app_priv_set_overlay(NULL);
}

bool wgf_debug_is_fps_shown(void)
{
    return fps.size > 0.0f;
}
