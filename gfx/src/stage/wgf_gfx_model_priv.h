#ifndef WGF_GFX_MODEL_PRIV_H
#define WGF_GFX_MODEL_PRIV_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_material.h"
#include "wgf_mesh.h"

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

/* What a failed model draws, the file part's (a unit cube of the placeholder checker, unlit):
 * false in a program without glTF, where no mesh fails. */
bool wgf_gfx_priv_model_placeholder(wgf_mesh_t *mesh, wgf_material_t *material);

#endif
