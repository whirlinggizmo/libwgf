#include "wgf_app_script_source_priv.h"

#include <emscripten.h>
#include <stdlib.h>

#include "wgf_log.h"

/* A script, on the web: the text the page hands the module, Module["wgfScript"] (a
 * page fetches it before starting the module, as `wgf play` and the browser check do),
 * read by its quoted key, so a minified page keeps it. C allocates and JS copies in,
 * so the page needs no exported allocator. */

EM_JS(int, wgf_app_priv_script_web_size, (void), {
    var text = Module["wgfScript"];
    return typeof text === "string" && text.length > 0 ? lengthBytesUTF8(text) + 1 : 0;
})

EM_JS(void, wgf_app_priv_script_web_copy, (char *out, int size), { stringToUTF8(Module["wgfScript"], out, size); })

char *wgf_app_priv_script_read_source(bool *named)
{
    const int size = wgf_app_priv_script_web_size();
    char *text = size > 0 ? (char *)malloc((size_t)size) : NULL;
    *named = size > 0;
    if (size > 0 && text == NULL) {
        wgf_log_error("wgf_script: out of memory for the page's script");
        return NULL;
    }
    if (text != NULL) {
        wgf_app_priv_script_web_copy(text, size);
        wgf_log_info("wgf_script: running the page's script");
    }
    return text;
}
