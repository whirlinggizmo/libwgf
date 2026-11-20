#ifndef WGF_GFX_MODEL_PRIV_H
#define WGF_GFX_MODEL_PRIV_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_material.h"
#include "wgf_mat4.h"
#include "wgf_mesh.h"
#include "wgf_vec3.h"

struct wgf_gfx_priv_animation_t;
struct wgf_gfx_priv_mesh_skin_t;

/* A file's mesh done loading (READY or FAILED), or loaded again, from glTF's loader: the
 * models of it make their trees now, or make them again in place (or, failed, stay empty,
 * drawn as the placeholder). */
void wgf_gfx_priv_model_mesh_done(wgf_mesh_t mesh);

/* The file part (wgf_gfx_model_file.c): a file root's tree, made once its mesh is READY,
 * glTF's create, which a scene's `model path=` loads through, and what a failed model
 * draws (only a file's mesh can fail). The models reach it through these, installed as the
 * program starts by glTF's file when the program links it (WGF_CORE_PRIV_ON_LINK), so a
 * program that never calls wgf_mesh_create links none of glTF (27.8 KB of gzip on
 * gfx-meshes, and the placeholder's 1 KB). */
typedef struct wgf_gfx_priv_model_file_t {
    bool (*add)(wgf_actor_t root);                   /* a root kept: its tree made as its mesh loads; false out of memory */
    void (*forget)(wgf_actor_t root);                /* as it goes */
    void (*build)(wgf_actor_t root, wgf_mesh_t mesh); /* its tree, its mesh READY: made, or made again in place */
    wgf_mesh_t (*create)(const char *path);
    bool (*placeholder)(wgf_mesh_t *mesh, wgf_material_t *material); /* a failed model's; false when none */
} wgf_gfx_priv_model_file_t;
void wgf_gfx_priv_model_set_file(const wgf_gfx_priv_model_file_t *part);
void wgf_gfx_priv_model_file_install(void);

/* A file root's tree (the file part's build makes it): each of its file's nodes' actor, by
 * the node's index (0: one not made, or destroyed since), and, a skin at a time, where the
 * stage last put its joint matrices among the frame's (`serial` the frame's number, `base`
 * the first matrix) and the box around its posed vertices in the root's space. */
typedef struct wgf_gfx_priv_model_tree_t {
    wgf_actor_t *nodes;
    int node_count;
    int skin_count;
    unsigned *joint_serial;
    int *joint_base; /* -1: none this frame (no room, or nothing to place) */
    wgf_vec3_t *skin_lo, *skin_hi;
} wgf_gfx_priv_model_tree_t;

/* Joint `joint` of `skin` (one of the file's skins, the tree's root's) as a skinned part is
 * posed by it, on the GPU (the stage) and on the CPU (immediate mode's model draws): the
 * joint's actor's world matrix taken into the root's space (`to_root`, the inverse of the
 * root's world matrix) times the joint's inverse bind matrix; the identity for a joint not
 * made, or destroyed since. */
wgf_mat4_t wgf_gfx_priv_model_joint_matrix(const wgf_gfx_priv_model_tree_t *tree,
                                           const struct wgf_gfx_priv_mesh_skin_t *skin, int joint,
                                           const wgf_mat4_t *to_root);

/* A file root's tree let go of (its model's free), and its animation player, through the
 * animation part's hook (set as it installs; NULL before, when there is none). */
void wgf_gfx_priv_model_tree_free(wgf_gfx_priv_model_tree_t *tree);
void wgf_gfx_priv_model_set_animation_free(void (*free_animation)(struct wgf_gfx_priv_animation_t *animation));

/* What a failed model draws, the file part's (a unit cube of the placeholder checker, unlit):
 * false in a program without glTF, where no mesh fails. */
bool wgf_gfx_priv_model_placeholder(wgf_mesh_t *mesh, wgf_material_t *material);

#endif
