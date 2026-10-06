#ifndef WGF_ECS_SCENE_PRIV_H
#define WGF_ECS_SCENE_PRIV_H

#include <stddef.h>

/* A scene file's text parsed (wgf_ecs_scene.c): NULL, each bad line logged naming
 * `path`, when it breaks the format (wgf_scene.h); freed with _free. Any thread. */
typedef struct wgf_ecs_priv_scene_data_t wgf_ecs_priv_scene_data_t;
wgf_ecs_priv_scene_data_t *wgf_ecs_priv_scene_parse(const char *text, size_t size, const char *path);
void wgf_ecs_priv_scene_data_free(wgf_ecs_priv_scene_data_t *data);

/* Every scene freed, with the ecs's stop. */
void wgf_ecs_priv_scene_shutdown(void);

#endif
