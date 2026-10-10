#include "wgf_core_part_priv.h"

#include <stddef.h>

/* The optional parts' list (wgf_core_part_priv.h), in order. The main thread's alone:
 * parts install, update, and stop there. */

static wgf_core_priv_part_t *parts;

void wgf_core_priv_part_install(wgf_core_priv_part_t *part)
{
    wgf_core_priv_part_t **at = &parts;
    if (part->installed) return;
    while (*at != NULL && (*at)->order <= part->order) at = &(*at)->next;
    part->next = *at;
    part->installed = true;
    *at = part;
}

void wgf_core_priv_part_update(float dt)
{
    const wgf_core_priv_part_t *part;
    for (part = parts; part != NULL; part = part->next) {
        if (part->update != NULL) part->update(dt);
    }
}

static float fraction;

void wgf_core_priv_part_set_fraction(float tick_fraction)
{
    fraction = tick_fraction;
}

float wgf_core_priv_part_get_fraction(void)
{
    return fraction;
}

void wgf_core_priv_part_tick_begin(void)
{
    const wgf_core_priv_part_t *part;
    for (part = parts; part != NULL; part = part->next) {
        if (part->tick_begin != NULL) part->tick_begin();
    }
}

void wgf_core_priv_part_tick(float dt)
{
    const wgf_core_priv_part_t *part;
    for (part = parts; part != NULL; part = part->next) {
        if (part->tick != NULL) part->tick(dt);
    }
}

void wgf_core_priv_part_stop(wgf_core_priv_part_layer_t layer)
{
    wgf_core_priv_part_t **at;
    const wgf_core_priv_part_t *part;
    for (part = parts; part != NULL; part = part->next) {
        if (part->layer == layer && part->stop != NULL) part->stop();
    }
    at = &parts;
    while (*at != NULL) {
        wgf_core_priv_part_t *next = (*at)->next;
        if ((*at)->layer == layer) {
            (*at)->installed = false;
            (*at)->next = NULL;
            *at = next;
        } else {
            at = &(*at)->next;
        }
    }
}

const wgf_core_priv_part_t *wgf_core_priv_part_list(void)
{
    return parts;
}
