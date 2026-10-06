#ifndef WGF_CORE_PART_PRIV_H
#define WGF_CORE_PART_PRIV_H

#include <stdbool.h>

/* libwgf's optional parts (CONVENTIONS.md, "An optional part is reached through its
 * hook"; ARCHITECTURE.md, "What a program links"), every layer's on one list. The
 * frame, the runtime, and the stops never call a part by name: a part installs itself
 * the first time the program creates one of it, and they run it through this list, so
 * a program that never creates one doesn't link it. wgrender's registered modules
 * (wgri_module_t, its src/wgr_module.c) without the constructors: nothing runs before
 * main but a static table's filling (WGF_CORE_PRIV_ON_LINK). A layer's other hooks (gfx's draw and pick hooks, its node kinds) stay its
 * own. */

/* The layer a part belongs to: its stop runs when that layer stops (gfx's while the GPU
 * is there), every other's at core's shutdown. */
typedef enum wgf_core_priv_part_layer_t {
    WGF_CORE_PRIV_PART_LAYER_GFX,
    WGF_CORE_PRIV_PART_LAYER_AUDIO,
    WGF_CORE_PRIV_PART_LAYER_ASSET, /* stopped after gfx and audio, whose loads it locates */
} wgf_core_priv_part_layer_t;

/* Where a part runs among the others: update, flush, end_frame, and stop run in this
 * order. Naming a part here links nothing. */
typedef enum wgf_core_priv_part_order_t {
    WGF_CORE_PRIV_PART_ECS,       /* entities and their systems (ecs/), whose nodes the rest draw */
    WGF_CORE_PRIV_PART_TEXT,      /* fonts and fontstash (gfx/src/text/wgf_gfx_font.c) */
    WGF_CORE_PRIV_PART_PARTICLES, /* emitters (gfx/src/emitter/wgf_gfx_emitter2d.c) */
    WGF_CORE_PRIV_PART_UI,        /* layout and widgets (ui/) */
    WGF_CORE_PRIV_PART_AUDIO,     /* sounds, voices, the mixer (audio/) */
    WGF_CORE_PRIV_PART_ASSET,     /* where files come from: fetching, the cache (asset/) */
} wgf_core_priv_part_order_t;

typedef struct wgf_core_priv_part_t {
    const char *name;
    wgf_core_priv_part_layer_t layer;
    wgf_core_priv_part_order_t order;
    /* every frame, in the parts' order, with the frame's dt: app's runtime runs it
       (wgf_core_priv_part_update) after the ticks and before the frame callback, where
       wgrender's runtime ran its modules' updates */
    void (*update)(float dt);
    /* every tick, after the program's, with the tick's dt: a module's systems (ecs) */
    void (*tick)(float dt);
    void (*flush)(void);     /* gfx's frame end, before any pass: the frame's records to the GPU */
    void (*end_frame)(void); /* after gfx's frame is submitted */
    void (*stop)(void);      /* when its layer stops: what it holds let go of */
    bool installed;          /* on the list; its layer's stop clears it */
    struct wgf_core_priv_part_t *next;
} wgf_core_priv_part_t;

/* Put `part` (a static) on the list, in its order; nothing when it is already there. */
void wgf_core_priv_part_install(wgf_core_priv_part_t *part);

/* Every part's update, in order. */
void wgf_core_priv_part_update(float dt);

/* Every part's tick, in order. */
void wgf_core_priv_part_tick(float dt);

/* Every part of `layer` stopped, in order, then taken off the list, its installed and
 * next cleared: the next run's first create installs it again. */
void wgf_core_priv_part_stop(wgf_core_priv_part_layer_t layer);

/* The parts, for the frame's flush and end and for tests: the first on the list. */
const wgf_core_priv_part_t *wgf_core_priv_part_list(void);

#endif /* WGF_CORE_PART_PRIV_H */
