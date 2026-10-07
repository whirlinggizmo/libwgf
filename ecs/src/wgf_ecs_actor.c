#include "wgf_component.h"

#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_ecs_priv.h"
#include "wgf_voice.h"

/* An actor's components (wgf_component.h): the systems' data as the store's components on
 * the actor's entity there, made with its record the first time; a voice in the record. */

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
    return data_id(component) != 0 && wgf_ecs_priv_store_has(record->id, data_id(component));
}

bool wgf_actor_add_component(wgf_actor_t actor, wgf_component_t component)
{
    wgf_ecs_priv_record_t *record;
    if ((int)component <= WGF_COMPONENT_NONE || component > WGF_COMPONENT_VOICE) return false;
    if (wgf_actor_has_component(actor, component)) return true;
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
    if (component == WGF_COMPONENT_COLLIDER) wgf_ecs_priv_forget_pairs(actor);
    wgf_ecs_priv_store_remove(record->id, data_id(component));
    return true;
}

wgf_voice_t wgf_actor_get_voice(wgf_actor_t actor)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    return record != NULL ? record->voice : 0;
}
