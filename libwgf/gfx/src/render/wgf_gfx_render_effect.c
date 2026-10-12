#include "wgf_render.h"

#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "material/wgf_gfx_shader_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_time.h"

/* Screen effects, libwgt's (wgrender's post-processing): the frame drawn into a texture of
 * its own instead of the screen, and each effect, a custom material whose shader is a
 * screen effect (wgf.glsl's wgf_screen), redrawing it into the next, the last onto the
 * screen. The textures are render targets (the screen's format and samples), made at the
 * screen's size as the frame's starts, and remade when it changes; on GL the screen's pass
 * is then recorded flipped, as a target's is, so every one is stored top row first and an
 * effect reads it as it is (wgf.glsl). A part of its own, installed by the first effect
 * added: render reaches it through one hook (wgf_gfx_priv_render_set_effects), so a
 * program without effects links none of it. */

#define MAX_EFFECTS 8

static struct {
    wgf_material_t materials[MAX_EFFECTS]; /* references held */
    int count;
    wgf_texture_t frame, chain[2]; /* the screen's pass drawn into frame; the effects between into chain */
    int width, height;            /* theirs */
    wgf_material_t active[MAX_EFFECTS]; /* this frame's chain, as it began: those READY, a reference each */
    int active_count;
    sg_swapchain swapchain; /* the screen's, kept for the last effect */
    sg_image white;
    sg_view white_view;
} effects;

/* The vertex block a screen effect reads (wgf_screen_view in wgf.glsl), and the fragment
 * one (wgf_screen_frame). */
typedef struct screen_view_t {
    float clip[4]; /* y: -1 where clip space is drawn flipped */
} screen_view_t;
typedef struct screen_frame_t {
    float info[4]; /* xy size in pixels, z seconds */
} screen_frame_t;

/* The frame's chain let go of, as it ends. */
static void release_active(void)
{
    int i;
    for (i = 0; i < effects.active_count; i++) wgf_resource_release(effects.active[i]);
    effects.active_count = 0;
}

static void release_textures(void)
{
    int i;
    if (effects.frame != 0) wgf_resource_release(effects.frame);
    for (i = 0; i < 2; i++) {
        if (effects.chain[i] != 0) wgf_resource_release(effects.chain[i]);
    }
    effects.frame = effects.chain[0] = effects.chain[1] = 0;
    effects.width = effects.height = 0;
}

/* The frame's textures at the screen's size `width` by `height`: false when they can't be
 * had (no room, or a size of 0). */
static bool ensure_textures(int width, int height)
{
    int i;
    if (effects.frame != 0 && effects.width == width && effects.height == height) return true;
    release_textures();
    if (width <= 0 || height <= 0) return false;
    effects.frame = wgf_texture_create_target(width, height);
    for (i = 0; i < 2; i++) effects.chain[i] = wgf_texture_create_target(width, height);
    effects.width = width;
    effects.height = height;
    return effects.frame != 0 && effects.chain[0] != 0 && effects.chain[1] != 0;
}

/* The pipeline drawing `shader`'s screen program, made at its first use. */
static sg_pipeline pipeline_of(wgf_gfx_priv_shader_t *shader)
{
    sg_pipeline_desc desc;
    if (shader->screen_pipelines[0].id != SG_INVALID_ID) return shader->screen_pipelines[0];
    memset(&desc, 0, sizeof(desc));
    desc.shader = shader->programs[WGF_GFX_PRIV_SHADER_PROGRAM_SCREEN].shader;
    desc.cull_mode = SG_CULLMODE_NONE; /* its triangle's winding turns with a flipped pass */
    desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    desc.label = "wgf-effect";
    shader->screen_pipelines[0] = sg_make_pipeline(&desc);
    return shader->screen_pipelines[0];
}

/* Effect `material` redrawing `source` into the open pass, `flipped` on GL. */
static void draw_effect(wgf_material_t material, wgf_texture_t source, bool flipped)
{
    const wgf_gfx_priv_material_texture_t *textures[WGF_GFX_PRIV_SHADER_MAX_TEXTURES];
    const unsigned char *params;
    wgf_gfx_priv_shader_t *shader =
        (wgf_gfx_priv_shader_t *)wgf_gfx_priv_material_custom_bind(material, &params, textures);
    const wgf_gfx_priv_shader_program_t *program;
    sg_bindings bindings;
    screen_view_t view;
    screen_frame_t frame;
    sg_view source_view;
    sg_sampler sampler;
    int t, width, height;
    bool placeholder;

    if (shader == NULL || !shader->screen) return;
    program = &shader->programs[WGF_GFX_PRIV_SHADER_PROGRAM_SCREEN];
    sg_apply_pipeline(pipeline_of(shader));
    memset(&bindings, 0, sizeof(bindings));
    for (t = 0; t < shader->texture_count; t++) {
        const wgf_gfx_priv_material_texture_t *texture = textures[t];
        sg_view tv = effects.white_view;
        sampler = wgf_gfx_priv_texture_sampler(WGF_TEXTURE_WRAP_REPEAT, WGF_TEXTURE_WRAP_REPEAT,
                                               WGF_TEXTURE_FILTER_LINEAR, true);
        if (texture != NULL) {
            if (texture->texture != 0 &&
                wgf_gfx_priv_texture_get_binding(texture->texture, &tv, &sampler, &width, &height, &placeholder) &&
                placeholder) {
                tv = effects.white_view;
            }
            sampler = wgf_gfx_priv_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
        }
        if (program->view_slot[t] >= 0) bindings.views[program->view_slot[t]] = tv;
        if (program->sampler_slot[t] >= 0) bindings.samplers[program->sampler_slot[t]] = sampler;
    }
    if (!wgf_gfx_priv_texture_get_binding(source, &source_view, &sampler, &width, &height, &placeholder)) return;
    sampler = wgf_gfx_priv_texture_sampler(WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_LINEAR,
                                           false);
    if (program->library_view[WGF_GFX_PRIV_SHADER_VIEW_SCREEN] >= 0) {
        bindings.views[program->library_view[WGF_GFX_PRIV_SHADER_VIEW_SCREEN]] = source_view;
    }
    if (program->library_sampler[WGF_GFX_PRIV_SHADER_VIEW_SCREEN] >= 0) {
        bindings.samplers[program->library_sampler[WGF_GFX_PRIV_SHADER_VIEW_SCREEN]] = sampler;
    }
    sg_apply_bindings(&bindings);
    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_OBJECT]) {
        memset(&view, 0, sizeof(view));
        view.clip[1] = flipped ? -1.0f : 1.0f;
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_OBJECT, &SG_RANGE(view));
    }
    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_FRAME]) {
        frame.info[0] = (float)effects.width;
        frame.info[1] = (float)effects.height;
        frame.info[2] = (float)wgf_time_get_seconds();
        frame.info[3] = 0.0f;
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_FRAME, &SG_RANGE(frame));
    }
    if (program->has_block[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS]) {
        const sg_range range = {params, (size_t)shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS]};
        sg_apply_uniforms(WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS, &range);
    }
    sg_draw(0, 3, 1);
}

/* Render's hook. BEGIN, as a frame starts: whether its screen's pass is recorded flipped
 * (it goes through the effects, an effect's shader READY and the textures had, on GL);
 * the chain is kept for the frame either way. SCREEN, before
 * the screen's pass begins: `pass` turned into the frame's texture. END, after it: each
 * effect in turn, the last onto the screen (`pass`'s swapchain). */
static bool at_frame(wgf_gfx_priv_render_effects_t when, sg_pass *pass)
{
    const bool gl_flips = !sg_query_features().origin_top_left;
    wgf_texture_t source;
    int i, width, height;

    if (when == WGF_GFX_PRIV_RENDER_EFFECTS_BEGIN) {
        release_active(); /* a frame that never ended */
        for (i = 0; i < effects.count; i++) {
            const wgf_gfx_priv_material_t *material = wgf_gfx_priv_material_get(effects.materials[i]);
            const wgf_gfx_priv_shader_t *shader = material != NULL ? wgf_gfx_priv_shader_get(material->shader) : NULL;
            if (shader != NULL && shader->screen) effects.active[effects.active_count++] = effects.materials[i];
        }
        if (effects.active_count == 0) {
            release_textures(); /* none to draw: the screen as it is */
            return false;
        }
        if (!ensure_textures(wgf_render_get_width(), wgf_render_get_height())) {
            effects.active_count = 0;
            return false;
        }
        for (i = 0; i < effects.active_count; i++) wgf_core_priv_resource_retain(effects.active[i]);
        return gl_flips;
    }
    if (effects.active_count == 0) return false;
    if (when == WGF_GFX_PRIV_RENDER_EFFECTS_SCREEN) {
        if (!wgf_gfx_priv_texture_get_target(effects.frame, &pass->attachments, &width, &height)) return false;
        effects.swapchain = pass->swapchain;
        memset(&pass->swapchain, 0, sizeof(pass->swapchain));
        return true;
    }
    (void)pass; /* END: from the frame's texture, through each effect */
    source = effects.frame;
    for (i = 0; i < effects.active_count; i++) {
        const bool last = i == effects.active_count - 1;
        const wgf_texture_t into = last ? 0 : effects.chain[i % 2];
        sg_pass effect_pass;
        memset(&effect_pass, 0, sizeof(effect_pass));
        effect_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR; /* each pixel is drawn over */
        effect_pass.label = "wgf-effect";
        if (last) {
            effect_pass.swapchain = effects.swapchain;
        } else if (!wgf_gfx_priv_texture_get_target(into, &effect_pass.attachments, &width, &height)) {
            return false;
        }
        sg_begin_pass(&effect_pass);
        draw_effect(effects.active[i], source, !last && gl_flips);
        sg_end_pass();
        source = into;
    }
    release_active();
    return true;
}

static void stop(void)
{
    release_active();
    wgf_render_clear_effects();
    release_textures();
    if (effects.white.id != SG_INVALID_ID) {
        sg_destroy_view(effects.white_view);
        sg_destroy_image(effects.white);
    }
    memset(&effects, 0, sizeof(effects));
    wgf_gfx_priv_render_set_effects(NULL);
}

static wgf_core_priv_part_t part = {.name = "effects",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_EFFECTS,
                                    .stop = stop};

/* ------------------------------------------------------------ public API ---- */

bool wgf_render_add_effect(wgf_material_t material)
{
    const wgf_gfx_priv_material_t *material_ptr = wgf_gfx_priv_material_get(material);
    const wgf_gfx_priv_shader_t *shader;
    if (material_ptr == NULL || material_ptr->custom == NULL) {
        wgf_log_warn("wgf_render_add_effect: needs a custom material (wgf_material_create_custom)");
        return false;
    }
    shader = wgf_gfx_priv_shader_get(material_ptr->shader);
    if (shader != NULL && !shader->screen) {
        wgf_log_warn("wgf_render_add_effect: its shader is a surface's, not a screen effect's (wgf_screen)");
        return false;
    }
    if (effects.count >= MAX_EFFECTS) {
        wgf_log_warn("wgf_render_add_effect: %d effects at most", MAX_EFFECTS);
        return false;
    }
    if (!wgf_gfx_priv_render_is_running()) {
        wgf_log_warn("wgf_render_add_effect: gfx isn't running yet (add effects from init on)");
        return false;
    }
    if (effects.white.id == SG_INVALID_ID) {
        static const uint32_t white = 0xFFFFFFFFu;
        sg_image_desc image;
        sg_view_desc view;
        memset(&image, 0, sizeof(image));
        image.width = image.height = 1;
        image.data.mip_levels[0].ptr = &white;
        image.data.mip_levels[0].size = sizeof(white);
        image.label = "wgf-effect-white";
        effects.white = sg_make_image(&image);
        memset(&view, 0, sizeof(view));
        view.texture.image = effects.white;
        effects.white_view = sg_make_view(&view);
    }
    wgf_core_priv_part_install(&part);
    wgf_gfx_priv_render_set_effects(at_frame);
    wgf_core_priv_resource_retain(material);
    effects.materials[effects.count++] = material;
    return true;
}

void wgf_render_clear_effects(void)
{
    int i;
    for (i = 0; i < effects.count; i++) wgf_resource_release(effects.materials[i]);
    effects.count = 0;
}

int wgf_render_get_effect_count(void)
{
    return effects.count;
}
