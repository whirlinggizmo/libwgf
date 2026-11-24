#ifndef WGF_ECS_SCENE_PRIV_H
#define WGF_ECS_SCENE_PRIV_H

#include <stdbool.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_ecs_priv.h"
#include "wgf_scene.h"

/* A scene file's text parsed (wgf_ecs_scene.c): NULL, each bad line logged naming
 * `path`, when it breaks the format (wgf_scene.h); freed with _free. Any thread. */
typedef struct wgf_ecs_priv_scene_data_t wgf_ecs_priv_scene_data_t;
wgf_ecs_priv_scene_data_t *wgf_ecs_priv_scene_parse(const char *text, size_t size, const char *path);
void wgf_ecs_priv_scene_data_free(wgf_ecs_priv_scene_data_t *data);

/* Every scene freed, with the ecs's stop. */
void wgf_ecs_priv_scene_shutdown(void);

/* ---- the parsed file, and what the scene's reload reaches -------------------------------
 * The scene file (wgf_ecs_scene.c) parses and makes; loading a scene again in place
 * (wgf_ecs_scene_reload.c, linked only by wgf_scene_enable_reload) compares a file's blocks
 * with what they were and changes what the scene made, through these. */

#define WGF_ECS_PRIV_SCENE_PATH_MAX 256
#define WGF_ECS_PRIV_SCENE_DEPTH_MAX 32 /* actors inside actors */

/* What a line kind is: a transform, an actor's kind, a component, or a behavior. */
enum {
    WGF_ECS_PRIV_SCENE_ROLE_TRANSFORM,
    WGF_ECS_PRIV_SCENE_ROLE_KIND,
    WGF_ECS_PRIV_SCENE_ROLE_COMPONENT,
    WGF_ECS_PRIV_SCENE_ROLE_BEHAVIOR
};

typedef struct wgf_ecs_priv_scene_setting_t {
    char key[WGF_ECS_PRIV_NAME_MAX];
    char *value; /* its text, unquoted; malloc'd */
} wgf_ecs_priv_scene_setting_t;

typedef struct wgf_ecs_priv_scene_line_t {
    int kind; /* into the table of line kinds */
    int number;
    wgf_ecs_priv_scene_setting_t *settings;
    int count;
} wgf_ecs_priv_scene_line_t;

typedef struct wgf_ecs_priv_scene_thing_t { /* a block: a prefab's actor or a scene's, its parent named by its path */
    char name[WGF_ECS_PRIV_NAME_MAX];        /* its path's last part */
    char path[WGF_ECS_PRIV_SCENE_PATH_MAX];  /* "ship/flame": its parent's path, then its name */
    bool prefab;
    int parent; /* its parent's block, into things; -1 at the top */
    int from;   /* the prefab it starts as, into things; -1 none */
    int kind;   /* its kind line's kind; -1 none: a plain actor, or its prefab's kind */
    wgf_ecs_priv_scene_line_t *lines;
    int count, capacity;
} wgf_ecs_priv_scene_thing_t;

struct wgf_ecs_priv_scene_data_t {
    wgf_ecs_priv_scene_thing_t *things;
    int count, capacity;
    int actors, prefabs; /* at the top */
};

/* What a scene made, kept where scene reload is enabled: an actor with its block (an index
 * into its scene's data, moved along as the file loads again), and a parent an instantiate
 * put the scene's top actors under. */
typedef struct wgf_ecs_priv_scene_made_t {
    wgf_scene_t scene; /* 0: let go of */
    int thing;
    wgf_actor_t actor;
    bool node; /* a glTF file's node the block's lines were put on: the file's, never removed */
} wgf_ecs_priv_scene_made_t;

typedef struct wgf_ecs_priv_scene_placed_t {
    wgf_scene_t scene;
    wgf_actor_t parent; /* 0: none */
} wgf_ecs_priv_scene_placed_t;

/* A line kind's role (WGF_ECS_PRIV_SCENE_ROLE_*) and what it is (an actor's kind, a
 * component); -1 for one that isn't. */
int wgf_ecs_priv_scene_role(int kind);
int wgf_ecs_priv_scene_what(int kind);
/* The block at `path` (a prefab's or an actor's), into the data's things; -1 none. */
int wgf_ecs_priv_scene_find_path(const wgf_ecs_priv_scene_data_t *data, const char *path, bool prefab);
/* A block's kind line's kind, its own or its prefab's; -1 none. */
int wgf_ecs_priv_scene_kind_of(const wgf_ecs_priv_scene_data_t *data, int thing);
/* One setting of a line of `kind` applied to `actor` (a behavior's parameter to `behavior`),
 * and a voice line's sound, as making an actor applies them. */
void wgf_ecs_priv_scene_apply(wgf_actor_t actor, int kind, const wgf_ecs_priv_scene_setting_t *setting,
                              int behavior);
void wgf_ecs_priv_scene_apply_sound(wgf_actor_t actor, const wgf_ecs_priv_scene_line_t *line);
/* Block `thing`'s actor made under `parent` with its children, as instantiate makes it. */
wgf_actor_t wgf_ecs_priv_scene_make(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t parent,
                                    int depth);
/* The references in the behaviors of `actor` and everything under it found. */
void wgf_ecs_priv_scene_resolve(wgf_actor_t actor);
/* The scene whose blocks are being made, set: the one before. */
wgf_scene_t wgf_ecs_priv_scene_set_making(wgf_scene_t scene);
/* What scenes made, and where they were instantiated: valid until an actor is made. */
wgf_ecs_priv_scene_made_t *wgf_ecs_priv_scene_made(int *count);
wgf_ecs_priv_scene_placed_t *wgf_ecs_priv_scene_placed(int *count);
/* The records of actors gone, and of scenes let go of, dropped; and while `on`, none
 * dropped, the records walked by index as a file loads again. */
void wgf_ecs_priv_scene_compact(void);
void wgf_ecs_priv_scene_set_changing(bool on);
/* What a scene's file loaded again calls (wgf_scene_enable_reload sets it): until then scenes
 * don't load again, and nothing is kept for it. */
void wgf_ecs_priv_scene_set_reload(void (*again)(wgf_scene_t scene, const wgf_ecs_priv_scene_data_t *was,
                                                const wgf_ecs_priv_scene_data_t *now));
/* `scene`'s file loaded again: its prefabs' handles and what waits for a glTF file following
 * their blocks by their paths. */
void wgf_ecs_priv_scene_follow(wgf_scene_t scene, const wgf_ecs_priv_scene_data_t *was,
                               const wgf_ecs_priv_scene_data_t *now);

#endif
