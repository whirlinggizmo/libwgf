#include "wgf_component.h"

#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_ecs_priv.h"
#include "wgf_voice.h"

/* An actor's components (wgf_component.h): the systems' data as the store's components on
 * the actor's entity there, made with its record the first time; a voice in the record; a
 * part's (a body, a vehicle) through the part's hooks. */

#include "wgf_log.h"

static const wgf_ecs_priv_part_component_t *parts[WGF_COMPONENT_VEHICLE + 1];

void wgf_ecs_priv_set_part_component(wgf_component_t component, const wgf_ecs_priv_part_component_t *part)
{
    if (component == WGF_COMPONENT_BODY || component == WGF_COMPONENT_VEHICLE) parts[component] = part;
}

const wgf_ecs_priv_part_component_t *wgf_ecs_priv_get_part_component(wgf_component_t component)
{
    return component == WGF_COMPONENT_BODY || component == WGF_COMPONENT_VEHICLE ? parts[component] : NULL;
}

static const wgf_ecs_priv_ids_t *ids(void)
{
    return wgf_ecs_priv_ids();
}

static wgf_ecs_priv_id_t data_id(wgf_component_t component)
{
    switch (component) {
        case WGF_COMPONENT_MOTION: return ids()->motion;
        case WGF_COMPONENT_BOUNDS: return ids()->bounds;
        case WGF_COMPONENT_LIFETIME: return ids()->lifetime;
        case WGF_COMPONENT_COLLIDER: return ids()->collider;
        default: return 0;
    }
}

bool wgf_actor_has_component(wgf_actor_t actor, wgf_component_t component)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    if (record == NULL) return false;
    if (component == WGF_COMPONENT_VOICE) return record->voice != 0;
    if (component == WGF_COMPONENT_BODY || component == WGF_COMPONENT_VEHICLE) {
        return parts[component] != NULL && wgf_ecs_priv_store_has(record->id, parts[component]->id);
    }
    return data_id(component) != 0 && wgf_ecs_priv_store_has(record->id, data_id(component));
}

bool wgf_actor_add_component(wgf_actor_t actor, wgf_component_t component)
{
    wgf_ecs_priv_record_t *record;
    if ((int)component <= WGF_COMPONENT_NONE || component > WGF_COMPONENT_VEHICLE) return false;
    if (wgf_actor_has_component(actor, component)) return true;
    if (component == WGF_COMPONENT_BODY || component == WGF_COMPONENT_VEHICLE) {
        static bool told;
        if (parts[component] == NULL) {
            if (!told) wgf_log_warn("wgf_ecs: a body or a vehicle before physics has started (wgf_physics_set_gravity); refused");
            told = true;
            return false;
        }
        if (wgf_ecs_priv_record_make(actor) == NULL) return false;
        parts[component]->add(actor);
        return wgf_actor_has_component(actor, component);
    }
    record = wgf_ecs_priv_record_make(actor);
    if (record == NULL) return false;
    switch (component) {
        case WGF_COMPONENT_MOTION: {
            wgf_ecs_priv_motion_t m;
            memset(&m, 0, sizeof(m));
            wgf_ecs_priv_store_set(record->id, ids()->motion, &m);
            return true;
        }
        case WGF_COMPONENT_BOUNDS: {
            wgf_ecs_priv_bounds_t b;
            memset(&b, 0, sizeof(b));
            b.rect[2] = 800.0f;
            b.rect[3] = 600.0f;
            b.mode = WGF_BOUNDS_MODE_WRAP;
            wgf_ecs_priv_store_set(record->id, ids()->bounds, &b);
            return true;
        }
        case WGF_COMPONENT_LIFETIME: {
            wgf_ecs_priv_lifetime_t l;
            l.seconds = 1.0f;
            wgf_ecs_priv_store_set(record->id, ids()->lifetime, &l);
            return true;
        }
        case WGF_COMPONENT_COLLIDER: {
            wgf_ecs_priv_collider_t c;
            c.radius = 1.0f;
            c.layer = 1;
            c.mask = -1;
            c.enabled = true;
            wgf_ecs_priv_store_set(record->id, ids()->collider, &c);
            return true;
        }
        default: /* the voice */
            record->voice = wgf_voice_create(0);
            if (record->voice != 0) wgf_ecs_priv_store_set(record->id, ids()->voice, NULL);
            return record->voice != 0;
    }
}

bool wgf_actor_remove_component(wgf_actor_t actor, wgf_component_t component)
{
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    if (record == NULL || !wgf_actor_has_component(actor, component)) return false;
    if (component == WGF_COMPONENT_VOICE) {
        wgf_voice_destroy(record->voice);
        record->voice = 0;
        wgf_ecs_priv_store_remove(record->id, ids()->voice);
        return true;
    }
    if (component == WGF_COMPONENT_BODY || component == WGF_COMPONENT_VEHICLE) {
        parts[component]->remove(actor);
        record = wgf_ecs_priv_record_of(actor); /* the part's let go of: the records may have moved */
        wgf_ecs_priv_store_remove(record->id, parts[component]->id);
        return true;
    }
    if (component == WGF_COMPONENT_COLLIDER) wgf_ecs_priv_forget_pairs(actor);
    wgf_ecs_priv_store_remove(record->id, data_id(component));
    return true;
}

wgf_voice_t wgf_actor_get_voice(wgf_actor_t actor)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    return record != NULL ? record->voice : 0;
}
