#include "wgf_stage2d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "draw/wgf_gfx_rounded_priv.h"
#include "text/wgf_gfx_font_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_handle.h"
#include "wgf_image.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"

/* Picking on a 2D stage, libwgt's canvas pick: the screen's point taken into the stage
 * through its camera, then into each actor's own units through its world transform, and
 * tested against what the actor draws -- a sprite's quad (and its texels' alpha, with a
 * threshold), a 2D shape's rectangle, circle, or polygon, a text's block. The stage is
 * walked as it is drawn, so the last hit is the topmost. A sprite's texels come from an
 * image (wgf_image.h) of its texture's file, made the first time a pick needs them and kept
 * here, one a texture, while the texture lives: a part of its own, which lets them go at
 * gfx's stop, before the images. Its own file, so a program that never picks links none of
 * it. */

#define HIT_FLOATS 4 /* the point in the stage's units, then in the actor's own */

typedef struct alpha_t {
    wgf_texture_t texture;
    wgf_image_t image;
} alpha_t;

static struct {
    alpha_t *items;
    int count, capacity;
    bool installed;
} alphas;

static void stop(void)
{
    int i;
    for (i = 0; i < alphas.count; i++) wgf_resource_release(alphas.items[i].image);
    free(alphas.items);
    memset(&alphas, 0, sizeof(alphas));
}

static wgf_core_priv_part_t part = {.name = "picks",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_PICKS,
                                    .stop = stop};

/* The image of `texture`'s file, asked for the first time it is needed; 0 for a texture with
 * no file, or no room. Images of textures gone are let go of on the way. */
static wgf_image_t alpha_image(wgf_texture_t texture)
{
    const char *path;
    int i = 0;
    while (i < alphas.count) {
        if (alphas.items[i].texture == texture && wgf_handle_is_alive(texture)) return alphas.items[i].image;
        if (!wgf_handle_is_alive(alphas.items[i].texture)) {
            wgf_resource_release(alphas.items[i].image);
            alphas.items[i] = alphas.items[--alphas.count];
        } else {
            i++;
        }
    }
    path = wgf_resource_get_path(texture);
    if (path == NULL || path[0] == '\0') return 0;
    if (alphas.count == alphas.capacity) {
        const int capacity = alphas.capacity > 0 ? alphas.capacity * 2 : 8;
        alpha_t *grown = (alpha_t *)realloc(alphas.items, sizeof(alpha_t) * (size_t)capacity);
        if (grown == NULL) return 0;
        alphas.items = grown;
        alphas.capacity = capacity;
    }
    if (!alphas.installed) {
        wgf_core_priv_part_install(&part);
        alphas.installed = true;
    }
    alphas.items[alphas.count].texture = texture;
    alphas.items[alphas.count].image = wgf_image_create(path);
    return alphas.items[alphas.count++].image;
}

/* Whether the sprite's texel under (x, y), in its own units within `quad`, is solid enough;
 * true until its texels can be read. */
static bool solid_at(const wgf_gfx_priv_sprite_t *sprite, const float quad[4], const float region[4], float x, float y)
{
    const wgf_image_t image = alpha_image(sprite->texture);
    const unsigned char *data;
    int iw, ih, tw, th, tx, ty;
    float u, v;
    if (image == 0 || wgf_resource_get_status(image) != WGF_RESOURCE_STATUS_READY) return true;
    data = wgf_image_get_data(image);
    iw = wgf_image_get_width(image);
    ih = wgf_image_get_height(image);
    tw = wgf_texture_get_width(sprite->texture);
    th = wgf_texture_get_height(sprite->texture);
    if (data == NULL || iw <= 0 || ih <= 0 || tw <= 0 || th <= 0 || quad[2] == quad[0] || quad[3] == quad[1]) {
        return true;
    }
    u = (region[0] + (x - quad[0]) / (quad[2] - quad[0]) * region[2]) / (float)tw; /* across the whole texture */
    v = (region[1] + (y - quad[1]) / (quad[3] - quad[1]) * region[3]) / (float)th;
    tx = (int)floorf(u * (float)iw);
    ty = (int)floorf(v * (float)ih);
    tx = tx < 0 ? 0 : (tx >= iw ? iw - 1 : tx);
    ty = ty < 0 ? 0 : (ty >= ih ? ih - 1 : ty);
    return (float)data[((size_t)ty * (size_t)iw + (size_t)tx) * 4 + 3] >= sprite->pick_alpha * 255.0f;
}

static bool in_polygon(const float *points, int floats, float x, float y)
{
    const int n = floats / 2;
    bool inside = false;
    int i, j;
    for (i = 0, j = n - 1; i < n; j = i++) {
        const float xi = points[2 * i], yi = points[2 * i + 1], xj = points[2 * j], yj = points[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

/* Whether a 2D shape covers its own point (x, y): its area, filled or outlined. */
static bool shape_hit(const wgf_gfx_priv_shape2d_t *shape, float x, float y)
{
    float dx = 0.0f, dy = 0.0f;
    if (shape->has_pivot && shape->kind == WGF_SHAPE2D_KIND_RECTANGLE) {
        dx = -shape->pivot[0] * shape->dim[0];
        dy = -shape->pivot[1] * shape->dim[1];
    } else if (shape->has_pivot && shape->kind == WGF_SHAPE2D_KIND_CIRCLE) {
        dx = (0.5f - shape->pivot[0]) * 2.0f * shape->dim[0];
        dy = (0.5f - shape->pivot[1]) * 2.0f * shape->dim[0];
    }
    x -= dx;
    y -= dy;
    if (shape->kind == WGF_SHAPE2D_KIND_RECTANGLE) {
        const float w = shape->dim[0], h = shape->dim[1];
        const float r = wgf_gfx_priv_rounded_radius(w, h, shape->corner);
        float cx, cy;
        if (x < 0.0f || y < 0.0f || x > w || y > h) return false;
        cx = x < r ? r : (x > w - r ? w - r : x); /* the nearest point of the rectangle inset by the radius */
        cy = y < r ? r : (y > h - r ? h - r : y);
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
    }
    if (shape->kind == WGF_SHAPE2D_KIND_CIRCLE) return x * x + y * y <= shape->dim[0] * shape->dim[0];
    if (shape->kind == WGF_SHAPE2D_KIND_POLYGON) return in_polygon(shape->points, shape->point_floats, x, y);
    return false;
}

static bool text_hit(const wgf_gfx_priv_text_t *text, float x, float y)
{
    float left = 0.0f, top = 0.0f, width = 0.0f, height = 0.0f;
    bool known;
    if (text->string == NULL) return false;
    wgf_gfx_priv_font_set_spacing(true, text->letter_spacing, text->line_spacing);
    known = wgf_gfx_priv_font_block_bounds(text->font, text->string, text->size, text->wrap_width, text->halign,
                                           text->valign, &left, &top, &width, &height);
    wgf_gfx_priv_font_set_spacing(false, 0.0f, 0.0f);
    return known && x >= left && y >= top && x <= left + width && y <= top + height;
}

/* Whether `actor_ptr` covers its own point (x, y). */
static bool actor_hit(const wgf_gfx_priv_actor_t *actor_ptr, float x, float y)
{
    if (actor_ptr->type == WGF_ACTOR_KIND_SPRITE) {
        float quad[4] = {0}, region[4] = {0};
        bool whole = false;
        if (!wgf_gfx_priv_sprite_quad_2d(actor_ptr, quad, region, &whole)) return false;
        if (x < fminf(quad[0], quad[2]) || x > fmaxf(quad[0], quad[2]) || y < fminf(quad[1], quad[3]) ||
            y > fmaxf(quad[1], quad[3])) {
            return false;
        }
        return whole || actor_ptr->as.sprite.pick_alpha <= 0.0f || solid_at(&actor_ptr->as.sprite, quad, region, x, y);
    }
    if (actor_ptr->type == WGF_ACTOR_KIND_SHAPE2D) return shape_hit(&actor_ptr->as.shape2d, x, y);
    if (actor_ptr->type == WGF_ACTOR_KIND_TEXT) return text_hit(&actor_ptr->as.text, x, y);
    return false;
}

wgf_actor_t wgf_stage2d_pick(wgf_actor_t stage, float x, float y, float *hit, int hit_count)
{
    float found[HIT_FLOATS] = {0.0f, 0.0f, 0.0f, 0.0f};
    wgf_actor_t *todo, picked = 0;
    wgf_vec2_t at;
    int count = 0, capacity = 64, i;
    const wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage); /* a dead one warned of */
    const bool is_stage = stage_ptr != NULL && stage_ptr->type == WGF_ACTOR_KIND_STAGE2D;
    if (is_stage && (todo = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)capacity)) != NULL) {
        at = wgf_stage2d_screen_to_world(stage, x, y);
        todo[count++] = stage;
        while (count > 0) { /* as the stage draws: a parent, then its children in order */
            const wgf_actor_t next = todo[--count];
            wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_silently(next);
            const wgf_actor_t *dense;
            int dense_count;
            if (actor_ptr == NULL || !actor_ptr->enabled) continue;
            if (actor_ptr->visible && actor_ptr->pickable &&
                (actor_ptr->type == WGF_ACTOR_KIND_SPRITE || actor_ptr->type == WGF_ACTOR_KIND_SHAPE2D ||
                 actor_ptr->type == WGF_ACTOR_KIND_TEXT)) {
                const wgf_mat4_t inverse = wgf_mat4_invert(wgf_gfx_priv_actor_world_of(next, actor_ptr));
                const wgf_vec3_t local = wgf_mat4_transform_point(inverse, wgf_vec3_make(at.x, at.y, 0.0f));
                if (isfinite(local.x) && isfinite(local.y) && actor_hit(actor_ptr, local.x, local.y)) {
                    picked = next; /* drawn later, over what was hit before */
                    found[0] = at.x;
                    found[1] = at.y;
                    found[2] = local.x;
                    found[3] = local.y;
                }
                actor_ptr = wgf_gfx_priv_actor_of_silently(next); /* an image made moves no actor, but all the same */
                if (actor_ptr == NULL) continue;
            }
            dense = wgf_gfx_priv_actor_children_of(actor_ptr, &dense_count);
            for (i = dense_count - 1; i >= 0; i--) {
                if (count == capacity) {
                    wgf_actor_t *grown = (wgf_actor_t *)realloc(todo, sizeof(wgf_actor_t) * (size_t)(capacity * 2));
                    if (grown == NULL) break;
                    todo = grown;
                    capacity *= 2;
                }
                todo[count++] = dense[i];
            }
        }
        free(todo);
    } else if (is_stage) {
        wgf_log_error("wgf_gfx_pick2d: out of memory picking");
    }
    for (i = 0; hit != NULL && i < hit_count && i < HIT_FLOATS; i++) hit[i] = found[i];
    return picked;
}
