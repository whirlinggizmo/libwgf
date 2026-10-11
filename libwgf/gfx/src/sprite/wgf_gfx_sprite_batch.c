#include "sprite/wgf_gfx_sprite_batch_priv.h"

#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"

/* the shader's sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_sprite.glsl.h"

/* Instanced sprites, libwgt's wgt_gfx_sprite_batch.c (wgrender's wgr_sprite_batch.c).
 * During the frame, sprites are appended to one instance array in the order they come and
 * grouped into batches: consecutive sprites with the same texture, pipeline, and camera.
 * Before the pass, the instances go up in one buffer update (or, without base-instance
 * draws, one float texture's); each batch then draws its range with one instanced draw, in
 * its place in a 3D stage's draw (wgf_gfx_stage3d.c). 2D sprites stay on immediate mode
 * (docs/HISTORY.md, "spritebench, and 2D sprites kept on immediate mode"); no lit or
 * custom-material sprites yet (milestone 2.5), nor render targets' flipped pipelines (step
 * 12). The part is installed by the first sprite a stage3d draws. */

#define INSTANCES_INITIAL 1024
/* Without base-instance draws (WebGL2, GL before 4.2) the sprites go into a float texture
 * the shader reads by index (sprite_pulled in wgf_gfx_sprite.glsl): 6 texels a sprite,
 * 256 sprites a row. */
#define PULLED_TEXELS 6
#define PULLED_SPRITES_PER_ROW 256
#define PULLED_WIDTH (PULLED_TEXELS * PULLED_SPRITES_PER_ROW)
/* Whether this frame's backend draws from a base instance: never on WebGL2 (sokol_gfx's
 * GLES3 caps), so a web build links only the pulled path, not the other shader too. */
#if defined(SOKOL_GLES3)
#define BASE_INSTANCE false
#else
#define BASE_INSTANCE batch.base_instance
#endif
#define BATCHES_INITIAL 64
#define CAMERAS_INITIAL 8
#define MAX_GROUPS 64 /* distinct texture / mode combinations grouped in one pass */

enum {
    PIPELINE_OPAQUE,    /* opaque and masked: no blending, depth written */
    PIPELINE_BLEND,     /* blended, depth not written (a stage's sorted pass) */
    PIPELINE_ADD,       /* added, depth not written */
    PIPELINE_COUNT
};

/* A sprite held back while unordered, to be grouped with others like it. */
typedef struct pending_t {
    wgf_gfx_priv_sprite_quad_t quad;
    uint32_t view, sampler;
    int pipeline;
    int camera;
} pending_t;

typedef struct batch_t {
    uint32_t view, sampler;
    int pipeline;
    int camera; /* into the frame's camera uniforms */
    int first, count;
} batch_t;

/* A group of unordered sprites: one texture, sampler, pipeline, and camera. */
typedef struct group_t {
    uint32_t view, sampler;
    int pipeline;
    int camera;
    int count, next; /* sprites in it; where its next one goes in the grouped order */
} group_t;

static struct {
    bool ready;
    bool base_instance; /* the backend can draw from a base instance (not WebGL2) */
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    /* no base instance: the sprites as texels, read by index */
    sg_image data_image;
    sg_view data_view;
    sg_sampler data_sampler;
    int data_rows;     /* the image's height */
    float *data;       /* texels for the frame's sprites, PULLED_WIDTH floats x 4 a row */
    int data_capacity; /* sprites `data` holds */
    sg_buffer quad;
    sg_buffer instance_buffer;
    int instance_buffer_capacity;
    wgf_gfx_priv_sprite_quad_t *instances;
    int instance_count, instance_capacity;
    batch_t *batches;
    int batch_count, batch_capacity;
    sprite_params_t *cameras;
    int camera_count, camera_capacity;
    bool unordered;
    pending_t *pending;
    int pending_count, pending_capacity;
    int *order; /* pending sprites grouped by texture and mode (order_capacity long) */
    int order_capacity;
    bool overflow_logged;
    int last_drawn; /* the batch drawn last, whose state may still be applied */
    int draw_calls, last_draw_calls;
    int drawn, last_drawn_sprites;
} batch;

/* Grow *items (count used, capacity) to hold one more, doubling. */
static bool reserve(void **items, int *capacity, int count, size_t item_size, int initial)
{
    void *moved;
    int grown;
    if (count < *capacity) return true;
    grown = *capacity > 0 ? *capacity * 2 : initial;
    moved = realloc(*items, item_size * (size_t)grown);
    if (moved == NULL) {
        if (!batch.overflow_logged) {
            wgf_log_error("wgf_gfx_sprite: out of memory");
            batch.overflow_logged = true;
        }
        return false;
    }
    *items = moved;
    *capacity = grown;
    return true;
}

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

static void make_pipelines(void)
{
    sg_pipeline_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.face_winding = SG_FACEWINDING_CW; /* the quad winds clockwise (sokol's default) */
    if (BASE_INSTANCE) {
        desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        desc.layout.attrs[ATTR_sprite_corner].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_sprite_inst_pos].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_sprite_inst_pos].buffer_index = 1;
        desc.layout.attrs[ATTR_sprite_inst_size].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_sprite_inst_size].buffer_index = 1;
        desc.layout.attrs[ATTR_sprite_inst_uv].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_sprite_inst_uv].buffer_index = 1;
        desc.layout.attrs[ATTR_sprite_inst_right].format = SG_VERTEXFORMAT_FLOAT3;
        desc.layout.attrs[ATTR_sprite_inst_right].buffer_index = 1;
        desc.layout.attrs[ATTR_sprite_inst_up].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_sprite_inst_up].buffer_index = 1;
        desc.layout.attrs[ATTR_sprite_inst_color].format = SG_VERTEXFORMAT_UBYTE4N;
        desc.layout.attrs[ATTR_sprite_inst_color].buffer_index = 1;
    } else { /* the sprites come from a texture: only the quad's corners are attributes */
        desc.layout.attrs[ATTR_sprite_pulled_corner].format = SG_VERTEXFORMAT_FLOAT2;
    }
    /* like immediate mode's 3D pipelines: blended, depth-tested, no culling */
    desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    desc.depth.write_enabled = false;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.label = "wgf-sprite";
    desc.shader = batch.shader;
    batch.pipelines[PIPELINE_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE; /* added: src x alpha + dst */
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
    batch.pipelines[PIPELINE_ADD] = sg_make_pipeline(&desc);
    memset(&desc.colors[0].blend, 0, sizeof(desc.colors[0].blend));
    desc.depth.write_enabled = true;
    batch.pipelines[PIPELINE_OPAQUE] = sg_make_pipeline(&desc);
}

static void install(void);

/* The shader, pipelines, and quad, made with the first sprite drawn rather than at startup:
 * a program without sprites doesn't compile them. */
static void ensure_gpu(void)
{
    /* two triangles: the quad's corners, x right, y up */
    static const float corners[12] = {-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};
    sg_buffer_desc quad;

    install();
    if (batch.ready) return;
    batch.base_instance = sg_query_features().draw_base_instance;
    if (BASE_INSTANCE) {
        batch.shader = sg_make_shader(sprite_shader_desc(shader_backend()));
    } else {
        sg_sampler_desc sampler;
        batch.shader = sg_make_shader(sprite_pulled_shader_desc(shader_backend()));
        memset(&sampler, 0, sizeof(sampler));
        sampler.min_filter = SG_FILTER_NEAREST;
        sampler.mag_filter = SG_FILTER_NEAREST;
        sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler.label = "wgf-sprite-data";
        batch.data_sampler = sg_make_sampler(&sampler);
    }
    make_pipelines();
    memset(&quad, 0, sizeof(quad));
    quad.data.ptr = corners;
    quad.data.size = sizeof(corners);
    quad.label = "wgf-sprite-quad";
    batch.quad = sg_make_buffer(&quad);
    batch.last_drawn = -1;
    batch.ready = true;
}

int wgf_gfx_priv_sprite_batch_add_camera(const float view_proj[16], wgf_vec3_t right, wgf_vec3_t up,
                                         wgf_vec3_t upright)
{
    sprite_params_t *c;
    ensure_gpu();
    if (!reserve((void **)&batch.cameras, &batch.camera_capacity, batch.camera_count, sizeof(sprite_params_t),
                 CAMERAS_INITIAL)) {
        return -1;
    }
    c = &batch.cameras[batch.camera_count];
    memset(c, 0, sizeof(*c));
    memcpy(c->view_proj, view_proj, sizeof(c->view_proj));
    c->camera_right[0] = right.x, c->camera_right[1] = right.y, c->camera_right[2] = right.z;
    c->camera_up[0] = up.x, c->camera_up[1] = up.y, c->camera_up[2] = up.z;
    c->upright[0] = upright.x, c->upright[1] = upright.y, c->upright[2] = upright.z;
    return batch.camera_count++;
}

/* Add one sprite to the frame, joining the open batch when it can. */
static int record(const wgf_gfx_priv_sprite_quad_t *instance, uint32_t view, uint32_t sampler, int pipeline,
                  int camera, bool extend)
{
    batch_t *last;

    ensure_gpu();
    if (camera < 0 || camera >= batch.camera_count) return -1;
    if (!reserve((void **)&batch.instances, &batch.instance_capacity, batch.instance_count,
                 sizeof(wgf_gfx_priv_sprite_quad_t), INSTANCES_INITIAL)) {
        return -1;
    }
    last = extend && batch.batch_count > 0 ? &batch.batches[batch.batch_count - 1] : NULL;
    /* extend the open batch when nothing else was drawn since and everything matches */
    if (last == NULL || last->view != view || last->sampler != sampler || last->pipeline != pipeline ||
        last->camera != camera) {
        if (!reserve((void **)&batch.batches, &batch.batch_capacity, batch.batch_count, sizeof(batch_t),
                     BATCHES_INITIAL)) {
            return -1;
        }
        last = &batch.batches[batch.batch_count++];
        last->view = view;
        last->sampler = sampler;
        last->pipeline = pipeline;
        last->camera = camera;
        last->first = batch.instance_count;
        last->count = 0;
    }
    batch.instances[batch.instance_count++] = *instance;
    last->count++;
    return (int)(last - batch.batches);
}

int wgf_gfx_priv_sprite_batch_add_3d(const wgf_gfx_priv_sprite_quad_t *instance, sg_view view, sg_sampler sampler,
                                     wgf_alpha_mode_t mode, int camera, bool extend)
{
    const int pipeline = mode == WGF_ALPHA_MODE_OPAQUE || mode == WGF_ALPHA_MODE_MASK ? PIPELINE_OPAQUE
                         : mode == WGF_ALPHA_MODE_ADD                                 ? PIPELINE_ADD
                                                                                      : PIPELINE_BLEND;
    pending_t *p;
    if (!batch.unordered) return record(instance, view.id, sampler.id, pipeline, camera, extend);
    if (!reserve((void **)&batch.pending, &batch.pending_capacity, batch.pending_count, sizeof(pending_t),
                 INSTANCES_INITIAL)) {
        return -1;
    }
    p = &batch.pending[batch.pending_count++];
    p->quad = *instance;
    p->view = view.id;
    p->sampler = sampler.id;
    p->pipeline = pipeline;
    p->camera = camera;
    return -1;
}

void wgf_gfx_priv_sprite_batch_begin_unordered(void)
{
    batch.unordered = true;
    batch.pending_count = 0;
}

static int compare_groups(const void *lhs, const void *rhs)
{
    const group_t *a = (const group_t *)lhs, *b = (const group_t *)rhs;
    if (a->pipeline != b->pipeline) return a->pipeline < b->pipeline ? -1 : 1;
    if (a->camera != b->camera) return a->camera < b->camera ? -1 : 1;
    if (a->view != b->view) return a->view < b->view ? -1 : 1;
    return (a->sampler > b->sampler) - (a->sampler < b->sampler);
}

static bool in_group(const group_t *group, const pending_t *p)
{
    return group->view == p->view && group->sampler == p->sampler && group->pipeline == p->pipeline &&
           group->camera == p->camera;
}

/* The group a pending sprite belongs to, adding it; -1 when there are too many. */
static int group_of(group_t *groups, int *count, const pending_t *p, int hint)
{
    int g;
    if (hint >= 0 && in_group(&groups[hint], p)) return hint;
    for (g = 0; g < *count; g++) {
        if (in_group(&groups[g], p)) return g;
    }
    if (*count >= MAX_GROUPS) return -1;
    memset(&groups[*count], 0, sizeof(groups[*count]));
    groups[*count].view = p->view;
    groups[*count].sampler = p->sampler;
    groups[*count].pipeline = p->pipeline;
    groups[*count].camera = p->camera;
    return (*count)++;
}

int wgf_gfx_priv_sprite_batch_end_unordered(bool extend, int *count)
{
    group_t groups[MAX_GROUPS];
    int group_count = 0, hint = -1, placed = 0, first = -1, i, g, k;

    batch.unordered = false;
    *count = 0;
    /* group by texture and mode, keeping each group's sprites in the order they came (a
       counting sort over the few groups: linear, and stable) */
    for (i = 0; i < batch.pending_count; i++) {
        g = group_of(groups, &group_count, &batch.pending[i], hint);
        if (g < 0) { /* too many kinds to group: record them as they came */
            group_count = 0;
            break;
        }
        groups[g].count++;
        hint = g;
    }
    if (group_count > 0 && batch.order_capacity < batch.pending_count) {
        int *grown = (int *)realloc(batch.order, sizeof(int) * (size_t)batch.pending_capacity);
        if (grown != NULL) {
            batch.order = grown;
            batch.order_capacity = batch.pending_capacity;
        }
    }
    if (group_count == 0 || batch.order_capacity < batch.pending_count) {
        for (i = 0; i < batch.pending_count; i++) {
            const pending_t *p = &batch.pending[i];
            const int b = record(&p->quad, p->view, p->sampler, p->pipeline, p->camera, extend || i > 0);
            if (first < 0 && b >= 0) first = b;
        }
    } else {
        qsort(groups, (size_t)group_count, sizeof(groups[0]), compare_groups); /* a handful */
        for (g = 0; g < group_count; g++) {
            groups[g].next = placed;
            placed += groups[g].count;
        }
        hint = -1;
        for (i = 0; i < batch.pending_count; i++) {
            g = group_of(groups, &group_count, &batch.pending[i], hint);
            batch.order[groups[g].next++] = i;
            hint = g;
        }
        for (k = 0; k < batch.pending_count; k++) {
            const pending_t *p = &batch.pending[batch.order[k]];
            const int b = record(&p->quad, p->view, p->sampler, p->pipeline, p->camera, extend || k > 0);
            if (first < 0 && b >= 0) first = b;
        }
    }
    batch.pending_count = 0;
    if (first < 0) return -1;
    /* the first may have joined the batch from before; the rest follow it */
    *count = batch.batch_count - first;
    return first;
}

/* No base instance: write the frame's sprites into the data texture, growing it. */
static void flush_pulled(void)
{
    const int max_rows = sg_query_limits().max_image_size_2d;
    int rows = (batch.instance_count + PULLED_SPRITES_PER_ROW - 1) / PULLED_SPRITES_PER_ROW, i;
    sg_write_image_desc write;

    if (rows > max_rows) {
        if (!batch.overflow_logged) {
            wgf_log_error("wgf_gfx_sprite: %d sprites in a frame, more than the sprite texture holds",
                          batch.instance_count);
            batch.overflow_logged = true;
        }
        batch.instance_count = max_rows * PULLED_SPRITES_PER_ROW;
        rows = max_rows;
    }
    if (batch.data_capacity < rows * PULLED_SPRITES_PER_ROW) {
        const int capacity = rows * PULLED_SPRITES_PER_ROW;
        float *grown = (float *)realloc(batch.data, sizeof(float) * 4 * PULLED_TEXELS * (size_t)capacity);
        if (grown == NULL) {
            batch.instance_count = 0;
            return;
        }
        batch.data = grown;
        batch.data_capacity = capacity;
    }
    if (rows > batch.data_rows) {
        sg_image_desc desc;
        sg_view_desc view;
        int height = batch.data_rows > 0 ? batch.data_rows : 4;
        while (height < rows) height *= 2;
        if (height > max_rows) height = max_rows;
        if (batch.data_view.id != SG_INVALID_ID) sg_destroy_view(batch.data_view);
        if (batch.data_image.id != SG_INVALID_ID) sg_destroy_image(batch.data_image);
        memset(&desc, 0, sizeof(desc));
        desc.width = PULLED_WIDTH;
        desc.height = height;
        desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
        desc.usage.write_transient = true;
        desc.label = "wgf-sprite-data";
        batch.data_image = sg_make_image(&desc);
        memset(&view, 0, sizeof(view));
        view.texture.image = batch.data_image;
        batch.data_view = sg_make_view(&view);
        batch.data_rows = height;
    }
    for (i = 0; i < batch.instance_count; i++) {
        const wgf_gfx_priv_sprite_quad_t *q = &batch.instances[i];
        float *t = &batch.data[(size_t)i * 4 * PULLED_TEXELS]; /* rows are whole sprites */
        const float texels[4 * PULLED_TEXELS] = {
            q->position[0], q->position[1], q->position[2], q->facing,
            q->size[0], q->size[1], q->pivot[0], q->pivot[1],
            q->uv[0], q->uv[1], q->uv[2], q->uv[3],
            q->right[0], q->right[1], q->right[2], 0.0f,
            q->up[0], q->up[1], q->up[2], q->alpha,
            q->color[0] / 255.0f, q->color[1] / 255.0f, q->color[2] / 255.0f, q->color[3] / 255.0f,
        };
        memcpy(t, texels, sizeof(texels));
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = batch.data;
    write.src.data.size = sizeof(float) * 4 * PULLED_WIDTH * (size_t)rows; /* just the rows in use */
    write.src.bytes_per_row = (int)sizeof(float) * 4 * PULLED_WIDTH;
    write.src.bytes_per_slice = (int)sizeof(float) * 4 * PULLED_WIDTH * rows;
    write.dst.image = batch.data_image;
    write.size.width = PULLED_WIDTH;
    write.size.height = rows;
    write.size.num_slices = 1;
    sg_write_image_transient(&write);
}

/* The part's flush, before the frame's pass: the frame's sprites up to the GPU, once. */
static void flush(void)
{
    sg_write_buffer_desc write;
    if (!batch.ready || batch.instance_count == 0) return;
    if (!BASE_INSTANCE) {
        flush_pulled();
        return;
    }
    if (batch.instance_count > batch.instance_buffer_capacity) {
        sg_buffer_desc desc;
        int capacity = batch.instance_buffer_capacity > 0 ? batch.instance_buffer_capacity : INSTANCES_INITIAL;
        while (capacity < batch.instance_count) capacity *= 2;
        if (batch.instance_buffer.id != SG_INVALID_ID) sg_destroy_buffer(batch.instance_buffer);
        memset(&desc, 0, sizeof(desc));
        desc.size = sizeof(wgf_gfx_priv_sprite_quad_t) * (size_t)capacity;
        desc.usage.vertex_buffer = true;
        desc.usage.write_transient = true;
        desc.label = "wgf-sprite-instances";
        batch.instance_buffer = sg_make_buffer(&desc);
        batch.instance_buffer_capacity = capacity;
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = batch.instances;
    write.src.data.size = sizeof(wgf_gfx_priv_sprite_quad_t) * (size_t)batch.instance_count;
    write.dst.buffer = batch.instance_buffer;
    sg_write_buffer_transient(&write);
}

void wgf_gfx_priv_sprite_batch_draw(int index, bool follows)
{
    const batch_t *b, *before;
    sg_bindings bind;

    if (!batch.ready || index < 0 || index >= batch.batch_count) return;
    b = &batch.batches[index];
    if (b->count == 0 || b->first >= batch.instance_count) return; /* lost to an overflow */
    /* right after another batch, only what differs is applied again (sorted sprites from
       several textures make long runs of small batches) */
    before = follows && batch.last_drawn >= 0 ? &batch.batches[batch.last_drawn] : NULL;
    batch.last_drawn = index;
    if (before == NULL || before->pipeline != b->pipeline) {
        sg_apply_pipeline(batch.pipelines[b->pipeline]);
        before = NULL; /* a new pipeline needs its uniforms again */
    }
    /* every batch binds the same instances (base instance) or sprite texture (read by
       index), so consecutive batches only change the texture and where they start */
    memset(&bind, 0, sizeof(bind));
    bind.vertex_buffers[0] = batch.quad;
    bind.views[VIEW_sprite_tex].id = b->view;
    bind.samplers[SMP_sprite_smp].id = b->sampler;
    if (BASE_INSTANCE) {
        bind.vertex_buffers[1] = batch.instance_buffer;
    } else {
        bind.views[VIEW_sprite_data] = batch.data_view;
        bind.samplers[SMP_sprite_data_smp] = batch.data_sampler;
    }
    sg_apply_bindings(&bind);
    if (before == NULL || before->camera != b->camera) {
        sg_apply_uniforms(UB_sprite_params, &SG_RANGE(batch.cameras[b->camera]));
    }
    if (BASE_INSTANCE) {
        sg_draw_ex(0, 6, b->count, 0, b->first);
    } else { /* the shader reads sprites first .. first + count - 1 */
        sprite_batch_t batch_params;
        memset(&batch_params, 0, sizeof(batch_params));
        batch_params.batch[0] = (float)b->first;
        sg_apply_uniforms(UB_sprite_batch, &SG_RANGE(batch_params));
        sg_draw(0, 6, b->count);
    }
    batch.draw_calls++;
    batch.drawn += b->count;
}

static void end_frame(void)
{
    batch.instance_count = 0;
    batch.batch_count = 0;
    batch.camera_count = 0;
    batch.last_drawn = -1;
    batch.last_draw_calls = batch.draw_calls;
    batch.draw_calls = 0;
    batch.last_drawn_sprites = batch.drawn;
    batch.drawn = 0;
}

int wgf_gfx_priv_sprite_batch_get_draw_calls(void)
{
    return batch.last_draw_calls;
}

int wgf_gfx_priv_sprite_batch_get_drawn(void)
{
    return batch.last_drawn_sprites;
}

static void stop(void)
{
    int i;
    if (batch.ready) { /* sokol_gfx is still up: gfx stops its parts first */
        if (batch.instance_buffer.id != SG_INVALID_ID) sg_destroy_buffer(batch.instance_buffer);
        if (batch.data_view.id != SG_INVALID_ID) sg_destroy_view(batch.data_view);
        if (batch.data_image.id != SG_INVALID_ID) sg_destroy_image(batch.data_image);
        if (batch.data_sampler.id != SG_INVALID_ID) sg_destroy_sampler(batch.data_sampler);
        sg_destroy_buffer(batch.quad);
        for (i = 0; i < PIPELINE_COUNT; i++) sg_destroy_pipeline(batch.pipelines[i]);
        sg_destroy_shader(batch.shader);
    }
    free(batch.data);
    free(batch.instances);
    free(batch.batches);
    free(batch.cameras);
    free(batch.pending);
    free(batch.order);
    memset(&batch, 0, sizeof(batch));
}

static wgf_core_priv_part_t part = {.name = "sprites",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SPRITES,
                                    .flush = flush,
                                    .end_frame = end_frame,
                                    .stop = stop};

static void install(void)
{
    wgf_core_priv_part_install(&part);
}
