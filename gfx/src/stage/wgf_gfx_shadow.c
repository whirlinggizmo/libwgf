#include "stage/wgf_gfx_shadow_priv.h"

#include <math.h>
#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_model.h"

/* the shader's sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_depth.glsl.h"

/* Shadow maps, libwgt's wgt_gfx_shadow.c and its scene's depth pass (wgrender's
 * wgr_shadow): the light and model calls that turn them on and tune them, which install
 * the part, so a program that never casts links none of this (wgrender's lesson: its
 * setters in the light's file kept the module linked); the fits; the depth array, a layer a
 * casting light, and its comparison sampler; and the pass, as the frame flushes, drawing the
 * stage's casters into each layer, one caster a draw (libwgt batches them through its
 * instance records, step 11's). */

#define SHADOW_PULLBACK 50.0f /* world units the light's near plane is pulled back */
#define MAP_SIZE_MIN 256
#define MAP_SIZE_MAX 4096

static struct {
    sg_image map; /* a depth array: a layer a casting light */
    sg_view map_view;
    sg_view layer_attachment[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS];
    sg_sampler sampler;
    int size, layers; /* 0: none made yet */
    bool unsupported; /* the backend can't sample a depth array (said once) */
    sg_shader shader;
    sg_pipeline pipeline;
    sg_image white_image; /* what an alpha test reads without a texture */
    sg_view white_view;
    sg_sampler white_sampler;
    int draw_calls, last_draw_calls, last_layers;
} sm;

/* ------------------------------------------------------------------ the calls ---- */

static wgf_gfx_priv_actor_t *light_of_at(wgf_actor_t light, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(light, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_LIGHT ? actor_ptr : NULL;
}
#define light_of(...) light_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

static wgf_gfx_priv_actor_t *model_of_at(wgf_actor_t model, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(model, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL ? actor_ptr : NULL;
}
#define model_of(...) model_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

bool wgf_light_set_shadow_casting(wgf_actor_t light, bool casting)
{
    wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    if (actor_ptr == NULL) return false;
    if (casting && actor_ptr->as.light.type == WGF_LIGHT_TYPE_POINT) {
        wgf_log_warn("wgf_light_set_shadow_casting: a point light doesn't cast shadows (it would need six maps, "
                     "one each way)");
        return false;
    }
    if (casting) wgf_gfx_priv_shadow_install(); /* the first to cast brings shadows in */
    actor_ptr->as.light.shadow_casting = casting;
    return true;
}

bool wgf_light_is_shadow_casting(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL && actor_ptr->as.light.shadow_casting;
}

bool wgf_light_set_shadow_distance(wgf_actor_t light, float distance)
{
    wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    if (actor_ptr == NULL || !(distance > 0.0f) || !isfinite(distance)) return false;
    actor_ptr->as.light.shadow_distance = distance;
    return true;
}

float wgf_light_get_shadow_distance(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL ? actor_ptr->as.light.shadow_distance : 0.0f;
}

bool wgf_light_set_shadow_map_size(wgf_actor_t light, int size)
{
    wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    int clamped = MAP_SIZE_MIN;
    if (actor_ptr == NULL || size < 1) return false;
    while (clamped * 2 <= size && clamped < MAP_SIZE_MAX) clamped *= 2; /* a power of two, rounded down */
    actor_ptr->as.light.shadow_map_size = clamped;
    return true;
}

int wgf_light_get_shadow_map_size(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL ? actor_ptr->as.light.shadow_map_size : 0;
}

bool wgf_light_set_shadow_strength(wgf_actor_t light, float strength)
{
    wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    if (actor_ptr == NULL || isnan(strength)) return false;
    actor_ptr->as.light.shadow_strength = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
    return true;
}

float wgf_light_get_shadow_strength(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL ? actor_ptr->as.light.shadow_strength : 0.0f;
}

bool wgf_light_set_shadow_color(wgf_actor_t light, wgf_color_t color)
{
    wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.light.shadow_color = color;
    return true;
}

wgf_color_t wgf_light_get_shadow_color(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL ? actor_ptr->as.light.shadow_color : 0;
}

bool wgf_light_set_shadow_bias(wgf_actor_t light, float constant, float slope)
{
    wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    if (actor_ptr == NULL || !(constant >= 0.0f) || !(slope >= 0.0f) || !isfinite(constant) || !isfinite(slope)) {
        return false;
    }
    actor_ptr->as.light.shadow_bias_constant = constant;
    actor_ptr->as.light.shadow_bias_slope = slope;
    return true;
}

float wgf_light_get_shadow_bias_constant(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL ? actor_ptr->as.light.shadow_bias_constant : 0.0f;
}

float wgf_light_get_shadow_bias_slope(wgf_actor_t light)
{
    const wgf_gfx_priv_actor_t *actor_ptr = light_of(light);
    return actor_ptr != NULL ? actor_ptr->as.light.shadow_bias_slope : 0.0f;
}

bool wgf_model_set_shadow_casting(wgf_actor_t model, bool casting)
{
    wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.model.shadow_off = !casting;
    return true;
}

bool wgf_model_is_shadow_casting(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    return actor_ptr != NULL && !actor_ptr->as.model.shadow_off;
}

bool wgf_model_set_shadow_receiving(wgf_actor_t model, bool receiving)
{
    wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.model.receive_off = !receiving;
    return true;
}

bool wgf_model_is_shadow_receiving(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    return actor_ptr != NULL && !actor_ptr->as.model.receive_off;
}

/* -------------------------------------------------------------------- fitting ---- */

/* The eight corners of what the camera sees between its near plane and `distance`. */
static void view_corners(const wgf_gfx_priv_shadow_camera_t *cam, float distance, wgf_vec3_t out[8])
{
    const wgf_vec3_t forward = wgf_vec3_normalize(cam->forward);
    const wgf_vec3_t right = wgf_vec3_normalize(wgf_vec3_cross(forward, cam->up));
    const wgf_vec3_t up = wgf_vec3_cross(right, forward);
    const float planes[2] = {cam->orthographic ? 0.0f : cam->near_z, distance};
    int p, c;
    for (p = 0; p < 2; p++) {
        const float z = planes[p];
        const float half_height = cam->orthographic ? cam->ortho_height * 0.5f : tanf(cam->fov * 0.5f) * z;
        const float half_width = half_height * cam->aspect;
        const wgf_vec3_t centre = wgf_vec3_add(cam->position, wgf_vec3_scale(forward, z));
        for (c = 0; c < 4; c++) {
            const float x = (c & 1) ? half_width : -half_width;
            const float y = (c & 2) ? half_height : -half_height;
            out[p * 4 + c] = wgf_vec3_add(centre, wgf_vec3_add(wgf_vec3_scale(right, x), wgf_vec3_scale(up, y)));
        }
    }
}

wgf_gfx_priv_shadow_fit_t wgf_gfx_priv_shadow_fit_directional(const wgf_gfx_priv_shadow_camera_t *camera,
                                                              wgf_vec3_t light_direction, float distance, int map_size,
                                                              float pullback)
{
    wgf_vec3_t corners[8], centre = wgf_vec3_make(0, 0, 0), dir = wgf_vec3_normalize(light_direction), eye, up;
    wgf_mat4_t light_view;
    wgf_gfx_priv_shadow_fit_t fit;
    float min_x = 1e30f, max_x = -1e30f, min_y = 1e30f, max_y = -1e30f, max_z = -1e30f;
    int i;

    if (!(wgf_vec3_dot(dir, dir) > 0.0f)) dir = wgf_vec3_make(0.0f, -1.0f, 0.0f);
    if (!(distance > 0.0f)) distance = 1.0f;
    if (map_size < 1) map_size = 1;
    view_corners(camera, distance, corners);
    for (i = 0; i < 8; i++) centre = wgf_vec3_add(centre, corners[i]);
    centre = wgf_vec3_scale(centre, 1.0f / 8.0f);

    /* look from far enough back along the light that the whole slice is in front */
    eye = wgf_vec3_sub(centre, wgf_vec3_scale(dir, distance + pullback));
    up = fabsf(dir.y) > 0.99f ? wgf_vec3_make(0.0f, 0.0f, 1.0f) : wgf_vec3_make(0.0f, 1.0f, 0.0f);
    light_view = wgf_mat4_look_at(eye, centre, up);
    for (i = 0; i < 8; i++) {
        const wgf_vec3_t p = wgf_mat4_transform_point(light_view, corners[i]);
        if (p.x < min_x) min_x = p.x;
        if (p.x > max_x) max_x = p.x;
        if (p.y < min_y) min_y = p.y;
        if (p.y > max_y) max_y = p.y;
        if (-p.z > max_z) max_z = -p.z; /* the view looks down -z: depth grows that way */
    }

    /* a square that holds the slice whichever way the camera turns, so its size (and so
       the texel size) doesn't change as it does */
    {
        const float width = max_x - min_x, height = max_y - min_y;
        const float side = width > height ? width : height;
        const float mid_x = (min_x + max_x) * 0.5f, mid_y = (min_y + max_y) * 0.5f;
        const float texel = side / (float)map_size;
        min_x = mid_x - side * 0.5f;
        min_y = mid_y - side * 0.5f;
        /* snap the corner to whole texels: the shadow then stays put as the camera moves
           instead of crawling along its edges */
        if (texel > 0.0f) {
            min_x = floorf(min_x / texel) * texel;
            min_y = floorf(min_y / texel) * texel;
        }
        max_x = min_x + side;
        max_y = min_y + side;
        fit.texel_world = texel;
    }
    fit.depth_range = max_z + pullback;
    fit.perspective = false;
    fit.near_z = 0.0f;
    fit.far_z = fit.depth_range;
    fit.view_proj = wgf_mat4_mul(wgf_mat4_orthographic(min_x, max_x, min_y, max_y, 0.0f, fit.depth_range), light_view);
    return fit;
}

wgf_gfx_priv_shadow_fit_t wgf_gfx_priv_shadow_fit_spot(wgf_vec3_t position, wgf_vec3_t direction, float cos_outer,
                                                       float distance, float near_plane, int map_size)
{
    wgf_vec3_t dir = wgf_vec3_normalize(direction), up;
    wgf_gfx_priv_shadow_fit_t fit;
    float fovy;

    if (!(wgf_vec3_dot(dir, dir) > 0.0f)) dir = wgf_vec3_make(0.0f, -1.0f, 0.0f);
    if (!(distance > 0.0f)) distance = 1.0f;
    if (!(near_plane > 0.0f) || near_plane >= distance) near_plane = distance * 0.01f;
    if (map_size < 1) map_size = 1;
    /* the whole cone has to fit, with a little margin so its edge isn't on the very last
       texel; a cone at or past a right angle is clamped to something projectable */
    fovy = 2.0f * acosf(cos_outer < -0.99f ? -0.99f : (cos_outer > 1.0f ? 1.0f : cos_outer)) * 1.1f;
    if (fovy > 2.8f) fovy = 2.8f; /* about 160 degrees */
    if (fovy < 0.02f) fovy = 0.02f;
    up = fabsf(dir.y) > 0.99f ? wgf_vec3_make(0.0f, 0.0f, 1.0f) : wgf_vec3_make(0.0f, 1.0f, 0.0f);
    fit.depth_range = distance;
    fit.perspective = true;
    fit.near_z = near_plane;
    fit.far_z = distance;
    /* a spot's texels grow with distance; this is the size at the far end, which is where
       its shadow usually lands */
    fit.texel_world = 2.0f * tanf(fovy * 0.5f) * distance / (float)map_size;
    fit.view_proj = wgf_mat4_mul(wgf_mat4_perspective(fovy, 1.0f, near_plane, distance),
                                 wgf_mat4_look_at(position, wgf_vec3_add(position, dir), up));
    return fit;
}

static wgf_gfx_priv_shadow_fit_t fit_light(const wgf_gfx_priv_stage3d_light_t *light,
                                           const wgf_gfx_priv_shadow_camera_t *camera, int map_size)
{
    if (light->type == WGF_LIGHT_TYPE_SPOT) { /* a spot only lights its cone, as far as its range */
        const float reach = wgf_gfx_priv_shadow_reach(light);
        return wgf_gfx_priv_shadow_fit_spot(light->position, light->direction, light->cos_outer, reach, reach * 0.01f,
                                            map_size);
    }
    return wgf_gfx_priv_shadow_fit_directional(camera, light->direction, light->shadow_distance, map_size,
                                               SHADOW_PULLBACK);
}

/* ------------------------------------------------------------------------ map ---- */

static bool depth_sampling_supported(void)
{
    const sg_pixelformat_info info = sg_query_pixelformat(SG_PIXELFORMAT_DEPTH);
    return info.depth && info.sample;
}

/* The map at `size` with `layers` layers, made on first use and again when either changes;
 * false (warned once) when the backend can't sample a depth array. */
static bool ensure_map(int size, int layers)
{
    int i;
    if (sm.size == size && sm.layers == layers && sm.map.id != SG_INVALID_ID) return true;
    if (!depth_sampling_supported()) {
        if (!sm.unsupported) {
            wgf_log_warn("wgf_gfx_shadow: this graphics backend can't sample a shadow map; shadows are off");
            sm.unsupported = true;
        }
        return false;
    }
    for (i = 0; i < WGF_GFX_PRIV_MAX_SHADOW_LIGHTS; i++) {
        if (sm.layer_attachment[i].id != SG_INVALID_ID) sg_destroy_view(sm.layer_attachment[i]);
        sm.layer_attachment[i].id = SG_INVALID_ID;
    }
    if (sm.map_view.id != SG_INVALID_ID) sg_destroy_view(sm.map_view);
    if (sm.map.id != SG_INVALID_ID) sg_destroy_image(sm.map);
    {
        sg_image_desc image;
        sg_view_desc view;
        memset(&image, 0, sizeof(image));
        image.type = SG_IMAGETYPE_ARRAY;
        image.usage.depth_stencil_attachment = true;
        image.width = image.height = size;
        image.num_slices = layers;
        image.pixel_format = SG_PIXELFORMAT_DEPTH;
        image.sample_count = 1;
        image.label = "wgf-shadow-map";
        sm.map = sg_make_image(&image);
        memset(&view, 0, sizeof(view));
        view.texture.image = sm.map;
        sm.map_view = sg_make_view(&view);
        for (i = 0; i < layers; i++) { /* an attachment a layer: a pass writes one */
            memset(&view, 0, sizeof(view));
            view.depth_stencil_attachment.image = sm.map;
            view.depth_stencil_attachment.slice = i;
            sm.layer_attachment[i] = sg_make_view(&view);
        }
    }
    if (sm.sampler.id == SG_INVALID_ID) {
        /* comparison sampling: reading it gives how lit the point is, filtered by the GPU */
        sg_sampler_desc sampler;
        memset(&sampler, 0, sizeof(sampler));
        sampler.min_filter = sampler.mag_filter = SG_FILTER_LINEAR;
        sampler.wrap_u = sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sampler.compare = SG_COMPAREFUNC_LESS_EQUAL;
        sm.sampler = sg_make_sampler(&sampler);
    }
    sm.size = size;
    sm.layers = layers;
    return sg_query_image_state(sm.map) == SG_RESOURCESTATE_VALID;
}

/* Open a depth-only pass into layer `layer`, cleared and kept (sokol's default for depth is
 * to discard it, which WebGPU takes at its word). */
static void begin_layer(int layer)
{
    sg_pass pass;
    memset(&pass, 0, sizeof(pass));
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.action.depth.clear_value = 1.0f;
    pass.action.stencil.load_action = SG_LOADACTION_DONTCARE;
    pass.attachments.depth_stencil = sm.layer_attachment[layer];
    pass.label = "wgf-shadow-map";
    sg_begin_pass(&pass);
}

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

/* The depth pipeline, depth only, never culled (a one-sided floor still casts), and what an
 * alpha test reads without a texture, made the first time. */
static void ensure_pipeline(void)
{
    sg_pipeline_desc desc;
    if (sm.pipeline.id != SG_INVALID_ID) return;
    sm.shader = sg_make_shader(depth_shader_desc(shader_backend()));
    memset(&desc, 0, sizeof(desc));
    desc.shader = sm.shader;
    desc.layout.buffers[0].stride = (int)(sizeof(float) * WGF_GFX_PRIV_MESH_VERTEX_FLOATS);
    desc.layout.attrs[ATTR_depth_position].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[ATTR_depth_position].offset = 0;
    desc.layout.attrs[ATTR_depth_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[ATTR_depth_texcoord0].offset = 24;
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    desc.depth.write_enabled = true;
    desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    desc.colors[0].pixel_format = SG_PIXELFORMAT_NONE;
    desc.color_count = 0;
    desc.sample_count = 1; /* the map isn't multisampled, whatever the screen is */
    desc.label = "wgf-depth";
    sm.pipeline = sg_make_pipeline(&desc);
    {
        static const uint8_t white[4] = {255, 255, 255, 255};
        sg_image_desc image;
        sg_view_desc view;
        sg_sampler_desc sampler;
        memset(&image, 0, sizeof(image));
        image.width = image.height = 1;
        image.pixel_format = SG_PIXELFORMAT_RGBA8;
        image.data.mip_levels[0].ptr = white;
        image.data.mip_levels[0].size = sizeof(white);
        sm.white_image = sg_make_image(&image);
        memset(&view, 0, sizeof(view));
        view.texture.image = sm.white_image;
        sm.white_view = sg_make_view(&view);
        memset(&sampler, 0, sizeof(sampler));
        sm.white_sampler = sg_make_sampler(&sampler);
    }
}

/* ------------------------------------------------------------------- the pass ---- */

/* A view's six planes (Gribb and Hartmann), each ax + by + cz + d >= 0 inside. */
static void frustum_planes(const wgf_mat4_t *m, float planes[6][4])
{
    int p, c;
    for (p = 0; p < 6; p++) {
        const int row = p / 2;
        const float sign = p % 2 == 0 ? 1.0f : -1.0f;
        for (c = 0; c < 4; c++) planes[p][c] = m->m[c * 4 + 3] + sign * m->m[c * 4 + row];
    }
}

static bool outside(float planes[6][4], wgf_vec3_t lo, wgf_vec3_t hi)
{
    int p;
    for (p = 0; p < 6; p++) {
        const float x = planes[p][0] >= 0.0f ? hi.x : lo.x, y = planes[p][1] >= 0.0f ? hi.y : lo.y;
        const float z = planes[p][2] >= 0.0f ? hi.z : lo.z;
        if (planes[p][0] * x + planes[p][1] * y + planes[p][2] * z + planes[p][3] < 0.0f) return true;
    }
    return false;
}

/* The casters of `job` that the light's fit reaches into the open layer, through its
 * `view_proj`. */
static void draw_casters(const wgf_gfx_priv_shadow_frame_t *job, wgf_mat4_t view_proj)
{
    float planes[6][4];
    float applied_cutoff = -1.0f;
    int i;
    frustum_planes(&view_proj, planes);
    sg_apply_pipeline(sm.pipeline);
    for (i = job->first; i < job->first + job->count; i++) {
        wgf_gfx_priv_mesh_primitive_t primitive;
        wgf_material_t material_handle;
        const wgf_gfx_priv_material_t *material;
        wgf_mat4_t world;
        wgf_vec3_t lo, hi;
        vs_depth_params_t vs;
        sg_bindings bindings;
        float cutoff;
        if (!wgf_gfx_priv_stage3d_caster(i, &world, &lo, &hi, &primitive, &material_handle) ||
            outside(planes, lo, hi)) {
            continue;
        }
        material = wgf_gfx_priv_material_get(material_handle);
        if (material == NULL) continue;
        memset(&bindings, 0, sizeof(bindings));
        bindings.vertex_buffers[0] = primitive.vertices;
        bindings.index_buffer = primitive.indices;
        bindings.views[VIEW_base_color_tex] = sm.white_view;
        bindings.samplers[SMP_base_color_smp] = sm.white_sampler;
        cutoff = material->alpha_mode == WGF_ALPHA_MODE_MASK ? material->alpha_cutoff : 0.0f;
        if (cutoff > 0.0f) { /* a cut-out casts the shape it draws */
            const wgf_gfx_priv_material_texture_t *base = &material->textures[WGF_GFX_PRIV_MATERIAL_TEXTURE_BASE_COLOR];
            sg_view view;
            sg_sampler sampler;
            int width, height;
            bool placeholder;
            if (base->texture != 0 &&
                wgf_gfx_priv_texture_get_binding(base->texture, &view, &sampler, &width, &height, &placeholder)) {
                bindings.views[VIEW_base_color_tex] = view;
                bindings.samplers[SMP_base_color_smp] =
                    wgf_gfx_priv_texture_sampler(base->wrap_u, base->wrap_v, base->filter, base->mipmaps);
            }
        }
        sg_apply_bindings(&bindings);
        memcpy(vs.mvp, wgf_mat4_mul(view_proj, world).m, sizeof(vs.mvp));
        sg_apply_uniforms(UB_vs_depth_params, &SG_RANGE(vs));
        if (cutoff != applied_cutoff) {
            const float values[4] = {cutoff, 0.0f, 0.0f, 0.0f};
            sg_apply_uniforms(UB_fs_depth_params, &SG_RANGE(values));
            applied_cutoff = cutoff;
        }
        sg_draw(0, primitive.index_count, 1);
        sm.draw_calls++;
    }
}

/* The part's flush, before the frame's pass: the stage draw with casting lights, each
 * light's caster drawn into its layer, and how to read them handed to its shading. */
static void flush(void)
{
    wgf_gfx_priv_shadow_frame_t job;
    int size = 0, i;
    sm.draw_calls = 0;
    sm.last_layers = 0;
    if (!wgf_gfx_priv_stage3d_shadow_frame(&job)) {
        sm.last_draw_calls = 0;
        return;
    }
    for (i = 0; i < job.light_count; i++) { /* the layers share one size: the largest asked for */
        if (job.lights[i]->shadow_map_size > size) size = job.lights[i]->shadow_map_size;
    }
    if (!ensure_map(size, job.light_count)) {
        sm.last_draw_calls = 0;
        return;
    }
    ensure_pipeline();
    for (i = 0; i < job.light_count; i++) {
        const wgf_gfx_priv_stage3d_light_t *light = job.lights[i];
        const wgf_gfx_priv_shadow_fit_t fit = fit_light(light, &job.camera, size);
        wgf_gfx_priv_shadow_slot_t *slot = &job.binding->slots[i];
        begin_layer(i);
        draw_casters(&job, fit.view_proj);
        sg_end_pass();
        slot->view_proj = fit.view_proj;
        slot->texel = 1.0f / (float)size;
        slot->bias_constant = light->shadow_bias_constant;
        slot->bias_slope = light->shadow_bias_slope;
        /* the bias is given in texels, which is what acne is made of; the shader works in
           the map's depth units, so carry the conversion with it */
        slot->bias_scale = fit.depth_range > 0.0f ? fit.texel_world / fit.depth_range : 0.0f;
        slot->texel_world = fit.texel_world;
        slot->strength = light->shadow_strength;
        slot->tint[0] = light->shadow_tint.x;
        slot->tint[1] = light->shadow_tint.y;
        slot->tint[2] = light->shadow_tint.z;
        slot->perspective = fit.perspective;
        slot->near_z = fit.near_z;
        slot->far_z = fit.far_z;
    }
    job.binding->map = sm.map_view;
    job.binding->sampler = sm.sampler;
    job.binding->flipped = sg_query_features().origin_top_left;
    job.binding->count = job.light_count;
    job.binding->valid = true;
    sm.last_draw_calls = sm.draw_calls;
    sm.last_layers = job.light_count;
}

int wgf_gfx_priv_shadow_get_draw_calls(void)
{
    return sm.last_draw_calls;
}

int wgf_gfx_priv_shadow_get_layers(void)
{
    return sm.last_layers;
}

static void stop(void)
{
    int i;
    if (sm.pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(sm.pipeline);
        sg_destroy_shader(sm.shader);
        sg_destroy_view(sm.white_view);
        sg_destroy_image(sm.white_image);
        sg_destroy_sampler(sm.white_sampler);
    }
    if (sm.sampler.id != SG_INVALID_ID) sg_destroy_sampler(sm.sampler);
    for (i = 0; i < WGF_GFX_PRIV_MAX_SHADOW_LIGHTS; i++) {
        if (sm.layer_attachment[i].id != SG_INVALID_ID) sg_destroy_view(sm.layer_attachment[i]);
    }
    if (sm.map_view.id != SG_INVALID_ID) sg_destroy_view(sm.map_view);
    if (sm.map.id != SG_INVALID_ID) sg_destroy_image(sm.map);
    memset(&sm, 0, sizeof(sm));
}

static wgf_core_priv_part_t part = {.name = "shadows",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SHADOWS,
                                    .flush = flush,
                                    .stop = stop};

void wgf_gfx_priv_shadow_install(void)
{
    wgf_core_priv_part_install(&part);
}
