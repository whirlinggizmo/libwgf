#include "wgf_stage3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "draw/wgf_gfx_draw3d_priv.h"
#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_environment_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "stage/wgf_gfx_shadow_priv.h"
#include "sprite/wgf_gfx_sprite3d_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_model.h"

/* the shader's sources and reflection for the build's backend; a headless build's
   dummy backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_model.glsl.h"

/* Stages: libwgt's scene, trimmed to what milestone 2 has so far (no skinning, picking,
 * sprites, or particles: their steps bring them). A draw walks the stage's tree as it is
 * now -- its lights (the first 4 that cast shadows each given a slot of the frame's shadow
 * map), then its models, each part of a model's mesh an item: placed, culled against the
 * camera's view (a caster out of view kept for the shadow pass alone when its shadow
 * reaches the view), given its lights; the opaque ones sorted to bring alike ones together,
 * the see-through ones back to front -- and records the items, as a command of the frame's
 * (render/wgf_gfx_render_commands.c).
 *
 * Instancing, libwgt's (wgrender's phase 2), with no API: each item's placement, its
 * normals' matrix, its tint, and whether it mirrors are its record, numbered in drawing order in an
 * RGBA32F texture written whole as the frame flushes (wgf_gfx_instance.glsl); the opaque
 * items, which depth testing lets be drawn in any order, are sorted by a hash of what a
 * draw sets outside the record -- material, mesh and primitive, the pipeline (blended,
 * double sided, mirrored), shadows received, the lights selected -- and a run of items equal
 * on all of it, compared exactly, goes up as one draw; see-through items keep their order,
 * and only neighbours alike share a draw. Before the frame's pass the shadow part, when a
 * light casts, draws the first such draw's casters into its map from the same records
 * (wgf_gfx_shadow.c); then the command draws the items with sokol_gfx, between the
 * immediate mode drawn before and after the stage, its background first. An environment's lighting and background are the
 * environment part's (wgf_gfx_environment.c), reached through its hooks: a program with
 * none links none of it. A part, installed by the first stage made: its end of frame
 * forgets the frame's items, and its stop frees its pipelines and default textures. */

typedef struct item_t {
    wgf_mat4_t world;
    wgf_mesh_t mesh;
    int primitive;
    wgf_material_t material;
    float tint[4];   /* linear */
    bool blended;    /* drawn after the opaque ones, back to front, depth not written */
    bool additive;   /* an ADD emitter's: after the blended ones */
    int particles;   /* an emitter's draw (the particles part's); -1, a model's part */
    int sprite;      /* a see-through sprite's, into the frame's sprites, until it is recorded; -1 none */
    int sprite_batch, sprite_batches; /* sprites: their first batch and how many (0: in the one before) */
    int text;        /* a text's: its index into the draw's texts until recorded, then its side layer; -1 none */
    int record;      /* a model part's, in the instance texture: model parts numbered in drawing order */
    bool mirrored;   /* its placement turns its triangles' winding (a negative determinant) */
    float depth;     /* along the camera's view, for the sort */
    int order;       /* as found, so equal depths keep it */
    uint64_t group;  /* its group key's hash: alike opaque ones sorted together */
    int lights[WGF_GFX_PRIV_MAX_DRAW_LIGHTS]; /* into the draw's lights */
    int light_count;
    bool casting, receiving; /* shadows: its model's */
    bool shadow_only;        /* out of view, kept for its shadow: drawn into the shadow map alone */
    wgf_vec3_t box_lo, box_hi; /* its model's box in the stage */
} item_t;

typedef struct stage_draw_t {
    int first, count; /* its items */
    int first_light, light_count;
    int shapes; /* its 3D shapes' side layer of immediate mode (-1: none), drawn after its opaque models */
    wgf_actor_t stage;
    int pass;            /* the frame's pass it is drawn in (0 the screen, else a render target) */
    bool flipped;        /* drawn with clip space's y flipped (a render target on GL) */
    wgf_mat4_t view_proj;
    wgf_mat4_t clip_proj; /* what its own drawing sees through: view_proj, flipped when it is */
    wgf_vec3_t camera_position;
    float ambient[3]; /* linear, times its intensity */
    int tonemap;
    float exposure;
    int viewport[4]; /* the visible area, in the pass's pixels */
    wgf_gfx_priv_shadow_camera_t shadow_camera; /* what its shadows are fitted around */
    int shadow_count;                           /* its lights with a shadow slot */
    int shadow_lights[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS]; /* into the frame's lights, by slot */
    wgf_gfx_priv_shadow_binding_t shadow;       /* the shadow pass's maps, valid once it drew them */
    wgf_handle_t environment, background;       /* 0: none */
    float environment_intensity, environment_rotation;
    float background_intensity, background_rotation, background_blur;
} stage_draw_t;

/* A text waiting for the sort: what its side layer is recorded from. */
typedef struct text_record_t {
    wgf_actor_t actor;
    wgf_mat4_t plane;
} text_record_t;

/* A see-through sprite waiting for the sort: what it records into the batch. */
typedef struct sprite_record_t {
    wgf_gfx_priv_sprite_quad_t quad;
    sg_view view;
    sg_sampler sampler;
    int mode; /* wgf_alpha_mode_t */
} sprite_record_t;

static struct {
    item_t *items;
    int item_count, item_capacity;
    text_record_t *texts; /* a draw's texts, for the sort */
    int text_count, text_capacity;
    sprite_record_t *sprites; /* a draw's see-through sprites, for the sort */
    int sprite_count, sprite_capacity;
    stage_draw_t *draws;
    int draw_count, draw_capacity;
    int shadowed; /* the draw the shadows were fit for (-1: none) */
    wgf_gfx_priv_stage3d_light_t *lights;
    int light_count, light_capacity;
    wgf_actor_t *todo; /* the walk's */
    int todo_capacity;
    /* the GPU's, made at the first replay */
    bool ready;
    sg_shader shader;
    sg_pipeline pipelines[8]; /* by blended, double sided, mirrored */
    sg_image white_image, flat_normal_image;
    sg_view white_view, flat_normal_view;
    sg_sampler sampler;
    sg_image no_shadow_image; /* an empty depth array, bound when nothing casts */
    sg_view no_shadow_view;
    sg_sampler no_shadow_sampler;
    sg_image no_environment_image; /* a black cube, bound with no environment */
    sg_view no_environment_view;
    /* the instance records: an item's at its index, written as the frame flushes */
    float *instances;
    int instance_capacity; /* records */
    sg_image instance_image;
    sg_view instance_view;
    sg_sampler instance_sampler;
    int instance_rows;
    bool instances_written;       /* this frame's are up */
    bool instance_overflow_logged;
    int draw_calls, last_draw_calls; /* for tests: the shading pass's draws */
} frame;

/* What the sort orders an item by (sort_items, below), and its room, kept between frames. */
typedef struct sort_key_t {
    float depth;
    int order;
    uint64_t group;
    int item;
    bool blended, additive;
} sort_key_t;

static struct {
    sort_key_t *keys;
    item_t *moved;
    int capacity;
} sorting;

#define INSTANCE_TEXELS 8
#define INSTANCES_PER_ROW 128
#define INSTANCE_TEXTURE_WIDTH (INSTANCES_PER_ROW * INSTANCE_TEXELS)
#define INSTANCE_FLOATS (INSTANCE_TEXELS * 4)

static wgf_gfx_priv_environment_hooks_t environment_hooks; /* the environment part's, once installed */
static wgf_gfx_priv_particle_hooks_t particle_hooks;       /* the particles part's, once installed */

void wgf_gfx_priv_set_particle_hooks(const wgf_gfx_priv_particle_hooks_t *hooks)
{
    if (hooks != NULL) {
        particle_hooks = *hooks;
    } else {
        memset(&particle_hooks, 0, sizeof(particle_hooks));
    }
}

void wgf_gfx_priv_set_environment_hooks(const wgf_gfx_priv_environment_hooks_t *hooks)
{
    if (hooks != NULL) {
        environment_hooks = *hooks;
    } else {
        memset(&environment_hooks, 0, sizeof(environment_hooks));
    }
}

static bool grow(void **items, int *capacity, int needed, size_t size)
{
    void *grown;
    int next;
    if (needed <= *capacity) return true;
    next = *capacity > 0 ? *capacity * 2 : 64;
    while (next < needed) next *= 2;
    grown = realloc(*items, size * (size_t)next);
    if (grown == NULL) {
        wgf_log_error("wgf_gfx_stage: out of memory drawing a stage");
        return false;
    }
    *items = grown;
    *capacity = next;
    return true;
}

static void end_frame(void)
{
    frame.item_count = frame.draw_count = frame.light_count = 0;
    frame.shadowed = -1;
    frame.instances_written = false;
    frame.last_draw_calls = frame.draw_calls;
    frame.draw_calls = 0;
}

static void flush(void);

static void stop(void)
{
    int i;
    end_frame();
    free(frame.items);
    free(frame.draws);
    free(frame.lights);
    free(frame.todo);
    free(frame.sprites);
    free(frame.texts);
    free(sorting.keys);
    free(sorting.moved);
    memset(&sorting, 0, sizeof(sorting));
    free(frame.instances);
    if (frame.instance_image.id != SG_INVALID_ID) { /* sokol_gfx is still up, as below */
        sg_destroy_view(frame.instance_view);
        sg_destroy_image(frame.instance_image);
        sg_destroy_sampler(frame.instance_sampler);
    }
    if (frame.ready) { /* sokol_gfx is still up: gfx stops its parts first */
        for (i = 0; i < 8; i++) {
            if (frame.pipelines[i].id != SG_INVALID_ID) sg_destroy_pipeline(frame.pipelines[i]);
        }
        sg_destroy_shader(frame.shader);
        sg_destroy_view(frame.white_view);
        sg_destroy_view(frame.flat_normal_view);
        sg_destroy_image(frame.white_image);
        sg_destroy_image(frame.flat_normal_image);
        sg_destroy_view(frame.no_shadow_view);
        sg_destroy_image(frame.no_shadow_image);
        sg_destroy_sampler(frame.no_shadow_sampler);
        sg_destroy_view(frame.no_environment_view);
        sg_destroy_image(frame.no_environment_image);
    }
    memset(&frame, 0, sizeof(frame));
}

static wgf_core_priv_part_t part = {.name = "stage",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_STAGE3D,
                                    .flush = flush,
                                    .end_frame = end_frame,
                                    .stop = stop};

/* ------------------------------------------------------------ the stage ---- */

static wgf_gfx_priv_actor_t *stage_of_at(wgf_actor_t stage, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(stage, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_STAGE3D ? actor_ptr : NULL;
}
#define stage_of(...) stage_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* A stage let go of: its environments' references. */
static void stage_free(wgf_actor_t stage, wgf_gfx_priv_actor_t *actor_ptr)
{
    (void)stage;
    if (actor_ptr->as.stage3d.environment != 0) wgf_resource_release(actor_ptr->as.stage3d.environment);
    if (actor_ptr->as.stage3d.background != 0) wgf_resource_release(actor_ptr->as.stage3d.background);
}

static const wgf_gfx_priv_actor_kind_t actor_kind = {stage_free, NULL, NULL, NULL};

wgf_actor_t wgf_stage3d_create(void)
{
    const wgf_actor_t stage = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_STAGE3D);
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(stage);
    if (actor_ptr == NULL) return 0;
    actor_ptr->as.stage3d.tonemap = WGF_STAGE3D_TONEMAP_NEUTRAL;
    actor_ptr->as.stage3d.culling = true;
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_STAGE3D, &actor_kind);
    wgf_core_priv_part_install(&part);
    wgf_gfx_priv_model_install(); /* the ecs's model component, now that there is somewhere to draw one */
    return stage;
}

bool wgf_stage3d_set_camera(wgf_actor_t stage, wgf_actor_t camera)
{
    wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL || (camera != 0 && wgf_actor_get_kind(camera) != WGF_ACTOR_KIND_CAMERA3D)) return false;
    actor_ptr->as.stage3d.camera = camera;
    return true;
}

wgf_actor_t wgf_stage3d_get_camera(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL || wgf_actor_get_kind(actor_ptr->as.stage3d.camera) != WGF_ACTOR_KIND_CAMERA3D) return 0;
    return actor_ptr->as.stage3d.camera;
}

wgf_actor_t wgf_stage3d_find(wgf_actor_t stage, const char *name)
{
    if (wgf_actor_get_kind(stage) != WGF_ACTOR_KIND_STAGE3D) return 0;
    return wgf_gfx_priv_actor_find_on_stage(stage, name);
}

bool wgf_stage3d_set_ambient(wgf_actor_t stage, wgf_color_t color, float intensity)
{
    wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.stage3d.ambient_color = color;
    actor_ptr->as.stage3d.ambient_intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

wgf_color_t wgf_stage3d_get_ambient_color(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.ambient_color : 0;
}

float wgf_stage3d_get_ambient_intensity(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.ambient_intensity : 0.0f;
}

bool wgf_stage3d_set_tonemap(wgf_actor_t stage, wgf_stage3d_tonemap_t tonemap, float exposure)
{
    wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL || (int)tonemap < WGF_STAGE3D_TONEMAP_NONE || tonemap > WGF_STAGE3D_TONEMAP_ACES) return false;
    actor_ptr->as.stage3d.tonemap = tonemap;
    actor_ptr->as.stage3d.exposure = exposure;
    return true;
}

wgf_stage3d_tonemap_t wgf_stage3d_get_tonemap(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? (wgf_stage3d_tonemap_t)actor_ptr->as.stage3d.tonemap : WGF_STAGE3D_TONEMAP_NONE;
}

float wgf_stage3d_get_exposure(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.exposure : 0.0f;
}

static bool is_environment(wgf_handle_t handle)
{
    return WGF_CORE_PRIV_HANDLE_KIND(handle) == WGF_CORE_PRIV_HANDLE_KIND_ENVIRONMENT &&
           wgf_core_priv_handle_is_alive(handle);
}

/* `*held` becomes `environment` (0: none), the reference moved: the new one's taken
 * before the old one's let go, which may be the same. */
static void hold(wgf_handle_t *held, wgf_handle_t environment)
{
    const wgf_handle_t old = *held;
    if (environment != 0) wgf_core_priv_resource_retain(environment);
    *held = environment;
    if (old != 0) wgf_resource_release(old);
}

bool wgf_stage3d_set_environment(wgf_actor_t stage, wgf_environment_t environment, float intensity, float rotation)
{
    wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL || (environment != 0 && !is_environment(environment))) return false;
    hold(&actor_ptr->as.stage3d.environment, environment);
    actor_ptr->as.stage3d.environment_intensity = environment != 0 ? (intensity > 0.0f ? intensity : 0.0f) : 0.0f;
    actor_ptr->as.stage3d.environment_rotation = environment != 0 ? rotation : 0.0f;
    return true;
}

wgf_environment_t wgf_stage3d_get_environment(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.environment : 0;
}

float wgf_stage3d_get_environment_intensity(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.environment_intensity : 0.0f;
}

float wgf_stage3d_get_environment_rotation(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.environment_rotation : 0.0f;
}

bool wgf_stage3d_set_background(wgf_actor_t stage, wgf_environment_t environment, float blur)
{
    wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL || (environment != 0 && !is_environment(environment))) return false;
    hold(&actor_ptr->as.stage3d.background, environment);
    actor_ptr->as.stage3d.background_blur = environment != 0 ? (blur < 0.0f ? 0.0f : (blur > 1.0f ? 1.0f : blur)) : 0.0f;
    return true;
}

wgf_environment_t wgf_stage3d_get_background(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.background : 0;
}

float wgf_stage3d_get_background_blur(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL ? actor_ptr->as.stage3d.background_blur : 0.0f;
}

bool wgf_stage3d_set_culling(wgf_actor_t stage, bool culling)
{
    wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.stage3d.culling = culling;
    return true;
}

bool wgf_stage3d_is_culling(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *actor_ptr = stage_of(stage);
    return actor_ptr != NULL && actor_ptr->as.stage3d.culling;
}

/* ------------------------------------------------------------- the draw ---- */

/* A view's six planes (Gribb and Hartmann), each ax + by + cz + d >= 0 inside, from a
 * view-projection matrix into OpenGL's clip space. */
static void frustum_planes(const wgf_mat4_t *m, float planes[6][4])
{
    int p, c;
    for (p = 0; p < 6; p++) {
        const int row = p / 2;
        const float sign = p % 2 == 0 ? 1.0f : -1.0f;
        float length;
        for (c = 0; c < 4; c++) planes[p][c] = m->m[c * 4 + 3] + sign * m->m[c * 4 + row];
        length = sqrtf(planes[p][0] * planes[p][0] + planes[p][1] * planes[p][1] + planes[p][2] * planes[p][2]);
        if (length > 0.0f) {
            for (c = 0; c < 4; c++) planes[p][c] /= length;
        }
    }
}

/* Whether the box [lo, hi] is wholly outside one of the planes. */
static bool outside(const float planes[6][4], wgf_vec3_t lo, wgf_vec3_t hi)
{
    int p;
    for (p = 0; p < 6; p++) { /* the box's corner furthest along the plane's normal */
        const float x = planes[p][0] >= 0.0f ? hi.x : lo.x, y = planes[p][1] >= 0.0f ? hi.y : lo.y;
        const float z = planes[p][2] >= 0.0f ? hi.z : lo.z;
        if (planes[p][0] * x + planes[p][1] * y + planes[p][2] * z + planes[p][3] < 0.0f) return true;
    }
    return false;
}

/* The box around [lo, hi] placed by `m`, grown a little against rounding. */
static void place_box(const wgf_mat4_t *m, wgf_vec3_t lo, wgf_vec3_t hi, wgf_vec3_t *out_lo, wgf_vec3_t *out_hi)
{
    const wgf_vec3_t center = wgf_mat4_transform_point(*m, wgf_vec3_scale(wgf_vec3_add(lo, hi), 0.5f));
    const wgf_vec3_t half = wgf_vec3_scale(wgf_vec3_sub(hi, lo), 0.5f);
    /* each world axis's reach: the absolute matrix times the half extents */
    const float rx = fabsf(m->m[0]) * half.x + fabsf(m->m[4]) * half.y + fabsf(m->m[8]) * half.z;
    const float ry = fabsf(m->m[1]) * half.x + fabsf(m->m[5]) * half.y + fabsf(m->m[9]) * half.z;
    const float rz = fabsf(m->m[2]) * half.x + fabsf(m->m[6]) * half.y + fabsf(m->m[10]) * half.z;
    const float pad = 1e-3f * (1.0f + rx + ry + rz);
    *out_lo = wgf_vec3_make(center.x - rx - pad, center.y - ry - pad, center.z - rz - pad);
    *out_hi = wgf_vec3_make(center.x + rx + pad, center.y + ry + pad, center.z + rz + pad);
}

static void linear_color(wgf_color_t color, float out[4])
{
    out[0] = wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_red(color) / 255.0f);
    out[1] = wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_green(color) / 255.0f);
    out[2] = wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_blue(color) / 255.0f);
    out[3] = (float)wgf_color_get_alpha(color) / 255.0f;
}

/* Every enabled actor of the tree from `root`, `root` first, depth first, as the
 * a 2D stage walks it; `visit` is given each with its world matrix. False when the walk ran
 * out of memory (what it visited stands). */
typedef void (*visit_t)(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user);
static void walk(wgf_actor_t root, visit_t visit, void *user)
{
    int count = 0;
    if (!grow((void **)&frame.todo, &frame.todo_capacity, 64, sizeof(wgf_actor_t))) return;
    frame.todo[count++] = root;
    while (count > 0) {
        const wgf_actor_t next = frame.todo[--count];
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(next);
        const wgf_actor_t *children;
        int i, child_count;
        wgf_mat4_t world;
        if (actor_ptr == NULL || !actor_ptr->enabled) continue; /* with its children */
        world = wgf_gfx_priv_actor_world_of(next, actor_ptr);
        if (actor_ptr->visible) visit(next, actor_ptr, &world, user);
        children = wgf_gfx_priv_actor_children_of(actor_ptr, &child_count);
        if (!grow((void **)&frame.todo, &frame.todo_capacity, count + child_count, sizeof(wgf_actor_t))) return;
        for (i = child_count - 1; i >= 0; i--) frame.todo[count++] = children[i];
    }
}

static void visit_light(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user)
{
    stage_draw_t *draw;
    (void)actor;
    (void)user;
    if (actor_ptr->type != WGF_ACTOR_KIND_LIGHT || frame.light_count - frame.draws[frame.draw_count].first_light >=
                                                    WGF_GFX_PRIV_MAX_STAGE_LIGHTS) {
        return;
    }
    if (!grow((void **)&frame.lights, &frame.light_capacity, frame.light_count + 1,
              sizeof(wgf_gfx_priv_stage3d_light_t))) {
        return;
    }
    wgf_gfx_priv_light_resolve(actor_ptr, *world, &frame.lights[frame.light_count]);
    draw = &frame.draws[frame.draw_count];
    if (frame.lights[frame.light_count].shadow_casting && draw->shadow_count < WGF_GFX_PRIV_MAX_SHADOW_LIGHTS) {
        frame.lights[frame.light_count].shadow_slot = draw->shadow_count; /* the first 4 to cast, in tree order */
        draw->shadow_lights[draw->shadow_count++] = frame.light_count;
    }
    frame.light_count++;
}

/* Whether a caster out of view at [lo, hi] could still shadow what is in it: its box swept
 * along each casting light as far as that light's shadows reach (wgrender's visible()). */
static bool shadow_reaches_view(const stage_draw_t *draw, const float planes[6][4], wgf_vec3_t lo, wgf_vec3_t hi)
{
    int i;
    for (i = 0; i < draw->shadow_count; i++) {
        const wgf_gfx_priv_stage3d_light_t *light = &frame.lights[draw->shadow_lights[i]];
        const wgf_vec3_t step = wgf_vec3_scale(light->direction, wgf_gfx_priv_shadow_reach(light));
        const wgf_vec3_t far_lo = wgf_vec3_add(lo, step), far_hi = wgf_vec3_add(hi, step);
        const wgf_vec3_t swept_lo = wgf_vec3_make(fminf(lo.x, far_lo.x), fminf(lo.y, far_lo.y), fminf(lo.z, far_lo.z));
        const wgf_vec3_t swept_hi = wgf_vec3_make(fmaxf(hi.x, far_hi.x), fmaxf(hi.y, far_hi.y), fmaxf(hi.z, far_hi.z));
        if (!outside(planes, swept_lo, swept_hi)) return true;
    }
    return false;
}

typedef struct walk_models_t {
    bool culling;
    float planes[6][4];
    wgf_vec3_t eye, forward;
    wgf_vec3_t right, up; /* the camera's: a particle quad's before its spin */
    wgf_gfx_priv_sprite_view_t sprite_view; /* the billboards' axes */
    int sprite_camera;                      /* the batch's camera for this draw; -1 until a sprite */
} walk_models_t;

static void visit_shape(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user)
{
    stage_draw_t *draw = &frame.draws[frame.draw_count];
    (void)actor;
    (void)user;
    if (actor_ptr->type != WGF_ACTOR_KIND_SHAPE3D || actor_ptr->as.shape3d.kind == 0) return;
    if (draw->shapes < 0) draw->shapes = wgf_gfx_priv_render_begin_side_layer(draw->view_proj.m);
    wgf_gfx_priv_shape3d_draw(actor_ptr, world);
}

/* What an item must agree on to share a draw with another: everything the draw sets
 * outside the instance record -- its material, its primitive's buffers (its mesh and
 * primitive), the pipeline (blended, double sided, mirrored), the shadows it receives, the
 * lights selected for it, and whether it is drawn at all (a caster kept for its shadow
 * alone isn't). The camera and the environment are the stage draw's. libwgt's. */
typedef struct group_key_t {
    wgf_material_t material;
    wgf_mesh_t mesh;
    int primitive;
    bool blended, double_sided, mirrored, receiving, shadow_only;
    int light_count, lights[WGF_GFX_PRIV_MAX_DRAW_LIGHTS];
} group_key_t;

static void item_key(const item_t *item, group_key_t *key)
{
    const wgf_gfx_priv_material_t *material = wgf_gfx_priv_material_get(item->material);
    int i;
    memset(key, 0, sizeof(*key)); /* the padding too: keys are compared and hashed as bytes */
    key->material = item->material;
    key->mesh = item->mesh;
    key->primitive = item->primitive;
    key->blended = item->blended;
    key->double_sided = material != NULL && material->double_sided;
    key->mirrored = item->mirrored;
    key->receiving = item->receiving;
    key->shadow_only = item->shadow_only;
    key->light_count = item->light_count;
    for (i = 0; i < item->light_count; i++) key->lights[i] = item->lights[i];
}

/* Whether two items go up as one draw, compared exactly: the hash below only orders them,
 * so a collision costs a split, never a wrong batch. */
static bool same_group(const item_t *a, const item_t *b)
{
    group_key_t ka, kb;
    if (a->particles >= 0 || b->particles >= 0) return false; /* an emitter's draw is its own */
    if (a->sprite >= 0 || b->sprite >= 0 || a->sprite_batch >= 0 || b->sprite_batch >= 0) return false; /* sprites' */
    if (a->text >= 0 || b->text >= 0) return false; /* a text's layer is its own */
    item_key(a, &ka);
    item_key(b, &kb);
    return memcmp(&ka, &kb, sizeof(ka)) == 0;
}

/* FNV-1a over the key's bytes. */
static uint64_t key_hash(const group_key_t *key)
{
    const unsigned char *bytes = (const unsigned char *)key;
    uint64_t h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < sizeof(*key); i++) h = (h ^ bytes[i]) * 1099511628211ULL;
    return h;
}

static void visit_model(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user)
{
    const walk_models_t *w = (const walk_models_t *)user;
    stage_draw_t *draw = &frame.draws[frame.draw_count];
    wgf_mesh_t mesh = actor_ptr->type == WGF_ACTOR_KIND_MODEL ? actor_ptr->as.model.mesh : 0;
    wgf_material_t forced = 0; /* the placeholder's, for a failed mesh */
    wgf_vec3_t lo, hi, box_lo, box_hi;
    float tint[4];
    int count = 0, p;
    bool shadow_only = false;
    if (mesh == 0) return;
    if (wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_FAILED) {
        if (!wgf_gfx_priv_model_placeholder(&mesh, &forced)) return; /* the file part's: only a file's mesh fails */
        count = 1;
        lo = wgf_vec3_make(-0.5f, -0.5f, -0.5f);
        hi = wgf_vec3_make(0.5f, 0.5f, 0.5f);
    } else if (actor_ptr->as.model.file_root || !wgf_gfx_priv_mesh_get_bounds(mesh, &lo, &hi)) {
        return; /* a file's root (its nodes draw), or pending: nothing */
    } else {
        count = wgf_gfx_priv_mesh_get_primitive_count(mesh);
    }
    place_box(world, lo, hi, &box_lo, &box_hi);
    if (w->culling && outside(w->planes, box_lo, box_hi)) {
        if (actor_ptr->as.model.shadow_off || draw->shadow_count == 0 ||
            !shadow_reaches_view(draw, w->planes, box_lo, box_hi)) {
            return;
        }
        shadow_only = true; /* out of view, its shadow not: the shadow pass's alone */
    }
    linear_color(actor_ptr->as.model.tint, tint);
    for (p = 0; p < count; p++) {
        wgf_gfx_priv_mesh_primitive_t primitive;
        const wgf_gfx_priv_material_t *material;
        item_t *item;
        if (!wgf_gfx_priv_mesh_get_primitive(mesh, p, &primitive)) continue;
        if (!grow((void **)&frame.items, &frame.item_capacity, frame.item_count + 1, sizeof(item_t))) return;
        item = &frame.items[frame.item_count];
        item->material = forced != 0 ? forced : wgf_model_get_material(actor, primitive.material);
        material = wgf_gfx_priv_material_get(item->material);
        if (material == NULL) continue; /* a slot with none draws nothing */
        item->world = *world;
        item->particles = -1;
        item->sprite = item->sprite_batch = item->text = -1;
        item->sprite_batches = 0;
        item->additive = false;
        item->mirrored = wgf_mat4_determinant(*world) < 0.0f;
        item->mesh = mesh;
        item->primitive = p;
        memcpy(item->tint, tint, sizeof(tint));
        item->blended = material->alpha_mode == WGF_ALPHA_MODE_BLEND || tint[3] < 1.0f;
        item->depth =
            wgf_vec3_dot(wgf_vec3_sub(wgf_vec3_scale(wgf_vec3_add(box_lo, box_hi), 0.5f), w->eye), w->forward);
        item->order = frame.item_count - draw->first;
        item->casting = !actor_ptr->as.model.shadow_off;
        item->receiving = !actor_ptr->as.model.receive_off;
        item->shadow_only = shadow_only;
        item->box_lo = box_lo;
        item->box_hi = box_hi;
        item->light_count = material->shading == WGF_MATERIAL_SHADING_PBR && !shadow_only
                                ? wgf_gfx_priv_light_select(&frame.lights[draw->first_light], draw->light_count,
                                                            box_lo, box_hi, item->lights, WGF_GFX_PRIV_MAX_DRAW_LIGHTS)
                                : 0;
        {
            group_key_t key;
            item_key(item, &key);
            item->group = key_hash(&key);
        }
        frame.item_count++;
    }
}

/* A 3D emitter's particles, recorded through the particles part's hook: an item drawn
 * with the opaque parts (OPAQUE, MASK), among the see-through ones by where it is (BLEND),
 * or after them (ADD). Not culled, casting and receiving nothing. */
static void visit_emitter(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user)
{
    const walk_models_t *w = (const walk_models_t *)user;
    stage_draw_t *draw = &frame.draws[frame.draw_count];
    item_t *item;
    int index, mode = WGF_ALPHA_MODE_ADD;
    if (actor_ptr->type != WGF_ACTOR_KIND_EMITTER3D || particle_hooks.record == NULL) return;
    index = particle_hooks.record(actor, draw->clip_proj.m, w->right, w->up, &mode);
    if (index < 0 || !grow((void **)&frame.items, &frame.item_capacity, frame.item_count + 1, sizeof(item_t))) return;
    item = &frame.items[frame.item_count];
    memset(item, 0, sizeof(*item));
    item->world = wgf_mat4_identity(); /* its record goes unread */
    item->particles = index;
    item->sprite = item->sprite_batch = item->text = -1;
    item->blended = mode == WGF_ALPHA_MODE_BLEND || mode == WGF_ALPHA_MODE_ADD;
    item->additive = mode == WGF_ALPHA_MODE_ADD;
    item->depth = wgf_vec3_dot(wgf_vec3_sub(wgf_mat4_get_translation(*world), w->eye), w->forward);
    item->order = frame.item_count - draw->first;
    item->group = (uint64_t)index; /* alone */
    frame.item_count++;
}

/* A new item past the frame's last, cleared; NULL out of memory. */
static item_t *new_item(void)
{
    item_t *item;
    if (!grow((void **)&frame.items, &frame.item_capacity, frame.item_count + 1, sizeof(item_t))) return NULL;
    item = &frame.items[frame.item_count];
    memset(item, 0, sizeof(*item));
    item->world = wgf_mat4_identity(); /* its record goes unread */
    item->particles = item->sprite = item->sprite_batch = item->text = -1;
    return item;
}

/* A sprite on the stage (wgf_gfx_sprite3d.c), culled by the box its quad can reach, kept:
 * an OPAQUE, MASK, or ADD one for its pass, grouped by texture once the walk is over; a
 * BLEND one an item of its own too, sorted among the see-through parts and recorded after
 * the sort, so alike neighbours share a batch. */
static void visit_sprite(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user)
{
    walk_models_t *w = (walk_models_t *)user;
    stage_draw_t *draw = &frame.draws[frame.draw_count];
    wgf_gfx_priv_sprite_quad_t quad;
    sg_view view;
    sg_sampler sampler;
    wgf_vec3_t at, reach;
    float r;
    int mode;
    (void)actor;
    if (actor_ptr->type != WGF_ACTOR_KIND_SPRITE || !wgf_gfx_priv_sprite_quad(actor_ptr, *world, &quad, &view, &sampler)) {
        return;
    }
    at = wgf_mat4_get_translation(*world);
    r = wgf_gfx_priv_sprite_radius(actor_ptr, *world);
    reach = wgf_vec3_make(r, r, r);
    if (w->culling && outside((const float(*)[4])w->planes, wgf_vec3_sub(at, reach), wgf_vec3_add(at, reach))) return;
    if (w->sprite_camera < 0) {
        w->sprite_camera = wgf_gfx_priv_sprite_batch_add_camera(draw->clip_proj.m, w->sprite_view.right,
                                                                w->sprite_view.up, w->sprite_view.upright);
        if (w->sprite_camera < 0) return;
    }
    mode = actor_ptr->as.sprite.alpha_mode;
    if (!grow((void **)&frame.sprites, &frame.sprite_capacity, frame.sprite_count + 1, sizeof(sprite_record_t))) {
        return;
    }
    frame.sprites[frame.sprite_count].quad = quad;
    frame.sprites[frame.sprite_count].view = view;
    frame.sprites[frame.sprite_count].sampler = sampler;
    frame.sprites[frame.sprite_count].mode = mode;
    if (mode != WGF_ALPHA_MODE_BLEND) { /* depth-tested or added: its pass's, any order */
        frame.sprite_count++;
        return;
    }
    {
        item_t *item = new_item();
        if (item == NULL) return;
        item->sprite = frame.sprite_count++;
        item->blended = true;
        item->depth = wgf_vec3_dot(wgf_vec3_sub(at, w->eye), w->forward);
        item->order = frame.item_count - draw->first;
        item->group = (uint64_t)item->sprite;
        frame.item_count++;
    }
}

/* The sprites of one pass, OPAQUE and MASK (`additive` false) or ADD, recorded grouped by
 * texture and mode: an item holding their batches. */
static void sprite_pass_item(bool additive, int camera)
{
    int first, count, i, held = 0;
    item_t *item;
    wgf_gfx_priv_sprite_batch_begin_unordered();
    for (i = 0; i < frame.sprite_count; i++) {
        const sprite_record_t *r = &frame.sprites[i];
        if (r->mode == WGF_ALPHA_MODE_BLEND || (r->mode == WGF_ALPHA_MODE_ADD) != additive) continue;
        wgf_gfx_priv_sprite_batch_add_3d(&r->quad, r->view, r->sampler, (wgf_alpha_mode_t)r->mode, camera, false);
        held++;
    }
    first = wgf_gfx_priv_sprite_batch_end_unordered(false, &count);
    if (held == 0) return;
    if (first < 0 || (item = new_item()) == NULL) return;
    item->sprite_batch = first;
    item->sprite_batches = count;
    item->blended = item->additive = additive;
    item->order = frame.item_count - frame.draws[frame.draw_count].first;
    item->group = additive ? UINT64_MAX : 0; /* opaque sprites first among the opaque parts */
    frame.item_count++;
}

/* After the sort: the see-through sprites recorded in its order, neighbours alike in one
 * batch (the items after the first of a batch keeping none of their own). */
static void record_sprites(const stage_draw_t *draw, int camera)
{
    int i, last = -2;
    for (i = draw->first; i < draw->first + draw->count; i++) {
        item_t *item = &frame.items[i];
        const sprite_record_t *r;
        int b;
        if (item->sprite < 0) {
            last = -2;
            continue;
        }
        r = &frame.sprites[item->sprite];
        b = wgf_gfx_priv_sprite_batch_add_3d(&r->quad, r->view, r->sampler, WGF_ALPHA_MODE_BLEND, camera,
                                             last == i - 1);
        if (b >= 0 && last == i - 1 && b == frame.items[i - 1].sprite_batch) {
            item->sprite_batches = 0; /* joined the batch drawn with the item before */
            item->sprite_batch = b;
        } else {
            item->sprite_batch = b;
            item->sprite_batches = b >= 0 ? 1 : 0;
        }
        last = i;
    }
}

/* Opaque first, alike ones together (by their keys' hash, then as found); then the
 * see-through ones, farthest first. */
static int compare_items(const void *lhs, const void *rhs)
{
    const item_t *a = (const item_t *)lhs, *b = (const item_t *)rhs;
    if (a->blended != b->blended) return a->blended ? 1 : -1;
    if (a->additive != b->additive) return a->additive ? 1 : -1;
    if (a->blended && a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    if (!a->blended && a->group != b->group) return a->group < b->group ? -1 : 1;
    return (a->order > b->order) - (a->order < b->order);
}

/* What the sort orders an item by, small: the items are moved once, after it, not at every
 * swap (an item is about 200 bytes; 16,000 see-through sprites sorted whole were most of a
 * frame). */

static int compare_keys(const void *lhs, const void *rhs)
{
    const sort_key_t *a = (const sort_key_t *)lhs, *b = (const sort_key_t *)rhs;
    if (a->blended != b->blended) return a->blended ? 1 : -1;
    if (a->additive != b->additive) return a->additive ? 1 : -1;
    if (a->blended && a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    if (!a->blended && a->group != b->group) return a->group < b->group ? -1 : 1;
    return (a->order > b->order) - (a->order < b->order);
}


/* A draw's items in drawing order (compare_keys'): opaque first, alike ones together;
 * then the see-through ones, farthest first; the added ones last. */
static void sort_items(int first, int count)
{
    int i;
    if (count < 2) return;
    if (count > sorting.capacity) {
        sort_key_t *keys = (sort_key_t *)realloc(sorting.keys, sizeof(sort_key_t) * (size_t)count);
        item_t *moved = keys != NULL ? (item_t *)realloc(sorting.moved, sizeof(item_t) * (size_t)count) : NULL;
        if (keys != NULL) sorting.keys = keys;
        if (moved == NULL) { /* out of memory: sorted whole, slower */
            qsort(&frame.items[first], (size_t)count, sizeof(item_t), compare_items);
            return;
        }
        sorting.moved = moved;
        sorting.capacity = count;
    }
    for (i = 0; i < count; i++) {
        const item_t *item = &frame.items[first + i];
        sort_key_t *key = &sorting.keys[i];
        key->depth = item->depth;
        key->order = item->order;
        key->group = item->group;
        key->item = first + i;
        key->blended = item->blended;
        key->additive = item->additive;
    }
    qsort(sorting.keys, (size_t)count, sizeof(sort_key_t), compare_keys);
    for (i = 0; i < count; i++) sorting.moved[i] = frame.items[sorting.keys[i].item];
    memcpy(&frame.items[first], sorting.moved, sizeof(item_t) * (size_t)count);
}

/* A text on the stage (its kind's place_3d, wgf_gfx_text.c), culled by the box around its
 * block: always a see-through item (its glyphs' edges blend), sorted by where it is. libwgt's. */
static void visit_text(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world, void *user)
{
    const walk_models_t *w = (const walk_models_t *)user;
    const wgf_gfx_priv_actor_kind_t *kind;
    stage_draw_t *draw = &frame.draws[frame.draw_count];
    wgf_mat4_t plane;
    wgf_vec3_t corners[4], lo, hi;
    item_t *item;
    int k;
    if (actor_ptr->type != WGF_ACTOR_KIND_TEXT) return;
    kind = wgf_gfx_priv_actor_get_kind(WGF_ACTOR_KIND_TEXT); /* set: there is text */
    if (kind == NULL || kind->place_3d == NULL || !kind->place_3d(actor_ptr, *world, &w->sprite_view, &plane, corners)) {
        return;
    }
    lo = hi = corners[0];
    for (k = 1; k < 4; k++) {
        lo = wgf_vec3_make(fminf(lo.x, corners[k].x), fminf(lo.y, corners[k].y), fminf(lo.z, corners[k].z));
        hi = wgf_vec3_make(fmaxf(hi.x, corners[k].x), fmaxf(hi.y, corners[k].y), fmaxf(hi.z, corners[k].z));
    }
    if (w->culling && outside((const float(*)[4])w->planes, lo, hi)) return;
    if (!grow((void **)&frame.texts, &frame.text_capacity, frame.text_count + 1, sizeof(text_record_t))) return;
    item = new_item();
    if (item == NULL) return;
    frame.texts[frame.text_count].actor = actor;
    frame.texts[frame.text_count].plane = plane;
    item->text = frame.text_count++;
    item->blended = true;
    item->depth = wgf_vec3_dot(wgf_vec3_sub(wgf_mat4_get_translation(*world), w->eye), w->forward);
    item->order = frame.item_count - draw->first;
    item->group = (uint64_t)item->order; /* alone */
    frame.item_count++;
}

/* Each sorted text's glyphs into a side layer of its own, through the draw's camera, the
 * layer drawn in its place among the see-through items. */
static void record_texts(stage_draw_t *draw)
{
    const wgf_gfx_priv_actor_kind_t *kind = wgf_gfx_priv_actor_get_kind(WGF_ACTOR_KIND_TEXT);
    int i;
    for (i = draw->first; i < draw->first + draw->count; i++) {
        item_t *item = &frame.items[i];
        const text_record_t *text;
        const wgf_gfx_priv_actor_t *actor_ptr;
        if (item->text < 0) continue;
        text = &frame.texts[item->text];
        actor_ptr = wgf_gfx_priv_actor_of(text->actor);
        item->text = -1;
        if (actor_ptr == NULL || kind == NULL || kind->draw_3d == NULL) continue;
        item->text = wgf_gfx_priv_render_begin_side_layer(draw->view_proj.m);
        if (item->text < 0) continue;
        kind->draw_3d(actor_ptr, &text->plane);
        wgf_gfx_priv_render_end_side_layer();
    }
}

static void replay(int index);

/* `stage` seen through `camera`, recorded for the frame's end. */
static void draw_through(wgf_actor_t stage, wgf_actor_t camera)
{
    const wgf_gfx_priv_actor_t *stage_ptr = stage_of(stage);
    float x, y, width, height, ambient[4];
    walk_models_t w;
    stage_draw_t *draw;
    wgf_mat4_t camera_world;

    if (stage_ptr == NULL || wgf_actor_get_kind(camera) != WGF_ACTOR_KIND_CAMERA3D || !wgf_gfx_priv_is_in_frame()) {
        return;
    }
    if (!grow((void **)&frame.draws, &frame.draw_capacity, frame.draw_count + 1, sizeof(stage_draw_t))) return;
    draw = &frame.draws[frame.draw_count];
    memset(draw, 0, sizeof(*draw));
    draw->shapes = -1;
    wgf_gfx_priv_render_get_visible(&x, &y, &width, &height);
    draw->view_proj = wgf_gfx_priv_camera3d_view_projection(camera, height > 0.0f ? width / height : 1.0f,
                                                            &draw->camera_position);
    draw->clip_proj = wgf_gfx_priv_render_clip(draw->view_proj);
    draw->flipped = wgf_gfx_priv_render_is_flipped();
    draw->pass = wgf_gfx_priv_render_get_pass();
    draw->stage = stage;
    wgf_gfx_priv_render_get_visible_viewport(draw->viewport);
    stage_ptr = stage_of(stage);
    linear_color(stage_ptr->as.stage3d.ambient_color, ambient);
    draw->ambient[0] = ambient[0] * stage_ptr->as.stage3d.ambient_intensity;
    draw->ambient[1] = ambient[1] * stage_ptr->as.stage3d.ambient_intensity;
    draw->ambient[2] = ambient[2] * stage_ptr->as.stage3d.ambient_intensity;
    draw->tonemap = stage_ptr->as.stage3d.tonemap;
    draw->exposure = stage_ptr->as.stage3d.exposure;
    draw->environment = stage_ptr->as.stage3d.environment;
    draw->environment_intensity = stage_ptr->as.stage3d.environment_intensity;
    draw->environment_rotation = stage_ptr->as.stage3d.environment_rotation;
    draw->background = stage_ptr->as.stage3d.background;
    draw->background_blur = stage_ptr->as.stage3d.background_blur;
    draw->background_intensity = draw->background == draw->environment ? draw->environment_intensity : 1.0f;
    draw->background_rotation = draw->background == draw->environment ? draw->environment_rotation : 0.0f;
    w.culling = stage_ptr->as.stage3d.culling;
    frustum_planes(&draw->view_proj, w.planes);
    camera_world = wgf_gfx_priv_actor_get_world_matrix(camera);
    w.eye = draw->camera_position;
    w.forward = wgf_vec3_normalize(wgf_mat4_transform_direction(camera_world, wgf_vec3_make(0.0f, 0.0f, -1.0f)));
    w.right = wgf_vec3_normalize(wgf_mat4_transform_direction(camera_world, wgf_vec3_make(1.0f, 0.0f, 0.0f)));
    w.up = wgf_vec3_normalize(wgf_mat4_transform_direction(camera_world, wgf_vec3_make(0.0f, 1.0f, 0.0f)));
    { /* the camera as the shadow fits see it */
        const wgf_gfx_priv_camera3d_t *lens = &wgf_gfx_priv_actor_of(camera)->as.camera3d;
        draw->shadow_camera.position = draw->camera_position;
        draw->shadow_camera.forward = w.forward;
        draw->shadow_camera.up =
            wgf_vec3_normalize(wgf_mat4_transform_direction(camera_world, wgf_vec3_make(0.0f, 1.0f, 0.0f)));
        draw->shadow_camera.orthographic = lens->orthographic;
        draw->shadow_camera.fov = lens->fov;
        draw->shadow_camera.ortho_height = lens->ortho_height;
        draw->shadow_camera.near_z = lens->near_z;
        draw->shadow_camera.aspect = height > 0.0f ? width / height : 1.0f;
    }

    /* its lights first, every model lit by the same list; then its models */
    draw->first_light = frame.light_count;
    walk(stage, visit_light, NULL);
    draw = &frame.draws[frame.draw_count];
    draw->light_count = frame.light_count - draw->first_light;
    draw->first = frame.item_count;
    walk(stage, visit_model, &w);
    walk(stage, visit_emitter, &w);
    /* its sprites: the depth-tested ones grouped into one item, the added ones into another,
       each blended one an item of its own until the sort has placed it */
    w.sprite_view = wgf_gfx_priv_sprite_view_of(camera_world);
    w.sprite_camera = -1;
    frame.sprite_count = 0;
    walk(stage, visit_sprite, &w);
    if (frame.sprite_count > 0) {
        sprite_pass_item(false, w.sprite_camera);
        sprite_pass_item(true, w.sprite_camera);
    }
    frame.text_count = 0;
    walk(stage, visit_text, &w);
    draw = &frame.draws[frame.draw_count];
    draw->count = frame.item_count - draw->first;
    sort_items(draw->first, draw->count);
    if (frame.sprite_count > 0) record_sprites(draw, w.sprite_camera);
    walk(stage, visit_shape, NULL); /* immediate mode: they move nothing in the pool */
    draw = &frame.draws[frame.draw_count];
    if (draw->shapes >= 0) wgf_gfx_priv_render_end_side_layer();
    if (frame.text_count > 0) record_texts(draw);
    if (wgf_gfx_priv_render_add_command(replay, frame.draw_count)) frame.draw_count++;
}

void wgf_stage3d_draw(wgf_actor_t stage)
{
    draw_through(stage, wgf_stage3d_get_camera(stage));
}

void wgf_stage3d_draw_with(wgf_actor_t stage, wgf_actor_t camera)
{
    draw_through(stage, camera);
}

/* ------------------------------------------------------------ the replay ---- */

static sg_image one_pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a, sg_view *view)
{
    const uint8_t pixel[4] = {r, g, b, a};
    sg_image_desc desc;
    sg_view_desc view_desc;
    sg_image image;
    memset(&desc, 0, sizeof(desc));
    desc.width = desc.height = 1;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = pixel;
    desc.data.mip_levels[0].size = sizeof(pixel);
    image = sg_make_image(&desc);
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.texture.image = image;
    *view = sg_make_view(&view_desc);
    return image;
}

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

static void ensure_ready(void)
{
    if (frame.ready) return;
    frame.shader = sg_make_shader(model_shader_desc(shader_backend()));
    frame.white_image = one_pixel(255, 255, 255, 255, &frame.white_view);
    frame.flat_normal_image = one_pixel(128, 128, 255, 255, &frame.flat_normal_view);
    frame.sampler = wgf_gfx_priv_texture_sampler(WGF_TEXTURE_WRAP_REPEAT, WGF_TEXTURE_WRAP_REPEAT,
                                                 WGF_TEXTURE_FILTER_LINEAR, true);
    { /* what the shader's shadow map is when nothing casts: never read, but bound */
        sg_image_desc image;
        sg_sampler_desc sampler;
        sg_view_desc view;
        memset(&image, 0, sizeof(image));
        image.type = SG_IMAGETYPE_ARRAY;
        image.usage.depth_stencil_attachment = true;
        image.width = image.height = 1;
        image.num_slices = 1;
        image.pixel_format = SG_PIXELFORMAT_DEPTH;
        image.sample_count = 1; /* never the window's: an array can't be multisampled */
        frame.no_shadow_image = sg_make_image(&image);
        memset(&view, 0, sizeof(view));
        view.texture.image = frame.no_shadow_image;
        frame.no_shadow_view = sg_make_view(&view);
        memset(&sampler, 0, sizeof(sampler));
        sampler.min_filter = sampler.mag_filter = SG_FILTER_LINEAR;
        sampler.wrap_u = sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler.compare = SG_COMPAREFUNC_LESS_EQUAL;
        frame.no_shadow_sampler = sg_make_sampler(&sampler);
    }
    { /* what the shader's environment is with none: never read, but bound */
        static const uint8_t black[6 * 4] = {0};
        sg_image_desc image;
        sg_view_desc view;
        memset(&image, 0, sizeof(image));
        image.type = SG_IMAGETYPE_CUBE;
        image.width = image.height = 1;
        image.pixel_format = SG_PIXELFORMAT_RGBA8;
        image.data.mip_levels[0].ptr = black;
        image.data.mip_levels[0].size = sizeof(black);
        frame.no_environment_image = sg_make_image(&image);
        memset(&view, 0, sizeof(view));
        view.texture.image = frame.no_environment_image;
        frame.no_environment_view = sg_make_view(&view);
    }
    frame.ready = true;
}

/* The pipeline for see-through or not, double sided or not, and a placement that
 * mirrors (its triangles' winding turned), made the first time it's needed. */
static sg_pipeline pipeline(bool blended, bool double_sided, bool mirrored)
{
    const int key = (blended ? 1 : 0) | (double_sided ? 2 : 0) | (mirrored ? 4 : 0);
    if (frame.pipelines[key].id == SG_INVALID_ID) {
        sg_pipeline_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.shader = frame.shader;
        desc.layout.buffers[0].stride = (int)(sizeof(float) * WGF_GFX_PRIV_MESH_VERTEX_FLOATS);
        desc.layout.attrs[ATTR_model_position].format = SG_VERTEXFORMAT_FLOAT3;
        desc.layout.attrs[ATTR_model_position].offset = 0;
        desc.layout.attrs[ATTR_model_normal].format = SG_VERTEXFORMAT_FLOAT3;
        desc.layout.attrs[ATTR_model_normal].offset = 12;
        desc.layout.attrs[ATTR_model_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_model_texcoord0].offset = 24;
        desc.layout.attrs[ATTR_model_texcoord1].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_model_texcoord1].offset = 32;
        desc.layout.attrs[ATTR_model_tangent].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_model_tangent].offset = 40;
        desc.layout.attrs[ATTR_model_color0].format = SG_VERTEXFORMAT_FLOAT4;
        desc.layout.attrs[ATTR_model_color0].offset = 56;
        desc.index_type = SG_INDEXTYPE_UINT32;
        desc.cull_mode = double_sided ? SG_CULLMODE_NONE : SG_CULLMODE_BACK;
        desc.face_winding = mirrored ? SG_FACEWINDING_CW : SG_FACEWINDING_CCW;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = !blended;
        desc.colors[0].write_mask = SG_COLORMASK_RGBA;
        if (blended) {
            desc.colors[0].blend.enabled = true;
            desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        }
        frame.pipelines[key] = sg_make_pipeline(&desc);
    }
    return frame.pipelines[key];
}

/* The texture a material slot draws with: its own once it can be drawn (the
 * placeholder's once it has FAILED), else `fallback`, which a texture still loading
 * draws as. */
static sg_view texture_view(const wgf_gfx_priv_material_texture_t *texture, sg_view fallback, sg_sampler *sampler)
{
    sg_view view;
    int width, height;
    bool placeholder;
    if (texture->texture != 0 &&
        wgf_gfx_priv_texture_get_binding(texture->texture, &view, sampler, &width, &height, &placeholder)) {
        *sampler = wgf_gfx_priv_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
        return view;
    }
    *sampler = frame.sampler;
    return fallback;
}

/* Items `first` to `first + count - 1`, alike (same_group), as one draw: their records
 * are side by side in the instance texture. `lit`: the draw's environment, READY, in
 * `env`. */
/* The shadow maps `draw` is shaded with: its own, or, seeing the stage the shadows were
 * fit for, theirs. */
static const wgf_gfx_priv_shadow_binding_t *shadow_of(const stage_draw_t *draw)
{
    const stage_draw_t *fit = frame.shadowed >= 0 && frame.shadowed < frame.draw_count ? &frame.draws[frame.shadowed]
                                                                                       : NULL;
    if (draw->shadow.valid || fit == NULL || fit->stage != draw->stage) return &draw->shadow;
    return &fit->shadow;
}

static void draw_run(const stage_draw_t *draw, int first, int count, bool lit, const wgf_gfx_priv_env_lighting_t *env)
{
    const wgf_gfx_priv_shadow_binding_t *shadow = shadow_of(draw);
    const item_t *item = &frame.items[first];
    const wgf_gfx_priv_material_t *material = wgf_gfx_priv_material_get(item->material);
    wgf_gfx_priv_mesh_primitive_t primitive;
    vs_params_t vs;
    fs_params_t fs;
    fs_scene_t scene;
    fs_lights_t lights;
    sg_bindings bindings;
    int t, i;

    if (material == NULL || !wgf_gfx_priv_mesh_get_primitive(item->mesh, item->primitive, &primitive)) return;
    sg_apply_pipeline(pipeline(item->blended, material->double_sided, item->mirrored != draw->flipped));
    memset(&bindings, 0, sizeof(bindings));
    bindings.vertex_buffers[0] = primitive.vertices;
    bindings.index_buffer = primitive.indices;
    for (t = 0; t < WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT; t++) {
        bindings.views[t] = texture_view(&material->textures[t],
                                         t == WGF_GFX_PRIV_MATERIAL_TEXTURE_NORMAL ? frame.flat_normal_view
                                                                                   : frame.white_view,
                                         &bindings.samplers[t]);
    }
    bindings.views[VIEW_shadow_tex] = shadow->valid ? shadow->map : frame.no_shadow_view;
    bindings.samplers[SMP_shadow_smp] = shadow->valid ? shadow->sampler : frame.no_shadow_sampler;
    bindings.views[VIEW_env_tex] = lit ? env->cube : frame.no_environment_view;
    bindings.samplers[SMP_env_smp] = lit ? env->cube_sampler : frame.sampler;
    bindings.views[VIEW_brdf_tex] = lit ? env->brdf : frame.white_view;
    bindings.samplers[SMP_brdf_smp] = lit ? env->brdf_sampler : frame.sampler;
    bindings.views[VIEW_instance_tex] = frame.instance_view;
    bindings.samplers[SMP_instance_smp] = frame.instance_sampler;
    sg_apply_bindings(&bindings);

    memset(&vs, 0, sizeof(vs));
    memcpy(vs.view_proj, draw->clip_proj.m, sizeof(vs.view_proj));
    vs.instance_base[0] = (float)item->record; /* its run's records follow it */
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(vs));

    memset(&fs, 0, sizeof(fs));
    for (i = 0; i < 4; i++) fs.u_base_color[i] = material->base_color[i]; /* the tint is the record's */
    fs.u_emissive[0] = material->emissive[0];
    fs.u_emissive[1] = material->emissive[1];
    fs.u_emissive[2] = material->emissive[2];
    fs.u_emissive[3] = material->normal_scale;
    fs.u_pbr[0] = material->metallic;
    fs.u_pbr[1] = material->roughness;
    fs.u_pbr[2] = material->occlusion_strength;
    fs.u_pbr[3] = material->shading == WGF_MATERIAL_SHADING_PBR ? 1.0f : 0.0f;
    fs.u_material[0] = material->alpha_mode == WGF_ALPHA_MODE_MASK ? material->alpha_cutoff : 0.0f;
    fs.u_material[1] = (float)item->light_count;
    fs.u_material[2] = item->receiving ? 1.0f : 0.0f;
    for (t = 0; t < WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT; t++) {
        float m[6];
        wgf_gfx_priv_material_uv_matrix(&material->textures[t], m);
        fs.u_uv_row0[t][0] = m[0];
        fs.u_uv_row0[t][1] = m[1];
        fs.u_uv_row0[t][2] = m[2];
        fs.u_uv_row0[t][3] = (float)material->textures[t].texcoord;
        fs.u_uv_row1[t][0] = m[3];
        fs.u_uv_row1[t][1] = m[4];
        fs.u_uv_row1[t][2] = m[5];
    }
    sg_apply_uniforms(UB_fs_params, &SG_RANGE(fs));

    memset(&scene, 0, sizeof(scene));
    scene.u_camera_pos[0] = draw->camera_position.x;
    scene.u_camera_pos[1] = draw->camera_position.y;
    scene.u_camera_pos[2] = draw->camera_position.z;
    memcpy(scene.u_ambient, draw->ambient, sizeof(draw->ambient));
    scene.u_tonemap[0] = (float)draw->tonemap;
    scene.u_tonemap[1] = exp2f(draw->exposure);
    if (lit) {
        scene.u_env[0] = draw->environment_intensity;
        scene.u_env[1] = env->max_lod;
        scene.u_env[2] = cosf(draw->environment_rotation);
        scene.u_env[3] = sinf(draw->environment_rotation);
        for (i = 0; i < 9; i++) memcpy(scene.u_sh[i], env->sh.c[i], sizeof(env->sh.c[i]));
    }
    for (i = 0; shadow->valid && i < shadow->count; i++) {
        const wgf_gfx_priv_shadow_slot_t *slot = &shadow->slots[i];
        memcpy(scene.u_shadow_mat[i], slot->view_proj.m, sizeof(scene.u_shadow_mat[i]));
        scene.u_shadow_params[i][0] = slot->texel;
        scene.u_shadow_params[i][1] = slot->texel_world;
        scene.u_shadow_params[i][2] = slot->bias_constant;
        scene.u_shadow_params[i][3] = slot->bias_slope;
        scene.u_shadow_tint[i][0] = slot->tint[0];
        scene.u_shadow_tint[i][1] = slot->tint[1];
        scene.u_shadow_tint[i][2] = slot->tint[2];
        scene.u_shadow_tint[i][3] = slot->bias_scale;
        scene.u_shadow_extra[i][0] = slot->strength;
        scene.u_shadow_extra[i][1] = slot->near_z;
        scene.u_shadow_extra[i][2] = slot->far_z;
        scene.u_shadow_extra[i][3] = slot->perspective ? 1.0f : 0.0f;
    }
    scene.u_shadow_map[0] = shadow->flipped ? 1.0f : 0.0f;
    sg_apply_uniforms(UB_fs_scene, &SG_RANGE(scene));

    memset(&lights, 0, sizeof(lights));
    for (i = 0; i < item->light_count; i++) {
        const wgf_gfx_priv_stage3d_light_t *l = &frame.lights[draw->first_light + item->lights[i]];
        lights.u_light_pos_range[i][0] = l->position.x;
        lights.u_light_pos_range[i][1] = l->position.y;
        lights.u_light_pos_range[i][2] = l->position.z;
        lights.u_light_pos_range[i][3] = l->range;
        lights.u_light_dir_type[i][0] = l->direction.x;
        lights.u_light_dir_type[i][1] = l->direction.y;
        lights.u_light_dir_type[i][2] = l->direction.z;
        lights.u_light_dir_type[i][3] = (float)l->type;
        lights.u_light_radiance[i][0] = l->radiance.x;
        lights.u_light_radiance[i][1] = l->radiance.y;
        lights.u_light_radiance[i][2] = l->radiance.z;
        lights.u_light_spot[i][0] = l->cos_inner;
        lights.u_light_spot[i][1] = l->cos_outer;
        lights.u_light_spot[i][2] = shadow->valid && l->shadow_slot >= 0 ? (float)l->shadow_slot : -1.0f;
    }
    sg_apply_uniforms(UB_fs_lights, &SG_RANGE(lights));
    sg_draw(0, primitive.index_count, count);
    frame.draw_calls++;
}

/* The items from `i` to before `end`, each run of alike ones a draw; those kept for the
 * shadow pass alone (a run of their own) are skipped. */
static void draw_items(const stage_draw_t *draw, int i, int end, bool lit, const wgf_gfx_priv_env_lighting_t *env)
{
    while (i < end) {
        int last = i;
        if (frame.items[i].particles >= 0) { /* an emitter's, through the same viewport */
            particle_hooks.draw(frame.items[i].particles); /* set: there is an emitter */
            i++;
            continue;
        }
        if (frame.items[i].text >= 0) { /* a text's side layer, through the same viewport */
            wgf_gfx_priv_render_draw_side_layer(frame.items[i].text);
            sg_apply_viewport(draw->viewport[0], draw->viewport[1], draw->viewport[2], draw->viewport[3], true);
            i++;
            continue;
        }
        if (frame.items[i].sprite_batch >= 0) { /* sprites: their batches, each after the first applying what differs */
            int k;
            for (k = 0; k < frame.items[i].sprite_batches; k++) {
                wgf_gfx_priv_sprite_batch_draw(frame.items[i].sprite_batch + k, k > 0);
            }
            i++;
            continue;
        }
        while (last + 1 < end && same_group(&frame.items[i], &frame.items[last + 1])) last++;
        if (!frame.items[i].shadow_only) draw_run(draw, i, last - i + 1, lit, env);
        i = last + 1;
    }
}

static void replay(int index)
{
    const stage_draw_t *draw;
    wgf_gfx_priv_env_lighting_t env;
    bool lit;
    int i;
    if (index < 0 || index >= frame.draw_count) return;
    ensure_ready();
    draw = &frame.draws[index];
    sg_apply_viewport(draw->viewport[0], draw->viewport[1], draw->viewport[2], draw->viewport[3], true);
    if (draw->background != 0 && environment_hooks.background != NULL) {
        environment_hooks.background(draw->background, draw->clip_proj, draw->background_blur,
                                     draw->background_intensity, draw->background_rotation, draw->tonemap,
                                     draw->exposure);
    }
    memset(&env, 0, sizeof(env));
    lit = draw->environment != 0 && draw->environment_intensity > 0.0f && environment_hooks.lighting != NULL &&
          environment_hooks.lighting(draw->environment, &env);
    i = draw->first; /* the opaque ones first, then the shapes, then the see-through ones */
    while (i < draw->first + draw->count && !frame.items[i].blended) i++;
    if (frame.instances_written) draw_items(draw, draw->first, i, lit, &env); /* else the records didn't fit */
    if (draw->shapes >= 0) { /* sokol_gl's layer, through the same viewport */
        wgf_gfx_priv_render_draw_side_layer(draw->shapes);
        sg_apply_viewport(draw->viewport[0], draw->viewport[1], draw->viewport[2], draw->viewport[3], true);
    }
    if (frame.instances_written) draw_items(draw, i, draw->first + draw->count, lit, &env);
}

/* ------------------------------------------------------ the instance records ---- */

/* One item's record: its placement and normals' matrices as rows (what the shader reads,
 * and what lets the placement cost three texels, not four), its tint, and its tangents'
 * handedness. */
static void write_instance(float *r, const item_t *item)
{
    const wgf_mat4_t normal = wgf_mat4_transpose(wgf_mat4_invert(item->world));
    int row, col;
    for (row = 0; row < 3; row++) {
        for (col = 0; col < 4; col++) {
            r[row * 4 + col] = item->world.m[col * 4 + row];
            r[12 + row * 4 + col] = normal.m[col * 4 + row];
        }
    }
    memcpy(&r[24], item->tint, sizeof(item->tint)); /* linear already */
    r[28] = 0.0f;
    r[29] = item->mirrored ? -1.0f : 1.0f;
    r[30] = r[31] = 0.0f;
}

/* The texture to hold `rows` rows of records, grown by doubling up to the GPU's limit;
 * false when they won't fit. */
static bool ensure_instance_texture(int rows)
{
    const int max_rows = sg_query_limits().max_image_size_2d;
    int height = frame.instance_rows > 0 ? frame.instance_rows : 4;
    sg_image_desc desc;
    sg_view_desc view;
    if (rows <= frame.instance_rows) return true;
    if (rows > max_rows) return false;
    while (height < rows) height *= 2;
    if (height > max_rows) height = max_rows;
    if (frame.instance_image.id != SG_INVALID_ID) {
        sg_destroy_view(frame.instance_view);
        sg_destroy_image(frame.instance_image);
    }
    memset(&desc, 0, sizeof(desc));
    desc.width = INSTANCE_TEXTURE_WIDTH;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
    desc.usage.write_transient = true;
    desc.label = "wgf-instances";
    frame.instance_image = sg_make_image(&desc);
    memset(&view, 0, sizeof(view));
    view.texture.image = frame.instance_image;
    frame.instance_view = sg_make_view(&view);
    frame.instance_rows = height;
    if (frame.instance_sampler.id == SG_INVALID_ID) {
        sg_sampler_desc sampler;
        memset(&sampler, 0, sizeof(sampler));
        sampler.min_filter = sampler.mag_filter = SG_FILTER_NEAREST;
        sampler.wrap_u = sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler.label = "wgf-instances";
        frame.instance_sampler = sg_make_sampler(&sampler);
    }
    return true;
}

/* The part's flush, before the frame's passes (the shadow part's among them): each model
 * part's record, numbered in drawing order so a run's are consecutive, written whole rows at
 * a time, the tail unwritten. Sprites and particles take none (a field of 16,000 sprites
 * would otherwise upload 2 MB of records nothing reads, a frame). */
static void flush(void)
{
    int rows, wanted, records = 0, i;
    sg_write_image_desc write;
    for (i = 0; i < frame.item_count; i++) {
        frame.items[i].record = frame.items[i].mesh != 0 ? records++ : -1;
    }
    if (records == 0) { /* no model part: nothing reads a record */
        frame.instances_written = frame.item_count > 0;
        return;
    }
    rows = (records + INSTANCES_PER_ROW - 1) / INSTANCES_PER_ROW;
    wanted = rows * INSTANCES_PER_ROW;
    if (wanted > frame.instance_capacity) {
        float *grown = (float *)realloc(frame.instances, sizeof(float) * INSTANCE_FLOATS * (size_t)wanted);
        if (grown == NULL) {
            wgf_log_error("wgf_gfx_stage: out of memory drawing a stage");
            return;
        }
        frame.instances = grown;
        frame.instance_capacity = wanted;
    }
    if (!ensure_instance_texture(rows)) {
        if (!frame.instance_overflow_logged) {
            wgf_log_error("wgf_gfx_stage: %d parts of models in a frame, more than the instance texture holds; none "
                          "drawn", records);
            frame.instance_overflow_logged = true;
        }
        return;
    }
    for (i = 0; i < frame.item_count; i++) {
        const item_t *item = &frame.items[i];
        if (item->record >= 0) write_instance(&frame.instances[(size_t)item->record * INSTANCE_FLOATS], item);
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = frame.instances;
    write.src.data.size = sizeof(float) * INSTANCE_FLOATS * (size_t)wanted;
    write.src.bytes_per_row = (int)sizeof(float) * 4 * INSTANCE_TEXTURE_WIDTH;
    write.src.bytes_per_slice = (int)sizeof(float) * 4 * INSTANCE_TEXTURE_WIDTH * rows;
    write.dst.image = frame.instance_image;
    write.size.width = INSTANCE_TEXTURE_WIDTH;
    write.size.height = rows;
    write.size.num_slices = 1;
    sg_write_image_transient(&write);
    frame.instances_written = true;
}

/* ------------------------------------------------- the shadow pass's view ---- */

/* The draw the frame's shadows are fit around: the first with a casting light into the
 * screen, else into a target (a mirror, a minimap). Another draw of the same stage reads
 * its maps where they reach (shadow_of). */
bool wgf_gfx_priv_stage3d_shadow_frame(wgf_gfx_priv_shadow_frame_t *out)
{
    int d, i, s;
    frame.shadowed = -1;
    for (d = 0; d < frame.draw_count * 2; d++) {
        stage_draw_t *draw = &frame.draws[d % frame.draw_count];
        bool casters = false, receivers = false;
        if (draw->shadow_count == 0 || (d < frame.draw_count) != (draw->pass == 0)) continue;
        for (i = draw->first; i < draw->first + draw->count && !(casters && receivers); i++) {
            casters = casters || (frame.items[i].casting && !frame.items[i].blended);
            receivers = receivers || (frame.items[i].receiving && !frame.items[i].shadow_only);
        }
        if (!casters || !receivers) return false; /* a map nothing would show */
        out->camera = draw->shadow_camera;
        out->light_count = draw->shadow_count;
        for (s = 0; s < draw->shadow_count; s++) out->lights[s] = &frame.lights[draw->shadow_lights[s]];
        out->first = draw->first;
        out->count = draw->count;
        out->binding = &draw->shadow;
        frame.shadowed = d % frame.draw_count;
        return true;
    }
    return false;
}

bool wgf_gfx_priv_stage3d_caster(int index, wgf_vec3_t *lo, wgf_vec3_t *hi, wgf_gfx_priv_mesh_primitive_t *primitive,
                                 wgf_material_t *material)
{
    const item_t *item = index >= 0 && index < frame.item_count ? &frame.items[index] : NULL;
    if (item == NULL || !item->casting || item->blended) return false; /* see-through parts don't cast */
    *lo = item->box_lo;
    *hi = item->box_hi;
    *material = item->material;
    return wgf_gfx_priv_mesh_get_primitive(item->mesh, item->primitive, primitive);
}

int wgf_gfx_priv_stage3d_record(int item)
{
    return item >= 0 && item < frame.item_count ? frame.items[item].record : -1;
}

bool wgf_gfx_priv_stage3d_same_caster(int a, int b)
{
    const item_t *x = &frame.items[a], *y = &frame.items[b];
    return x->mesh == y->mesh && x->primitive == y->primitive && x->material == y->material;
}

bool wgf_gfx_priv_stage3d_instances(sg_view *view, sg_sampler *sampler)
{
    *view = frame.instance_view;
    *sampler = frame.instance_sampler;
    return frame.instances_written;
}

/* For tests: the draws the last frame's shading pass made (a run of alike parts one). */
int wgf_gfx_priv_stage3d_get_draw_calls(void)
{
    return frame.last_draw_calls;
}

/* For tests: how many parts the frame's stage draws drew (after culling), and how many
 * lights the last one's first part was lit by. */
int wgf_gfx_priv_stage3d_get_item_count(void)
{
    return frame.item_count;
}

int wgf_gfx_priv_stage3d_get_item_lights(int item)
{
    return item >= 0 && item < frame.item_count ? frame.items[item].light_count : -1;
}
