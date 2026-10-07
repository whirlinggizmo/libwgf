#ifndef WGF_ECS_STORE_PRIV_H
#define WGF_ECS_STORE_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The components' store: what the ecs keeps its plain data in, behind one interface so
 * the storage could be chosen by measurement: plain sparse sets (wgf_ecs_store.c), which
 * replaced flecs (docs/HISTORY.md, "flecs or sparse sets, measured"). An entity is a row of
 * components; a component is a size (0: a tag, carrying nothing); a query walks the
 * entities that have every one of up to 3 components, a row at a time, with a pointer to
 * each one's data. The store is the main thread's. Nothing may be added to or removed
 * from an entity, or an entity deleted, while a query walks. */

#define WGF_ECS_PRIV_STORE_TERMS_MAX 3

typedef uint64_t wgf_ecs_priv_id_t; /* an entity or a component; 0 is none */

/* Made with the ecs's first record, and gone with its stop: every entity, component, and
 * query with it. False when out of memory. */
bool wgf_ecs_priv_store_start(void);
void wgf_ecs_priv_store_stop(void);

/* A component of `size` bytes aligned to `alignment`; 0 bytes: a tag. 0 when out of room. */
wgf_ecs_priv_id_t wgf_ecs_priv_store_component(size_t size, size_t alignment);

/* An entity with nothing, and one deleted with its components. */
wgf_ecs_priv_id_t wgf_ecs_priv_store_new(void);
void wgf_ecs_priv_store_delete(wgf_ecs_priv_id_t entity);

/* `component` set on `entity` from `data` (its size; NULL zeroes it, and a tag ignores
 * it), added the first time; its data, NULL for a tag or when out of memory. And asked
 * about, read for writing (NULL when it hasn't it, or for a tag), and removed. */
void *wgf_ecs_priv_store_set(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component, const void *data);
bool wgf_ecs_priv_store_has(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component);
void *wgf_ecs_priv_store_get(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component);
void wgf_ecs_priv_store_remove(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component);

/* How many entities have `component`. */
int wgf_ecs_priv_store_count(wgf_ecs_priv_id_t component);

/* A query of the entities with every one of `count` (1 to 3) components, made once and
 * walked as often as wanted; NULL when out of memory. */
typedef struct wgf_ecs_priv_query_t wgf_ecs_priv_query_t;
wgf_ecs_priv_query_t *wgf_ecs_priv_store_query(const wgf_ecs_priv_id_t *components, int count);
void wgf_ecs_priv_store_query_free(wgf_ecs_priv_query_t *query);

/* A query walked: begun (a walk not run to its end before let go of), then each next
 * fills `fields` (the query's count of them, each a component's data, NULL for a tag)
 * and the entity, false at the end. One walk of a query at a time. */
void wgf_ecs_priv_store_walk(wgf_ecs_priv_query_t *query);
bool wgf_ecs_priv_store_next(wgf_ecs_priv_query_t *query, void **fields, wgf_ecs_priv_id_t *entity);

#endif
