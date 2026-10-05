#ifndef WGF_CORE_RESOURCE_PRIV_H
#define WGF_CORE_RESOURCE_PRIV_H

#include <stdbool.h>

#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_resource.h"

/* The resource core, ported from wgrender's wgr_resource: what every resource kind
 * shares, so each module keeps only what is its own (wgf_resource.h says what a
 * resource is). A resource module's records start with a wgf_core_priv_resource_t; it
 * registers its handle pool with a description, and the core does the reference
 * counting, finding a resource by the path it was created from, the load request, the
 * status and the path, and release. wgrender's asset layer marks a load READY or
 * FAILED; here the module does, from its loader's finish and fail
 * (wgf_core_priv_resource_loaded / _failed), since a texture runs loads of its own
 * beside its file's (its alpha mask) that mustn't. */

typedef struct wgf_core_priv_resource_t {
    int refs;
    wgf_resource_status_t status;
    bool permanent;                         /* a built-in: never freed, and release does nothing */
    char path[WGF_CORE_PRIV_FS_PATH_MAX];  /* the path it was created from, normalized, which finds it again; "" none */
    char found[WGF_CORE_PRIV_FS_PATH_MAX]; /* the file it is read from (shown once READY) */
} wgf_core_priv_resource_t;

typedef struct wgf_core_priv_resource_kind_t {
    const char *create; /* the public create's name, for logs: "wgf_texture_create" */
    /* The file to read for `path` (normalized) into `out`, when it isn't `path` itself (a
     * texture's "name.ktx": this GPU's variant), or NULL. */
    void (*map)(const char *path, char *out, size_t out_size);
    /* The loader for the file at `path` (normalized, mapped); NULL for a kind made only from numbers. */
    const wgf_core_priv_loader_t *(*loader)(const char *path);
    /* A new record's defaults past the header (zeroed), or NULL. */
    void (*init)(void *record);
    /* Free what a record holds past the header (GPU objects, memory, references). */
    void (*free)(wgf_handle_t resource, void *record);
    /* For a kind whose records another thread reads (audio's, read by its mixer), or
     * NULL: held while a record is made (the pool may move) and while one is freed, its
     * free included, which so must not take it again. As wgrender's resource kinds. */
    void (*lock)(void);
    void (*unlock)(void);
} wgf_core_priv_resource_kind_t;

/* Register the resources in `pool`, whose records start with a wgf_core_priv_resource_t,
 * for its handle kind; `kind` must outlive the registration. */
void wgf_core_priv_resource_register(wgf_core_priv_handle_pool_t *pool, const wgf_core_priv_resource_kind_t *kind);

/* Free every resource in `pool` (at shutdown: built-ins too) and forget the kind; the
 * module then destroys its pool. */
void wgf_core_priv_resource_unregister(wgf_core_priv_handle_pool_t *pool);

/* The header of a live resource, or NULL (no warning) for anything else. Don't hold it
 * across a create: the records move when a pool grows. */
wgf_core_priv_resource_t *wgf_core_priv_resource_get(wgf_handle_t resource);

/* Load on create: the resource of `kind` at `path`, as wgf_resource.h says. A path
 * already created gives the same handle with one more reference; otherwise a new
 * record, PENDING, its file requested of the load pipeline with the kind's loader (a
 * bad path or a missing file FAILED in a later update). 0 only when the kind isn't
 * registered or its pool is full. */
wgf_handle_t wgf_core_priv_resource_create(wgf_core_priv_handle_kind_t kind, const char *path);

/* The live resource of `kind` created from `path`, with one more reference, or 0: for a
 * module that may have one ready without loading it (a font fontstash still has). */
wgf_handle_t wgf_core_priv_resource_find(wgf_core_priv_handle_kind_t kind, const char *path);

/* Make `resource` (one just added) the one created from `path`, READY and read from it,
 * so a later create of `path` finds it. */
void wgf_core_priv_resource_set_path(wgf_handle_t resource, const char *path);

/* A resource made from numbers: a new record of `kind` with no path, READY, holding one
 * reference; the module fills in the rest. 0 when the pool is full. */
wgf_handle_t wgf_core_priv_resource_add(wgf_core_priv_handle_kind_t kind);

void wgf_core_priv_resource_retain(wgf_handle_t resource);

/* From the module's loader, when the load into `resource` is done: READY, read from
 * `found` (a path under fs's root; NULL: the file it was asked to read), or FAILED. */
void wgf_core_priv_resource_loaded(wgf_handle_t resource, const char *found);

/* Where a loading resource's file was found, when its load found it elsewhere than its
 * path said (a redirect, a URL's key): what wgf_resource_get_path shows once READY. */
void wgf_core_priv_resource_set_found(wgf_handle_t resource, const char *found);
void wgf_core_priv_resource_failed(wgf_handle_t resource);

#endif
