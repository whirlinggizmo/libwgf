#include "wgf_canvas.h"

#include <stdlib.h>

#include "node/wgf_gfx_node_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_camera2d.h"
#include "wgf_log.h"
#include "wgf_render.h"

/* Canvases: a 2D tree drawn depth first through the frame's immediate mode recording,
 * each node's world transform computed on the way down, with a list rather than C
 * recursion so a deep tree can't run out of stack. Each type with something to draw
 * draws through its kind (wgf_gfx_node_priv.h), so the canvas names no part. libwgt's,
 * without picking or clip nodes. */

wgf_node_t wgf_canvas_create(void)
{
    return wgf_gfx_priv_node_create(WGF_NODE_TYPE_CANVAS);
}

bool wgf_canvas_set_camera(wgf_node_t canvas, wgf_node_t camera)
{
    wgf_gfx_priv_node_t *canvas_ptr = wgf_gfx_priv_node_of(canvas);
    if (canvas_ptr == NULL || canvas_ptr->type != WGF_NODE_TYPE_CANVAS) return false;
    if (camera != 0 && wgf_node_get_type(camera) != WGF_NODE_TYPE_CAMERA2D) return false;
    canvas_ptr->as.canvas.camera = camera;
    return true;
}

wgf_node_t wgf_canvas_get_camera(wgf_node_t canvas)
{
    const wgf_gfx_priv_node_t *canvas_ptr = wgf_gfx_priv_node_of(canvas);
    if (canvas_ptr == NULL || canvas_ptr->type != WGF_NODE_TYPE_CANVAS) return 0;
    /* a camera destroyed since leaves the canvas with none */
    return wgf_node_get_type(canvas_ptr->as.canvas.camera) == WGF_NODE_TYPE_CAMERA2D ? canvas_ptr->as.canvas.camera
                                                                                      : 0;
}

/* Canvas units to the frame's logical pixels: none without a camera; with one, its
 * world position to the frame's center, turned back by its angle and zoomed. */
static wgf_mat4_t view_of(wgf_node_t canvas)
{
    const wgf_node_t camera = wgf_canvas_get_camera(canvas);
    wgf_mat4_t world, view, step;
    wgf_vec3_t position;
    float zoom, angle, x, y, width, height;
    if (camera == 0) return wgf_mat4_identity();
    wgf_gfx_priv_render_get_visible(&x, &y, &width, &height); /* the camera looks at what is visible's center */
    world = wgf_gfx_priv_node_get_world_matrix(camera);
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

void wgf_canvas_draw(wgf_node_t canvas)
{
    wgf_gfx_priv_node_t *canvas_ptr = wgf_gfx_priv_node_of(canvas);
    wgf_node_t *todo;
    wgf_mat4_t view;
    int count = 0, capacity = 64;

    if (canvas_ptr == NULL || canvas_ptr->type != WGF_NODE_TYPE_CANVAS || !wgf_gfx_priv_is_in_frame()) return;
    todo = (wgf_node_t *)malloc(sizeof(wgf_node_t) * (size_t)capacity);
    if (todo == NULL) {
        wgf_log_error("wgf_gfx_canvas: out of memory drawing a canvas");
        return;
    }
    wgf_gfx_priv_render_set_2d(); /* vertices go in already placed */
    view = view_of(canvas);
    todo[count++] = canvas;
    while (count > 0) {
        const wgf_node_t next = todo[--count];
        /* looked up once: drawing creates no node, so the record stays where it is */
        wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(next);
        const wgf_gfx_priv_node_kind_t *kind;
        const wgf_node_t *dense;
        int i, dense_count;
        if (node_ptr == NULL || !node_ptr->enabled) continue; /* with its children */
        /* kept from the last draw unless something moved: top down, a dirty node's parent
           was just made clean, so this rebuilds one matrix at most. A hidden node still
           carries its children. */
        {
            const wgf_mat4_t world = wgf_gfx_priv_node_world_of(next, node_ptr);
            kind = node_ptr->visible ? wgf_gfx_priv_node_get_kind(node_ptr->type) : NULL;
            if (kind != NULL && kind->draw != NULL) {
                const wgf_mat4_t placed = wgf_mat4_mul(view, world);
                kind->draw(next, node_ptr, &placed, &view);
            }
        }
        /* children last first onto the list, so the first comes off first */
        dense = wgf_gfx_priv_node_children_of(node_ptr, &dense_count); /* holes closed */
        for (i = dense_count - 1; i >= 0; i--) {
            if (count == capacity) {
                wgf_node_t *grown = (wgf_node_t *)realloc(todo, sizeof(wgf_node_t) * (size_t)(capacity * 2));
                if (grown == NULL) break;
                todo = grown;
                capacity *= 2;
            }
            todo[count++] = dense[i];
        }
    }
    free(todo);
}
