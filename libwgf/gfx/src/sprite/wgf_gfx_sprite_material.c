#include "actor/wgf_gfx_actor_priv.h"
#include "material/wgf_gfx_material_priv.h"
#include "sprite/wgf_gfx_sprite_batch_priv.h"
#include "stage/wgf_gfx_stage3d_draw_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_handle.h"
#include "wgf_mesh.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"

/* A sprite drawn with a material (wgf_sprite_set_material): on a 3D stage, a quad shaded as
 * a model's part is, an item of the stage's (wgf_gfx_stage3d.c's material_sprite). This file
 * is the quad, the item's making, and the call; the stage reaches it through the hook the
 * call sets, so a program whose sprites have no material links none of it. */

/* The sprites' quad: one unit across in x and y about its middle, facing +z, its texture
 * coordinates 0, 0 at its top left and 1, 1 at its bottom right, as a sprite's region runs.
 * Made the first time one is drawn, and again after gfx has stopped and started. */
static wgf_mesh_t quad;

static wgf_mesh_t quad_mesh(void)
{
    static const float positions[12] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    static const float normals[12] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    static const float uvs[8] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    static const int indices[6] = {0, 1, 2, 0, 2, 3}; /* counterclockwise from its front */
    if (!wgf_handle_is_alive(quad)) quad = wgf_mesh_create_triangles(positions, 12, normals, 12, uvs, 8, indices, 6);
    return quad;
}

/* The stage's hook (wgf_gfx_stage3d_priv.h): the quad placed by the facing's axes, the
 * sprite's size, and its pivot, as wgf_gfx_sprite.glsl places a sprite's own. */
static bool fill_item(const wgf_gfx_priv_actor_t *sprite, const wgf_gfx_priv_sprite_quad_t *q,
                      const wgf_gfx_priv_sprite_view_t *view, wgf_gfx_priv_stage3d_item_t *item)
{
    const int mode = sprite->as.sprite.alpha_mode;
    wgf_vec3_t right, up, normal, center;
    if (q->facing < 0.5f) {
        right = view->right;
        up = view->up;
    } else if (q->facing < 1.5f) {
        right = view->upright;
        up = wgf_vec3_make(0.0f, 1.0f, 0.0f);
    } else {
        right = wgf_vec3_make(q->right[0], q->right[1], q->right[2]);
        up = wgf_vec3_make(q->up[0], q->up[1], q->up[2]);
    }
    normal = wgf_vec3_cross(right, up);
    if (wgf_vec3_length(normal) < 1e-12f) return false; /* a quad with no face */
    normal = wgf_vec3_normalize(normal);
    /* the pivot sits on the position: the quad's middle is off it by what is left each way */
    center = wgf_vec3_add(wgf_vec3_make(q->position[0], q->position[1], q->position[2]),
                          wgf_vec3_add(wgf_vec3_scale(right, (0.5f - q->pivot[0]) * q->size[0]),
                                       wgf_vec3_scale(up, (q->pivot[1] - 0.5f) * q->size[1])));
    right = wgf_vec3_scale(right, q->size[0]);
    up = wgf_vec3_scale(up, q->size[1]);
    item->world.m[0] = right.x, item->world.m[1] = right.y, item->world.m[2] = right.z;
    item->world.m[4] = up.x, item->world.m[5] = up.y, item->world.m[6] = up.z;
    item->world.m[8] = normal.x, item->world.m[9] = normal.y, item->world.m[10] = normal.z;
    item->world.m[12] = center.x, item->world.m[13] = center.y, item->world.m[14] = center.z;
    item->mirrored = wgf_mat4_determinant(item->world) < 0.0f;
    item->mesh = quad_mesh();
    item->material = sprite->as.sprite.material;
    item->material_sprite = true;
    item->uv[0] = q->uv[2] - q->uv[0];
    item->uv[1] = q->uv[3] - q->uv[1];
    item->uv[2] = q->uv[0];
    item->uv[3] = q->uv[1];
    item->cutoff = mode == WGF_ALPHA_MODE_MASK ? sprite->as.sprite.alpha_cutoff : 0.0f;
    /* MASK and OPAQUE: with the opaque parts */
    item->blended = mode == WGF_ALPHA_MODE_BLEND || mode == WGF_ALPHA_MODE_ADD;
    return item->mesh != 0;
}

bool wgf_sprite_set_material(wgf_actor_t sprite, wgf_material_t material)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(sprite);
    wgf_material_t *held;
    if (actor_ptr == NULL || actor_ptr->type != WGF_ACTOR_KIND_SPRITE) return false;
    if (material != 0 && wgf_gfx_priv_material_get(material) == NULL) return false;
    if (material != 0) wgf_gfx_priv_set_sprite_item(fill_item); /* the stage's way here */
    held = &actor_ptr->as.sprite.material;
    if (*held == material) return true;
    if (material != 0) wgf_core_priv_resource_retain(material);
    if (*held != 0) wgf_resource_release(*held);
    *held = material;
    return true;
}

wgf_material_t wgf_sprite_get_material(wgf_actor_t sprite)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(sprite);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_SPRITE ? actor_ptr->as.sprite.material : 0;
}
