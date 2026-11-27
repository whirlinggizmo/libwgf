#include "wgf_stage2d.h"

#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_camera2d.h"
#include "wgf_presentation.h"
#include "wgf_log.h"
#include "wgf_render.h"

/* 2D stages: a 2D tree drawn depth first through the frame's immediate mode recording,
 * each actor's world transform computed on the way down, with a list rather than C
 * recursion so a deep tree can't run out of stack. Each type with something to draw
 * draws through its kind (wgf_gfx_actor_priv.h), so the stage names no part. libwgt's,
 * without picking or clip actors. */

wgf_actor_t wgf_stage2d_create(void)
{
    return wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_STAGE2D);
}

bool wgf_stage2d_set_camera(wgf_actor_t stage, wgf_actor_t camera)
{
    wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage);
    if (stage_ptr == NULL || stage_ptr->type != WGF_ACTOR_KIND_STAGE2D) return false;
    if (camera != 0 && wgf_actor_get_kind(camera) != WGF_ACTOR_KIND_CAMERA2D) return false;
    stage_ptr->as.stage2d.camera = camera;
    return true;
}

wgf_actor_t wgf_stage2d_get_camera(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage);
    if (stage_ptr == NULL || stage_ptr->type != WGF_ACTOR_KIND_STAGE2D) return 0;
    /* a camera destroyed since leaves the stage with none */
    return wgf_actor_get_kind(stage_ptr->as.stage2d.camera) == WGF_ACTOR_KIND_CAMERA2D ? stage_ptr->as.stage2d.camera
                                                                                      : 0;
}

/* Stage units to the frame's logical pixels: none without a camera; with one, its
 * world position to the frame's center, turned back by its angle and zoomed. */
static wgf_mat4_t view_in(wgf_actor_t stage, float x, float y, float width, float height)
{
    const wgf_actor_t camera = wgf_stage2d_get_camera(stage);
    wgf_mat4_t world, view, step;
    wgf_vec3_t position;
    float zoom, angle;
    if (camera == 0) return wgf_mat4_identity();
    world = wgf_gfx_priv_actor_get_world_matrix(camera);
    position = wgf_mat4_get_translation(world);
    angle = wgf_quat_to_euler(wgf_mat4_get_rotation(world)).z;
    zoom = wgf_camera2d_get_zoom(camera);
    /* center * zoom * turn back * move the camera to the origin, applied right to left */
    view = wgf_mat4_from_trs(wgf_vec3_make(x + width * 0.5f, y + height * 0.5f, 0.0f),
                             wgf_quat_from_euler(wgf_vec3_make(0.0f, 0.0f, -angle)), wgf_vec3_make(zoom, zoom, 1.0f));
    step = wgf_mat4_from_trs(wgf_vec3_make(-position.x, -position.y, 0.0f), wgf_quat_identity(),
                             wgf_vec3_make(1.0f, 1.0f, 1.0f));
    return wgf_mat4_mul(view, step);
}

/* In the frame's visible area: the camera looks at what is visible's center. */
static wgf_mat4_t view_of(wgf_actor_t stage)
{
    float x, y, width, height;
    wgf_gfx_priv_render_get_visible(&x, &y, &width, &height);
    return view_in(stage, x, y, width, height);
}

/* In the presentation's visible area now: the screen's, outside a frame too. */
static wgf_mat4_t view_now(wgf_actor_t stage)
{
    const wgf_vec4_t visible = wgf_presentation_get_visible();
    return view_in(stage, visible.x, visible.y, visible.z, visible.w);
}

/* An actor the walk is in: its record, its children (holes closed), the next of them to
 * draw, and its world matrix through the stage's view, which its children are placed in. */
typedef struct frame_t {
    wgf_gfx_priv_actor_t *actor_ptr;
    const wgf_actor_t *children;
    int count, next;
    wgf_mat4_t placed;
} frame_t;

/* Kept from draw to draw, as a frame's stages walk the same trees. */
static frame_t *frames;
static int frame_capacity;

/* `actor` drawn, its world matrix made clean under `parent_ptr` (NULL for the stage); its
 * children, if it has any, then the next frame of the walk (`*count` one more). False when
 * there is no room for that frame: its children aren't drawn. */
static bool enter(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr, const wgf_gfx_priv_actor_t *parent_ptr,
                  const wgf_mat4_t *view, bool viewed, int *count)
{
    const wgf_gfx_priv_actor_kind_t *kind;
    const wgf_actor_t *dense;
    int dense_count;
    wgf_mat4_t world_of_stage, placed;
    const wgf_mat4_t *world;
    /* kept from the last draw unless something moved: top down, a dirty actor's parent
       was just made clean, so this rebuilds one matrix at most. A hidden actor still
       carries its children. */
    if (parent_ptr != NULL) {
        world = wgf_gfx_priv_actor_world_under(actor_ptr, parent_ptr);
    } else {
        world_of_stage = wgf_gfx_priv_actor_world_of(actor, actor_ptr);
        world = &world_of_stage;
    }
    placed = viewed ? wgf_mat4_mul(*view, *world) : *world;
    kind = actor_ptr->visible ? wgf_gfx_priv_actor_get_kind(actor_ptr->type) : NULL;
    if (kind != NULL && kind->draw != NULL) kind->draw(actor, actor_ptr, &placed, view);
    dense = wgf_gfx_priv_actor_children_of(actor_ptr, &dense_count);
    if (dense_count == 0) return true;
    if (*count == frame_capacity) {
        const int capacity = frame_capacity > 0 ? frame_capacity * 2 : 16;
        frame_t *grown = (frame_t *)realloc(frames, sizeof(frame_t) * (size_t)capacity);
        if (grown == NULL) {
            wgf_log_error("wgf_gfx_stage2d: out of memory drawing a stage");
            return false;
        }
        frames = grown;
        frame_capacity = capacity;
    }
    frames[*count].actor_ptr = actor_ptr;
    frames[*count].children = dense;
    frames[*count].count = dense_count;
    frames[*count].next = 0;
    frames[*count].placed = placed;
    (*count)++;
    return true;
}

void wgf_stage2d_draw(wgf_actor_t stage)
{
    wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage);
    wgf_mat4_t view;
    bool viewed;
    int count = 0;

    if (stage_ptr == NULL || stage_ptr->type != WGF_ACTOR_KIND_STAGE2D || !wgf_gfx_priv_is_in_frame()) return;
    wgf_gfx_priv_render_set_2d(); /* vertices go in already placed */
    wgf_gfx_priv_render_note_drawn(&stage_ptr->as.stage2d.drawn_frame, &stage_ptr->as.stage2d.drawn_order);
    view = view_of(stage);
    { /* no camera: an actor is placed by its world matrix alone */
        const wgf_mat4_t identity = wgf_mat4_identity();
        viewed = memcmp(&view, &identity, sizeof(view)) != 0;
    }
    wgf_gfx_priv_texture_hold_bindings(true); /* neighbouring sprites mostly share a texture */
    if (stage_ptr->enabled) enter(stage, stage_ptr, NULL, &view, viewed, &count);
    /* depth first, each actor's children in order, each drawn before the next's: an actor
       with none is drawn by its kind's fast way when it has one (draw_leaf_2d), which
       places it in its parent without making its own world matrix */
    while (count > 0) {
        frame_t *top = &frames[count - 1];
        wgf_actor_t child;
        /* looked up once: drawing creates no actor, so the record stays where it is */
        wgf_gfx_priv_actor_t *child_ptr;
        if (top->next == top->count) {
            count--;
            continue;
        }
        child = top->children[top->next++];
        child_ptr = wgf_gfx_priv_actor_of(child);
        if (child_ptr == NULL || !child_ptr->enabled) continue; /* with its children */
        if (child_ptr->live_children == 0) {
            const wgf_gfx_priv_actor_kind_t *kind;
            if (!child_ptr->visible) continue;
            kind = wgf_gfx_priv_actor_get_kind(child_ptr->type);
            if (kind != NULL && kind->draw_leaf_2d != NULL && kind->draw_leaf_2d(child_ptr, &top->placed)) continue;
        }
        enter(child, child_ptr, top->actor_ptr, &view, viewed, &count); /* may move `frames` */
    }
    wgf_gfx_priv_texture_hold_bindings(false);
}

wgf_actor_t wgf_stage2d_find(wgf_actor_t stage, const char *name)
{
    if (wgf_actor_get_kind(stage) != WGF_ACTOR_KIND_STAGE2D) return 0;
    return wgf_gfx_priv_actor_find_on_stage(stage, name);
}

wgf_vec2_t wgf_stage2d_world_to_screen(wgf_actor_t stage, float x, float y)
{
    wgf_vec3_t at;
    if (wgf_actor_get_kind(stage) != WGF_ACTOR_KIND_STAGE2D) return wgf_vec2_make(0.0f, 0.0f);
    at = wgf_mat4_transform_point(view_now(stage), wgf_vec3_make(x, y, 0.0f));
    return wgf_vec2_make(at.x, at.y);
}

wgf_vec2_t wgf_stage2d_screen_to_world(wgf_actor_t stage, float x, float y)
{
    wgf_vec3_t at;
    if (wgf_actor_get_kind(stage) != WGF_ACTOR_KIND_STAGE2D) return wgf_vec2_make(0.0f, 0.0f);
    at = wgf_mat4_transform_point(wgf_mat4_invert(view_now(stage)), wgf_vec3_make(x, y, 0.0f));
    return wgf_vec2_make(at.x, at.y);
}
