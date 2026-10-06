#include <stdio.h>

#include "wgf_app.h"
#include "wgf_debug.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* Fixed-rate simulation (ticks) against drawing (frames). A deliberately slow 10 Hz
 * tick moves two squares at the same speed: the top one is drawn where the latest tick
 * left it, so it steps; the bottom one between its last two ticks, by the tick
 * fraction, so it moves smoothly at any frame rate. Space pauses the ticks (the time
 * scale) while frames go on; Enter counts presses in the tick and in the frame, which
 * always match, as each press is seen by exactly one tick. Escape quits where quitting
 * means anything.
 *
 * libwgt's app-tick (wgrender's tick) done 1:1 for the size table. The differences:
 *   - the title and the heading say libwgf where libwgt's say libwgt;
 *   - The readout is libwgf's overlay (wgf_debug_show_fps), as libwgt's is its
 *     wgt_loop_draw_fps: the same place, font, size, and color, with the frame's
 *     cost in milliseconds beside the rate. */

enum { WIDTH = 800, HEIGHT = 450, TICK_HZ = 10, SQUARE = 40, LEFT = 40, RIGHT = WIDTH - 80 };

static const float SPEED = 240.0f; /* logical pixels a second */

static struct {
    float previous_x, x; /* the simulation: its last two tick positions */
    int ticks;
    int tick_presses, frame_presses;
} g;


static void tick(void *user)
{
    (void)user;
    g.previous_x = g.x;
    g.x += SPEED * wgf_loop_get_tick_delta();
    if (g.x > RIGHT) {
        g.x = LEFT;
        g.previous_x = g.x; /* not smoothed across the wrap */
    }
    g.ticks++;
    if (wgf_keyboard_is_pressed(WGF_KEY_ENTER)) g.tick_presses++;
}

static void frame(void *user)
{
    const float t = wgf_loop_get_tick_fraction();
    char line[128];
    (void)user;

    if (wgf_keyboard_is_pressed(WGF_KEY_ENTER)) g.frame_presses++;
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) wgf_loop_set_time_scale(wgf_loop_get_time_scale() > 0.0f ? 0.0f : 1.0f);
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    wgf_draw_text(0, "libwgf tick: a 10 Hz simulation, drawn every frame", 20, 20, 20, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "frame delta %.4f s   tick fraction %.2f   ticks %d   %s", wgf_loop_get_frame_delta(),
             t, g.ticks, wgf_loop_get_time_scale() > 0.0f ? "running (Space pauses)" : "paused (Space resumes)");
    wgf_draw_text(0, line, 20, 50, 16, WGF_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "Enter presses: tick %d, frame %d", g.tick_presses, g.frame_presses);
    wgf_draw_text(0, line, 20, 74, 16, WGF_COLOR_LIGHTGRAY);

    wgf_draw_text(0, "where the latest tick left it", 20, 130, 16, WGF_COLOR_GRAY);
    wgf_draw_rectangle(g.x, 155, SQUARE, SQUARE, WGF_COLOR_ORANGE);
    wgf_draw_text(0, "between the last two ticks, by the tick fraction", 20, 250, 16, WGF_COLOR_GRAY);
    wgf_draw_rectangle(g.previous_x + (g.x - g.previous_x) * t, 275, SQUARE, SQUARE, WGF_COLOR_SKYBLUE);

}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(24, 26, 34, 255));
    wgf_loop_set_tick_rate(TICK_HZ);
    g.x = g.previous_x = LEFT;
}

int main(void)
{
    wgf_window_set_title("libwgf tick");
    wgf_window_set_size(WIDTH, HEIGHT);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 20, HEIGHT - 30, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's draw_fps */
    wgf_app_run(init, tick, frame, NULL, NULL);
    return 0;
}
