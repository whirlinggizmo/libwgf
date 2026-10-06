#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_mouse.h"
#include "wgf_platform_input_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_presentation.h"
#include "wgf_touch.h"
#include "wgf_window.h"

/* The presentation modes (wgf_presentation.h), with no window (a headless build): each mode's transform and
 * visible area over framebuffers of several sizes and shapes, and the pointer and a touch
 * mapped back into the design's coordinates through it, as the window's events are. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

/* Now's transform, over a framebuffer of width by height. */
static wgf_platform_priv_presentation_t over(int width, int height)
{
    wgf_platform_priv_presentation_t p;
    wgf_platform_priv_headless_set_framebuffer(width, height);
    wgf_platform_priv_get_presentation(&p);
    return p;
}

static int visible(const wgf_platform_priv_presentation_t *p, float x, float y, float w, float h)
{
    return near(p->visible_x, x) && near(p->visible_y, y) && near(p->visible_width, w) && near(p->visible_height, h);
}

/* A mouse at framebuffer pixel (x, y), as the window delivers one; where the program reads it. */
static wgf_vec2_t mouse_at(float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_MOUSE_MOVE;
    event.mouse_x = x;
    event.mouse_y = y;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
    return wgf_mouse_get_position();
}

int main(void)
{
    wgf_platform_priv_presentation_t p;
    wgf_vec2_t at;

    /* none: the window's logical pixels, at its DPI scale */
    expect(wgf_presentation_get_mode() == WGF_PRESENTATION_MODE_NONE, "no mode by default");
    wgf_platform_priv_headless_set_dpi_scale(2.0f);
    p = over(1600, 1200);
    expect(near(p.scale_x, 2) && near(p.offset_x, 0) && visible(&p, 0, 0, 800, 600) && !p.bars,
           "none: the DPI scale, the whole window visible");
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    expect(!wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 0, 600) &&
               !wgf_presentation_set((wgf_presentation_mode_t)9, 1, 1),
           "a size under 1, or no such mode, refused");

    /* fit: alike on both axes, centered, bars on the long side */
    expect(wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 800, 600) &&
               wgf_presentation_get_width() == 800 && wgf_presentation_get_height() == 600,
           "fit, 800 by 600");
    p = over(1600, 900); /* wider: bars left and right */
    expect(near(p.scale_x, 1.5f) && near(p.scale_y, 1.5f) && near(p.offset_x, 200) && near(p.offset_y, 0) &&
               visible(&p, 0, 0, 800, 600) && p.bars,
           "fit, wider: 1.5 to a logical pixel, 200 pixels of bar each side");
    p = over(800, 1200); /* taller: bars above and below */
    expect(near(p.scale_x, 1) && near(p.offset_y, 300) && visible(&p, 0, 0, 800, 600),
           "fit, taller: bars above and below");
    p = over(1600, 900);
    at = mouse_at(200 + 750, 0 + 450); /* the pixel at the design's (500, 300) */
    expect(near(at.x, 500) && near(at.y, 300), "the pointer, mapped back into the design");
    at = mouse_at(100, 450); /* in the left bar */
    expect(at.x < 0, "a pointer in a bar is outside the design");
    {
        sapp_event touch;
        memset(&touch, 0, sizeof(touch));
        touch.type = SAPP_EVENTTYPE_TOUCHES_BEGAN;
        touch.num_touches = 1;
        touch.touches[0].identifier = 7;
        touch.touches[0].pos_x = 200 + 150;
        touch.touches[0].pos_y = 300;
        touch.touches[0].changed = true;
        wgf_platform_priv_input_handle_event(&touch, 1.0f);
        expect(wgf_touch_get_count() == 1 && near(wgf_touch_get_position(wgf_touch_get_id(0)).x, 100) &&
                   near(wgf_touch_get_position(wgf_touch_get_id(0)).y, 200),
               "a touch, mapped back into the design");
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        wgf_platform_priv_input_handle_event(&touch, 1.0f);
    }

    /* fill: alike, covering, cropped */
    wgf_presentation_set(WGF_PRESENTATION_MODE_FILL, 800, 600);
    p = over(1600, 900);
    expect(near(p.scale_x, 2) && near(p.offset_y, -150) && visible(&p, 0, 75, 800, 450) && !p.bars,
           "fill, wider: 2 to a logical pixel, the top and bottom cropped");

    /* expand: as fit, the visible area growing past the design */
    wgf_presentation_set(WGF_PRESENTATION_MODE_EXPAND, 800, 600);
    p = over(1600, 900);
    expect(near(p.scale_x, 1.5f) && visible(&p, -133.33f, 0, 1066.67f, 600) && !p.bars,
           "expand, wider: the visible area wider than the design, centered on it");
    p = over(800, 1200);
    expect(visible(&p, 0, -300, 800, 1200), "expand, taller: taller than the design");

    /* stretch: each axis its own */
    wgf_presentation_set(WGF_PRESENTATION_MODE_STRETCH, 800, 600);
    p = over(1600, 900);
    expect(near(p.scale_x, 2) && near(p.scale_y, 1.5f) && near(p.offset_x, 0) && visible(&p, 0, 0, 800, 600),
           "stretch: 2 across, 1.5 down, the design filling the window");

    /* integer: whole pixels, at least 1 */
    wgf_presentation_set(WGF_PRESENTATION_MODE_INTEGER, 320, 180);
    p = over(1600, 900);
    expect(near(p.scale_x, 5) && near(p.offset_x, 0) && p.bars, "integer: 5 to a logical pixel, exactly");
    p = over(1000, 600);
    expect(near(p.scale_x, 3) && near(p.offset_x, 20) && near(p.offset_y, 30) && visible(&p, 0, 0, 320, 180),
           "integer: 3, not 3.125, and bars on whole pixels");
    p = over(200, 100);
    expect(near(p.scale_x, 1), "integer: never below 1");

    /* back to none, and the window: a design is its size when the program set none */
    expect(wgf_presentation_set(WGF_PRESENTATION_MODE_NONE, 0, 0) &&
               wgf_presentation_get_mode() == WGF_PRESENTATION_MODE_NONE,
           "back to none");
    p = over(640, 480);
    expect(near(p.scale_x, 1) && visible(&p, 0, 0, 640, 480), "none again: the window");
    expect(wgf_window_get_width() == 320 && wgf_window_get_height() == 180,
           "a window whose size the program set none of opens at the last design's size");
    expect(wgf_window_set_size(1000, 700), "a size of the program's own");
    wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 800, 600);
    expect(wgf_window_get_width() == 1000 && wgf_window_get_height() == 700, "which a design doesn't change");
    return failures == 0 ? 0 : 1;
}
