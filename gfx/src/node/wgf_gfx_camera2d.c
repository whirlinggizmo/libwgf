#include "wgf_camera2d.h"

#include <stddef.h>

#include "node/wgf_gfx_node_priv.h"

wgf_node_t wgf_camera2d_create(void)
{
    const wgf_node_t camera = wgf_gfx_priv_node_create(WGF_NODE_TYPE_CAMERA2D);
    wgf_gfx_priv_node_t *camera_ptr = wgf_gfx_priv_node_of(camera);
    if (camera_ptr != NULL) camera_ptr->as.camera2d_zoom = 1.0f;
    return camera;
}

bool wgf_camera2d_set_zoom(wgf_node_t camera, float zoom)
{
    wgf_gfx_priv_node_t *camera_ptr = wgf_gfx_priv_node_of(camera);
    if (camera_ptr == NULL || camera_ptr->type != WGF_NODE_TYPE_CAMERA2D || !(zoom > 0.0f)) return false;
    camera_ptr->as.camera2d_zoom = zoom;
    return true;
}

float wgf_camera2d_get_zoom(wgf_node_t camera)
{
    const wgf_gfx_priv_node_t *camera_ptr = wgf_gfx_priv_node_of(camera);
    return camera_ptr != NULL && camera_ptr->type == WGF_NODE_TYPE_CAMERA2D ? camera_ptr->as.camera2d_zoom : 0.0f;
}
