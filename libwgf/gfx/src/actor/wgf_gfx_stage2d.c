#include "wgf_stage2d.h"

#include <stdlib.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_camera2d.h"
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
static wgf_mat4_t view_of(wgf_actor_t stage)
{
    const wgf_actor_t camera = wgf_stage2d_get_camera(stage);
    wgf_mat4_t world, view, step;
    wgf_vec3_t position;
    float zoom, angle, x, y, width, height;
    if (camera == 0) return wgf_mat4_identity();
    wgf_gfx_priv_render_get_visible(&x, &y, &width, &height); /* the camera looks at what is visible's center */
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

void wgf_stage2d_draw(wgf_actor_t stage)
{
    wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage);
    wgf_actor_t *todo;
    wgf_mat4_t view;
    int count = 0, capacity = 64;

    if (stage_ptr == NULL || stage_ptr->type != WGF_ACTOR_KIND_STAGE2D || !wgf_gfx_priv_is_in_frame()) return;
    todo = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)capacity);
    if (todo == NULL) {
        wgf_log_error("wgf_gfx_stage2d: out of memory drawing a stage");
        return;
    }
    wgf_gfx_priv_render_set_2d(); /* vertices go in already placed */
    view = view_of(stage);
    todo[count++] = stage;
    while (count > 0) {
        const wgf_actor_t next = todo[--count];
        /* looked up once: drawing creates no actor, so the record stays where it is */
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(next);
        const wgf_gfx_priv_actor_kind_t *kind;
        const wgf_actor_t *dense;
        int i, dense_count;
        if (actor_ptr == NULL || !actor_ptr->enabled) continue; /* with its children */
        /* kept from the last draw unless something moved: top down, a dirty actor's parent
           was just made clean, so this rebuilds one matrix at most. A hidden actor still
           carries its children. */
        {
            const wgf_mat4_t world = wgf_gfx_priv_actor_world_of(next, actor_ptr);
            kind = actor_ptr->visible ? wgf_gfx_priv_actor_get_kind(actor_ptr->type) : NULL;
            if (kind != NULL && kind->draw != NULL) {
                const wgf_mat4_t placed = wgf_mat4_mul(view, world);
                kind->draw(next, actor_ptr, &placed, &view);
            }
        }
        /* children last first onto the list, so the first comes off first */
        dense = wgf_gfx_priv_actor_children_of(actor_ptr, &dense_count); /* holes closed */
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
}

wgf_actor_t wgf_stage2d_find(wgf_actor_t stage, const char *name)
{
    if (wgf_actor_get_kind(stage) != WGF_ACTOR_KIND_STAGE2D) return 0;
    return wgf_gfx_priv_actor_find_on_stage(stage, name);
}
