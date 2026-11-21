#include "text/wgf_gfx_glyph_priv.h"

#include <emscripten.h>
#include <string.h>

/* Glyphs drawn by the browser (wgf_gfx_glyph_priv.h), in a page: a font's bytes a FontFace, in
 * the page's fonts under a family of its own once loaded, and a glyph its text filled on a
 * canvas kept for it, read back for its alpha. helix's CanvasFontRasterizerAdapter. */

EM_JS(void, wgf_gfx_js_face_open, (const unsigned char *bytes, int size), {
    const faces = Module.wgfFaces || (Module.wgfFaces = new Map());
    Module.wgfFaceCount = (Module.wgfFaceCount || 0) + 1;
    const entry = {state : 0, family : "wgf-font-" + Module.wgfFaceCount, face : null};
    faces.set(bytes, entry);
    try {
        entry.face = new FontFace(entry.family, HEAPU8.slice(bytes, bytes + size));
    } catch (error) {
        entry.state = 2;
        return;
    }
    entry.face.load().then(
        () => {
            if (faces.get(bytes) !== entry) return; /* let go of meanwhile */
            document.fonts.add(entry.face);
            entry.state = 1;
        },
        () => { entry.state = 2; });
})

/* -1 for bytes with no face. */
EM_JS(int, wgf_gfx_js_face_state, (const unsigned char *bytes), {
    const entry = Module.wgfFaces && Module.wgfFaces.get(bytes);
    return entry ? entry.state : -1;
})

EM_JS(void, wgf_gfx_js_face_close, (const unsigned char *bytes), {
    const entry = Module.wgfFaces && Module.wgfFaces.get(bytes);
    if (!entry) return;
    Module.wgfFaces.delete(bytes);
    if (entry.state === 1) document.fonts.delete(entry.face);
})

EM_JS(void, wgf_gfx_js_face_close_all, (void), {
    if (!Module.wgfFaces) return;
    for (const entry of Module.wgfFaces.values()) {
        if (entry.state === 1) document.fonts.delete(entry.face);
    }
    Module.wgfFaces.clear();
})

EM_JS(int, wgf_gfx_js_face_any_pending, (void), {
    if (!Module.wgfFaces) return 0;
    for (const entry of Module.wgfFaces.values()) {
        if (entry.state === 0) return 1;
    }
    return 0;
})

/* The canvas's context, sized, its font set; null for a face that isn't READY. */
EM_JS(void, wgf_gfx_js_glyph_setup, (void), {
    if (Module.wgfGlyphContext) return;
    Module.wgfGlyphContext = (bytes, em, width, height) => {
        const entry = Module.wgfFaces && Module.wgfFaces.get(bytes);
        if (!entry || entry.state !== 1) return null;
        const canvas = Module.wgfGlyphCanvas || (Module.wgfGlyphCanvas = new OffscreenCanvas(1, 1));
        canvas.width = width; /* which clears it too */
        canvas.height = height;
        const context = canvas.getContext("2d", {willReadFrequently : true});
        context.font = em + "px \"" + entry.family + "\"";
        context.textBaseline = "alphabetic";
        context.textAlign = "left";
        context.fillStyle = "#000";
        return context;
    };
})

EM_JS(void, wgf_gfx_js_glyph_measure, (const unsigned char *bytes, int codepoint, double em, int *box), {
    const context = Module.wgfGlyphContext(bytes, em, 1, 1);
    if (!context) return;
    const measured = context.measureText(String.fromCodePoint(codepoint));
    HEAP32[box >> 2] = Math.floor(-measured.actualBoundingBoxLeft);
    HEAP32[(box >> 2) + 1] = Math.floor(-measured.actualBoundingBoxAscent);
    HEAP32[(box >> 2) + 2] = Math.ceil(measured.actualBoundingBoxRight);
    HEAP32[(box >> 2) + 3] = Math.ceil(measured.actualBoundingBoxDescent);
})

EM_JS(void, wgf_gfx_js_glyph_draw,
      (const unsigned char *bytes, int codepoint, double em, int x, int y, unsigned char *out, int width, int height,
       int stride),
      {
          const context = Module.wgfGlyphContext(bytes, em, width, height);
          if (!context) return;
          context.fillText(String.fromCodePoint(codepoint), x, y);
          const pixels = context.getImageData(0, 0, width, height).data;
          for (let row = 0; row < height; row++) {
              for (let column = 0; column < width; column++) {
                  HEAPU8[out + row * stride + column] = pixels[(row * width + column) * 4 + 3];
              }
          }
      })

/* A glyph was asked for while its face was PENDING, and made blank. */
static bool missed;

wgf_gfx_priv_face_state_t wgf_gfx_priv_face_wait(const unsigned char *bytes, int size)
{
    int state = wgf_gfx_js_face_state(bytes);
    if (state < 0) {
        wgf_gfx_js_face_open(bytes, size);
        state = wgf_gfx_js_face_state(bytes);
    }
    return (wgf_gfx_priv_face_state_t)state;
}

void wgf_gfx_priv_face_close(const unsigned char *bytes)
{
    if (bytes != NULL) wgf_gfx_js_face_close(bytes);
}

void wgf_gfx_priv_face_close_all(void)
{
    wgf_gfx_js_face_close_all();
    missed = false;
}

bool wgf_gfx_priv_face_any_pending(void)
{
    return wgf_gfx_js_face_any_pending() != 0;
}

bool wgf_gfx_priv_face_take_missed(void)
{
    if (!missed || wgf_gfx_priv_face_any_pending()) return false;
    missed = false;
    return true;
}

void wgf_gfx_priv_glyph_measure(const unsigned char *bytes, int codepoint, float em, int box[4])
{
    memset(box, 0, sizeof(int) * 4);
    if (wgf_gfx_js_face_state(bytes) == WGF_GFX_PRIV_FACE_PENDING) missed = true;
    wgf_gfx_js_glyph_setup();
    wgf_gfx_js_glyph_measure(bytes, codepoint, (double)em, box);
}

void wgf_gfx_priv_glyph_draw(const unsigned char *bytes, int codepoint, float em, int x, int y, unsigned char *out,
                             int width, int height, int stride)
{
    int row;
    if (width <= 0 || height <= 0) return;
    for (row = 0; row < height; row++) memset(out + (size_t)row * (size_t)stride, 0, (size_t)width);
    if (wgf_gfx_js_face_state(bytes) == WGF_GFX_PRIV_FACE_PENDING) missed = true;
    wgf_gfx_js_glyph_setup();
    wgf_gfx_js_glyph_draw(bytes, codepoint, (double)em, x, y, out, width, height, stride);
}
