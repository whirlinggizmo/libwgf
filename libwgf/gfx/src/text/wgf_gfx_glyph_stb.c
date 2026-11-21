#include "text/wgf_gfx_glyph_priv.h"

/* Glyphs drawn by stb_truetype (wgf_gfx_glyph_priv.h), inside fontstash: a font is ready as its
 * bytes are, and there is no face to keep. */

wgf_gfx_priv_face_state_t wgf_gfx_priv_face_wait(const unsigned char *bytes, int size)
{
    (void)bytes;
    (void)size;
    return WGF_GFX_PRIV_FACE_READY;
}

void wgf_gfx_priv_face_close(const unsigned char *bytes)
{
    (void)bytes;
}

void wgf_gfx_priv_face_close_all(void)
{
}

bool wgf_gfx_priv_face_any_pending(void)
{
    return false;
}

bool wgf_gfx_priv_face_take_missed(void)
{
    return false;
}
