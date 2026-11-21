#include <stdio.h>

#include "wgf_app.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* A UI of panels, labels, buttons, and a progress bar, styled in colors: on the web its
 * link map (tools/check_link_map.py, wgf_ui_unlinked_check) holds none of the skins or the
 * widgets it doesn't call, which the frame's code reaches only through what the program's
 * calls give it. */

static int frames;

static void on_frame(void *user)
{
    (void)user;
    if (wgf_ui_begin()) {
        wgf_ui_set_style_value(WGF_UI_VALUE_GAP, 10);
        wgf_ui_begin_panel("menu");
        wgf_ui_label("menu", 0);
        wgf_ui_begin_box("row", WGF_UI_DIRECTION_ROW);
        wgf_ui_button("play", "Play");
        wgf_ui_spacer(4);
        wgf_ui_button("quit", "Quit");
        wgf_ui_end_box();
        wgf_ui_progress(0.5f);
        wgf_ui_end_panel();
        wgf_ui_end();
    }
    if (++frames == 3) wgf_app_quit();
}

int main(void)
{
    wgf_window_set_size(320, 240);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return frames >= 3 ? 0 : 1;
}
