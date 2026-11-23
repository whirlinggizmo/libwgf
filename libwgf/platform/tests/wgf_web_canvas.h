#ifndef WGF_WEB_CANVAS_H
#define WGF_WEB_CANVAS_H

/* For the web tests: sizes the page's canvas, as a page's CSS does. The page sizes the canvas,
 * never the program (wgf_window_set_size changes nothing on the web), and the tests' page is
 * tools/run_in_browser.py's, 64 by 64. A test that needs another size calls this, before the
 * run or during it. */

#include <emscripten.h>

/* Sets the canvas's size on the page, in CSS pixels, and tells the window it was resized. */
EM_JS(void, web_canvas_set_size, (int width, int height), {
    Module.canvas.style.width = width + "px";
    Module.canvas.style.height = height + "px";
    window.dispatchEvent(new Event("resize"));
})

#endif
