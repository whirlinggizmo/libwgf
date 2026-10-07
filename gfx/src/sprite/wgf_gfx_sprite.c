#include "wgf_sprite.h"

#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "wgf_core_handle_priv.h"
#include "wgf_resource.h"

/* Sprites on a 2D stage: a textured quad, its corners placed through the actor into the
 * frame's logical pixels and drawn through the frame's immediate mode recording, as
 * libwgt's 2D sprites, through sokol_gl in place of its instanced batch
 * (docs/HISTORY.md, "2D drawn through sokol_gl"). */

static wgf_gfx_priv_sprite_t *sprite_of(wgf_actor_t sprite)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(sprite);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_SPRITE ? &actor_ptr->as.sprite : NULL;
}

/* The region shown, and the size drawn: what was set, or the texture's. */
static void region_of(const wgf_gfx_priv_sprite_t *sprite, float region[4], float size[2])
{
    const float tw = (float)wgf_texture_get_width(sprite->texture), th = (float)wgf_texture_get_height(sprite->texture);
    const bool whole = sprite->source[2] <= 0.0f || sprite->source[3] <= 0.0f;
    region[0] = whole ? 0.0f : sprite->source[0];
    region[1] = whole ? 0.0f : sprite->source[1];
    region[2] = whole ? tw : sprite->source[2];
    region[3] = whole ? th : sprite->source[3];
    size[0] = sprite->size[0] > 0.0f && sprite->size[1] > 0.0f ? sprite->size[0] : region[2];
    size[1] = sprite->size[0] > 0.0f && sprite->size[1] > 0.0f ? sprite->size[1] : region[3];
}

static void vertex(const wgf_mat4_t *m, float x, float y, float u, float v)
{
    sgl_v2f_t2f(m->m[0] * x + m->m[4] * y + m->m[12], m->m[1] * x + m->m[5] * y + m->m[13], u, v);
}

static void draw_sprite(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                        const wgf_mat4_t *view)
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    sg_view image;
    sg_sampler sampler;
    int tw, th;
    bool placeholder;
    float region[4], size[2], u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f, x0, y0, x1, y1;
    (void)actor;
    (void)view;
    if (!wgf_gfx_priv_texture_get_binding(sprite->texture, &image, &sampler, &tw, &th, &placeholder)) return;
    region_of(sprite, region, size);
    if (placeholder && (size[0] <= 0.0f || size[1] <= 0.0f)) { /* a failed texture with no size: the checker's */
        size[0] = (float)tw;
        size[1] = (float)th;
    }
    if (!placeholder && tw > 0 && th > 0) {
        u0 = region[0] / (float)tw;
        v0 = region[1] / (float)th;
        u1 = (region[0] + region[2]) / (float)tw;
        v1 = (region[1] + region[3]) / (float)th;
    }
    x0 = -sprite->pivot[0] * size[0];
    y0 = -sprite->pivot[1] * size[1];
    x1 = x0 + size[0];
    y1 = y0 + size[1];
    sgl_enable_texture();
    sgl_texture(image, sampler);
    sgl_begin_triangles();
    sgl_c4b((uint8_t)wgf_color_get_red(sprite->tint), (uint8_t)wgf_color_get_green(sprite->tint),
            (uint8_t)wgf_color_get_blue(sprite->tint), (uint8_t)wgf_color_get_alpha(sprite->tint));
    vertex(placed, x0, y0, u0, v0);
    vertex(placed, x1, y0, u1, v0);
    vertex(placed, x1, y1, u1, v1);
    vertex(placed, x0, y0, u0, v0);
    vertex(placed, x1, y1, u1, v1);
    vertex(placed, x0, y1, u0, v1);
    sgl_end();
    sgl_disable_texture();
}

static void free_sprite(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    (void)actor;
    if (actor_ptr->as.sprite.texture != 0) wgf_resource_release(actor_ptr->as.sprite.texture);
    actor_ptr->as.sprite.texture = 0;
}

static const wgf_gfx_priv_actor_kind_t kind = {free_sprite, draw_sprite};

static bool is_texture(wgf_texture_t texture)
{
    return texture == 0 || (WGF_CORE_PRIV_HANDLE_KIND(texture) == WGF_CORE_PRIV_HANDLE_KIND_TEXTURE &&
                            wgf_resource_get_status(texture) != WGF_RESOURCE_STATUS_NONE);
}

wgf_actor_t wgf_sprite_create(wgf_texture_t texture)
{
    wgf_actor_t sprite;
    wgf_gfx_priv_sprite_t *sprite_ptr;
    if (!is_texture(texture)) return 0;
    sprite = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_SPRITE);
    sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return 0;
    sprite_ptr->tint = 0xFFFFFFFFu;
    sprite_ptr->pivot[0] = sprite_ptr->pivot[1] = 0.5f;
    if (texture != 0) {
        wgf_gfx_priv_texture_retain(texture);
        sprite_ptr->texture = texture;
    }
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_SPRITE, &kind);
    return sprite;
}

bool wgf_sprite_set_texture(wgf_actor_t sprite, wgf_texture_t texture)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL || !is_texture(texture)) return false;
    if (texture == sprite_ptr->texture) return true;
    if (texture != 0) wgf_gfx_priv_texture_retain(texture);
    if (sprite_ptr->texture != 0) wgf_resource_release(sprite_ptr->texture);
    sprite_of(sprite)->texture = texture;
    return true;
}

wgf_texture_t wgf_sprite_get_texture(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? sprite_ptr->texture : 0;
}

bool wgf_sprite_set_source(wgf_actor_t sprite, float x, float y, float width, float height)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->source[0] = x;
    sprite_ptr->source[1] = y;
    sprite_ptr->source[2] = width;
    sprite_ptr->source[3] = height;
    return true;
}

wgf_vec4_t wgf_sprite_get_source(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
    return wgf_vec4_make(sprite_ptr->source[0], sprite_ptr->source[1], sprite_ptr->source[2], sprite_ptr->source[3]);
}

bool wgf_sprite_set_size(wgf_actor_t sprite, float width, float height)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->size[0] = width;
    sprite_ptr->size[1] = height;
    return true;
}

wgf_vec2_t wgf_sprite_get_size(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    float region[4], size[2];
    if (sprite_ptr == NULL) return wgf_vec2_make(0.0f, 0.0f);
    region_of(sprite_ptr, region, size);
    return wgf_vec2_make(size[0], size[1]);
}

bool wgf_sprite_set_pivot(wgf_actor_t sprite, float x, float y)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->pivot[0] = x;
    sprite_ptr->pivot[1] = y;
    return true;
}

wgf_vec2_t wgf_sprite_get_pivot(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? wgf_vec2_make(sprite_ptr->pivot[0], sprite_ptr->pivot[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_sprite_set_tint(wgf_actor_t sprite, wgf_color_t tint)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->tint = tint;
    return true;
}

wgf_color_t wgf_sprite_get_tint(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? sprite_ptr->tint : 0u;
}
