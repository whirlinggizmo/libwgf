#include "wgf_behavior.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_ecs_priv.h"
#include "wgf_log.h"

/* Behaviors (wgf_behavior.h): a list on the actor's record, each its name, its parameters,
 * and its id; CREATED raised as one is added, DESTROYED as it is removed or its actor goes
 * (wgf_ecs.c's record_free). */

static bool fits(const char *text, size_t most, bool empty_ok)
{
    const size_t n = text != NULL ? strlen(text) : 0;
    return text != NULL && n < most && (empty_ok || n > 0);
}

static int index_of(const wgf_ecs_priv_record_t *record, int behavior)
{
    int b;
    for (b = 0; record != NULL && b < record->behavior_count; b++) {
        if (record->behaviors[b]->id == behavior) return b;
    }
    return -1;
}

static wgf_ecs_priv_behavior_t *behavior_of(wgf_actor_t actor, int behavior)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    const int at = index_of(record, behavior);
    return at >= 0 ? record->behaviors[at] : NULL;
}

int wgf_actor_add_behavior(wgf_actor_t actor, const char *name)
{
    wgf_ecs_priv_record_t *record;
    wgf_ecs_priv_behavior_t *b;
    wgf_ecs_priv_id_t tag;
    if (!fits(name, WGF_ECS_PRIV_NAME_MAX, false)) return 0;
    record = wgf_ecs_priv_record_make(actor);
    if (record == NULL) return 0;
    if (record->behavior_count == record->behavior_capacity) {
        const int capacity = record->behavior_capacity > 0 ? record->behavior_capacity * 2 : 2;
        wgf_ecs_priv_behavior_t **grown = (wgf_ecs_priv_behavior_t **)realloc(
            record->behaviors, sizeof(wgf_ecs_priv_behavior_t *) * (size_t)capacity);
        if (grown == NULL) return 0;
        record->behaviors = grown;
        record->behavior_capacity = capacity;
    }
    b = (wgf_ecs_priv_behavior_t *)calloc(1, sizeof(wgf_ecs_priv_behavior_t));
    if (b == NULL) {
        wgf_log_error("wgf_actor_add_behavior: out of memory");
        return 0;
    }
    tag = wgf_ecs_priv_behavior_tag(name, true);
    if (tag == 0) {
        free(b);
        wgf_log_error("wgf_actor_add_behavior: out of memory");
        return 0;
    }
    wgf_ecs_priv_store_set(record->id, tag, NULL); /* once, however many of the name */
    b->id = record->next_behavior++;
    memcpy(b->name, name, strlen(name) + 1);
    record->behaviors[record->behavior_count++] = b;
    wgf_ecs_priv_raise(WGF_ECS_EVENT_CREATED, (int)actor, b->id, 0);
    return b->id;
}

bool wgf_actor_remove_behavior(wgf_actor_t actor, int behavior)
{
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    const int at = index_of(record, behavior);
    int b;
    if (at < 0) return false;
    for (b = 0; b < record->behavior_count; b++) { /* the last of its name: the tag goes */
        if (b != at && strcmp(record->behaviors[b]->name, record->behaviors[at]->name) == 0) break;
    }
    if (b == record->behavior_count) {
        wgf_ecs_priv_store_remove(record->id,
                      wgf_ecs_priv_behavior_tag(record->behaviors[at]->name, false));
    }
    wgf_ecs_priv_behavior_free(record->behaviors[at]);
    memmove(&record->behaviors[at], &record->behaviors[at + 1],
            sizeof(record->behaviors[0]) * (size_t)(record->behavior_count - at - 1));
    record->behavior_count--;
    wgf_ecs_priv_raise(WGF_ECS_EVENT_DESTROYED, (int)actor, behavior, 0);
    return true;
}

int wgf_actor_get_behavior_count(wgf_actor_t actor)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    return record != NULL ? record->behavior_count : 0;
}

int wgf_actor_get_behavior(wgf_actor_t actor, int index)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    return record != NULL && index >= 0 && index < record->behavior_count ? record->behaviors[index]->id : 0;
}

int wgf_actor_find_behavior(wgf_actor_t actor, const char *name)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    int b;
    for (b = 0; record != NULL && name != NULL && b < record->behavior_count; b++) {
        if (strcmp(record->behaviors[b]->name, name) == 0) return record->behaviors[b]->id;
    }
    return 0;
}

const char *wgf_behavior_get_name(wgf_actor_t actor, int behavior)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    return b != NULL ? b->name : "";
}

/* The actor a reference refers to from `actor`, the text after its '@': steps joined by '/'.
 * A first step of "." or ".." starts at the actor itself or its parent (and ".." again
 * goes up again); any other first step is a name on the actor's stage (the one actor so
 * named there). Each step after is a child's name. 0 for none. */
static wgf_actor_t resolve(wgf_actor_t actor, const char *reference)
{
    char step[WGF_ECS_PRIV_VALUE_MAX];
    const char *p = reference;
    wgf_actor_t at = actor;
    bool first = true, relative = false;
    while (at != 0) {
        const char *slash = strchr(p, '/');
        const size_t length = slash != NULL ? (size_t)(slash - p) : strlen(p);
        memcpy(step, p, length);
        step[length] = '\0';
        if (first && strcmp(step, ".") == 0) {
            relative = true; /* from the actor itself */
        } else if ((first || relative) && strcmp(step, "..") == 0) {
            relative = true;
            at = wgf_actor_get_parent(at);
        } else if (first) {
            at = length > 0 ? wgf_gfx_priv_actor_find_on_stage(wgf_gfx_priv_actor_get_root(actor), step) : 0;
        } else {
            relative = false; /* ".." only before the first name */
            at = wgf_actor_find(at, step);
        }
        if (slash == NULL) return at;
        first = false;
        p = slash + 1;
    }
    return 0;
}

void wgf_ecs_priv_behaviors_resolve(wgf_actor_t actor)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    int b, i;
    for (b = 0; record != NULL && b < record->behavior_count; b++) {
        wgf_ecs_priv_behavior_t *behavior = record->behaviors[b];
        for (i = 0; i < behavior->param_count; i++) {
            wgf_ecs_priv_param_t *param = &behavior->params[i];
            if (param->value[0] != '@') continue;
            param->actor = resolve(actor, param->value + 1);
            if (param->actor == 0) {
                wgf_log_warn("wgf_scene: %s's %s=%s refers to no actor (a name on the stage, unique there, or a "
                             "path from one)",
                             behavior->name, param->key, param->value);
            }
        }
    }
}

static int find(const wgf_ecs_priv_behavior_t *b, const char *key)
{
    int i;
    for (i = 0; i < b->param_count; i++) {
        if (strcmp(b->params[i].key, key) == 0) return i;
    }
    return -1;
}

void wgf_ecs_priv_behavior_free(wgf_ecs_priv_behavior_t *behavior)
{
    int i;
    if (behavior == NULL) return;
    for (i = 0; i < behavior->param_count; i++) {
        free(behavior->params[i].key);
        free(behavior->params[i].value);
    }
    free(behavior->params);
    free(behavior);
}

static char *copy_of(const char *text)
{
    const size_t n = strlen(text) + 1;
    char *copy = (char *)malloc(n);
    if (copy != NULL) memcpy(copy, text, n);
    return copy;
}

bool wgf_behavior_set_param(wgf_actor_t actor, int behavior, const char *key, const char *value)
{
    wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    char *copy;
    int at;
    if (b == NULL || !fits(key, WGF_ECS_PRIV_NAME_MAX, false)) return false;
    at = find(b, key);
    if (value == NULL) { /* removed, the order kept */
        if (at < 0) return false;
        free(b->params[at].key);
        free(b->params[at].value);
        memmove(&b->params[at], &b->params[at + 1], sizeof(b->params[0]) * (size_t)(b->param_count - at - 1));
        b->param_count--;
        return true;
    }
    if (!fits(value, WGF_ECS_PRIV_VALUE_MAX, true)) return false;
    copy = copy_of(value);
    if (copy == NULL) return false;
    if (at < 0) {
        if (b->param_count == WGF_ECS_PRIV_PARAMS_MAX) {
            free(copy);
            return false;
        }
        if (b->param_count == b->param_capacity) {
            const int capacity = b->param_capacity > 0 ? b->param_capacity * 2 : 2;
            wgf_ecs_priv_param_t *grown =
                (wgf_ecs_priv_param_t *)realloc(b->params, sizeof(wgf_ecs_priv_param_t) * (size_t)capacity);
            if (grown == NULL) {
                free(copy);
                return false;
            }
            b->params = grown;
            b->param_capacity = capacity;
        }
        b->params[b->param_count].key = copy_of(key);
        if (b->params[b->param_count].key == NULL) {
            free(copy);
            return false;
        }
        b->params[b->param_count].value = NULL;
        at = b->param_count++;
    }
    free(b->params[at].value);
    b->params[at].value = copy;
    b->params[at].actor = value[0] == '@' ? resolve(actor, value + 1) : 0;
    return true;
}

const char *wgf_behavior_get_param(wgf_actor_t actor, int behavior, const char *key)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    const int at = b != NULL && key != NULL ? find(b, key) : -1;
    return at >= 0 ? b->params[at].value : "";
}

double wgf_behavior_get_param_number(wgf_actor_t actor, int behavior, const char *key)
{
    const char *text = wgf_behavior_get_param(actor, behavior, key);
    char *end;
    double value;
    if (text[0] == '\0') return 0.0;
    errno = 0;
    value = strtod(text, &end);
    return errno == 0 && *end == '\0' && isfinite(value) ? value : 0.0;
}

wgf_actor_t wgf_behavior_get_param_actor(wgf_actor_t actor, int behavior, const char *key)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    const int at = b != NULL && key != NULL ? find(b, key) : -1;
    if (at < 0 || wgf_actor_get_kind(b->params[at].actor) == WGF_ACTOR_KIND_NONE) return 0;
    return b->params[at].actor;
}

bool wgf_behavior_has_param(wgf_actor_t actor, int behavior, const char *key)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    return b != NULL && key != NULL && find(b, key) >= 0;
}

int wgf_behavior_get_param_count(wgf_actor_t actor, int behavior)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    return b != NULL ? b->param_count : 0;
}

const char *wgf_behavior_get_param_key(wgf_actor_t actor, int behavior, int index)
{
    const wgf_ecs_priv_behavior_t *b = behavior_of(actor, behavior);
    return b != NULL && index >= 0 && index < b->param_count ? b->params[index].key : "";
}
