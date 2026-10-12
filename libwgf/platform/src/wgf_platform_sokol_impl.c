/* The sokol implementations, compiled once for the whole library, as wgrender's
 * wgr_sokol_impl.c: gfx and gl (immediate mode drawing), and, with a window,
 * app, app_utils (window size, style, visibility, and focus, from
 * deps/sokol_utils), and glue. The backend is the build's (CMakeLists.txt):
 * OpenGL core natively, WebGL2 or WebGPU on the web, and the dummy backend, with no
 * GPU, in a headless build, which has no window either. Compiled with warnings off:
 * they aren't libwgf's to fix. fontstash and stb are gfx's (wgf_gfx_*_impl.c), and
 * sokol_time core's (wgf_core_sokol_impl.c). */
#define _POSIX_C_SOURCE 200809L /* clock_gettime, under strict C11 */
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY /* the program has its own main, which calls wgf_app_run */
/* sokol's Windows GL loader, and one call more it doesn't load: a render target read back */
#define SG_GL_FUNCS_EXT                                                                                                \
    _SG_XMACRO(glReadPixels, void,                                                                                     \
               (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *data))
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#if !defined(SOKOL_DUMMY_BACKEND)
#include "sokol_app.h"
#include "sokol_app_utils.h"
#include "sokol_glue.h"
#endif

/* wgf_platform_priv_read_image (wgf_platform_priv.h, not included: it would include
 * sokol's headers again, here where they are implemented, and sokol_app's headless): GL's
 * own read, the image's texture on a framebuffer of its own, read, and let go of, the
 * framebuffer bound before it bound again and sokol told its state has changed. */
bool wgf_platform_priv_read_image(sg_image image, int width, int height, unsigned char *rgba)
{
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    const sg_gl_image_info info = sg_gl_query_image_info(image);
    const GLuint texture = info.tex[info.active_slot];
    GLint bound = 0;
    GLuint framebuffer = 0;
    bool read;
    if (texture == 0 || info.tex_target != GL_TEXTURE_2D) return false;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    read = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (read) glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)bound);
    glDeleteFramebuffers(1, &framebuffer);
    sg_reset_state_cache();
    return read;
#else
    (void)image, (void)width, (void)height, (void)rgba;
    return false;
#endif
}
