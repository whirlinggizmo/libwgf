#include <math.h>
#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "material/wgf_gfx_shader_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "stage/wgf_gfx_stage3d_draw_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_time.h"

/* A stage's items drawn by a custom material's shader (wgf_shader.h), libwgt's scene's
 * draw_custom (wgrender's): its model program with libwgf's vertex shader, which reads the
 * placements from the frame's instance records as the built-in shading does, and the
 * blocks wgf.glsl declares -- the camera and time, the frame's lights, environment, and
 * shadows as the built-in shading gets them, and the material's parameters -- with its
 * textures and libwgf's bound by the slots its program has them at. The custom material's
 * hooks reach it, so a program without one links none of it. */

/* The vertex block (wgf_object in wgf.glsl), std140. */
typedef struct object_block_t {
    float view_proj[16];
    float time_base[4]; /* x seconds, y the draw's first record */
} object_block_t;

/* The fragment block a surface shader reads (wgf_frame in wgf.glsl), std140. */
typedef struct frame_block_t {
    float camera_time[4];   /* xyz the camera, w seconds */
    float ambient_count[4]; /* rgb ambient, w how many lights */
    float output[4];        /* x alpha cutoff, y tone mapping, z exposure scale */
    float light_pos_range[WGF_GFX_PRIV_MAX_DRAW_LIGHTS][4];
    float light_dir_type[WGF_GFX_PRIV_MAX_DRAW_LIGHTS][4];
    float light_radiance[WGF_GFX_PRIV_MAX_DRAW_LIGHTS][4];
    float light_spot[WGF_GFX_PRIV_MAX_DRAW_LIGHTS][4];
    float env[4];
    float sh[9][4];
    float shadow_mat[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS][16];
    float shadow_params[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS][4];
    float shadow_tint[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS][4];
    float shadow_extra[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS][4];
    float shadow_map[4];
} frame_block_t;

/* The pipeline for `shader` drawing see-through or not, double sided or not, its winding
 * turned or not: the built-in shading's (wgf_gfx_stage3d.c's pipeline), made the first time
 * it's needed. */
static sg_pipeline pipeline_of(wgf_gfx_priv_shader_t *shader, bool blended, bool double_sided, bool mirrored)
{
    const int key = (blended ? 1 : 0) | (double_sided ? 2 : 0) | (mirrored ? 4 : 0);
    static const sg_vertex_format formats[6] = {SG_VERTEXFORMAT_FLOAT3, SG_VERTEXFORMAT_FLOAT3, SG_VERTEXFORMAT_FLOAT2,
                                                SG_VERTEXFORMAT_FLOAT2, SG_VERTEXFORMAT_FLOAT4, SG_VERTEXFORMAT_FLOAT4};
    static const int offsets[6] = {0, 12, 24, 32, 40, 56}; /* position, normal, uv0, uv1, tangent, color */
    sg_pipeline_desc desc;
    int a;
    if (shader->pipelines[key].id != SG_INVALID_ID) return shader->pipelines[key];
    memset(&desc, 0, sizeof(desc));
    desc.shader = shader->programs[WGF_GFX_PRIV_SHADER_PROGRAM_MODEL].shader;
    desc.layout.buffers[0].stride = (int)(sizeof(float) * WGF_GFX_PRIV_MESH_VERTEX_FLOATS);
    for (a = 0; a < 6; a++) { /* wgf.glsl's locations are the mesh's attributes in order */
        desc.layout.attrs[a].format = formats[a];
        desc.layout.attrs[a].offset = offsets[a];
    }
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
    desc.label = "wgf-custom-model";
    shader->pipelines[key] = sg_make_pipeline(&desc);
    return shader->pipelines[key];
}

/* The fragment block for `item` in `draw`, lit as the built-in shading lights it. */
static void fill_frame(const wgf_gfx_priv_stage3d_frame_t *frame, const wgf_gfx_priv_stage3d_draw_t *draw,
                       const wgf_gfx_priv_stage3d_item_t *item, const wgf_gfx_priv_material_t *material,
                       const wgf_gfx_priv_shadow_binding_t *shadow, const wgf_gfx_priv_env_lighting_t *env,
                       frame_block_t *out)
{
    const bool receiving = item->receiving && shadow->valid;
    int i;
    memset(out, 0, sizeof(*out));
    out->camera_time[0] = draw->camera_position.x;
    out->camera_time[1] = draw->camera_position.y;
    out->camera_time[2] = draw->camera_position.z;
    out->camera_time[3] = (float)wgf_time_get_seconds();
    memcpy(out->ambient_count, draw->ambient, sizeof(draw->ambient));
    out->ambient_count[3] = (float)item->light_count;
    out->output[0] = material->alpha_mode == WGF_ALPHA_MODE_MASK ? material->alpha_cutoff : 0.0f;
    out->output[1] = (float)draw->tonemap;
    out->output[2] = exp2f(draw->exposure);
    for (i = 0; i < item->light_count; i++) {
        const wgf_gfx_priv_stage3d_light_t *l = &frame->lights[draw->first_light + item->lights[i]];
        out->light_pos_range[i][0] = l->position.x;
        out->light_pos_range[i][1] = l->position.y;
        out->light_pos_range[i][2] = l->position.z;
        out->light_pos_range[i][3] = l->range;
        out->light_dir_type[i][0] = l->direction.x;
        out->light_dir_type[i][1] = l->direction.y;
        out->light_dir_type[i][2] = l->direction.z;
        out->light_dir_type[i][3] = (float)l->type;
        out->light_radiance[i][0] = l->radiance.x;
        out->light_radiance[i][1] = l->radiance.y;
        out->light_radiance[i][2] = l->radiance.z;
        out->light_spot[i][0] = l->cos_inner;
        out->light_spot[i][1] = l->cos_outer;
        out->light_spot[i][2] = receiving && l->shadow_slot >= 0 ? (float)l->shadow_slot : -1.0f;
    }
    if (env != NULL) {
        out->env[0] = draw->environment_intensity;
        out->env[1] = env->max_lod;
        out->env[2] = cosf(draw->environment_rotation);
        out->env[3] = sinf(draw->environment_rotation);
        for (i = 0; i < 9; i++) memcpy(out->sh[i], env->sh.c[i], sizeof(env->sh.c[i]));
    }
    for (i = 0; receiving && i < shadow->count; i++) {
        const wgf_gfx_priv_shadow_slot_t *slot = &shadow->slots[i];
        memcpy(out->shadow_mat[i], slot->view_proj.m, sizeof(out->shadow_mat[i]));
        out->shadow_params[i][0] = slot->texel;
        out->shadow_params[i][1] = slot->texel_world;
        out->shadow_params[i][2] = slot->bias_constant;
        out->shadow_params[i][3] = slot->bias_slope;
        out->shadow_tint[i][0] = slot->tint[0];
        out->shadow_tint[i][1] = slot->tint[1];
        out->shadow_tint[i][2] = slot->tint[2];
        out->shadow_tint[i][3] = slot->bias_scale;
        out->shadow_extra[i][0] = slot->strength;
        out->shadow_extra[i][1] = slot->near_z;
        out->shadow_extra[i][2] = slot->far_z;
        out->shadow_extra[i][3] = slot->perspective ? 1.0f : 0.0f;
    }
    out->shadow_map[0] = shadow->flipped ? 1.0f : 0.0f;
}

/* `view` and `sampler` at `program`'s slots for libwgf's texture `which`, where it has one. */
static void bind_library(const wgf_gfx_priv_shader_program_t *program, wgf_gfx_priv_shader_view_t which, sg_view view,
                         sg_sampler sampler, sg_bindings *bindings)
{
    if (program->library_view[which] >= 0) bindings->views[program->library_view[which]] = view;
    if (program->library_sampler[which] >= 0) bindings->samplers[program->library_sampler[which]] = sampler;
}

void wgf_gfx_priv_stage3d_draw_custom(wgf_gfx_priv_stage3d_frame_t *frame, const wgf_gfx_priv_stage3d_draw_t *draw,
                                      int first, int count, const wgf_gfx_priv_shadow_binding_t *shadow,
                                      const wgf_gfx_priv_env_lighting_t *env)
{
    const wgf_gfx_priv_stage3d_item_t *item = &frame->items[first];
    const wgf_gfx_priv_material_t *material = wgf_gfx_priv_material_get(item->material);
    const wgf_gfx_priv_material_texture_t *textures[WGF_GFX_PRIV_SHADER_MAX_TEXTURES];
    const unsigned char *params;
    const wgf_gfx_priv_shader_t *bound = wgf_gfx_priv_material_custom_bind(item->material, &params, textures);
    wgf_gfx_priv_shader_t *shader = (wgf_gfx_priv_shader_t *)bound; /* its pipelines are made here */
    const wgf_gfx_priv_shader_program_t *program;
    wgf_gfx_priv_mesh_primitive_t primitive;
    sg_bindings bindings;
    int t;

    if (shader == NULL || shader->screen || material == NULL) return; /* still loading, failed, or an effect's */
    if (!wgf_gfx_priv_mesh_get_primitive(item->mesh, item->primitive, &primitive)) return;
    program = &shader->programs[WGF_GFX_PRIV_SHADER_PROGRAM_MODEL];
    sg_apply_pipeline(pipeline_of(shader, item->blended, material->double_sided, item->mirrored != draw->flipped));

    memset(&bindings, 0, sizeof(bindings));
    bindings.vertex_buffers[0] = primitive.vertices;
    bindings.index_buffer = primitive.indices;
    for (t = 0; t < shader->texture_count; t++) {
        const wgf_gfx_priv_material_texture_t *texture = textures[t];
        sg_view view = frame->white_view; /* none set, still loading, or failed: white, as libwgt's */
        sg_sampler sampler = frame->sampler;
        int width, height;
        bool placeholder;
        if (texture != NULL) {
            if (texture->texture != 0 &&
                wgf_gfx_priv_texture_get_binding(texture->texture, &view, &sampler, &width, &height, &placeholder) &&
                placeholder) {
                view = frame->white_view;
            }
            sampler = wgf_gfx_priv_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
        }
        if (program->view_slot[t] >= 0) bindings.views[program->view_slot[t]] = view;
        if (program->sampler_slot[t] >= 0) bindings.samplers[program->sampler_slot[t]] = sampler;
    }
    bind_library(program, WGF_GFX_PRIV_SHADER_VIEW_SHADOW, shadow->valid ? shadow->map : frame->no_shadow_view,
                 shadow->valid ? shadow->sampler : frame->no_shadow_sampler, &bindings);
    /* the BRDF table shares the environment's sampler (wgf.glsl), so it is bound first */
    bind_library(program, WGF_GFX_PRIV_SHADER_VIEW_BRDF, env != NULL ? env->brdf : frame->white_view,
                 env != NULL ? env->brdf_sampler : frame->sampler, &bindings);
    bind_library(program, WGF_GFX_PRIV_SHADER_VIEW_ENVIRONMENT, env != NULL ? env->cube : frame->no_environment_view,
                 env != NULL ? env->cube_sampler : frame->sampler, &bindings);
    bind_library(program, WGF_GFX_PRIV_SHADER_VIEW_INSTANCE, frame->instance_view, frame->instance_sampler, &bindings);
    sg_apply_bindings(&bindings);

    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_OBJECT]) {
        object_block_t object;
        memcpy(object.view_proj, draw->clip_proj.m, sizeof(object.view_proj));
        object.time_base[0] = (float)wgf_time_get_seconds();
        object.time_base[1] = (float)item->record; /* its run's records follow it */
        object.time_base[2] = object.time_base[3] = 0.0f;
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_OBJECT, &SG_RANGE(object));
    }
    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_FRAME]) {
        frame_block_t block;
        fill_frame(frame, draw, item, material, shadow, env, &block);
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_FRAME, &SG_RANGE(block));
    }
    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS]) {
        const sg_range range = {params, (size_t)shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS]};
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS, &range);
    }
    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS]) {
        const sg_range range = {params + shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS],
                                (size_t)shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS]};
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS, &range);
    }
    sg_draw(0, primitive.index_count, count);
    frame->draw_calls++;
}
