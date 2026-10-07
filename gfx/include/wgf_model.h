#ifndef WGF_MODEL_H
#define WGF_MODEL_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A model: a node showing a mesh on a stage (wgf_stage.h), each of the mesh's material
 * slots drawn with the mesh's material or one of the model's own (wgf_material.h), lit
 * by the stage's lights. It holds a reference to its mesh and to its own materials.
 * Place it with the node calls. In a canvas it draws nothing. */

/* A model of `mesh`; 0 for none yet. 0 when `mesh` isn't a mesh, or there is no room
 * for another node. Tinted white: as its materials are. */
WGF_API wgf_node_t wgf_model_create(wgf_mesh_t mesh);

/* The model's mesh; 0 is none, and nothing drawn. False when `mesh` isn't a mesh. */
WGF_API bool wgf_model_set_mesh(wgf_node_t model, wgf_mesh_t mesh);
WGF_API wgf_mesh_t wgf_model_get_mesh(wgf_node_t model);

/* A color its materials are multiplied by, as wgrender's tint (default white); alpha
 * below 255 makes the model see-through, blended back to front. */
WGF_API bool wgf_model_set_tint(wgf_node_t model, wgf_color_t color);
WGF_API wgf_color_t wgf_model_get_tint(wgf_node_t model);

/* Draw material slot `slot` (0..31) with `material` instead of the mesh's; -1 sets every
 * slot, and 0 goes back to the mesh's. The model holds its own reference; its materials
 * stay when the mesh changes. False for a slot out of range, or a handle that isn't a
 * material. get_material is the material the slot draws with: borrowed, the model's
 * own or the mesh's, 0 for neither. */
WGF_API bool wgf_model_set_material(wgf_node_t model, int slot, wgf_material_t material);
WGF_API wgf_material_t wgf_model_get_material(wgf_node_t model, int slot);

#ifdef __cplusplus
}
#endif

#endif
