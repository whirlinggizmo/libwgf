#include <stddef.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* The feature cost ladder's step 1 (tools/measure_sizes.py, docs/benchmarks.md): the
 * skeleton (examples/c/app-skeleton) and, each adding one thing to the one before,
 *   1. text: one line drawn in the built-in font (fontstash, stb_truetype, the font).
 * Each step's size less the one before is what its feature costs on its own. Nothing to
 * look at for its own sake: it is measured, not shown. */

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(WGF_COLOR_RAYWHITE);
}

static void frame(void *user)
{
    (void)user;
    wgf_draw_text(0, "libwgf", 40, 40, 32, WGF_COLOR_DARKGRAY);
}

int main(void)
{
    wgf_window_set_title("ladder-1-text");
    wgf_window_set_size(800, 600);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
