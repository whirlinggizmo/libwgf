#include "wgf_camera3d.h"

#include <stddef.h>

#include "actor/wgf_gfx_actor_priv.h"

/* 3D cameras, libwgt's: their settings, and what they see as a matrix. */

#define PI 3.14159265358979323846f

static wgf_gfx_priv_actor_t *camera_of_at(wgf_actor_t camera, const char *caller)
{
    wgf_gfx_priv_actor_t *camera_ptr = wgf_gfx_priv_actor_of_at(camera, caller);
    return camera_ptr != NULL && camera_ptr->type == WGF_ACTOR_KIND_CAMERA3D ? camera_ptr : NULL;
}
#define camera_of(...) camera_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

wgf_actor_t wgf_camera3d_create(void)
{
    const wgf_actor_t camera = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_CAMERA3D);
    wgf_gfx_priv_actor_t *camera_ptr = wgf_gfx_priv_actor_of(camera);
    if (camera_ptr != NULL) {
        camera_ptr->as.camera3d.fov = PI / 3.0f;
        camera_ptr->as.camera3d.near_z = 0.1f;
        camera_ptr->as.camera3d.far_z = 1000.0f;
        camera_ptr->as.camera3d.ortho_height = 10.0f;
    }
    return camera;
}

bool wgf_camera3d_set_fov(wgf_actor_t camera, float radians)
{
    wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    if (camera_ptr == NULL) return false;
    camera_ptr->as.camera3d.fov = radians >= 0.01f ? (radians <= 3.13f ? radians : 3.13f) : 0.01f;
    return true;
}

float wgf_camera3d_get_fov(wgf_actor_t camera)
{
    const wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    return camera_ptr != NULL ? camera_ptr->as.camera3d.fov : 0.0f;
}

bool wgf_camera3d_set_clip(wgf_actor_t camera, float near_z, float far_z)
{
    wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    if (camera_ptr == NULL || !(near_z > 0.0f && far_z > near_z)) return false;
    camera_ptr->as.camera3d.near_z = near_z;
    camera_ptr->as.camera3d.far_z = far_z;
    return true;
}

float wgf_camera3d_get_near(wgf_actor_t camera)
{
    const wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    return camera_ptr != NULL ? camera_ptr->as.camera3d.near_z : 0.0f;
}

float wgf_camera3d_get_far(wgf_actor_t camera)
{
    const wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    return camera_ptr != NULL ? camera_ptr->as.camera3d.far_z : 0.0f;
}

bool wgf_camera3d_set_orthographic(wgf_actor_t camera, bool orthographic)
{
    wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    if (camera_ptr == NULL) return false;
    camera_ptr->as.camera3d.orthographic = orthographic;
    return true;
}

bool wgf_camera3d_is_orthographic(wgf_actor_t camera)
{
    const wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    return camera_ptr != NULL && camera_ptr->as.camera3d.orthographic;
}

bool wgf_camera3d_set_ortho_height(wgf_actor_t camera, float height)
{
    wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    if (camera_ptr == NULL || !(height > 0.0f)) return false;
    camera_ptr->as.camera3d.ortho_height = height;
    return true;
}

float wgf_camera3d_get_ortho_height(wgf_actor_t camera)
{
    const wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    return camera_ptr != NULL ? camera_ptr->as.camera3d.ortho_height : 0.0f;
}

wgf_mat4_t wgf_gfx_priv_camera3d_view_projection(wgf_actor_t camera, float aspect, wgf_vec3_t *position)
{
    const wgf_gfx_priv_actor_t *camera_ptr = camera_of(camera);
    wgf_gfx_priv_camera3d_t c;
    wgf_mat4_t world, view, projection;
    if (camera_ptr == NULL) return wgf_mat4_identity();
    c = camera_ptr->as.camera3d;
    world = wgf_gfx_priv_actor_get_world_matrix(camera);
    *position = wgf_mat4_get_translation(world);
    /* the camera's world transform without its scale: a scaled camera still sees */
    view = wgf_mat4_invert(wgf_mat4_from_trs(*position, wgf_mat4_get_rotation(world), wgf_vec3_make(1.0f, 1.0f, 1.0f)));
    if (c.orthographic) {
        const float half_h = c.ortho_height * 0.5f, half_w = half_h * aspect;
        projection = wgf_mat4_orthographic(-half_w, half_w, -half_h, half_h, c.near_z, c.far_z);
    } else {
        projection = wgf_mat4_perspective(c.fov, aspect, c.near_z, c.far_z);
    }
    return wgf_mat4_mul(projection, view);
}
