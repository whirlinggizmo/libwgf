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

/* wgf_platform_priv_read_screen, as read_image: the window's own framebuffer bound for the
 * read, then the one bound before. */
bool wgf_platform_priv_read_screen(int width, int height, unsigned char *rgba)
{
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(SOKOL_DUMMY_BACKEND)
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)sglue_swapchain().gl.framebuffer);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)bound);
    sg_reset_state_cache();
    return true;
#else
    (void)width, (void)height, (void)rgba;
    return false;
#endif
}

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

/* ---- a lost GPU context (wgf_platform_priv.h, "The GPU's context") ----------------------
 *
 * WebGL2 alone loses its context. sokol_gfx has nothing for it, so its objects are made
 * again here in place, under their ids, by the GL backend's own creates on the slots sokol
 * keeps: the ids gfx, sokol_gl, and sokol_fontstash hold stay good, and a pipeline (on GL
 * only state, its shader's program found again by name) needs nothing. What a create needs
 * that sokol doesn't keep is kept from the creates, on sokol's trace hooks: every shader's
 * description, its strings copied (a custom shader frees its sources once made), and the
 * data an immutable buffer or image was made with when it is small; everything else comes
 * back empty for its owner to fill. Natively, and on the web's other backends, nothing is
 * kept and nothing is done. */
#if defined(SOKOL_GLES3)
#include <stdlib.h>

void wgf_platform_priv_gpu_reset(void); /* wgf_platform_system_web.c: Emscripten's side */

#define KEPT_MAX 16384 /* bytes: the placeholder checker's 64 by 64 texels, and anything smaller */

typedef struct kept_bytes_t {
    sg_range data; /* an image's levels, one after another as it was made with them, or a buffer's bytes: below */
    unsigned char bytes[];
} kept_bytes_t;

static struct {
    bool on;
    sg_trace_hooks before; /* the hooks installed before (gfx's stats), called first */
    kept_bytes_t **buffers, **images; /* by slot, as big as sokol's pools */
    sg_shader_desc **shaders;
    int buffer_count, image_count, shader_count;
} kept;

static int slot_of(uint32_t id, int count)
{
    const int slot = (int)(id & _SG_SLOT_MASK);
    return slot > 0 && slot < count ? slot : 0;
}

static void forget(void **table, int count, uint32_t id)
{
    const int slot = slot_of(id, count);
    if (table == NULL || slot == 0) return;
    free(table[slot]);
    table[slot] = NULL;
}

/* `data` copied, or NULL when there is none or more than KEPT_MAX bytes of it. */
static kept_bytes_t *copy_data(const sg_range *data)
{
    kept_bytes_t *copy;
    if (data->ptr == NULL || data->size == 0 || data->size > KEPT_MAX) return NULL;
    copy = (kept_bytes_t *)calloc(1, sizeof(kept_bytes_t) + data->size);
    if (copy == NULL) return NULL;
    memcpy(copy->bytes, data->ptr, data->size);
    copy->data.ptr = copy->bytes;
    copy->data.size = data->size;
    return copy;
}

static void keep_buffer(const sg_buffer_desc *desc, sg_buffer result, void *user)
{
    const int slot = slot_of(result.id, kept.buffer_count);
    if (kept.before.make_buffer != NULL) kept.before.make_buffer(desc, result, user);
    if (!kept.on || slot == 0) return;
    free(kept.buffers[slot]);
    kept.buffers[slot] = sg_query_buffer_state(result) == SG_RESOURCESTATE_VALID ? copy_data(&desc->data) : NULL;
}

static void keep_image(const sg_image_desc *desc, sg_image result, void *user)
{
    const int slot = slot_of(result.id, kept.image_count);
    if (kept.before.make_image != NULL) kept.before.make_image(desc, result, user);
    if (!kept.on || slot == 0) return;
    free(kept.images[slot]);
    kept.images[slot] = sg_query_image_state(result) == SG_RESOURCESTATE_VALID ? copy_data(&desc->data) : NULL;
}

/* A string's room in a copy, and the copy made there. */
static size_t string_room(const char *s)
{
    return s != NULL ? strlen(s) + 1 : 0;
}

static const char *copy_string(const char *s, char **at)
{
    char *copy;
    if (s == NULL) return NULL;
    copy = *at;
    memcpy(copy, s, strlen(s) + 1);
    *at += strlen(s) + 1;
    return copy;
}

/* What the GL backend's create reads of a shader's description, its strings (sources, and
 * attribute, uniform, and texture names) copied after it; what it doesn't read let go of. */
static void keep_shader(const sg_shader_desc *desc, sg_shader result, void *user)
{
    const int slot = slot_of(result.id, kept.shader_count);
    sg_shader_desc *copy;
    size_t room = sizeof(sg_shader_desc);
    char *at;
    int i, u;
    if (kept.before.make_shader != NULL) kept.before.make_shader(desc, result, user);
    if (!kept.on || slot == 0) return;
    free(kept.shaders[slot]);
    kept.shaders[slot] = NULL;
    if (sg_query_shader_state(result) != SG_RESOURCESTATE_VALID) return;
    room += string_room(desc->vertex_func.source) + string_room(desc->fragment_func.source) +
            string_room(desc->compute_func.source);
    for (i = 0; i < SG_MAX_VERTEX_ATTRIBUTES; i++) room += string_room(desc->attrs[i].glsl_name);
    for (i = 0; i < SG_MAX_UNIFORMBLOCK_BINDSLOTS; i++) {
        for (u = 0; u < SG_MAX_UNIFORMBLOCK_MEMBERS; u++) {
            room += string_room(desc->uniform_blocks[i].glsl_uniforms[u].glsl_name);
        }
    }
    for (i = 0; i < SG_MAX_TEXTURE_SAMPLER_PAIRS; i++) room += string_room(desc->texture_sampler_pairs[i].glsl_name);
    copy = (sg_shader_desc *)malloc(room);
    if (copy == NULL) return;
    *copy = *desc;
    at = (char *)(copy + 1);
    memset(&copy->vertex_func, 0, sizeof(copy->vertex_func));
    memset(&copy->fragment_func, 0, sizeof(copy->fragment_func));
    memset(&copy->compute_func, 0, sizeof(copy->compute_func));
    copy->vertex_func.source = copy_string(desc->vertex_func.source, &at);
    copy->fragment_func.source = copy_string(desc->fragment_func.source, &at);
    copy->compute_func.source = copy_string(desc->compute_func.source, &at);
    for (i = 0; i < SG_MAX_VERTEX_ATTRIBUTES; i++) {
        copy->attrs[i].glsl_name = copy_string(desc->attrs[i].glsl_name, &at);
        copy->attrs[i].hlsl_sem_name = NULL;
    }
    for (i = 0; i < SG_MAX_UNIFORMBLOCK_BINDSLOTS; i++) {
        for (u = 0; u < SG_MAX_UNIFORMBLOCK_MEMBERS; u++) {
            copy->uniform_blocks[i].glsl_uniforms[u].glsl_name =
                copy_string(desc->uniform_blocks[i].glsl_uniforms[u].glsl_name, &at);
        }
    }
    for (i = 0; i < SG_MAX_TEXTURE_SAMPLER_PAIRS; i++) {
        copy->texture_sampler_pairs[i].glsl_name = copy_string(desc->texture_sampler_pairs[i].glsl_name, &at);
    }
    copy->label = NULL;
    kept.shaders[slot] = copy;
}

static void forget_buffer(sg_buffer buffer, void *user)
{
    if (kept.before.destroy_buffer != NULL) kept.before.destroy_buffer(buffer, user);
    forget((void **)kept.buffers, kept.buffer_count, buffer.id);
}

static void forget_image(sg_image image, void *user)
{
    if (kept.before.destroy_image != NULL) kept.before.destroy_image(image, user);
    forget((void **)kept.images, kept.image_count, image.id);
}

static void forget_shader(sg_shader shader, void *user)
{
    if (kept.before.destroy_shader != NULL) kept.before.destroy_shader(shader, user);
    forget((void **)kept.shaders, kept.shader_count, shader.id);
}

static void free_table(void **table, int count)
{
    int i;
    for (i = 0; table != NULL && i < count; i++) free(table[i]);
    free(table);
}

void wgf_platform_priv_gpu_keep(bool keep)
{
    sg_trace_hooks hooks;
    if (keep == kept.on) return;
    if (!keep) {
        kept.on = false;
        free_table((void **)kept.buffers, kept.buffer_count);
        free_table((void **)kept.images, kept.image_count);
        free_table((void **)kept.shaders, kept.shader_count);
        kept.buffers = kept.images = NULL;
        kept.shaders = NULL;
        kept.buffer_count = kept.image_count = kept.shader_count = 0;
        return; /* the hooks stay, doing nothing more, until sokol stops */
    }
    kept.buffer_count = _sg.pools.buffer_pool.size;
    kept.image_count = _sg.pools.image_pool.size;
    kept.shader_count = _sg.pools.shader_pool.size;
    kept.buffers = (kept_bytes_t **)calloc((size_t)kept.buffer_count, sizeof(kept_bytes_t *));
    kept.images = (kept_bytes_t **)calloc((size_t)kept.image_count, sizeof(kept_bytes_t *));
    kept.shaders = (sg_shader_desc **)calloc((size_t)kept.shader_count, sizeof(sg_shader_desc *));
    if (kept.buffers == NULL || kept.images == NULL || kept.shaders == NULL) {
        kept.on = true;
        wgf_platform_priv_gpu_keep(false);
        return;
    }
    memset(&hooks, 0, sizeof(hooks));
    kept.before = sg_install_trace_hooks(&hooks); /* what was there, called on from these */
    hooks = kept.before;
    hooks.make_buffer = keep_buffer;
    hooks.make_image = keep_image;
    hooks.make_shader = keep_shader;
    hooks.destroy_buffer = forget_buffer;
    hooks.destroy_image = forget_image;
    hooks.destroy_shader = forget_shader;
    sg_install_trace_hooks(&hooks);
    kept.on = true;
}

void wgf_platform_priv_gpu_lost(void)
{
    int i;
    for (i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
    } /* the context's lost error, read once, and anything before it */
}

bool wgf_platform_priv_gpu_restore(void)
{
    bool ok = true;
    int i;
    wgf_platform_priv_gpu_reset();
    for (i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
    }
    /* sokol's own: its vertex array and framebuffers, and its state as setup leaves it */
    glGenVertexArrays(1, &_sg.gl.vao);
    glBindVertexArray(_sg.gl.vao);
    glGenFramebuffers(1, &_sg.gl.fb);
    glGenFramebuffers(1, &_sg.gl.copy_src_fb);
    glGenFramebuffers(1, &_sg.gl.copy_dst_fb);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    _sg_gl_reset_state_cache();
    for (i = 1; i < _sg.pools.buffer_pool.size; i++) {
        _sg_buffer_t *buf = &_sg.pools.buffers[i];
        const kept_bytes_t *data = i < kept.buffer_count && kept.buffers != NULL ? kept.buffers[i] : NULL;
        sg_buffer_desc desc;
        if (buf->slot.state != SG_RESOURCESTATE_VALID || buf->gl.injected) continue;
        memset(&desc, 0, sizeof(desc));
        if (data != NULL) desc.data = data->data;
        memset(buf->gl.buf, 0, sizeof(buf->gl.buf));
        if (_sg_gl_create_buffer(buf, &desc) != SG_RESOURCESTATE_VALID) ok = false;
    }
    for (i = 1; i < _sg.pools.image_pool.size; i++) {
        _sg_image_t *img = &_sg.pools.images[i];
        const kept_bytes_t *data = i < kept.image_count && kept.images != NULL ? kept.images[i] : NULL;
        sg_image_desc desc;
        if (img->slot.state != SG_RESOURCESTATE_VALID || img->gl.injected) continue;
        memset(&desc, 0, sizeof(desc));
        if (data != NULL) desc.data = data->data;
        memset(img->gl.tex, 0, sizeof(img->gl.tex));
        if (_sg_gl_create_image(img, &desc) != SG_RESOURCESTATE_VALID) ok = false;
    }
    for (i = 1; i < _sg.pools.sampler_pool.size; i++) {
        _sg_sampler_t *smp = &_sg.pools.samplers[i];
        sg_sampler_desc desc;
        sg_sampler id;
        if (smp->slot.state != SG_RESOURCESTATE_VALID || smp->gl.injected) continue;
        id.id = smp->slot.id;
        desc = sg_query_sampler_desc(id);
        smp->gl.smp = 0;
        if (_sg_gl_create_sampler(smp, &desc) != SG_RESOURCESTATE_VALID) ok = false;
    }
    for (i = 1; i < _sg.pools.shader_pool.size; i++) {
        _sg_shader_t *shd = &_sg.pools.shaders[i];
        const sg_shader_desc *desc = i < kept.shader_count && kept.shaders != NULL ? kept.shaders[i] : NULL;
        if (shd->slot.state != SG_RESOURCESTATE_VALID) continue;
        memset(&shd->gl, 0, sizeof(shd->gl));
        if (desc == NULL || _sg_gl_create_shader(shd, desc) != SG_RESOURCESTATE_VALID) ok = false;
    }
    for (i = 1; i < _sg.pools.view_pool.size; i++) {
        _sg_view_t *view = &_sg.pools.views[i];
        sg_view_desc desc;
        if (view->slot.state != SG_RESOURCESTATE_VALID) continue;
        memset(&desc, 0, sizeof(desc));
        memset(&view->gl, 0, sizeof(view->gl));
        if (_sg_gl_create_view(view, &desc) != SG_RESOURCESTATE_VALID) ok = false;
    }
    _sg_gl_reset_state_cache();
    sg_commit(); /* a frame of sokol's: what was written in the last before the loss may be written again */
    return ok;
}
#else
void wgf_platform_priv_gpu_keep(bool keep)
{
    (void)keep;
}

void wgf_platform_priv_gpu_lost(void)
{
}

bool wgf_platform_priv_gpu_restore(void)
{
    return true;
}
#endif
