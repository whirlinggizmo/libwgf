#include "wgf_sprite.h"

#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "draw/wgf_gfx_nine_slice_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sprite/wgf_gfx_sprite_run_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "wgf_core_handle_priv.h"
#include "wgf_resource.h"

/* Sprites on a 2D stage: a textured quad, its corners placed through the actor into the
 * frame's logical pixels and drawn through the frame's immediate mode recording, as
 * libwgt's 2D sprites, through sokol_gl in place of its instanced batch
 * (docs/HISTORY.md, "2D drawn through sokol_gl"). */

static wgf_gfx_priv_sprite_t *sprite_of_at(wgf_actor_t sprite, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(sprite, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_SPRITE ? &actor_ptr->as.sprite : NULL;
}
#define sprite_of(...) sprite_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* The region shown, and the size drawn: what was set, or the texture's, `tw` by `th`. */
static void region_in(const wgf_gfx_priv_sprite_t *sprite, float tw, float th, float region[4], float size[2])
{
    const bool whole = sprite->source[2] <= 0.0f || sprite->source[3] <= 0.0f;
    region[0] = whole ? 0.0f : sprite->source[0];
    region[1] = whole ? 0.0f : sprite->source[1];
    region[2] = whole ? tw : sprite->source[2];
    region[3] = whole ? th : sprite->source[3];
    { /* a trimmed frame's default size: the whole picture's */
        const bool trimmed = sprite->trim[2] > 0.0f && sprite->trim[3] > 0.0f;
        size[0] = sprite->size[0] > 0.0f && sprite->size[1] > 0.0f ? sprite->size[0]
                                                                   : (trimmed ? sprite->trim[2] : region[2]);
        size[1] = sprite->size[0] > 0.0f && sprite->size[1] > 0.0f ? sprite->size[1]
                                                                   : (trimmed ? sprite->trim[3] : region[3]);
    }
}

static void region_of(const wgf_gfx_priv_sprite_t *sprite, float region[4], float size[2])
{
    region_in(sprite, (float)wgf_texture_get_width(sprite->texture), (float)wgf_texture_get_height(sprite->texture),
              region, size);
}

/* Its kept quad (wgf_gfx_sprite_run_priv.h) forgotten: a setter changed what it is made
 * from, or its slot holds a new sprite. */
static void forget_quad(wgf_actor_t sprite)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_silently(sprite);
    wgf_gfx_priv_kept_quad_t *quad =
        actor_ptr != NULL ? wgf_gfx_priv_sprite_run_quad(wgf_gfx_priv_actor_index_of(actor_ptr), false) : NULL;
    if (quad != NULL) quad->kept = false;
}

void wgf_gfx_priv_sprite_set_trim(wgf_actor_t sprite, float x, float y, float w, float h)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL) return;
    sprite_ptr->trim[0] = x;
    sprite_ptr->trim[1] = y;
    sprite_ptr->trim[2] = w > 0.0f && h > 0.0f ? w : 0.0f;
    sprite_ptr->trim[3] = w > 0.0f && h > 0.0f ? h : 0.0f;
    forget_quad(sprite);
}

static void vertex(const wgf_mat4_t *m, float x, float y, float u, float v)
{
    sgl_v2f_t2f(m->m[0] * x + m->m[4] * y + m->m[12], m->m[1] * x + m->m[5] * y + m->m[13], u, v);
}

/* Its quad for a texture of `tw` by `th` (`placeholder`: the checker's): `rect` x0, y0, x1,
 * y1 in its own units, `uv` u0, v0, u1, v1, and the region shown; whether it is a
 * nine-slice, drawn as nine quads. */
static bool quad_in(const wgf_gfx_priv_sprite_t *sprite, int tw, int th, bool placeholder, float rect[4], float uv[4],
                    float region[4])
{
    float size[2], pivot[2];
    if (placeholder) region_of(sprite, region, size);
    else region_in(sprite, (float)tw, (float)th, region, size); /* the binding's size is the texture's */
    if (placeholder && (size[0] <= 0.0f || size[1] <= 0.0f)) { /* a failed texture with no size: the checker's */
        size[0] = (float)tw;
        size[1] = (float)th;
    }
    uv[0] = uv[1] = 0.0f;
    uv[2] = uv[3] = 1.0f;
    if (!placeholder && tw > 0 && th > 0) {
        uv[0] = region[0] / (float)tw;
        uv[1] = region[1] / (float)th;
        uv[2] = (region[0] + region[2]) / (float)tw;
        uv[3] = (region[1] + region[3]) / (float)th;
    }
    pivot[0] = sprite->pivot[0];
    pivot[1] = sprite->pivot[1];
    if (!placeholder) wgf_gfx_priv_sprite_trimmed(sprite, region[2], region[3], size, pivot);
    rect[0] = -pivot[0] * size[0];
    rect[1] = -pivot[1] * size[1];
    rect[2] = rect[0] + size[0];
    rect[3] = rect[1] + size[1];
    return !placeholder &&
           (sprite->nine[0] > 0.0f || sprite->nine[1] > 0.0f || sprite->nine[2] > 0.0f || sprite->nine[3] > 0.0f);
}

/* Its quad for a texture of `tw` by `th`: the one kept, or one made and kept (not a
 * placeholder's); whether it is a nine-slice, which is neither. */
static bool kept_quad_in(const wgf_gfx_priv_actor_t *actor_ptr, int tw, int th, bool placeholder, float rect[4],
                         float uv[4], float region[4])
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    wgf_gfx_priv_kept_quad_t *quad;
    if (placeholder) return quad_in(sprite, tw, th, placeholder, rect, uv, region);
    quad = wgf_gfx_priv_sprite_run_quad(wgf_gfx_priv_actor_index_of(actor_ptr), true);
    if (quad != NULL && quad->kept && quad->width == tw && quad->height == th) {
        memcpy(rect, quad->rect, sizeof(quad->rect));
        memcpy(uv, quad->uv, sizeof(quad->uv));
        return false;
    }
    if (quad_in(sprite, tw, th, placeholder, rect, uv, region)) return true;
    if (quad != NULL) {
        memcpy(quad->rect, rect, sizeof(quad->rect));
        memcpy(quad->uv, uv, sizeof(quad->uv));
        quad->width = tw;
        quad->height = th;
        quad->kept = true;
    }
    return false;
}

/* Its alpha mode as a run or immediate mode draws it: MASK as BLEND. */
static int drawn_mode(const wgf_gfx_priv_sprite_t *sprite)
{
    return sprite->alpha_mode == WGF_ALPHA_MODE_MASK ? WGF_ALPHA_MODE_BLEND : sprite->alpha_mode;
}

/* Its one quad added to the stage's run (wgf_gfx_sprite_run.c), through `placed`, the six
 * numbers of its matrix a 2D quad reads: both ways a stage draws a sprite come here. True
 * when that is done, or nothing is to be drawn; false for a nine-slice, drawn in nine. */
static bool add_quad(const wgf_gfx_priv_actor_t *actor_ptr, const float placed[6])
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    sg_view image;
    sg_sampler sampler;
    int tw, th;
    bool placeholder;
    float rect[4], uv[4], region[4];
    if (!wgf_gfx_priv_texture_get_binding(sprite->texture, &image, &sampler, &tw, &th, &placeholder)) return true;
    if (kept_quad_in(actor_ptr, tw, th, placeholder, rect, uv, region)) return false;
    wgf_gfx_priv_sprite_run_add(placed, rect, uv, sprite->tint, drawn_mode(sprite), image, sampler);
    return true;
}

static void draw_sprite(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                        const wgf_mat4_t *view)
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    const float *m = placed->m;
    const float affine[6] = {m[0], m[1], m[4], m[5], m[12], m[13]};
    sg_view image;
    sg_sampler sampler;
    int tw, th;
    bool placeholder;
    float rect[4], uv[4], region[4], x0, y0, x1, y1, u0, v0, u1, v1;
    (void)actor;
    (void)view;
    if (add_quad(actor_ptr, affine)) return;
    /* a nine-slice */
    if (!wgf_gfx_priv_texture_get_binding(sprite->texture, &image, &sampler, &tw, &th, &placeholder)) return;
    quad_in(sprite, tw, th, placeholder, rect, uv, region);
    x0 = rect[0], y0 = rect[1], x1 = rect[2], y1 = rect[3];
    u0 = uv[0], v0 = uv[1], u1 = uv[2], v1 = uv[3];
    sgl_push_pipeline(); /* its own mode, whatever immediate mode's is (MASK drawn as BLEND) */
    sgl_load_pipeline(wgf_gfx_priv_render_pipeline_2d(sprite->alpha_mode == WGF_ALPHA_MODE_MASK ? WGF_ALPHA_MODE_BLEND
                                                                                                : sprite->alpha_mode));
    sgl_enable_texture();
    sgl_texture(image, sampler);
    sgl_begin_triangles();
    sgl_c4b((uint8_t)wgf_color_get_red(sprite->tint), (uint8_t)wgf_color_get_green(sprite->tint),
            (uint8_t)wgf_color_get_blue(sprite->tint), (uint8_t)wgf_color_get_alpha(sprite->tint));
    { /* a nine-slice: its corners at their texels, the rest stretched */
        float xs[4], ys[4], us[4], vs[4];
        int i, j;
        wgf_gfx_priv_nine_slice(x0, y0, x1, y1, u0, v0, u1, v1, region[2], region[3], sprite->nine, xs, ys, us, vs);
        for (j = 0; j < 3; j++) {
            for (i = 0; i < 3; i++) {
                vertex(placed, xs[i], ys[j], us[i], vs[j]);
                vertex(placed, xs[i + 1], ys[j], us[i + 1], vs[j]);
                vertex(placed, xs[i + 1], ys[j + 1], us[i + 1], vs[j + 1]);
                vertex(placed, xs[i], ys[j], us[i], vs[j]);
                vertex(placed, xs[i + 1], ys[j + 1], us[i + 1], vs[j + 1]);
                vertex(placed, xs[i], ys[j + 1], us[i], vs[j + 1]);
            }
        }
    }
    sgl_end();
    sgl_disable_texture();
    sgl_pop_pipeline();
}

bool wgf_gfx_priv_sprite_quad_2d(const wgf_gfx_priv_actor_t *actor_ptr, float quad[4], float region[4], bool *whole)
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    sg_view image;
    sg_sampler sampler;
    int tw, th;
    bool placeholder;
    float size[2], pivot[2];
    if (actor_ptr->type != WGF_ACTOR_KIND_SPRITE ||
        !wgf_gfx_priv_texture_get_binding(sprite->texture, &image, &sampler, &tw, &th, &placeholder)) {
        return false;
    }
    region_of(sprite, region, size);
    if (placeholder && (size[0] <= 0.0f || size[1] <= 0.0f)) {
        size[0] = (float)tw;
        size[1] = (float)th;
    }
    pivot[0] = sprite->pivot[0];
    pivot[1] = sprite->pivot[1];
    if (!placeholder) wgf_gfx_priv_sprite_trimmed(sprite, region[2], region[3], size, pivot);
    quad[0] = -pivot[0] * size[0];
    quad[1] = -pivot[1] * size[1];
    quad[2] = quad[0] + size[0];
    quad[3] = quad[1] + size[1];
    *whole = placeholder || sprite->nine[0] > 0.0f || sprite->nine[1] > 0.0f || sprite->nine[2] > 0.0f ||
             sprite->nine[3] > 0.0f;
    return true;
}

bool wgf_sprite_set_pick_alpha(wgf_actor_t sprite, float threshold)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL || !(threshold >= 0.0f && threshold <= 1.0f)) return false;
    sprite_ptr->pick_alpha = threshold;
    return true;
}

float wgf_sprite_get_pick_alpha(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? sprite_ptr->pick_alpha : 0.0f;
}

static void free_sprite(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    (void)actor;
    if (actor_ptr->as.sprite.texture != 0) wgf_resource_release(actor_ptr->as.sprite.texture);
    actor_ptr->as.sprite.texture = 0;
    if (actor_ptr->as.sprite.material != 0) wgf_resource_release(actor_ptr->as.sprite.material);
    actor_ptr->as.sprite.material = 0;
}

/* Its quad through its parent's matrix and its own position and scale: draw_sprite's, with
 * the product of the two matrices worked only as far as a 2D quad reads it. */
static bool draw_leaf_sprite(const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *parent)
{
    const wgf_quat_t q = actor_ptr->rotation;
    const wgf_vec3_t p = actor_ptr->position, k = actor_ptr->scale;
    const float *a = parent->m;
    if (actor_ptr->previous != NULL || q.x != 0.0f || q.y != 0.0f || q.z != 0.0f) return false;
    {
        const float placed[6] = {a[0] * k.x,
                                 a[1] * k.x,
                                 a[4] * k.y,
                                 a[5] * k.y,
                                 a[0] * p.x + a[4] * p.y + a[8] * p.z + a[12],
                                 a[1] * p.x + a[5] * p.y + a[9] * p.z + a[13]};
        return add_quad(actor_ptr, placed); /* a nine-slice: the usual way */
    }
}

static const wgf_gfx_priv_actor_kind_t kind = {free_sprite, draw_sprite, NULL, NULL, draw_leaf_sprite};

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
    forget_quad(sprite); /* a slot used before */
    sprite_ptr->tint = 0xFFFFFFFFu;
    sprite_ptr->pivot[0] = sprite_ptr->pivot[1] = 0.5f;
    sprite_ptr->alpha_mode = WGF_ALPHA_MODE_BLEND;
    sprite_ptr->alpha_cutoff = 0.5f;
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
    sprite_ptr->texture = texture;
    forget_quad(sprite);
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
    forget_quad(sprite);
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
    forget_quad(sprite);
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
    forget_quad(sprite);
    return true;
}

wgf_vec2_t wgf_sprite_get_pivot(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? wgf_vec2_make(sprite_ptr->pivot[0], sprite_ptr->pivot[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_sprite_set_nine_slice(wgf_actor_t sprite, float left, float top, float right, float bottom)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL || !isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom)) return false;
    sprite_ptr->nine[0] = left > 0.0f ? left : 0.0f;
    sprite_ptr->nine[1] = top > 0.0f ? top : 0.0f;
    sprite_ptr->nine[2] = right > 0.0f ? right : 0.0f;
    sprite_ptr->nine[3] = bottom > 0.0f ? bottom : 0.0f;
    forget_quad(sprite);
    return true;
}

wgf_vec4_t wgf_sprite_get_nine_slice(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? wgf_vec4_make(sprite_ptr->nine[0], sprite_ptr->nine[1], sprite_ptr->nine[2],
                                              sprite_ptr->nine[3])
                              : wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
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

bool wgf_sprite_set_alpha_mode(wgf_actor_t sprite, wgf_alpha_mode_t mode, float cutoff)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL || (int)mode < WGF_ALPHA_MODE_OPAQUE || mode > WGF_ALPHA_MODE_PREMULTIPLIED) return false;
    sprite_ptr->alpha_mode = (int)mode;
    sprite_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : (cutoff > 1.0f ? 1.0f : cutoff);
    return true;
}

wgf_alpha_mode_t wgf_sprite_get_alpha_mode(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? (wgf_alpha_mode_t)sprite_ptr->alpha_mode : WGF_ALPHA_MODE_OPAQUE;
}

float wgf_sprite_get_alpha_cutoff(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? sprite_ptr->alpha_cutoff : 0.0f;
}

bool wgf_sprite_set_facing(wgf_actor_t sprite, wgf_sprite_facing_t facing)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL || (int)facing < WGF_SPRITE_FACING_CAMERA || facing > WGF_SPRITE_FACING_FREE) return false;
    sprite_ptr->facing = (int)facing;
    return true;
}

wgf_sprite_facing_t wgf_sprite_get_facing(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? (wgf_sprite_facing_t)sprite_ptr->facing : WGF_SPRITE_FACING_CAMERA;
}

bool wgf_sprite_set_depth_offset(wgf_actor_t sprite, float offset)
{
    wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    if (sprite_ptr == NULL || !isfinite(offset)) return false;
    sprite_ptr->depth_offset = offset;
    return true;
}

float wgf_sprite_get_depth_offset(wgf_actor_t sprite)
{
    const wgf_gfx_priv_sprite_t *sprite_ptr = sprite_of(sprite);
    return sprite_ptr != NULL ? sprite_ptr->depth_offset : 0.0f;
}
