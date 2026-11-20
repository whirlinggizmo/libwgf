#ifndef WGF_CORE_JSON_PRIV_H
#define WGF_CORE_JSON_PRIV_H

#include <stdbool.h>
#include <stddef.h>

/* JSON read (cJSON, deps/cjson), wgutils-c's wrapper's shape: a parsed tree, its values read
 * by kind. What libwgf reads outside glTF (cgltf's) and its manifests (their own reader): a
 * sprite sheet's atlas, a Tiled map. An object's members keep the text's order. Any thread. */

typedef struct wgf_core_priv_json_t wgf_core_priv_json_t;

/* The text's tree, or NULL for text that isn't JSON (or out of memory). Freed with free. */
wgf_core_priv_json_t *wgf_core_priv_json_parse(const char *text, size_t length);
void wgf_core_priv_json_free(wgf_core_priv_json_t *json);

/* An object's member named `key`; an array's or object's `index`th value; how many it holds
 * (0 for a value that isn't either). NULL for none. */
const wgf_core_priv_json_t *wgf_core_priv_json_get(const wgf_core_priv_json_t *object, const char *key);
const wgf_core_priv_json_t *wgf_core_priv_json_at(const wgf_core_priv_json_t *container, int index);
int wgf_core_priv_json_size(const wgf_core_priv_json_t *container);

bool wgf_core_priv_json_is_object(const wgf_core_priv_json_t *value);
bool wgf_core_priv_json_is_array(const wgf_core_priv_json_t *value);
bool wgf_core_priv_json_is_number(const wgf_core_priv_json_t *value);
bool wgf_core_priv_json_is_string(const wgf_core_priv_json_t *value);

/* A value as a number (`fallback` for one that isn't), a string ("" for one that isn't), a
 * truth (true for `true` alone), and an object member's key ("" for a value that isn't one). */
double wgf_core_priv_json_number(const wgf_core_priv_json_t *value, double fallback);
const char *wgf_core_priv_json_string(const wgf_core_priv_json_t *value);
bool wgf_core_priv_json_truth(const wgf_core_priv_json_t *value);
const char *wgf_core_priv_json_key(const wgf_core_priv_json_t *member);

/* An object's member read as a number or a string, as above. */
double wgf_core_priv_json_get_number(const wgf_core_priv_json_t *object, const char *key, double fallback);
const char *wgf_core_priv_json_get_string(const wgf_core_priv_json_t *object, const char *key);

#endif /* WGF_CORE_JSON_PRIV_H */
