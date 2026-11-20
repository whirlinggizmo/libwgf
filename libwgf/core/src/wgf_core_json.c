#include "wgf_core_json_priv.h"

#include "cJSON.h"
#include "wgf_core_thread_priv.h"

/* JSON through cJSON (wgf_core_json_priv.h): its tree, its node the wrapper's value. cJSON is
 * compiled with it, in wgf_core_cjson_impl.c. */

#define NODE(value) ((const cJSON *)(const void *)(value))
#define VALUE(node) ((const wgf_core_priv_json_t *)(const void *)(node))

static wgf_core_priv_mutex_t lock;
static bool locking;

void wgf_core_priv_json_init(void)
{
    if (locking) return;
    wgf_core_priv_mutex_init(&lock);
    locking = true;
}

void wgf_core_priv_json_deinit(void)
{
    if (!locking) return;
    wgf_core_priv_mutex_destroy(&lock);
    locking = false;
}

wgf_core_priv_json_t *wgf_core_priv_json_parse(const char *text, size_t length)
{
    cJSON *json;
    if (text == NULL) return NULL;
    if (locking) wgf_core_priv_mutex_lock(&lock);
    json = cJSON_ParseWithLength(text, length);
    if (locking) wgf_core_priv_mutex_unlock(&lock);
    return (wgf_core_priv_json_t *)(void *)json;
}

void wgf_core_priv_json_free(wgf_core_priv_json_t *json)
{
    cJSON_Delete((cJSON *)(void *)json);
}

const wgf_core_priv_json_t *wgf_core_priv_json_get(const wgf_core_priv_json_t *object, const char *key)
{
    return object != NULL && key != NULL && cJSON_IsObject(NODE(object))
               ? VALUE(cJSON_GetObjectItemCaseSensitive(NODE(object), key))
               : NULL;
}

const wgf_core_priv_json_t *wgf_core_priv_json_at(const wgf_core_priv_json_t *container, int index)
{
    return container != NULL && index >= 0 ? VALUE(cJSON_GetArrayItem(NODE(container), index)) : NULL;
}

int wgf_core_priv_json_size(const wgf_core_priv_json_t *container)
{
    return container != NULL && (cJSON_IsArray(NODE(container)) || cJSON_IsObject(NODE(container)))
               ? cJSON_GetArraySize(NODE(container))
               : 0;
}

bool wgf_core_priv_json_is_object(const wgf_core_priv_json_t *value)
{
    return value != NULL && cJSON_IsObject(NODE(value));
}

bool wgf_core_priv_json_is_array(const wgf_core_priv_json_t *value)
{
    return value != NULL && cJSON_IsArray(NODE(value));
}

bool wgf_core_priv_json_is_number(const wgf_core_priv_json_t *value)
{
    return value != NULL && cJSON_IsNumber(NODE(value));
}

bool wgf_core_priv_json_is_string(const wgf_core_priv_json_t *value)
{
    return value != NULL && cJSON_IsString(NODE(value));
}

double wgf_core_priv_json_number(const wgf_core_priv_json_t *value, double fallback)
{
    return value != NULL && cJSON_IsNumber(NODE(value)) ? NODE(value)->valuedouble : fallback;
}

const char *wgf_core_priv_json_string(const wgf_core_priv_json_t *value)
{
    return value != NULL && cJSON_IsString(NODE(value)) && NODE(value)->valuestring != NULL ? NODE(value)->valuestring
                                                                                            : "";
}

bool wgf_core_priv_json_truth(const wgf_core_priv_json_t *value)
{
    return value != NULL && cJSON_IsTrue(NODE(value));
}

const char *wgf_core_priv_json_key(const wgf_core_priv_json_t *member)
{
    return member != NULL && NODE(member)->string != NULL ? NODE(member)->string : "";
}

double wgf_core_priv_json_get_number(const wgf_core_priv_json_t *object, const char *key, double fallback)
{
    return wgf_core_priv_json_number(wgf_core_priv_json_get(object, key), fallback);
}

const char *wgf_core_priv_json_get_string(const wgf_core_priv_json_t *object, const char *key)
{
    return wgf_core_priv_json_string(wgf_core_priv_json_get(object, key));
}
