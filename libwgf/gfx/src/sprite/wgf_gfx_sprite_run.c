#include "sprite/wgf_gfx_sprite_run_priv.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_material.h"

/* the shader's sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_sprites.glsl.h"

/* 2D sprites drawn in runs, as PixiJS's batcher draws its sprites: each sprite a stage draws
 * is a 48-byte record, made then, added to the run drawn last when it can join it
 * (wgf_gfx_priv_render_is_last_command) and to a new run, a command of its own, when not.
 * The frame's records go up to one buffer in its first run's replay, and each run binds it
 * from its first record, so no base instance is needed (WebGL2). A run's textures are a
 * slot each, so sprites over 8 textures in any order are one draw. The part is installed by
 * the first sprite drawn. */

#define SLOTS 8
#define MAX_RECORDS (1 << 24)

/* One sprite, as the shader reads it (per-instance vertex data). */
typedef struct record_t {
    float origin[2]; /* the top-left corner, placed */
    float edges[4];  /* the top edge, then the left */
    float uv[4];
    uint8_t color[4]; /* tint, sRGB */
    float slot;
} record_t;
_Static_assert(sizeof(record_t) == 48, "a record is 48 bytes");

typedef struct run_t {
    int first, count; /* its records */
    int pipeline, textures;
    sg_view views[SLOTS];
    sg_sampler samplers[SLOTS];
    sprites_params_t params;
} run_t;

/* the pipelines, by the alpha modes immediate mode draws a sprite in */
enum { PIPELINE_BLEND, PIPELINE_ADD, PIPELINE_OPAQUE, PIPELINE_MULTIPLY, PIPELINE_PREMULTIPLIED, PIPELINE_COUNT };

static struct {
    bool installed, ready;
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    sg_buffer corners, buffer;
    int buffer_capacity; /* records */
    record_t *records;
    int record_count, record_capacity;
    run_t *runs;
    int run_count, run_capacity;
    unsigned frame;    /* the frame the runs were made in */
    unsigned uploaded; /* the frame the records went up in, + 1 */
    int draw_calls, drawn, last_draw_calls, last_drawn;
    wgf_gfx_priv_kept_quad_t *quads; /* sprites' kept quads, by actor slot */
    int quad_capacity;
} sr;

static bool grow(void **items, int *capacity, int needed, size_t size, int initial)
{
    void *moved;
    int grown = *capacity > 0 ? *capacity : initial;
    if (needed <= *capacity) return true;
    if (needed > MAX_RECORDS) return false;
    while (grown < needed) grown = grown > MAX_RECORDS / 2 ? MAX_RECORDS : grown * 2;
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

/* The shader, pipelines, and corners, made with the first run drawn. Each pipeline blends
 * as immediate mode's 2D pipeline for its mode does (wgf_gfx_render.c). */
static void ensure_gpu(void)
{
    static const float corners[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    sg_pipeline_desc desc;
    sg_blend_state *blend = &desc.colors[0].blend;
    sg_buffer_desc quad;
    if (sr.ready) return;
    sr.shader = sg_make_shader(sprites_shader_desc(shader_backend()));
    memset(&desc, 0, sizeof(desc));
    desc.shader = sr.shader;
    desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    desc.layout.attrs[ATTR_sprites_corner].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[ATTR_sprites_inst_origin].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[ATTR_sprites_inst_origin].buffer_index = 1;
    desc.layout.attrs[ATTR_sprites_inst_edges].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_sprites_inst_edges].buffer_index = 1;
    desc.layout.attrs[ATTR_sprites_inst_uv].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_sprites_inst_uv].buffer_index = 1;
    desc.layout.attrs[ATTR_sprites_inst_color].format = SG_VERTEXFORMAT_UBYTE4N;
    desc.layout.attrs[ATTR_sprites_inst_color].buffer_index = 1;
    desc.layout.attrs[ATTR_sprites_inst_slot].format = SG_VERTEXFORMAT_FLOAT;
    desc.layout.attrs[ATTR_sprites_inst_slot].buffer_index = 1;
    desc.depth.compare = SG_COMPAREFUNC_ALWAYS; /* 2D, as immediate mode's 2D is */
    desc.colors[0].write_mask = SG_COLORMASK_RGBA;
    desc.label = "wgf-sprite-run";
    blend->enabled = true;
    blend->src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    blend->dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend->src_factor_alpha = SG_BLENDFACTOR_ONE;
    blend->dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    sr.pipelines[PIPELINE_BLEND] = sg_make_pipeline(&desc);
    blend->src_factor_rgb = SG_BLENDFACTOR_ONE; /* premultiplied: its color its alpha's */
    sr.pipelines[PIPELINE_PREMULTIPLIED] = sg_make_pipeline(&desc);
    blend->src_factor_rgb = SG_BLENDFACTOR_DST_COLOR; /* multiplied, raylib's */
    sr.pipelines[PIPELINE_MULTIPLY] = sg_make_pipeline(&desc);
    blend->src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    blend->dst_factor_rgb = SG_BLENDFACTOR_ONE; /* added */
    blend->dst_factor_alpha = SG_BLENDFACTOR_ONE;
    sr.pipelines[PIPELINE_ADD] = sg_make_pipeline(&desc);
    blend->enabled = false;
    sr.pipelines[PIPELINE_OPAQUE] = sg_make_pipeline(&desc);
    memset(&quad, 0, sizeof(quad));
    quad.data.ptr = corners;
    quad.data.size = sizeof(corners);
    quad.label = "wgf-sprite-run-corners";
    sr.corners = sg_make_buffer(&quad);
    sr.ready = true;
}

/* The frame's records up to the buffer, once; false when they can't go. */
static bool upload(void)
{
    sg_write_buffer_desc write;
    if (sr.uploaded == sr.frame + 1) return true;
    if (sr.buffer_capacity < sr.record_count) {
        sg_buffer_desc desc;
        int capacity = sr.buffer_capacity > 0 ? sr.buffer_capacity : 1024;
        while (capacity < sr.record_count) capacity = capacity > MAX_RECORDS / 2 ? MAX_RECORDS : capacity * 2;
        if (sr.buffer.id != SG_INVALID_ID) sg_destroy_buffer(sr.buffer);
        memset(&desc, 0, sizeof(desc));
        desc.size = sizeof(record_t) * (size_t)capacity;
        desc.usage.vertex_buffer = true;
        desc.usage.write_transient = true;
        desc.label = "wgf-sprite-run-records";
        sr.buffer = sg_make_buffer(&desc);
        sr.buffer_capacity = sg_query_buffer_state(sr.buffer) == SG_RESOURCESTATE_VALID ? capacity : 0;
        if (sr.buffer_capacity == 0) return false;
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = sr.records;
    write.src.data.size = sizeof(record_t) * (size_t)sr.record_count;
    write.dst.buffer = sr.buffer;
    sg_write_buffer_transient(&write);
    sr.uploaded = sr.frame + 1;
    return true;
}

static void replay(int index)
{
    const run_t *r;
    sg_bindings bind;
    int i;
    if (index < 0 || index >= sr.run_count || !upload()) return;
    r = &sr.runs[index];
    sg_apply_pipeline(sr.pipelines[r->pipeline]);
    memset(&bind, 0, sizeof(bind));
    bind.vertex_buffers[0] = sr.corners;
    bind.vertex_buffers[1] = sr.buffer;
    bind.vertex_buffer_offsets[1] = (int)(sizeof(record_t) * (size_t)r->first);
    for (i = 0; i < SLOTS; i++) { /* a slot the run doesn't use shows its first texture, never sampled */
        bind.views[VIEW_sprites_tex0 + i] = r->views[i < r->textures ? i : 0];
        bind.samplers[SMP_sprites_smp0 + i] = r->samplers[i < r->textures ? i : 0];
    }
    sg_apply_bindings(&bind);
    sg_apply_uniforms(UB_sprites_params, &SG_RANGE(r->params));
    sg_draw(0, 6, r->count);
    sr.draw_calls++;
    sr.drawn += r->count;
}

static void end_frame(void)
{
    sr.last_draw_calls = sr.draw_calls;
    sr.last_drawn = sr.drawn;
    sr.draw_calls = sr.drawn = 0;
}

static void stop(void)
{
    int i;
    if (sr.ready) { /* sokol_gfx is still up: gfx stops its parts first */
        for (i = 0; i < PIPELINE_COUNT; i++) sg_destroy_pipeline(sr.pipelines[i]);
        sg_destroy_buffer(sr.corners);
        if (sr.buffer.id != SG_INVALID_ID) sg_destroy_buffer(sr.buffer);
        sg_destroy_shader(sr.shader);
    }
    free(sr.records);
    free(sr.runs);
    free(sr.quads);
    memset(&sr, 0, sizeof(sr));
}

static wgf_core_priv_part_t part = {.name = "sprite runs",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SPRITE_RUNS,
                                    .end_frame = end_frame,
                                    .stop = stop};

static int pipeline_of(int mode)
{
    switch (mode) {
    case WGF_ALPHA_MODE_ADD: return PIPELINE_ADD;
    case WGF_ALPHA_MODE_OPAQUE: return PIPELINE_OPAQUE;
    case WGF_ALPHA_MODE_MULTIPLY: return PIPELINE_MULTIPLY;
    case WGF_ALPHA_MODE_PREMULTIPLIED: return PIPELINE_PREMULTIPLIED;
    default: return PIPELINE_BLEND;
    }
}

/* The slot `view` and `sampler` have in run `r`, added when there is room; -1 when not. */
static int slot_of(run_t *r, sg_view view, sg_sampler sampler)
{
    int i;
    for (i = 0; i < r->textures; i++) {
        if (r->views[i].id == view.id && r->samplers[i].id == sampler.id) return i;
    }
    if (r->textures == SLOTS) return -1;
    r->views[r->textures] = view;
    r->samplers[r->textures] = sampler;
    return r->textures++;
}

/* A new run, a command of its own; NULL when there is no room. */
static run_t *begin_run(int pipeline)
{
    const wgf_mat4_t projection = wgf_gfx_priv_render_projection_2d();
    run_t *r;
    if (!sr.installed) {
        wgf_core_priv_part_install(&part);
        sr.installed = true;
    }
    ensure_gpu();
    if (!grow((void **)&sr.runs, &sr.run_capacity, sr.run_count + 1, sizeof(run_t), 16)) return NULL;
    if (!wgf_gfx_priv_render_add_command(replay, sr.run_count)) return NULL;
    r = &sr.runs[sr.run_count++];
    memset(r, 0, sizeof(*r));
    r->first = sr.record_count;
    r->pipeline = pipeline;
    memcpy(r->params.projection, projection.m, sizeof(r->params.projection));
    return r;
}

void wgf_gfx_priv_sprite_run_add(const float placed[6], const float rect[4], const float uv[4], uint32_t tint, int mode,
                                 sg_view view, sg_sampler sampler)
{
    const int pipeline = pipeline_of(mode);
    const float *m = placed, w = rect[2] - rect[0], h = rect[3] - rect[1];
    run_t *r = NULL;
    record_t *record;
    int slot = -1;
    if (sr.frame != wgf_gfx_priv_render_get_frame()) { /* a frame's first: the last frame's are drawn */
        sr.frame = wgf_gfx_priv_render_get_frame();
        sr.run_count = sr.record_count = 0;
        sr.uploaded = 0;
    }
    if (!grow((void **)&sr.records, &sr.record_capacity, sr.record_count + 1, sizeof(record_t), 1024)) return;
    if (sr.run_count > 0 && wgf_gfx_priv_render_is_last_command(replay, sr.run_count - 1)) {
        r = &sr.runs[sr.run_count - 1];
        if (r->pipeline == pipeline) slot = slot_of(r, view, sampler);
    }
    if (slot < 0) {
        r = begin_run(pipeline);
        if (r == NULL) return;
        slot = slot_of(r, view, sampler);
    }
    record = &sr.records[sr.record_count++];
    r->count++;
    record->origin[0] = m[0] * rect[0] + m[2] * rect[1] + m[4];
    record->origin[1] = m[1] * rect[0] + m[3] * rect[1] + m[5];
    record->edges[0] = m[0] * w;
    record->edges[1] = m[1] * w;
    record->edges[2] = m[2] * h;
    record->edges[3] = m[3] * h;
    memcpy(record->uv, uv, sizeof(record->uv));
    record->color[0] = (uint8_t)(tint >> 24);
    record->color[1] = (uint8_t)(tint >> 16);
    record->color[2] = (uint8_t)(tint >> 8);
    record->color[3] = (uint8_t)tint;
    record->slot = (float)slot;
}

wgf_gfx_priv_kept_quad_t *wgf_gfx_priv_sprite_run_quad(int index, bool make)
{
    if (index < 0) return NULL;
    if (index >= sr.quad_capacity) {
        const int old = sr.quad_capacity;
        if (!make || !grow((void **)&sr.quads, &sr.quad_capacity, index + 1, sizeof(wgf_gfx_priv_kept_quad_t), 1024)) {
            return NULL;
        }
        memset(sr.quads + old, 0, sizeof(wgf_gfx_priv_kept_quad_t) * (size_t)(sr.quad_capacity - old));
        if (!sr.installed) { /* freed at the part's stop */
            wgf_core_priv_part_install(&part);
            sr.installed = true;
        }
    }
    return &sr.quads[index];
}

int wgf_gfx_priv_sprite_run_get_draw_calls(void)
{
    return sr.last_draw_calls;
}

int wgf_gfx_priv_sprite_run_get_drawn(void)
{
    return sr.last_drawn;
}
