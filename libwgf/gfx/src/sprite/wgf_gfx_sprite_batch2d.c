#include "wgf_sprite_batch.h"

#include "sprite/wgf_gfx_sprite_batch2d_priv.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_resource.h"

/* the shader's sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_quads.glsl.h"

/* Sprite batches on a 2D stage: an actor holding many quads as 32-byte records, the
 * program's to write in bulk, drawn as one instanced draw among the frame's immediate mode
 * (a command, as 2D particles are). A batch owns a GPU buffer its records go up to in the
 * frame it is first drawn, written once that frame however many stages draw it; each draw
 * binds it from its start, so no base instance is needed (WebGL2). The part is installed by
 * the first batch made. */

#define MAX_QUADS (1 << 24)
#define DRAWS_INITIAL 16

/* One quad, as the shader reads it (per-instance vertex data). */
typedef struct quad_t {
    float place[4]; /* x, y, width, height */
    float rotation;
    int16_t region[4]; /* x, y, width, height in texels */
    uint8_t color[4];  /* tint, sRGB */
} quad_t;
_Static_assert(sizeof(quad_t) == 32, "a quad is 32 bytes");

typedef struct wgf_gfx_priv_sprite_batch_t {
    wgf_texture_t texture;
    quad_t *quads;
    int count, capacity;
    float pivot[2];
    int alpha_mode;
    float alpha_cutoff;
    sg_buffer buffer;
    int buffer_capacity; /* quads */
    unsigned written;    /* the frame its records went up in, + 1 */
    struct wgf_gfx_priv_sprite_batch_t *next, *prev; /* every batch, for the part's stop */
} wgf_gfx_priv_sprite_batch_t;

typedef struct draw_t {
    sg_buffer buffer;
    sg_view view;
    sg_sampler sampler;
    int pipeline, count;
    quads_params_t params;
} draw_t;

enum { PIPELINE_OPAQUE, PIPELINE_BLEND, PIPELINE_ADD, PIPELINE_COUNT };

static struct {
    bool installed, ready;
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    sg_buffer corners;
    draw_t *draws;
    int draw_count, draw_capacity;
    sg_buffer *doomed; /* buffers of batches freed after being drawn this frame */
    int doomed_count, doomed_capacity;
    unsigned frame;
    int draw_calls, drawn, last_draw_calls, last_drawn;
    wgf_gfx_priv_sprite_batch_t *batches;
} sb;

static wgf_gfx_priv_sprite_batch_t *batch_of_at(wgf_actor_t batch, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(batch, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_SPRITE_BATCH ? actor_ptr->as.sprite_batch : NULL;
}
#define batch_of(...) batch_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

static bool grow(void **items, int *capacity, int needed, size_t size, int initial)
{
    void *moved;
    int grown = *capacity > 0 ? *capacity : initial;
    if (needed <= *capacity) return true;
    while (grown < needed) grown = grown > MAX_QUADS / 2 ? MAX_QUADS : grown * 2;
    moved = realloc(*items, size * (size_t)grown);
    if (moved == NULL) return false;
    *items = moved;
    *capacity = grown;
    return true;
}

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

/* The shader, pipelines, and corners, made with the first batch drawn. */
static void ensure_gpu(void)
{
    static const float corners[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    sg_pipeline_desc desc;
    sg_blend_state blend;
    sg_buffer_desc quad;
    if (sb.ready) return;
    sb.shader = sg_make_shader(quads_shader_desc(shader_backend()));
    memset(&desc, 0, sizeof(desc));
    desc.shader = sb.shader;
    desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    desc.layout.attrs[ATTR_quads_corner].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[ATTR_quads_inst_place].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_quads_inst_place].buffer_index = 1;
    desc.layout.attrs[ATTR_quads_inst_rotation].format = SG_VERTEXFORMAT_FLOAT;
    desc.layout.attrs[ATTR_quads_inst_rotation].buffer_index = 1;
    desc.layout.attrs[ATTR_quads_inst_region].format = SG_VERTEXFORMAT_SHORT4;
    desc.layout.attrs[ATTR_quads_inst_region].buffer_index = 1;
    desc.layout.attrs[ATTR_quads_inst_color].format = SG_VERTEXFORMAT_UBYTE4N;
    desc.layout.attrs[ATTR_quads_inst_color].buffer_index = 1;
    desc.depth.compare = SG_COMPAREFUNC_ALWAYS; /* 2D, as immediate mode's 2D is */
    desc.label = "wgf-sprite-batch";
    memset(&blend, 0, sizeof(blend));
    blend.enabled = true;
    blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    sb.pipelines[PIPELINE_OPAQUE] = sg_make_pipeline(&desc);
    desc.colors[0].blend = blend;
    sb.pipelines[PIPELINE_BLEND] = sg_make_pipeline(&desc);
    blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend = blend;
    sb.pipelines[PIPELINE_ADD] = sg_make_pipeline(&desc);
    memset(&quad, 0, sizeof(quad));
    quad.data.ptr = corners;
    quad.data.size = sizeof(corners);
    quad.label = "wgf-sprite-batch-corners";
    sb.corners = sg_make_buffer(&quad);
    sb.ready = true;
}

/* Its records up to its buffer, once this frame; false when there is nothing to draw. */
static bool upload(wgf_gfx_priv_sprite_batch_t *b)
{
    sg_write_buffer_desc write;
    if (b->written == sb.frame + 1) return true;
    if (b->buffer_capacity < b->count) {
        sg_buffer_desc desc;
        int capacity = b->buffer_capacity > 0 ? b->buffer_capacity : 256;
        while (capacity < b->count) capacity = capacity > MAX_QUADS / 2 ? MAX_QUADS : capacity * 2;
        if (b->buffer.id != SG_INVALID_ID) sg_destroy_buffer(b->buffer);
        memset(&desc, 0, sizeof(desc));
        desc.size = sizeof(quad_t) * (size_t)capacity;
        desc.usage.vertex_buffer = true;
        desc.usage.write_transient = true;
        desc.label = "wgf-sprite-batch-quads";
        b->buffer = sg_make_buffer(&desc);
        b->buffer_capacity = sg_query_buffer_state(b->buffer) == SG_RESOURCESTATE_VALID ? capacity : 0;
        if (b->buffer_capacity == 0) return false;
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = b->quads;
    write.src.data.size = sizeof(quad_t) * (size_t)b->count;
    write.dst.buffer = b->buffer;
    sg_write_buffer_transient(&write);
    b->written = sb.frame + 1;
    return true;
}

static void replay(int index)
{
    const draw_t *d;
    sg_bindings bind;
    if (index < 0 || index >= sb.draw_count) return;
    d = &sb.draws[index];
    sg_apply_pipeline(sb.pipelines[d->pipeline]);
    memset(&bind, 0, sizeof(bind));
    bind.vertex_buffers[0] = sb.corners;
    bind.vertex_buffers[1] = d->buffer;
    bind.views[VIEW_quads_tex] = d->view;
    bind.samplers[SMP_quads_smp] = d->sampler;
    sg_apply_bindings(&bind);
    sg_apply_uniforms(UB_quads_params, &SG_RANGE(d->params));
    sg_draw(0, 6, d->count);
    sb.draw_calls++;
    sb.drawn += d->count;
}

/* On a 2D stage: its quads through `placed` (the actor's matrix into the frame's logical
 * pixels) and the frame's 2D projection, a command among the frame's immediate mode. */
static void draw_batch(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                       const wgf_mat4_t *view)
{
    wgf_gfx_priv_sprite_batch_t *b = actor_ptr->as.sprite_batch;
    sg_view image;
    sg_sampler sampler;
    int tw, th;
    bool placeholder;
    draw_t *d;
    wgf_mat4_t view_proj;
    (void)actor;
    (void)view;
    if (b == NULL || b->count == 0) return;
    if (!wgf_gfx_priv_texture_get_binding(b->texture, &image, &sampler, &tw, &th, &placeholder)) return;
    if (tw <= 0 || th <= 0) return;
    ensure_gpu();
    if (!grow((void **)&sb.draws, &sb.draw_capacity, sb.draw_count + 1, sizeof(draw_t), DRAWS_INITIAL)) return;
    if (!upload(b)) return;
    d = &sb.draws[sb.draw_count];
    memset(d, 0, sizeof(*d));
    d->buffer = b->buffer;
    d->view = image;
    d->sampler = sampler;
    d->count = b->count;
    d->pipeline = b->alpha_mode == WGF_ALPHA_MODE_BLEND ? PIPELINE_BLEND
                  : b->alpha_mode == WGF_ALPHA_MODE_ADD ? PIPELINE_ADD
                                                         : PIPELINE_OPAQUE;
    view_proj = wgf_mat4_mul(wgf_gfx_priv_render_projection_2d(), *placed);
    memcpy(d->params.view_proj, view_proj.m, sizeof(d->params.view_proj));
    d->params.texture_size[0] = (float)tw;
    d->params.texture_size[1] = (float)th;
    d->params.texture_size[2] = b->pivot[0];
    d->params.texture_size[3] = b->pivot[1];
    d->params.mode[0] = b->alpha_mode == WGF_ALPHA_MODE_MASK ? (b->alpha_cutoff > 0.0f ? b->alpha_cutoff : 1e-6f)
                        : b->alpha_mode == WGF_ALPHA_MODE_OPAQUE ? -1.0f
                                                                 : 0.0f;
    d->params.mode[1] = placeholder ? 1.0f : 0.0f;
    if (wgf_gfx_priv_render_add_command(replay, sb.draw_count)) sb.draw_count++;
}

static void end_frame(void)
{
    int i;
    for (i = 0; i < sb.doomed_count; i++) sg_destroy_buffer(sb.doomed[i]);
    sb.doomed_count = 0;
    sb.draw_count = 0;
    sb.frame++;
    sb.last_draw_calls = sb.draw_calls;
    sb.last_drawn = sb.drawn;
    sb.draw_calls = sb.drawn = 0;
}

int wgf_gfx_priv_sprite_batch2d_get_draw_calls(void)
{
    return sb.last_draw_calls;
}

int wgf_gfx_priv_sprite_batch2d_get_drawn(void)
{
    return sb.last_drawn;
}

static void stop(void)
{
    wgf_gfx_priv_sprite_batch_t *b;
    int i;
    if (sb.ready) { /* sokol_gfx is still up: gfx stops its parts first */
        for (b = sb.batches; b != NULL; b = b->next) {
            if (b->buffer.id != SG_INVALID_ID) sg_destroy_buffer(b->buffer);
            b->buffer.id = SG_INVALID_ID;
            b->buffer_capacity = 0;
        }
        for (i = 0; i < sb.doomed_count; i++) sg_destroy_buffer(sb.doomed[i]);
        for (i = 0; i < PIPELINE_COUNT; i++) sg_destroy_pipeline(sb.pipelines[i]);
        sg_destroy_buffer(sb.corners);
        sg_destroy_shader(sb.shader);
    }
    free(sb.draws);
    free(sb.doomed);
    b = sb.batches;
    memset(&sb, 0, sizeof(sb));
    sb.batches = b; /* still owned by their actors, freed after the parts stop */
}

static wgf_core_priv_part_t part = {.name = "sprite batches",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SPRITE_BATCHES,
                                    .end_frame = end_frame,
                                    .stop = stop};

static void free_batch(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    wgf_gfx_priv_sprite_batch_t *b = actor_ptr->as.sprite_batch;
    (void)actor;
    if (b == NULL) return;
    if (b->texture != 0) wgf_resource_release(b->texture);
    if (b->buffer.id != SG_INVALID_ID && sb.ready) {
        /* drawn this frame: its buffer is bound by a command still to be replayed */
        if (b->written == sb.frame + 1 &&
            grow((void **)&sb.doomed, &sb.doomed_capacity, sb.doomed_count + 1, sizeof(sg_buffer), 8)) {
            sb.doomed[sb.doomed_count++] = b->buffer;
        } else {
            sg_destroy_buffer(b->buffer);
        }
    }
    if (b->prev != NULL) b->prev->next = b->next;
    else sb.batches = b->next;
    if (b->next != NULL) b->next->prev = b->prev;
    free(b->quads);
    free(b);
    actor_ptr->as.sprite_batch = NULL;
}

static const wgf_gfx_priv_actor_kind_t kind = {free_batch, draw_batch, NULL, NULL};

static bool is_texture(wgf_texture_t texture)
{
    return texture == 0 || (WGF_CORE_PRIV_HANDLE_KIND(texture) == WGF_CORE_PRIV_HANDLE_KIND_TEXTURE &&
                            wgf_resource_get_status(texture) != WGF_RESOURCE_STATUS_NONE);
}

wgf_actor_t wgf_sprite_batch_create(wgf_texture_t texture)
{
    wgf_actor_t actor;
    wgf_gfx_priv_actor_t *actor_ptr;
    wgf_gfx_priv_sprite_batch_t *b;
    if (!is_texture(texture)) return 0;
    b = (wgf_gfx_priv_sprite_batch_t *)calloc(1, sizeof(*b));
    if (b == NULL) return 0;
    actor = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_SPRITE_BATCH);
    actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) {
        free(b);
        return 0;
    }
    if (!sb.installed) {
        wgf_core_priv_part_install(&part);
        sb.installed = true;
    }
    b->pivot[0] = b->pivot[1] = 0.5f;
    b->alpha_mode = WGF_ALPHA_MODE_BLEND;
    b->alpha_cutoff = 0.5f;
    if (texture != 0) {
        wgf_gfx_priv_texture_retain(texture);
        b->texture = texture;
    }
    b->next = sb.batches;
    if (sb.batches != NULL) sb.batches->prev = b;
    sb.batches = b;
    actor_ptr->as.sprite_batch = b;
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_SPRITE_BATCH, &kind);
    return actor;
}

bool wgf_sprite_batch_set_texture(wgf_actor_t batch, wgf_texture_t texture)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    if (b == NULL || !is_texture(texture)) return false;
    if (texture == b->texture) return true;
    if (texture != 0) wgf_gfx_priv_texture_retain(texture);
    if (b->texture != 0) wgf_resource_release(b->texture);
    b->texture = texture;
    return true;
}

wgf_texture_t wgf_sprite_batch_get_texture(wgf_actor_t batch)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    return b != NULL ? b->texture : 0;
}

bool wgf_sprite_batch_set_count(wgf_actor_t batch, int count)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    int i;
    if (b == NULL || count < 0 || count > MAX_QUADS) return false;
    if (!grow((void **)&b->quads, &b->capacity, count, sizeof(quad_t), 64)) return false;
    for (i = b->count; i < count; i++) {
        memset(&b->quads[i], 0, sizeof(quad_t));
        memset(b->quads[i].color, 0xFF, sizeof(b->quads[i].color));
    }
    b->count = count;
    b->written = 0;
    return true;
}

int wgf_sprite_batch_get_count(wgf_actor_t batch)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    return b != NULL ? b->count : 0;
}

/* The quads a write of `count` values, `per` a quad, from `first` covers: 0 for none. */
static int span(wgf_gfx_priv_sprite_batch_t *b, int first, const void *values, int count, int per)
{
    int quads;
    if (b == NULL || values == NULL || first < 0 || first >= b->count || count <= 0 || count % per != 0) return 0;
    quads = count / per;
    if (quads > b->count - first) quads = b->count - first;
    b->written = 0;
    return quads;
}

/* As span, for a read into `out`: as many whole quads as fit. */
static int span_out(const wgf_gfx_priv_sprite_batch_t *b, int first, const void *out, int count, int per)
{
    int quads;
    if (b == NULL || out == NULL || first < 0 || first >= b->count || count <= 0) return 0;
    quads = count / per;
    if (quads > b->count - first) quads = b->count - first;
    return quads;
}

int wgf_sprite_batch_set_positions(wgf_actor_t batch, int first, const float *positions, int count)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span(b, first, positions, count, 2);
    quad_t *q;
    int i;
    for (i = 0; i < quads; i++) {
        q = &b->quads[first + i];
        q->place[0] = positions[2 * i];
        q->place[1] = positions[2 * i + 1];
    }
    return quads;
}

int wgf_sprite_batch_get_positions(wgf_actor_t batch, int first, float *out, int count)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span_out(b, first, out, count, 2);
    int i;
    for (i = 0; i < quads; i++) {
        out[2 * i] = b->quads[first + i].place[0];
        out[2 * i + 1] = b->quads[first + i].place[1];
    }
    return quads * 2;
}

int wgf_sprite_batch_set_sizes(wgf_actor_t batch, int first, const float *sizes, int count)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span(b, first, sizes, count, 2);
    int i;
    for (i = 0; i < quads; i++) {
        b->quads[first + i].place[2] = sizes[2 * i];
        b->quads[first + i].place[3] = sizes[2 * i + 1];
    }
    return quads;
}

int wgf_sprite_batch_get_sizes(wgf_actor_t batch, int first, float *out, int count)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span_out(b, first, out, count, 2);
    int i;
    for (i = 0; i < quads; i++) {
        out[2 * i] = b->quads[first + i].place[2];
        out[2 * i + 1] = b->quads[first + i].place[3];
    }
    return quads * 2;
}

int wgf_sprite_batch_set_rotations(wgf_actor_t batch, int first, const float *radians, int count)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span(b, first, radians, count, 1);
    int i;
    for (i = 0; i < quads; i++) b->quads[first + i].rotation = radians[i];
    return quads;
}

int wgf_sprite_batch_get_rotations(wgf_actor_t batch, int first, float *out, int count)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span_out(b, first, out, count, 1);
    int i;
    for (i = 0; i < quads; i++) out[i] = b->quads[first + i].rotation;
    return quads;
}

static int16_t texel(float value)
{
    const float rounded = floorf(value + 0.5f);
    return (int16_t)(rounded < 0.0f ? 0.0f : rounded > 32767.0f ? 32767.0f : rounded);
}

int wgf_sprite_batch_set_regions(wgf_actor_t batch, int first, const float *regions, int count)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span(b, first, regions, count, 4);
    int i, k;
    for (i = 0; i < quads; i++) {
        for (k = 0; k < 4; k++) b->quads[first + i].region[k] = texel(regions[4 * i + k]);
    }
    return quads;
}

int wgf_sprite_batch_get_regions(wgf_actor_t batch, int first, float *out, int count)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span_out(b, first, out, count, 4);
    int i, k;
    for (i = 0; i < quads; i++) {
        for (k = 0; k < 4; k++) out[4 * i + k] = (float)b->quads[first + i].region[k];
    }
    return quads * 4;
}

int wgf_sprite_batch_set_tints(wgf_actor_t batch, int first, const int *tints, int count)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span(b, first, tints, count, 1);
    int i;
    for (i = 0; i < quads; i++) {
        const uint32_t c = (uint32_t)tints[i];
        uint8_t *color = b->quads[first + i].color;
        color[0] = (uint8_t)(c >> 24);
        color[1] = (uint8_t)(c >> 16);
        color[2] = (uint8_t)(c >> 8);
        color[3] = (uint8_t)c;
    }
    return quads;
}

int wgf_sprite_batch_get_tints(wgf_actor_t batch, int first, int *out, int count)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    const int quads = span_out(b, first, out, count, 1);
    int i;
    for (i = 0; i < quads; i++) {
        const uint8_t *color = b->quads[first + i].color;
        out[i] = (int)(((uint32_t)color[0] << 24) | ((uint32_t)color[1] << 16) | ((uint32_t)color[2] << 8) | color[3]);
    }
    return quads;
}

bool wgf_sprite_batch_set_pivot(wgf_actor_t batch, float x, float y)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    if (b == NULL) return false;
    b->pivot[0] = x;
    b->pivot[1] = y;
    return true;
}

wgf_vec2_t wgf_sprite_batch_get_pivot(wgf_actor_t batch)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    return b != NULL ? wgf_vec2_make(b->pivot[0], b->pivot[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_sprite_batch_set_alpha_mode(wgf_actor_t batch, wgf_alpha_mode_t mode, float cutoff)
{
    wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    if (b == NULL || (int)mode < WGF_ALPHA_MODE_OPAQUE || mode > WGF_ALPHA_MODE_ADD) return false;
    b->alpha_mode = (int)mode;
    b->alpha_cutoff = cutoff < 0.0f ? 0.0f : (cutoff > 1.0f ? 1.0f : cutoff);
    return true;
}

wgf_alpha_mode_t wgf_sprite_batch_get_alpha_mode(wgf_actor_t batch)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    return b != NULL ? (wgf_alpha_mode_t)b->alpha_mode : WGF_ALPHA_MODE_OPAQUE;
}

float wgf_sprite_batch_get_alpha_cutoff(wgf_actor_t batch)
{
    const wgf_gfx_priv_sprite_batch_t *b = batch_of(batch);
    return b != NULL ? b->alpha_cutoff : 0.0f;
}
