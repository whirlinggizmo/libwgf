#include "wgf_ui_document_priv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "text/wgf_gfx_font_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "wgf_log.h"
#include "wgf_mat4.h"
#include "wgf_material.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_ui_priv.h"
#include "wgf_ui_rmlui_priv.h"

/* the shaders' sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_ui_document.glsl.h"

/* What documents draw, as RmlUi's render interface asks (wgf_ui_rmlui_priv.h's drawing
 * calls), into the frame: triangles into immediate mode (sokol_gl) with the transform as
 * its model-view and the scissor as gfx's clip stack, from the visible area's top-left;
 * and past them, as RmlUi's GL3 backend does them, on gfx's render targets:
 *
 * - Clip masks in the stencil (the screen's and every target's have one): set by drawing
 *   triangles with the stencil's write, tested equal by every draw while one is on, and
 *   drawn again into each pass entered, since a pass begins with its stencil cleared.
 * - Layers as render targets the visible area's size at the frame's pixel density. A pass
 *   is begun only when something is drawn into a layer (gfx draws each target's passes in
 *   the order begun, before the screen's, so what a pass reads was drawn in an earlier
 *   one). Once a frame pushes a layer, what the documents draw on is a target too (the
 *   base), composited onto the screen as the drawing ends, since the screen's pass is drawn
 *   last of all and a layer read there would hold what it was last drawn with; a frame
 *   after one that used layers draws on the base from its start.
 * - Filters as passes over a whole layer through three spare targets: a color matrix
 *   (opacity and CSS's color filters, composed and applied at the end, as the passes
 *   between are linear), a blur (one pass across, one down, its taps up to 32 each side,
 *   spread for a wide one), a drop shadow (the layer's alpha tinted, offset, and blurred,
 *   the layer over it), and a mask (a layer saved, its alpha).
 * - Gradients as a shader of their own on a geometry's triangles, kept on the GPU while
 *   RmlUi keeps the geometry.
 *
 * Shaders and passes are the frame's commands (wgf_gfx_priv_render_add_command), drawn
 * in their place among immediate mode. What a frame lets go of (a layer saved, a mask, a
 * geometry's buffers) is freed in the next update, once the frame that drew with it has
 * been drawn. Documents drawn inside a program's own render target can't begin targets of
 * their own: there, layers are the target drawn into (filters and masks not drawn). */

#define LAYERS_MAX 32 /* deep */
#define MASKS_MAX 16  /* clip mask operations since the last set */
#define REFS 8        /* stencil values a clip mask is tested at, 1 to 7 */
#define TEMPS 3
#define BASES 4 /* draws of the documents a frame that can each have a base */
#define STOPS_MAX 16
#define BLUR_TAPS 32

enum { PIPE_STRAIGHT, PIPE_PREMULTIPLIED, PIPE_FONT, PIPE_KINDS };
enum { WRITE_ZERO, WRITE_ONE, WRITE_INCREMENT, WRITES };
enum { PROGRAM_GRADIENT, PROGRAM_BLIT, PROGRAM_BLUR, PROGRAM_SHADOW, PROGRAM_MASK, PROGRAMS };
enum { BLEND_REPLACE, BLEND_OVER, BLENDS };

/* A clip mask operation, kept to be drawn again into each pass entered. */
typedef struct mask_t {
    int operation; /* 0 set, 1 set inverse, 2 intersect */
    wgf_ui_priv_vertex_t *vertices;
    int *indices;
    int vertex_count, index_count;
    float tx, ty;
    bool transformed;
    float matrix[16];
} mask_t;

typedef struct filter_t {
    wgf_ui_priv_filter_t kind;
    float values[16];
    wgf_texture_t mask; /* a mask's layer, saved */
} filter_t;

typedef struct gradient_t {
    gradient_fs_params_t params;
} gradient_t;

/* A geometry's triangles on the GPU, for a gradient. */
typedef struct mesh_t {
    uintptr_t geometry;
    sg_buffer vertices, indices;
    int count;
} mesh_t;

/* A draw of the frame's, replayed among immediate mode as a command. */
typedef struct op_t {
    int program;
    sg_pipeline pipeline;
    sg_view views[2];
    sg_sampler sampler;
    sg_buffer vertices, indices;
    int count;
    union {
        gradient_vs_params_t gradient;
        pass_vs_params_t pass;
    } vs;
    union {
        gradient_fs_params_t gradient;
        blit_fs_params_t blit;
        blur_fs_params_t blur;
        shadow_fs_params_t shadow;
    } fs;
} op_t;

static struct {
    /* the frame's drawing */
    bool drawing;
    float origin_x, origin_y; /* the visible area's top-left on the screen, logical */
    float visible_w, visible_h;
    float scale;       /* a layer's pixels a logical pixel */
    int width, height; /* a layer's, in its pixels */
    bool clipped;      /* a clip pushed */
    bool scissor_on;
    int scissor[4];
    bool transformed;
    float matrix[16];

    /* the clip mask */
    bool mask_on;
    int mask_ref;
    mask_t masks[MASKS_MAX];
    int mask_count;
    bool mask_logged;

    /* layers: what is drawn into from now (want), the pass open now (current), 0 the screen */
    bool layers_ok; /* the drawing began on the screen's pass */
    wgf_texture_t want, current;
    bool fresh[LAYERS_MAX + 1]; /* a layer pushed and not yet entered: cleared as it is */
    wgf_texture_t base; /* what the documents draw on, a target; 0 the screen */
    wgf_texture_t layers[LAYERS_MAX + 1];
    wgf_texture_t temps[TEMPS];
    wgf_texture_t bases[BASES];
    int base_count; /* this frame's */
    int pool_w, pool_h;
    int depth;
    int dead; /* layers pushed that couldn't be: drawn into the one under them */
    bool used, used_last;
    unsigned frame;
    int counts[6]; /* this frame's, for tests (wgf_ui_priv_document_draw_counts) */

    /* pipelines and shaders */
    sgl_pipeline pipes[PIPE_KINDS][REFS];
    sgl_pipeline writes[WRITES];
    sg_shader font_shader;
    sg_shader shaders[PROGRAMS];
    sg_pipeline op_pipes[PROGRAMS][BLENDS][REFS];
    sg_sampler sampler;

    /* the frame's commands */
    op_t *ops;
    int op_count, op_capacity;

    mesh_t *meshes;
    int mesh_count, mesh_capacity;

    /* let go of this frame, freed in the next update */
    wgf_texture_t *doomed_textures;
    int doomed_texture_count, doomed_texture_capacity;
    sg_buffer *doomed_buffers;
    int doomed_buffer_count, doomed_buffer_capacity;
} dr;

static bool grow(void **items, int *capacity, int needed, size_t size)
{
    void *grown;
    int next;
    if (needed <= *capacity) return true;
    next = *capacity > 0 ? *capacity * 2 : 16;
    while (next < needed) next *= 2;
    grown = realloc(*items, size * (size_t)next);
    if (grown == NULL) {
        wgf_log_error("wgf_document: out of memory drawing");
        return false;
    }
    *items = grown;
    *capacity = next;
    return true;
}

static void doom_texture(wgf_texture_t texture)
{
    if (texture == 0) return;
    if (!grow((void **)&dr.doomed_textures, &dr.doomed_texture_capacity, dr.doomed_texture_count + 1,
              sizeof(wgf_texture_t))) {
        return; /* kept: a leak, not a draw of what is gone */
    }
    dr.doomed_textures[dr.doomed_texture_count++] = texture;
}

static void doom_buffer(sg_buffer buffer)
{
    if (buffer.id == SG_INVALID_ID) return;
    if (!grow((void **)&dr.doomed_buffers, &dr.doomed_buffer_capacity, dr.doomed_buffer_count + 1,
              sizeof(sg_buffer))) {
        return;
    }
    dr.doomed_buffers[dr.doomed_buffer_count++] = buffer;
}

static void release_texture(wgf_texture_t texture)
{
    if (texture != 0 && wgf_resource_get_status(texture) != WGF_RESOURCE_STATUS_NONE) wgf_resource_release(texture);
}

static void free_doomed(void)
{
    int i;
    for (i = 0; i < dr.doomed_texture_count; i++) release_texture(dr.doomed_textures[i]);
    for (i = 0; i < dr.doomed_buffer_count; i++) sg_destroy_buffer(dr.doomed_buffers[i]);
    dr.doomed_texture_count = dr.doomed_buffer_count = 0;
}

/* ---- pipelines ---------------------------------------------------------------------- */

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

/* A clip mask tested: drawn only where the stencil is `ref`. */
static void stencil_test(sg_stencil_state *stencil, int ref)
{
    stencil->enabled = true;
    stencil->front.compare = SG_COMPAREFUNC_EQUAL;
    stencil->front.fail_op = SG_STENCILOP_KEEP;
    stencil->front.depth_fail_op = SG_STENCILOP_KEEP;
    stencil->front.pass_op = SG_STENCILOP_KEEP;
    stencil->back = stencil->front;
    stencil->read_mask = 0xFF;
    stencil->write_mask = 0;
    stencil->ref = (uint8_t)ref;
}

/* Immediate mode's: blended straight (libwgf's textures and colors) or premultiplied (a
 * layer's), or glyphs from the atlas through sokol_fontstash's shader; under a clip mask
 * at `ref` (0: none). Alpha written too, so what a layer holds composites. */
static sgl_pipeline sgl_pipeline_of(int kind, int ref)
{
    sg_pipeline_desc desc;
    if (kind == PIPE_FONT) {
        sg_shader shader;
        int r;
        if (!wgf_gfx_priv_font_shader(&shader)) return dr.pipes[PIPE_STRAIGHT][0];
        if (shader.id != dr.font_shader.id) { /* fontstash made again: its pipelines too */
            for (r = 0; r < REFS; r++) {
                if (dr.pipes[PIPE_FONT][r].id != SG_INVALID_ID) sgl_destroy_pipeline(dr.pipes[PIPE_FONT][r]);
                dr.pipes[PIPE_FONT][r].id = SG_INVALID_ID;
            }
            dr.font_shader = shader;
        }
    }
    if (dr.pipes[kind][ref].id != SG_INVALID_ID) return dr.pipes[kind][ref];
    memset(&desc, 0, sizeof(desc));
    if (kind == PIPE_FONT) desc.shader = dr.font_shader;
    desc.colors[0].write_mask = SG_COLORMASK_RGBA;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = kind == PIPE_PREMULTIPLIED ? SG_BLENDFACTOR_ONE : SG_BLENDFACTOR_SRC_ALPHA;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    if (ref > 0) stencil_test(&desc.stencil, ref);
    desc.label = "wgf-document";
    sgl_set_context(wgf_gfx_priv_render_get_context());
    dr.pipes[kind][ref] = sgl_make_pipeline(&desc);
    return dr.pipes[kind][ref];
}

/* A clip mask drawn: no color, the stencil set to 0 or 1, or counted up, where drawn. */
static sgl_pipeline write_pipeline(int write)
{
    sg_pipeline_desc desc;
    if (dr.writes[write].id != SG_INVALID_ID) return dr.writes[write];
    memset(&desc, 0, sizeof(desc));
    desc.colors[0].write_mask = SG_COLORMASK_NONE;
    desc.stencil.enabled = true;
    desc.stencil.front.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.front.fail_op = SG_STENCILOP_KEEP;
    desc.stencil.front.depth_fail_op = SG_STENCILOP_KEEP;
    desc.stencil.front.pass_op = write == WRITE_INCREMENT ? SG_STENCILOP_INCR_CLAMP : SG_STENCILOP_REPLACE;
    desc.stencil.back = desc.stencil.front;
    desc.stencil.read_mask = 0xFF;
    desc.stencil.write_mask = 0xFF;
    desc.stencil.ref = write == WRITE_ONE ? 1 : 0;
    desc.label = "wgf-document-mask";
    sgl_set_context(wgf_gfx_priv_render_get_context());
    dr.writes[write] = sgl_make_pipeline(&desc);
    return dr.writes[write];
}

/* A command's: a program, blended over (premultiplied) or replacing, under a clip mask
 * at `ref` (0: none). */
static sg_pipeline op_pipeline(int program, int blend, int ref)
{
    sg_pipeline_desc desc;
    sg_pipeline *pipeline = &dr.op_pipes[program][blend][ref];
    if (pipeline->id != SG_INVALID_ID) return *pipeline;
    if (dr.shaders[program].id == SG_INVALID_ID) {
        const sg_backend backend = shader_backend();
        switch (program) {
        case PROGRAM_GRADIENT: dr.shaders[program] = sg_make_shader(ui_gradient_shader_desc(backend)); break;
        case PROGRAM_BLIT: dr.shaders[program] = sg_make_shader(ui_blit_shader_desc(backend)); break;
        case PROGRAM_BLUR: dr.shaders[program] = sg_make_shader(ui_blur_shader_desc(backend)); break;
        case PROGRAM_SHADOW: dr.shaders[program] = sg_make_shader(ui_shadow_shader_desc(backend)); break;
        default: dr.shaders[program] = sg_make_shader(ui_mask_shader_desc(backend)); break;
        }
    }
    memset(&desc, 0, sizeof(desc));
    desc.shader = dr.shaders[program];
    if (program == PROGRAM_GRADIENT) { /* RmlUi's vertices, as the C++ side keeps them */
        desc.layout.buffers[0].stride = (int)sizeof(wgf_ui_priv_vertex_t);
        desc.layout.attrs[ATTR_ui_gradient_position].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_ui_gradient_position].offset = 0;
        desc.layout.attrs[ATTR_ui_gradient_texcoord].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_ui_gradient_texcoord].offset = 8;
        desc.layout.attrs[ATTR_ui_gradient_color0].format = SG_VERTEXFORMAT_UBYTE4N;
        desc.layout.attrs[ATTR_ui_gradient_color0].offset = 16;
        desc.index_type = SG_INDEXTYPE_UINT32;
    }
    desc.colors[0].write_mask = SG_COLORMASK_RGBA;
    if (blend == BLEND_OVER) {
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }
    if (ref > 0) stencil_test(&desc.stencil, ref);
    desc.label = "wgf-document-pass";
    *pipeline = sg_make_pipeline(&desc);
    return *pipeline;
}

/* ---- the frame's commands ------------------------------------------------------------ */

static void replay(int index)
{
    const op_t *op;
    sg_bindings bind;
    if (index < 0 || index >= dr.op_count) return;
    op = &dr.ops[index];
    sg_apply_pipeline(op->pipeline);
    memset(&bind, 0, sizeof(bind));
    if (op->program == PROGRAM_GRADIENT) {
        bind.vertex_buffers[0] = op->vertices;
        bind.index_buffer = op->indices;
    } else {
        bind.views[0] = op->views[0];
        bind.views[1] = op->views[1];
        bind.samplers[0] = op->sampler;
    }
    sg_apply_bindings(&bind);
    switch (op->program) {
    case PROGRAM_GRADIENT:
        sg_apply_uniforms(UB_gradient_vs_params, &SG_RANGE(op->vs.gradient));
        sg_apply_uniforms(UB_gradient_fs_params, &SG_RANGE(op->fs.gradient));
        break;
    case PROGRAM_BLIT:
        sg_apply_uniforms(UB_pass_vs_params, &SG_RANGE(op->vs.pass));
        sg_apply_uniforms(UB_blit_fs_params, &SG_RANGE(op->fs.blit));
        break;
    case PROGRAM_BLUR:
        sg_apply_uniforms(UB_pass_vs_params, &SG_RANGE(op->vs.pass));
        sg_apply_uniforms(UB_blur_fs_params, &SG_RANGE(op->fs.blur));
        break;
    case PROGRAM_SHADOW:
        sg_apply_uniforms(UB_pass_vs_params, &SG_RANGE(op->vs.pass));
        sg_apply_uniforms(UB_shadow_fs_params, &SG_RANGE(op->fs.shadow));
        break;
    default: sg_apply_uniforms(UB_pass_vs_params, &SG_RANGE(op->vs.pass)); break;
    }
    sg_draw(0, op->count, 1);
}

/* A new command's record, or NULL (out of memory). */
static op_t *new_op(int program, int blend, int ref)
{
    op_t *op;
    if (!grow((void **)&dr.ops, &dr.op_capacity, dr.op_count + 1, sizeof(op_t))) return NULL;
    op = &dr.ops[dr.op_count];
    memset(op, 0, sizeof(*op));
    op->program = program;
    op->pipeline = op_pipeline(program, blend, ref);
    op->count = 6;
    return op;
}

static void add_op(void)
{
    if (wgf_gfx_priv_render_add_command(replay, dr.op_count)) dr.op_count++;
}

/* ---- the pass drawn into ------------------------------------------------------------- */

static float pass_scale(void)
{
    return dr.current != 0 ? dr.scale : 1.0f;
}

static float pass_x(void)
{
    return dr.current != 0 ? 0.0f : dr.origin_x;
}

static float pass_y(void)
{
    return dr.current != 0 ? 0.0f : dr.origin_y;
}

static void unclip(void)
{
    if (dr.clipped) wgf_render_pop_clip();
    dr.clipped = false;
}

static void apply_transform(void)
{
    const float s = pass_scale();
    sgl_matrix_mode_modelview();
    sgl_load_identity();
    sgl_translate(pass_x(), pass_y(), 0.0f);
    if (s != 1.0f) sgl_scale(s, s, 1.0f);
    if (dr.transformed) sgl_mult_matrix(dr.matrix);
}

static void apply_scissor(void)
{
    const float s = pass_scale();
    unclip();
    if (!dr.scissor_on) return;
    wgf_render_push_clip(pass_x() + (float)dr.scissor[0] * s, pass_y() + (float)dr.scissor[1] * s,
                         (float)(dr.scissor[2] > 0 ? dr.scissor[2] : 0) * s,
                         (float)(dr.scissor[3] > 0 ? dr.scissor[3] : 0) * s);
    dr.clipped = true;
}

/* A rectangle of the pass's own, in its 2D coordinates, the model-view as set. */
static void quad(float x0, float y0, float x1, float y1)
{
    sgl_begin_quads();
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x0, y1);
    sgl_end();
}

/* The visible area in the pass's coordinates. */
static void pass_area(float *x0, float *y0, float *x1, float *y1)
{
    if (dr.current != 0) {
        *x0 = *y0 = 0.0f;
        *x1 = (float)dr.width;
        *y1 = (float)dr.height;
    } else {
        *x0 = dr.origin_x;
        *y0 = dr.origin_y;
        *x1 = dr.origin_x + dr.visible_w;
        *y1 = dr.origin_y + dr.visible_h;
    }
}

/* Clip mask operation `m` drawn into the stencil (under the scissor, as a GL clear is). */
static void draw_mask(const mask_t *m)
{
    const float s = pass_scale();
    int i;
    sgl_push_pipeline();
    sgl_disable_texture();
    if (m->operation != 2) { /* set: the stencil cleared first, to 0 or, for its inverse, to 1 */
        float x0, y0, x1, y1;
        pass_area(&x0, &y0, &x1, &y1);
        sgl_load_pipeline(write_pipeline(m->operation == 0 ? WRITE_ZERO : WRITE_ONE));
        sgl_matrix_mode_modelview();
        sgl_load_identity();
        quad(x0, y0, x1, y1);
    }
    sgl_load_pipeline(write_pipeline(m->operation == 0 ? WRITE_ONE : m->operation == 1 ? WRITE_ZERO : WRITE_INCREMENT));
    sgl_matrix_mode_modelview();
    sgl_load_identity();
    sgl_translate(pass_x(), pass_y(), 0.0f);
    if (s != 1.0f) sgl_scale(s, s, 1.0f);
    if (m->transformed) sgl_mult_matrix(m->matrix);
    sgl_begin_triangles();
    for (i = 0; i + 2 < m->index_count; i += 3) {
        int k;
        for (k = 0; k < 3; k++) {
            const wgf_ui_priv_vertex_t *v = &m->vertices[m->indices[i + k]];
            sgl_v2f(v->x + m->tx, v->y + m->ty);
        }
    }
    sgl_end();
    sgl_pop_pipeline();
    apply_transform();
}

/* The clip mask's operations drawn again: a pass begins with its stencil cleared. */
static void draw_masks(void)
{
    int i;
    for (i = 0; i < dr.mask_count; i++) draw_mask(&dr.masks[i]);
}

/* What is drawn from now goes where `want` says: its pass begun when it isn't the one
 * open (a layer drawn into before this frame keeps what it holds), a layer just pushed
 * cleared, and the scissor, transform, and clip mask applied in it. */
static void sync(void)
{
    bool entered = false;
    int layer;
    if (dr.want != dr.current) {
        unclip();
        if (dr.current != 0) wgf_render_end_target();
        dr.current = 0;
        if (dr.want != 0 && wgf_render_begin_target(dr.want)) dr.current = dr.want;
        dr.want = dr.current; /* a target refused (logged): the screen */
        entered = true;
    }
    for (layer = 1; layer <= dr.depth; layer++) {
        if (dr.fresh[layer] && dr.layers[layer] == dr.current) break;
    }
    if (dr.current != 0 && layer <= dr.depth) { /* a layer just pushed: cleared whole, before any scissor */
        unclip();
        wgf_gfx_priv_render_set_2d();
        sgl_load_pipeline(wgf_gfx_priv_render_pipeline_2d(WGF_ALPHA_MODE_OPAQUE));
        sgl_disable_texture();
        sgl_c4b(0, 0, 0, 0);
        quad(0.0f, 0.0f, (float)dr.width, (float)dr.height);
        sgl_c4b(255, 255, 255, 255);
        dr.fresh[layer] = false;
        entered = true;
    }
    if (!entered) return;
    wgf_gfx_priv_render_set_2d();
    apply_transform();
    apply_scissor();
    draw_masks();
}

/* A pass into a spare target with nothing applied: whole, no scissor, no clip mask. */
static void enter_raw(wgf_texture_t target)
{
    unclip();
    if (dr.current != 0) wgf_render_end_target();
    dr.current = 0;
    if (target != 0 && wgf_render_begin_target(target)) dr.current = target;
}

/* ---- the frame ------------------------------------------------------------------------ */

static void release_pool(void)
{
    int i;
    for (i = 0; i <= LAYERS_MAX; i++) {
        doom_texture(dr.layers[i]);
        dr.layers[i] = 0;
    }
    for (i = 0; i < TEMPS; i++) {
        doom_texture(dr.temps[i]);
        dr.temps[i] = 0;
    }
    for (i = 0; i < BASES; i++) {
        doom_texture(dr.bases[i]);
        dr.bases[i] = 0;
    }
}

/* A target of the pool's, made the first time. */
static wgf_texture_t pooled(wgf_texture_t *slot)
{
    if (*slot == 0) {
        *slot = wgf_texture_create_target(dr.width, dr.height);
        if (*slot != 0) wgf_render_set_target_clear_color(*slot, 0); /* transparent */
    }
    return *slot;
}

/* What the documents draw on made a target of its own, from now; false when it can't be. */
static bool start_base(void)
{
    if (dr.base != 0) return true;
    if (!dr.layers_ok || dr.base_count == BASES) return false;
    dr.base = pooled(&dr.bases[dr.base_count]);
    if (dr.base == 0) return false;
    dr.base_count++;
    dr.used = true;
    if (dr.depth == 0) dr.want = dr.base;
    return true;
}

static wgf_texture_t layer_target(int layer)
{
    if (layer == 0) return dr.base;
    return layer > 0 && layer <= dr.depth ? dr.layers[layer] : 0;
}

static wgf_texture_t top_target(void)
{
    return layer_target(dr.depth);
}

void wgf_ui_priv_doc_draw_begin(float origin_x, float origin_y, float width, float height)
{
    const unsigned frame = wgf_gfx_priv_render_get_frame();
    if (frame != dr.frame) {
        dr.frame = frame;
        dr.op_count = 0;
        dr.base_count = 0;
        memset(dr.counts, 0, sizeof(dr.counts));
    }
    dr.drawing = true;
    dr.origin_x = origin_x;
    dr.origin_y = origin_y;
    dr.visible_w = width;
    dr.visible_h = height;
    dr.scale = wgf_render_get_dpi_scale();
    if (dr.scale <= 0.0f) dr.scale = 1.0f;
    dr.width = (int)ceilf(width * dr.scale);
    dr.height = (int)ceilf(height * dr.scale);
    if (dr.width < 1) dr.width = 1;
    if (dr.height < 1) dr.height = 1;
    if (dr.width != dr.pool_w || dr.height != dr.pool_h) { /* the window resized: the layers made again */
        release_pool();
        dr.pool_w = dr.width;
        dr.pool_h = dr.height;
    }
    dr.layers_ok = wgf_gfx_priv_render_get_pass() == 0;
    dr.want = dr.current = dr.base = 0;
    memset(dr.fresh, 0, sizeof(dr.fresh));
    dr.clipped = dr.scissor_on = dr.transformed = dr.mask_on = false;
    dr.mask_ref = 0;
    dr.depth = dr.dead = 0;
    dr.used = false;
    if (dr.sampler.id == SG_INVALID_ID) { /* layers are read filtered, clamped at their edges */
        dr.sampler = wgf_gfx_priv_texture_sampler(WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                                                  WGF_TEXTURE_FILTER_LINEAR, false);
    }
    wgf_gfx_priv_render_set_2d();
    apply_transform();
    if (dr.used_last) start_base(); /* the last frame used layers: so will this one, likely */
}

static void free_masks(void)
{
    int i;
    for (i = 0; i < dr.mask_count; i++) {
        free(dr.masks[i].vertices);
        free(dr.masks[i].indices);
    }
    dr.mask_count = 0;
}

/* The base drawn onto the screen, premultiplied over what was drawn before. */
static void composite_base(void)
{
    sg_view view;
    sg_sampler sampler;
    int w, h;
    bool placeholder;
    op_t *op;
    wgf_mat4_t proj;
    if (!wgf_gfx_priv_texture_get_binding(dr.base, &view, &sampler, &w, &h, &placeholder)) return;
    op = new_op(PROGRAM_BLIT, BLEND_OVER, 0);
    if (op == NULL) return;
    proj = wgf_gfx_priv_render_projection_2d();
    memcpy(op->vs.pass.proj, proj.m, sizeof(op->vs.pass.proj));
    op->vs.pass.rect[0] = dr.origin_x;
    op->vs.pass.rect[1] = dr.origin_y;
    op->vs.pass.rect[2] = dr.origin_x + dr.visible_w;
    op->vs.pass.rect[3] = dr.origin_y + dr.visible_h;
    op->vs.pass.uv_rect[2] = dr.visible_w * dr.scale / (float)dr.width;
    op->vs.pass.uv_rect[3] = dr.visible_h * dr.scale / (float)dr.height;
    op->fs.blit.matrix[0] = op->fs.blit.matrix[5] = op->fs.blit.matrix[10] = op->fs.blit.matrix[15] = 1.0f;
    op->views[0] = view;
    op->sampler = dr.sampler;
    add_op();
}

void wgf_ui_priv_doc_draw_end(void)
{
    if (!dr.drawing) return;
    unclip();
    if (dr.current != 0) wgf_render_end_target();
    dr.current = dr.want = 0;
    memset(dr.fresh, 0, sizeof(dr.fresh));
    if (dr.base != 0) {
        wgf_gfx_priv_render_set_2d();
        composite_base();
    }
    dr.used_last = dr.used;
    dr.base = 0;
    dr.depth = dr.dead = 0;
    dr.mask_on = false;
    free_masks();
    sgl_matrix_mode_modelview();
    sgl_load_identity();
    dr.drawing = false;
}

void wgf_ui_priv_doc_draw_update(void)
{
    free_doomed();
}

void wgf_ui_priv_doc_draw_restore(void)
{
    int i;
    for (i = 0; i < dr.mesh_count; i++) {
        doom_buffer(dr.meshes[i].vertices);
        doom_buffer(dr.meshes[i].indices);
    }
    dr.mesh_count = 0;
}

void wgf_ui_priv_doc_draw_stop(void)
{
    int i, b, r;
    free_masks();
    release_pool();
    for (i = 0; i < dr.mesh_count; i++) {
        doom_buffer(dr.meshes[i].vertices);
        doom_buffer(dr.meshes[i].indices);
    }
    free_doomed();
    for (i = 0; i < PIPE_KINDS; i++) {
        for (r = 0; r < REFS; r++) {
            if (dr.pipes[i][r].id != SG_INVALID_ID) sgl_destroy_pipeline(dr.pipes[i][r]);
        }
    }
    for (i = 0; i < WRITES; i++) {
        if (dr.writes[i].id != SG_INVALID_ID) sgl_destroy_pipeline(dr.writes[i]);
    }
    for (i = 0; i < PROGRAMS; i++) {
        for (b = 0; b < BLENDS; b++) {
            for (r = 0; r < REFS; r++) {
                if (dr.op_pipes[i][b][r].id != SG_INVALID_ID) sg_destroy_pipeline(dr.op_pipes[i][b][r]);
            }
        }
        if (dr.shaders[i].id != SG_INVALID_ID) sg_destroy_shader(dr.shaders[i]);
    }
    free(dr.ops);
    free(dr.meshes);
    free(dr.doomed_textures);
    free(dr.doomed_buffers);
    memset(&dr, 0, sizeof(dr));
}

/* ---- triangles, the scissor, the transform ------------------------------------------ */

void wgf_ui_priv_doc_draw(const wgf_ui_priv_vertex_t *vertices, int vertex_count, const int *indices, int index_count,
                          float tx, float ty, uintptr_t texture)
{
    sg_view view = {SG_INVALID_ID};
    sg_sampler sampler = {SG_INVALID_ID}; /* set when textured */
    bool textured = false, premultiplied = false;
    int kind = PIPE_STRAIGHT, i;
    if (!dr.drawing || vertices == NULL || indices == NULL || index_count < 3) return;
    sync(); /* first: a texture is bound outside the pass that draws into it */
    if (texture == WGF_UI_PRIV_ATLAS) {
        sgl_pipeline unused;
        if (!wgf_gfx_priv_font_atlas(&view, &sampler, &unused)) return;
        textured = true;
        kind = PIPE_FONT;
    } else if (texture != 0) {
        if (!wgf_ui_priv_doc_texture_binding(texture, &view, &sampler, &premultiplied)) return;
        textured = true;
        if (premultiplied) kind = PIPE_PREMULTIPLIED;
    }
    sgl_push_pipeline();
    sgl_load_pipeline(sgl_pipeline_of(kind, dr.mask_on ? dr.mask_ref : 0));
    if (textured) {
        sgl_enable_texture();
        sgl_texture(view, sampler);
    } else {
        sgl_disable_texture();
    }
    sgl_begin_triangles();
    for (i = 0; i + 2 < index_count; i += 3) {
        int k;
        if (indices[i] < 0 || indices[i] >= vertex_count || indices[i + 1] < 0 || indices[i + 1] >= vertex_count ||
            indices[i + 2] < 0 || indices[i + 2] >= vertex_count)
            continue;
        for (k = 0; k < 3; k++) {
            const wgf_ui_priv_vertex_t *v = &vertices[indices[i + k]];
            if (premultiplied) { /* a layer's texture: its tint premultiplied, as it is */
                const unsigned a = v->a;
                sgl_v2f_t2f_c4b(v->x + tx, v->y + ty, v->u, v->v, (uint8_t)((v->r * a + 127u) / 255u),
                                (uint8_t)((v->g * a + 127u) / 255u), (uint8_t)((v->b * a + 127u) / 255u), v->a);
            } else {
                sgl_v2f_t2f_c4b(v->x + tx, v->y + ty, v->u, v->v, v->r, v->g, v->b, v->a);
            }
        }
    }
    sgl_end();
    if (textured) sgl_disable_texture();
    sgl_pop_pipeline();
}

void wgf_ui_priv_doc_scissor(bool enable, int x, int y, int width, int height)
{
    if (!dr.drawing) return;
    dr.scissor_on = enable;
    dr.scissor[0] = x;
    dr.scissor[1] = y;
    dr.scissor[2] = width;
    dr.scissor[3] = height;
    if (dr.want == dr.current) apply_scissor(); /* else as the pass is entered */
}

void wgf_ui_priv_doc_transform(const float *matrix)
{
    if (!dr.drawing) return;
    dr.transformed = matrix != NULL;
    if (matrix != NULL) memcpy(dr.matrix, matrix, sizeof(dr.matrix));
    if (dr.want == dr.current) apply_transform();
}

/* ---- the clip mask -------------------------------------------------------------------- */

void wgf_ui_priv_doc_clip_mask(bool enable)
{
    dr.mask_on = enable;
}

void wgf_ui_priv_doc_clip_mask_draw(int operation, const wgf_ui_priv_vertex_t *vertices, int vertex_count,
                                    const int *indices, int index_count, float tx, float ty)
{
    mask_t m;
    int i;
    if (!dr.drawing || vertices == NULL || indices == NULL || operation < 0 || operation > 2) return;
    if (operation != 2) free_masks();
    sync(); /* a pass entered draws the mask as it was: then this part of it */
    if (dr.mask_count == MASKS_MAX) {
        if (!dr.mask_logged) wgf_log_warn("wgf_document: a clip mask of more than %d parts", MASKS_MAX);
        dr.mask_logged = true;
        return;
    }
    memset(&m, 0, sizeof(m));
    m.operation = operation;
    m.tx = tx;
    m.ty = ty;
    m.transformed = dr.transformed;
    memcpy(m.matrix, dr.matrix, sizeof(m.matrix));
    m.vertices =
        (wgf_ui_priv_vertex_t *)malloc(sizeof(wgf_ui_priv_vertex_t) * (size_t)(vertex_count > 0 ? vertex_count : 1));
    m.indices = (int *)malloc(sizeof(int) * (size_t)(index_count > 0 ? index_count : 1));
    if (m.vertices == NULL || m.indices == NULL) {
        free(m.vertices);
        free(m.indices);
        return;
    }
    if (vertex_count > 0) memcpy(m.vertices, vertices, sizeof(wgf_ui_priv_vertex_t) * (size_t)vertex_count);
    for (i = 0; i + 2 < index_count; i += 3) { /* only whole triangles of vertices it has */
        if (indices[i] < 0 || indices[i] >= vertex_count || indices[i + 1] < 0 || indices[i + 1] >= vertex_count ||
            indices[i + 2] < 0 || indices[i + 2] >= vertex_count)
            continue;
        memcpy(&m.indices[m.index_count], &indices[i], sizeof(int) * 3);
        m.index_count += 3;
    }
    m.vertex_count = vertex_count;
    dr.masks[dr.mask_count++] = m;
    dr.mask_ref = operation != 2 ? 1 : dr.mask_ref < REFS - 1 ? dr.mask_ref + 1 : REFS - 1;
    draw_mask(&dr.masks[dr.mask_count - 1]);
    dr.counts[1]++;
}

/* ---- layers --------------------------------------------------------------------------- */

int wgf_ui_priv_doc_layer_push(void)
{
    wgf_texture_t layer;
    if (!dr.drawing) return 0;
    if (dr.dead > 0 || dr.depth == LAYERS_MAX || !start_base()) {
        dr.dead++;
        dr.counts[5]++;
        return dr.depth;
    }
    layer = pooled(&dr.layers[dr.depth + 1]);
    if (layer == 0) {
        dr.dead++;
        dr.counts[5]++;
        return dr.depth;
    }
    dr.counts[2]++;
    dr.depth++;
    dr.want = layer;
    dr.fresh[dr.depth] = true; /* "transparent black", as it is entered */
    return dr.depth;
}

void wgf_ui_priv_doc_layer_pop(void)
{
    if (!dr.drawing) return;
    if (dr.dead > 0) {
        dr.dead--;
        return;
    }
    if (dr.depth == 0) return;
    dr.fresh[dr.depth] = false;
    dr.depth--;
    dr.want = top_target();
}

static void set_identity(float *m)
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static bool is_identity(const float *m)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (m[i] != ((i % 5) == 0 ? 1.0f : 0.0f)) return false;
    }
    return true;
}

/* m = a times m, column by column. */
static void compose(const float *a, float *m)
{
    float out[16];
    int c, r, k;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            float sum = 0.0f;
            for (k = 0; k < 4; k++) sum += a[k * 4 + r] * m[c * 4 + k];
            out[c * 4 + r] = sum;
        }
    }
    memcpy(m, out, sizeof(out));
}

/* A spare target that is neither `a` nor `b`. */
static wgf_texture_t spare(wgf_texture_t a, wgf_texture_t b)
{
    int i;
    for (i = 0; i < TEMPS; i++) {
        const wgf_texture_t t = pooled(&dr.temps[i]);
        if (t != 0 && t != a && t != b) return t;
    }
    return 0;
}

/* A pass's op over the whole of the pass drawn into now (a layer's size), reading `from`. */
static op_t *whole_op(int program, int blend, int ref, wgf_texture_t from)
{
    sg_view view;
    sg_sampler sampler;
    int w, h;
    bool placeholder;
    op_t *op;
    wgf_mat4_t proj;
    if (!wgf_gfx_priv_texture_get_binding(from, &view, &sampler, &w, &h, &placeholder)) return NULL;
    op = new_op(program, blend, ref);
    if (op == NULL) return NULL;
    proj = wgf_gfx_priv_render_projection_2d();
    memcpy(op->vs.pass.proj, proj.m, sizeof(op->vs.pass.proj));
    op->vs.pass.rect[2] = (float)dr.width;
    op->vs.pass.rect[3] = (float)dr.height;
    op->vs.pass.uv_rect[2] = op->vs.pass.uv_rect[3] = 1.0f;
    op->views[0] = view;
    op->sampler = dr.sampler;
    return op;
}

static wgf_texture_t blit_to_spare(wgf_texture_t from, const float *matrix)
{
    const wgf_texture_t to = spare(from, 0);
    op_t *op;
    if (to == 0) return from;
    enter_raw(to);
    op = whole_op(PROGRAM_BLIT, BLEND_REPLACE, 0, from);
    if (op == NULL) return from;
    memcpy(op->fs.blit.matrix, matrix, sizeof(op->fs.blit.matrix));
    add_op();
    return to;
}

/* One blur pass, across or down, from `from` into the pass open now. */
static void blur_pass(wgf_texture_t from, float sigma, bool across)
{
    float spacing = 1.0f, taps = ceilf(3.0f * sigma);
    op_t *op;
    if (taps > (float)BLUR_TAPS) {
        spacing = 3.0f * sigma / (float)BLUR_TAPS;
        taps = (float)BLUR_TAPS;
    }
    op = whole_op(PROGRAM_BLUR, BLEND_REPLACE, 0, from);
    if (op == NULL) return;
    op->fs.blur.blur[0] = across ? spacing / (float)dr.width : 0.0f;
    op->fs.blur.blur[1] = across ? 0.0f : spacing / (float)dr.height;
    op->fs.blur.blur[2] = sigma / spacing;
    op->fs.blur.blur[3] = taps;
    add_op();
}

/* `from` blurred by sigma (the context's pixels), in a spare target; `from` for a
 * sigma too small to show. */
static wgf_texture_t blur(wgf_texture_t from, float sigma)
{
    const float s = sigma * dr.scale;
    const wgf_texture_t a = spare(from, 0), b = spare(from, a);
    if (s < 0.5f || a == 0 || b == 0) return from;
    enter_raw(a);
    blur_pass(from, s, true);
    enter_raw(b);
    blur_pass(a, s, false);
    return b;
}

/* `from` over its shadow: its alpha tinted, offset, and blurred. */
static wgf_texture_t drop_shadow(wgf_texture_t from, const filter_t *f)
{
    const float s = f->values[0] * dr.scale, alpha = f->values[6];
    const wgf_texture_t a = spare(from, 0), b = spare(from, a);
    op_t *op;
    if (a == 0 || b == 0) return from;
    enter_raw(a);
    op = whole_op(PROGRAM_SHADOW, BLEND_REPLACE, 0, from);
    if (op == NULL) return from;
    op->fs.shadow.shadow_color[0] = f->values[3] * alpha;
    op->fs.shadow.shadow_color[1] = f->values[4] * alpha;
    op->fs.shadow.shadow_color[2] = f->values[5] * alpha;
    op->fs.shadow.shadow_color[3] = alpha;
    op->fs.shadow.offset[0] = f->values[1] * dr.scale / (float)dr.width;
    op->fs.shadow.offset[1] = f->values[2] * dr.scale / (float)dr.height;
    add_op();
    if (s >= 0.5f) {
        enter_raw(b);
        blur_pass(a, s, true);
        enter_raw(a);
        blur_pass(b, s, false);
    }
    op = whole_op(PROGRAM_BLIT, BLEND_OVER, 0, from);
    if (op == NULL) return a;
    set_identity(op->fs.blit.matrix);
    add_op();
    return a;
}

static wgf_texture_t masked(wgf_texture_t from, wgf_texture_t mask)
{
    const wgf_texture_t to = spare(from, 0);
    sg_view view;
    sg_sampler sampler;
    int w, h;
    bool placeholder;
    op_t *op;
    if (to == 0) return from;
    enter_raw(to); /* first: the mask is bound outside the pass that saved it */
    if (!wgf_gfx_priv_texture_get_binding(mask, &view, &sampler, &w, &h, &placeholder)) return from;
    op = whole_op(PROGRAM_MASK, BLEND_REPLACE, 0, from);
    if (op == NULL) return from;
    op->views[1] = view;
    add_op();
    return to;
}

void wgf_ui_priv_doc_layer_composite(int source, int destination, bool replace, const uintptr_t *filters,
                                     int filter_count)
{
    wgf_texture_t from, to, now;
    float matrix[16];
    int i;
    op_t *op;
    if (!dr.drawing || dr.dead > 0) return;
    if ((source == 0 || destination == 0) && !start_base()) return;
    from = layer_target(source);
    to = layer_target(destination);
    if (from == 0 || to == 0) return;
    if (source > 0 && dr.fresh[source] && !replace) return; /* never drawn into: transparent */
    now = from;
    set_identity(matrix);
    for (i = 0; i < filter_count && filters != NULL; i++) {
        const filter_t *f = (const filter_t *)filters[i];
        if (f == NULL) continue;
        switch (f->kind) {
        case WGF_UI_PRIV_FILTER_MATRIX: compose(f->values, matrix); break;
        case WGF_UI_PRIV_FILTER_BLUR: now = blur(now, f->values[0]); break;
        case WGF_UI_PRIV_FILTER_SHADOW:
            if (!is_identity(matrix)) { /* what is shadowed is what the filters before made */
                now = blit_to_spare(now, matrix);
                set_identity(matrix);
            }
            now = drop_shadow(now, f);
            break;
        case WGF_UI_PRIV_FILTER_MASK: now = masked(now, f->mask); break;
        default: break;
        }
    }
    if (now == to) { /* onto itself: through a spare one, as a target can't be read where it is drawn */
        float identity[16];
        if (is_identity(matrix) && !replace) return;
        set_identity(identity);
        now = blit_to_spare(now, identity);
        if (now == to) return;
    }
    dr.want = to; /* entered first: a target can't be bound in its own pass */
    sync();
    if (dr.current != to) return; /* refused: nothing drawn */
    op = whole_op(PROGRAM_BLIT, replace ? BLEND_REPLACE : BLEND_OVER, dr.mask_on ? dr.mask_ref : 0, now);
    if (op != NULL) {
        memcpy(op->fs.blit.matrix, matrix, sizeof(matrix));
        add_op();
        dr.counts[3]++;
    }
    dr.want = top_target();
}

/* The top layer's region x, y, width by height (the context's pixels) into a target of
 * its own; 0 when it can't be. */
static wgf_texture_t save_region(int x, int y, int width, int height, int *pixel_w, int *pixel_h)
{
    const wgf_texture_t top = top_target();
    wgf_texture_t saved;
    op_t *op;
    *pixel_w = (int)ceilf((float)width * dr.scale);
    *pixel_h = (int)ceilf((float)height * dr.scale);
    if (!dr.drawing || top == 0 || width <= 0 || height <= 0) return 0;
    if (dr.depth > 0 && dr.fresh[dr.depth]) sync(); /* never drawn into: cleared first */
    saved = wgf_texture_create_target(*pixel_w, *pixel_h);
    if (saved == 0) return 0;
    wgf_render_set_target_clear_color(saved, 0);
    enter_raw(saved);
    if (dr.current != saved) {
        release_texture(saved);
        return 0;
    }
    op = whole_op(PROGRAM_BLIT, BLEND_REPLACE, 0, top);
    if (op != NULL) {
        op->vs.pass.rect[2] = (float)*pixel_w;
        op->vs.pass.rect[3] = (float)*pixel_h;
        op->vs.pass.uv_rect[0] = (float)x * dr.scale / (float)dr.width;
        op->vs.pass.uv_rect[1] = (float)y * dr.scale / (float)dr.height;
        op->vs.pass.uv_rect[2] = (float)(x + width) * dr.scale / (float)dr.width;
        op->vs.pass.uv_rect[3] = (float)(y + height) * dr.scale / (float)dr.height;
        set_identity(op->fs.blit.matrix);
        add_op();
    }
    dr.counts[4]++;
    return saved;
}

uintptr_t wgf_ui_priv_doc_layer_save_texture(int x, int y, int width, int height)
{
    int pixel_w, pixel_h;
    const wgf_texture_t saved = save_region(x, y, width, height, &pixel_w, &pixel_h);
    return saved != 0 ? wgf_ui_priv_doc_texture_adopt(saved, pixel_w, pixel_h) : 0;
}

uintptr_t wgf_ui_priv_doc_layer_save_mask(void)
{
    int pixel_w, pixel_h;
    filter_t *f;
    wgf_texture_t saved;
    if (!dr.drawing) return 0;
    saved = save_region(0, 0, (int)ceilf((float)dr.width / dr.scale), (int)ceilf((float)dr.height / dr.scale),
                        &pixel_w, &pixel_h);
    if (saved == 0) return 0;
    f = (filter_t *)calloc(1, sizeof(filter_t));
    if (f == NULL) {
        doom_texture(saved);
        return 0;
    }
    f->kind = WGF_UI_PRIV_FILTER_MASK;
    f->mask = saved;
    return (uintptr_t)f;
}

/* ---- filters and gradients ------------------------------------------------------------ */

uintptr_t wgf_ui_priv_doc_filter_make(wgf_ui_priv_filter_t kind, const float *values)
{
    filter_t *f = (filter_t *)calloc(1, sizeof(filter_t));
    if (f == NULL || values == NULL) {
        free(f);
        return 0;
    }
    f->kind = kind;
    memcpy(f->values, values,
           sizeof(float) * (kind == WGF_UI_PRIV_FILTER_MATRIX ? 16 : kind == WGF_UI_PRIV_FILTER_SHADOW ? 7 : 1));
    return (uintptr_t)f;
}

void wgf_ui_priv_doc_filter_release(uintptr_t filter)
{
    filter_t *f = (filter_t *)filter;
    if (f == NULL) return;
    doom_texture(f->mask);
    free(f);
}

uintptr_t wgf_ui_priv_doc_gradient_make(int kind, bool repeating, float px, float py, float vx, float vy,
                                        const float *stops, int stop_count)
{
    gradient_t *g;
    int i;
    if (kind < 0 || kind > 2 || stops == NULL || stop_count <= 0) return 0;
    g = (gradient_t *)calloc(1, sizeof(gradient_t));
    if (g == NULL) return 0;
    if (stop_count > STOPS_MAX) stop_count = STOPS_MAX;
    g->params.shape[0] = (float)kind;
    g->params.shape[1] = repeating ? 1.0f : 0.0f;
    g->params.shape[2] = (float)stop_count;
    g->params.pv[0] = px;
    g->params.pv[1] = py;
    g->params.pv[2] = vx;
    g->params.pv[3] = vy;
    for (i = 0; i < stop_count; i++) {
        g->params.positions[i][0] = stops[i * 5];
        memcpy(g->params.colors[i], &stops[i * 5 + 1], sizeof(float) * 4);
    }
    return (uintptr_t)g;
}

void wgf_ui_priv_doc_gradient_release(uintptr_t gradient)
{
    free((gradient_t *)gradient);
}

/* The geometry's triangles on the GPU, made the first time. */
static const mesh_t *mesh_of(uintptr_t geometry, const wgf_ui_priv_vertex_t *vertices, int vertex_count,
                             const int *indices, int index_count)
{
    sg_buffer_desc desc;
    mesh_t *m;
    int i;
    for (i = 0; i < dr.mesh_count; i++) {
        if (dr.meshes[i].geometry == geometry) return &dr.meshes[i];
    }
    for (i = 0; i < index_count; i++) {
        if (indices[i] < 0 || indices[i] >= vertex_count) return NULL; /* drawn by none of it */
    }
    if (!grow((void **)&dr.meshes, &dr.mesh_capacity, dr.mesh_count + 1, sizeof(mesh_t))) return NULL;
    m = &dr.meshes[dr.mesh_count];
    memset(m, 0, sizeof(*m));
    m->geometry = geometry;
    m->count = index_count;
    memset(&desc, 0, sizeof(desc));
    desc.usage.vertex_buffer = true;
    desc.data.ptr = vertices;
    desc.data.size = sizeof(wgf_ui_priv_vertex_t) * (size_t)vertex_count;
    desc.label = "wgf-document-gradient";
    m->vertices = sg_make_buffer(&desc);
    memset(&desc, 0, sizeof(desc));
    desc.usage.index_buffer = true;
    desc.data.ptr = indices;
    desc.data.size = sizeof(int) * (size_t)index_count;
    desc.label = "wgf-document-gradient";
    m->indices = sg_make_buffer(&desc);
    if (sg_query_buffer_state(m->vertices) != SG_RESOURCESTATE_VALID ||
        sg_query_buffer_state(m->indices) != SG_RESOURCESTATE_VALID) {
        sg_destroy_buffer(m->vertices);
        sg_destroy_buffer(m->indices);
        return NULL;
    }
    dr.mesh_count++;
    return m;
}

void wgf_ui_priv_doc_geometry_gone(uintptr_t geometry)
{
    int i;
    for (i = 0; i < dr.mesh_count; i++) {
        if (dr.meshes[i].geometry != geometry) continue;
        doom_buffer(dr.meshes[i].vertices);
        doom_buffer(dr.meshes[i].indices);
        dr.meshes[i] = dr.meshes[--dr.mesh_count];
        return;
    }
}

void wgf_ui_priv_doc_gradient_draw(uintptr_t gradient, uintptr_t geometry, const wgf_ui_priv_vertex_t *vertices,
                                   int vertex_count, const int *indices, int index_count, float tx, float ty)
{
    const gradient_t *g = (const gradient_t *)gradient;
    const mesh_t *m;
    op_t *op;
    wgf_mat4_t mvp, model;
    float s;
    if (!dr.drawing || g == NULL || vertices == NULL || indices == NULL || vertex_count <= 0 || index_count < 3) return;
    m = mesh_of(geometry, vertices, vertex_count, indices, index_count);
    if (m == NULL) return;
    sync();
    op = new_op(PROGRAM_GRADIENT, BLEND_OVER, dr.mask_on ? dr.mask_ref : 0);
    if (op == NULL) return;
    s = pass_scale();
    model = wgf_mat4_identity();
    model.m[0] = model.m[5] = s;
    model.m[12] = pass_x();
    model.m[13] = pass_y();
    if (dr.transformed) {
        wgf_mat4_t transform;
        memcpy(transform.m, dr.matrix, sizeof(transform.m));
        model = wgf_mat4_mul(model, transform);
    }
    {
        wgf_mat4_t moved = wgf_mat4_identity();
        moved.m[12] = tx;
        moved.m[13] = ty;
        model = wgf_mat4_mul(model, moved);
    }
    mvp = wgf_mat4_mul(wgf_gfx_priv_render_projection_2d(), model);
    memcpy(op->vs.gradient.mvp, mvp.m, sizeof(op->vs.gradient.mvp));
    op->fs.gradient = g->params;
    op->vertices = m->vertices;
    op->indices = m->indices;
    op->count = m->count;
    add_op();
    dr.counts[0]++;
}

void wgf_ui_priv_document_draw_counts(int counts[6])
{
    memcpy(counts, dr.counts, sizeof(dr.counts));
}
