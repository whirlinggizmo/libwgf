#include <stddef.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* A window cleared each frame, and nothing drawn: what every program pays, the core
 * alone, since every optional part is linked by the call that creates one of it
 * (docs/ARCHITECTURE.md, "Optional parts"). No keys.
 *
 * libwgt's app-skeleton (libwgt's own; wgrender has none) done 1:1 for the size table.
 * The one difference: the title says libwgf where libwgt's says libwgt. */

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(WGF_COLOR_RAYWHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf skeleton");
    wgf_window_set_size(800, 600);
    wgf_app_run(init, NULL, NULL, NULL, NULL);
    return 0;
}
