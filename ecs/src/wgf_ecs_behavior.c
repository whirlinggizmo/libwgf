#include "wgf_behavior.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_ecs_priv.h"

/* Behaviors (wgf_behavior.h): a name and parameters in the entity's record; their
 * lifecycle's events are raised where it happens (wgf_ecs_entity.c, wgf_ecs.c). */

static wgf_ecs_priv_behavior_t *behavior_of(wgf_entity_t entity)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    return record != NULL ? record->behavior : NULL;
}

static bool fits(const char *text, size_t most, bool empty_ok)
{
    const size_t n = text != NULL ? strlen(text) : 0;
    return text != NULL && n < most && (empty_ok || n > 0);
}

bool wgf_behavior_set_name(wgf_entity_t entity, const char *name)
{
    wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    if (b == NULL || (name != NULL && !fits(name, WGF_ECS_PRIV_NAME_MAX, true))) return false;
    memcpy(b->name, name != NULL ? name : "", name != NULL ? strlen(name) + 1 : 1);
    return true;
}

const char *wgf_behavior_get_name(wgf_entity_t entity)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    return b != NULL ? b->name : "";
}

static int find(const wgf_ecs_priv_behavior_t *b, const char *key)
{
    int i;
    for (i = 0; i < b->param_count; i++) {
        if (strcmp(b->params[i].key, key) == 0) return i;
    }
    return -1;
}

bool wgf_behavior_set_param(wgf_entity_t entity, const char *key, const char *value)
{
    wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    int at;
    if (b == NULL || !fits(key, WGF_ECS_PRIV_NAME_MAX, false)) return false;
    at = find(b, key);
    if (value == NULL) { /* removed, the order kept */
        if (at < 0) return false;
        memmove(&b->params[at], &b->params[at + 1], sizeof(b->params[0]) * (size_t)(b->param_count - at - 1));
        b->param_count--;
        return true;
    }
    if (!fits(value, WGF_ECS_PRIV_VALUE_MAX, true)) return false;
    if (at < 0) {
        if (b->param_count == WGF_ECS_PRIV_PARAMS_MAX) return false;
        at = b->param_count++;
        memcpy(b->params[at].key, key, strlen(key) + 1);
    }
    memcpy(b->params[at].value, value, strlen(value) + 1);
    return true;
}

const char *wgf_behavior_get_param(wgf_entity_t entity, const char *key)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    const int at = b != NULL && key != NULL ? find(b, key) : -1;
    return at >= 0 ? b->params[at].value : "";
}

double wgf_behavior_get_param_number(wgf_entity_t entity, const char *key)
{
    const char *text = wgf_behavior_get_param(entity, key);
    char *end;
    double value;
    if (text[0] == '\0') return 0.0;
    errno = 0;
    value = strtod(text, &end);
    return errno == 0 && *end == '\0' && isfinite(value) ? value : 0.0;
}

bool wgf_behavior_has_param(wgf_entity_t entity, const char *key)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    return b != NULL && key != NULL && find(b, key) >= 0;
}

int wgf_behavior_get_param_count(wgf_entity_t entity)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    return b != NULL ? b->param_count : 0;
}

const char *wgf_behavior_get_param_key(wgf_entity_t entity, int index)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(entity);
    return b != NULL && index >= 0 && index < b->param_count ? b->params[index].key : "";
}
