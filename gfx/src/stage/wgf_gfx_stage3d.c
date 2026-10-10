#include "wgf_stage3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "draw/wgf_gfx_draw3d_priv.h"
#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_environment_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "stage/wgf_gfx_shadow_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"

/* the shader's sources and reflection for the build's backend; a headless build's
   dummy backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_model.glsl.h"

/* Stages: libwgt's scene, trimmed to what milestone 2 has so far (no instancing,
 * skinning, picking, sprites, or particles: their steps bring them). A
 * draw walks the stage's tree as it is now -- its lights (the first 4 that cast shadows
 * each given a slot of the frame's shadow map), then its models, each part of a model's
 * mesh an item: placed, culled against the camera's view (a caster out of view kept for
 * the shadow pass alone when its shadow reaches the view), given its lights, and the
 * see-through ones sorted back to front -- and records the items, as a command of the
 * frame's (render/wgf_gfx_render_commands.c). Before the frame's pass the shadow part, when
 * a light casts, draws the first such draw's casters into its map (wgf_gfx_shadow.c); then
 * the command draws the items with sokol_gfx, between the immediate mode drawn before and
 * after the stage, its background first. An environment's lighting and background are the
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
    float depth;     /* along the camera's view, for the sort */
    int order;       /* as found, so equal depths keep it */
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
    wgf_mat4_t view_proj;
    wgf_vec3_t camera_position;
    float ambient[3]; /* linear, times its intensity */
    int tonemap;
    float exposure;
    int viewport[4]; /* the visible area, in the framebuffer's pixels */
    wgf_gfx_priv_shadow_camera_t shadow_camera; /* what its shadows are fitted around */
    int shadow_count;                           /* its lights with a shadow slot */
    int shadow_lights[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS]; /* into the frame's lights, by slot */
    wgf_gfx_priv_shadow_binding_t shadow;       /* the shadow pass's maps, valid once it drew them */
    wgf_handle_t environment, background;       /* 0: none */
    float environment_intensity, environment_rotation;
    float background_intensity, background_rotation, background_blur;
} stage_draw_t;

static struct {
    item_t *items;
    int item_count, item_capacity;
    stage_draw_t *draws;
    int draw_count, draw_capacity;
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
} frame;

static wgf_gfx_priv_environment_hooks_t environment_hooks; /* the environment part's, once installed */

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
}

static void stop(void)
{
    int i;
    end_frame();
    free(frame.items);
    free(frame.draws);
    free(frame.lights);
    free(frame.todo);
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

static const wgf_gfx_priv_actor_kind_t actor_kind = {stage_free, NULL};

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
        frame.item_count++;
    }
}

/* Opaque first, in the order found; then the see-through ones, farthest first. */
static int compare_items(const void *lhs, const void *rhs)
{
    const item_t *a = (const item_t *)lhs, *b = (const item_t *)rhs;
    if (a->blended != b->blended) return a->blended ? 1 : -1;
    if (a->blended && a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    return (a->order > b->order) - (a->order < b->order);
}

static void replay(int index);

void wgf_stage3d_draw(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *stage_ptr = stage_of(stage);
    const wgf_actor_t camera = wgf_stage3d_get_camera(stage);
    float x, y, width, height, ambient[4];
    wgf_platform_priv_presentation_t present;
    walk_models_t w;
    stage_draw_t *draw;
    wgf_mat4_t camera_world;

    if (stage_ptr == NULL || camera == 0 || !wgf_gfx_priv_is_in_frame()) return;
    if (!grow((void **)&frame.draws, &frame.draw_capacity, frame.draw_count + 1, sizeof(stage_draw_t))) return;
    draw = &frame.draws[frame.draw_count];
    memset(draw, 0, sizeof(*draw));
    draw->shapes = -1;
    wgf_gfx_priv_render_get_visible(&x, &y, &width, &height);
    draw->view_proj = wgf_gfx_priv_camera3d_view_projection(camera, height > 0.0f ? width / height : 1.0f,
                                                            &draw->camera_position);
    wgf_platform_priv_get_presentation(&present);
    draw->viewport[0] = (int)(present.offset_x + x * present.scale_x + 0.5f);
    draw->viewport[1] = (int)(present.offset_y + y * present.scale_y + 0.5f);
    draw->viewport[2] = (int)(width * present.scale_x + 0.5f);
    draw->viewport[3] = (int)(height * present.scale_y + 0.5f);
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
    draw = &frame.draws[frame.draw_count];
    draw->count = frame.item_count - draw->first;
    if (draw->count > 1) qsort(&frame.items[draw->first], (size_t)draw->count, sizeof(item_t), compare_items);
    walk(stage, visit_shape, NULL); /* immediate mode: they move nothing in the pool */
    draw = &frame.draws[frame.draw_count];
    if (draw->shapes >= 0) wgf_gfx_priv_render_end_side_layer();
    if (wgf_gfx_priv_render_add_command(replay, frame.draw_count)) frame.draw_count++;
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

/* `lit`: the draw's environment, READY, in `env`. */
static void draw_item(const stage_draw_t *draw, const item_t *item, bool lit, const wgf_gfx_priv_env_lighting_t *env)
{
    const wgf_gfx_priv_material_t *material = wgf_gfx_priv_material_get(item->material);
    const float det = wgf_mat4_determinant(item->world);
    wgf_gfx_priv_mesh_primitive_t primitive;
    vs_params_t vs;
    fs_params_t fs;
    fs_scene_t scene;
    fs_lights_t lights;
    sg_bindings bindings;
    int t, i;

    if (material == NULL || !wgf_gfx_priv_mesh_get_primitive(item->mesh, item->primitive, &primitive)) return;
    sg_apply_pipeline(pipeline(item->blended, material->double_sided, det < 0.0f));
    memset(&bindings, 0, sizeof(bindings));
    bindings.vertex_buffers[0] = primitive.vertices;
    bindings.index_buffer = primitive.indices;
    for (t = 0; t < WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT; t++) {
        bindings.views[t] = texture_view(&material->textures[t],
                                         t == WGF_GFX_PRIV_MATERIAL_TEXTURE_NORMAL ? frame.flat_normal_view
                                                                                   : frame.white_view,
                                         &bindings.samplers[t]);
    }
    bindings.views[VIEW_shadow_tex] = draw->shadow.valid ? draw->shadow.map : frame.no_shadow_view;
    bindings.samplers[SMP_shadow_smp] = draw->shadow.valid ? draw->shadow.sampler : frame.no_shadow_sampler;
    bindings.views[VIEW_env_tex] = lit ? env->cube : frame.no_environment_view;
    bindings.samplers[SMP_env_smp] = lit ? env->cube_sampler : frame.sampler;
    bindings.views[VIEW_brdf_tex] = lit ? env->brdf : frame.white_view;
    bindings.samplers[SMP_brdf_smp] = lit ? env->brdf_sampler : frame.sampler;
    sg_apply_bindings(&bindings);

    memset(&vs, 0, sizeof(vs));
    memcpy(vs.mvp, wgf_mat4_mul(draw->view_proj, item->world).m, sizeof(vs.mvp));
    memcpy(vs.model, item->world.m, sizeof(vs.model));
    memcpy(vs.normal_mat, wgf_mat4_transpose(wgf_mat4_invert(item->world)).m, sizeof(vs.normal_mat));
    vs.extra[0] = det < 0.0f ? -1.0f : 1.0f;
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(vs));

    memset(&fs, 0, sizeof(fs));
    for (i = 0; i < 4; i++) fs.u_base_color[i] = material->base_color[i] * item->tint[i];
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
    for (i = 0; draw->shadow.valid && i < draw->shadow.count; i++) {
        const wgf_gfx_priv_shadow_slot_t *slot = &draw->shadow.slots[i];
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
    scene.u_shadow_map[0] = draw->shadow.flipped ? 1.0f : 0.0f;
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
        lights.u_light_spot[i][2] = draw->shadow.valid && l->shadow_slot >= 0 ? (float)l->shadow_slot : -1.0f;
    }
    sg_apply_uniforms(UB_fs_lights, &SG_RANGE(lights));
    sg_draw(0, primitive.index_count, 1);
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
        environment_hooks.background(draw->background, draw->view_proj, draw->background_blur,
                                     draw->background_intensity, draw->background_rotation, draw->tonemap,
                                     draw->exposure);
    }
    memset(&env, 0, sizeof(env));
    lit = draw->environment != 0 && draw->environment_intensity > 0.0f && environment_hooks.lighting != NULL &&
          environment_hooks.lighting(draw->environment, &env);
    for (i = draw->first; i < draw->first + draw->count && !frame.items[i].blended; i++) {
        if (!frame.items[i].shadow_only) draw_item(draw, &frame.items[i], lit, &env);
    }
    if (draw->shapes >= 0) { /* sokol_gl's layer, through the same viewport */
        wgf_gfx_priv_render_draw_side_layer(draw->shapes);
        sg_apply_viewport(draw->viewport[0], draw->viewport[1], draw->viewport[2], draw->viewport[3], true);
    }
    for (; i < draw->first + draw->count; i++) draw_item(draw, &frame.items[i], lit, &env);
}

/* ------------------------------------------------- the shadow pass's view ---- */

bool wgf_gfx_priv_stage3d_shadow_frame(wgf_gfx_priv_shadow_frame_t *out)
{
    int d, i, s;
    for (d = 0; d < frame.draw_count; d++) {
        stage_draw_t *draw = &frame.draws[d];
        bool casters = false, receivers = false;
        if (draw->shadow_count == 0) continue;
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
        return true;
    }
    return false;
}

bool wgf_gfx_priv_stage3d_caster(int index, wgf_mat4_t *world, wgf_vec3_t *lo, wgf_vec3_t *hi,
                                 wgf_gfx_priv_mesh_primitive_t *primitive, wgf_material_t *material)
{
    const item_t *item = index >= 0 && index < frame.item_count ? &frame.items[index] : NULL;
    if (item == NULL || !item->casting || item->blended) return false; /* see-through parts don't cast */
    *world = item->world;
    *lo = item->box_lo;
    *hi = item->box_hi;
    *material = item->material;
    return wgf_gfx_priv_mesh_get_primitive(item->mesh, item->primitive, primitive);
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
