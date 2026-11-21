#include "texture/wgf_gfx_decode_priv.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>

#include "wgf_log.h"

/* Images decoded by the browser (wgf_gfx_decode_priv.h), in a page: createImageBitmap, with no
 * premultiplying and no color conversion, so its pixels are the file's; read back through a
 * WebGL2 context of the decoder's own (a 2D canvas would premultiply them, losing a level or so
 * at a partly transparent edge), never the program's, whose state stays as it was. Done in a
 * later update. helix's page decoder, read back exactly. Its context is let go of (lost on
 * purpose) once no decode is under way, so a page holds it only while it decodes: a browser
 * keeps few WebGL contexts at once, and past them it loses the oldest, which may be a page's
 * own. Whatever throws reading one back is that decode FAILED, never one left pending. */

/* The decoder's context let go of when nothing is decoding. */
EM_JS(void, wgf_gfx_js_decode_idle, (void), {
    const gl = Module.wgfDecodeGl;
    if (!gl || (Module.wgfDecodes && Module.wgfDecodes.size > 0)) return;
    Module.wgfDecodeGl = null;
    const lose = gl.getExtension("WEBGL_lose_context");
    if (lose) lose.loseContext();
})

EM_JS(void, wgf_gfx_js_decode_start, (int id, const unsigned char *bytes, int size), {
    const decodes = Module.wgfDecodes || (Module.wgfDecodes = new Map());
    const entry = {state : 0, pixels : null, width : 0, height : 0};
    decodes.set(id, entry);
    const read = (bitmap) => {
        let gl = Module.wgfDecodeGl;
        if (!gl || gl.isContextLost()) {
            gl = Module.wgfDecodeGl = document.createElement("canvas").getContext("webgl2", {premultipliedAlpha : false});
        }
        if (!gl) return null;
        const texture = gl.createTexture(), framebuffer = gl.createFramebuffer();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, bitmap);
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
        let pixels = null;
        if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE) {
            pixels = new Uint8Array(bitmap.width * bitmap.height * 4);
            gl.readPixels(0, 0, bitmap.width, bitmap.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels); /* the top row first */
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.deleteFramebuffer(framebuffer);
        gl.deleteTexture(texture);
        return pixels;
    };
    const options = {premultiplyAlpha : "none", colorSpaceConversion : "none"};
    createImageBitmap(new Blob([HEAPU8.slice(bytes, bytes + size)]), options).then(
        (bitmap) => {
            if (!decodes.has(id)) { /* let go of meanwhile */
                bitmap.close();
                return;
            }
            try {
                entry.pixels = read(bitmap);
                entry.width = bitmap.width;
                entry.height = bitmap.height;
                entry.state = entry.pixels ? 1 : 2;
            } catch (error) {
                entry.state = 2;
            }
            bitmap.close();
        },
        () => { entry.state = 2; });
})

EM_JS(int, wgf_gfx_js_decode_state, (int id), {
    const entry = Module.wgfDecodes && Module.wgfDecodes.get(id);
    return entry ? entry.state : 2;
})

EM_JS(int, wgf_gfx_js_decode_width, (int id), { return Module.wgfDecodes.get(id).width; })
EM_JS(int, wgf_gfx_js_decode_height, (int id), { return Module.wgfDecodes.get(id).height; })

/* Its pixels copied to `out` (width x height x 4), then let go of. */
EM_JS(void, wgf_gfx_js_decode_copy, (int id, unsigned char *out), {
    const entry = Module.wgfDecodes.get(id);
    HEAPU8.set(entry.pixels, out);
    Module.wgfDecodes.delete(id);
})

EM_JS(void, wgf_gfx_js_decode_free, (int id), {
    if (Module.wgfDecodes) Module.wgfDecodes.delete(id);
})

struct wgf_gfx_priv_decode_t {
    int id;
    char what[160]; /* for its failure's log */
};

static int next_id = 1;

wgf_gfx_priv_decode_t *wgf_gfx_priv_decode_start(const unsigned char *bytes, int size, const char *what)
{
    wgf_gfx_priv_decode_t *decode = (wgf_gfx_priv_decode_t *)calloc(1, sizeof(*decode));
    if (decode == NULL) return NULL;
    decode->id = next_id++;
    snprintf(decode->what, sizeof(decode->what), "%s", what != NULL ? what : "");
    wgf_gfx_js_decode_start(decode->id, bytes, size);
    return decode;
}

wgf_gfx_priv_decode_state_t wgf_gfx_priv_decode_poll(wgf_gfx_priv_decode_t *decode)
{
    return decode != NULL ? (wgf_gfx_priv_decode_state_t)wgf_gfx_js_decode_state(decode->id) : WGF_GFX_PRIV_DECODE_FAILED;
}

unsigned char *wgf_gfx_priv_decode_take(wgf_gfx_priv_decode_t *decode, int *width, int *height)
{
    unsigned char *pixels = NULL;
    if (decode == NULL) return NULL;
    if (wgf_gfx_js_decode_state(decode->id) == WGF_GFX_PRIV_DECODE_DONE) {
        *width = wgf_gfx_js_decode_width(decode->id);
        *height = wgf_gfx_js_decode_height(decode->id);
        pixels = (unsigned char *)malloc((size_t)*width * (size_t)*height * 4);
        if (pixels != NULL) wgf_gfx_js_decode_copy(decode->id, pixels);
        else wgf_log_warn("wgf_gfx: %s: no memory for its %dx%d pixels", decode->what, *width, *height);
    } else {
        wgf_log_warn("wgf_gfx: %s: not an image this browser reads", decode->what);
    }
    wgf_gfx_js_decode_free(decode->id);
    free(decode);
    wgf_gfx_js_decode_idle();
    return pixels;
}

void wgf_gfx_priv_decode_cancel(wgf_gfx_priv_decode_t *decode)
{
    if (decode == NULL) return;
    wgf_gfx_js_decode_free(decode->id);
    free(decode);
    wgf_gfx_js_decode_idle();
}
