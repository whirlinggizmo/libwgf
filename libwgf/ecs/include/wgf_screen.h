#ifndef WGF_SCREEN_H
#define WGF_SCREEN_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_component.h"
#include "wgf_ease.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Screens and scene flow: a game as a stack of screens (a title, play, a pause menu, game
 * over), helix-legacy's scene flow (Flow, Transition, Loader, LoadingScreen) without its
 * callbacks. A screen is an actor (a game's behaviors for it go on it) with its stages, 2D
 * and 3D, added to it in the order they draw (stages are always roots: they belong to it
 * without being its children); everything else of its hangs under them.
 *
 * The stack: push puts a screen on top, pop takes the top off, replace swaps the top for
 * another. The top runs. A screen under one that pauses (the default) is paused
 * (wgf_actor_set_paused: drawn, frozen), it and its stages; one under one that doesn't (a
 * HUD laid over play) runs on. A screen off the stack is switched off and paused, it and its
 * stages (wgf_actor_set_enabled: not drawn, nothing under them moving), kept for the program
 * to push again or destroy. draw
 * draws what is shown: from the bottom up, each screen's stages in their order, but none
 * under an opaque one (the default), then the transition's cover.
 *
 * A change runs through the transition set (none to start: at once). It covers the frame
 * over half its seconds, makes the change at the middle, and uncovers over the other half,
 * each half on the ease. A screen given assets to wait for (set_assets: a group,
 * wgf_asset.h), or set to wait for every load pending (set_wait_loads: the textures and
 * meshes made as it was built), is changed to only once they have settled: the stack held
 * covered meanwhile, with the loading screen (set_loading_screen) drawn over the cover and
 * its progress readable. While one runs, another change is refused (false).
 *
 * Each change is told as world events (wgf_world.h): SCREEN_ENTER for the screen that
 * comes to the top, SCREEN_EXIT for the one that leaves it (pushed under, popped, or
 * replaced), each with the other screen (0 for none) as its second; a binding's behaviors
 * on a screen hear them. */

typedef enum wgf_transition_t {
    WGF_TRANSITION_NONE = 0,  /* at once */
    WGF_TRANSITION_FADE = 1,  /* a fade to the color and back */
    WGF_TRANSITION_SLIDE_LEFT = 2, /* the color slides in from the right, across, and out to the left */
    WGF_TRANSITION_SLIDE_RIGHT = 3,
    WGF_TRANSITION_SLIDE_UP = 4,
    WGF_TRANSITION_SLIDE_DOWN = 5,
    WGF_TRANSITION_WIPE = 6, /* wiped over from the left, then off to the right */
    WGF_TRANSITION_IRIS = 7  /* a circle closing on the middle, then opening */
} wgf_transition_t;

/* A screen: an actor at the top of its own tree, off the stack (switched off). 0 when there is
 * no room for another actor. */
WGF_API wgf_actor_t wgf_screen_create(void);

/* Its stages, drawn in the order added (at most 8): add is false for an actor that isn't a
 * screen, one that isn't a stage, or one it has; remove, for one it hasn't. A stage destroyed
 * leaves it. */
WGF_API bool wgf_screen_add_stage(wgf_actor_t screen, wgf_actor_t stage);
WGF_API bool wgf_screen_remove_stage(wgf_actor_t screen, wgf_actor_t stage);
WGF_API int wgf_screen_get_stage_count(wgf_actor_t screen);
WGF_API wgf_actor_t wgf_screen_get_stage(wgf_actor_t screen, int index);

/* The stack's changes, run through the transition. push: false for an actor that isn't one,
 * or one already on the stack; pop: false with none, or the bottom one alone; replace: false
 * as push, or with none to replace. All false while a change runs. */
WGF_API bool wgf_screen_push(wgf_actor_t screen);
WGF_API bool wgf_screen_pop(void);
WGF_API bool wgf_screen_replace(wgf_actor_t screen);

/* The top (0 with none), how many are on the stack, and the one `index` up from the bottom (0
 * for an index off it). Each as of the last change made, a change running not yet counted. */
WGF_API wgf_actor_t wgf_screen_get_top(void);
WGF_API int wgf_screen_get_count(void);
WGF_API wgf_actor_t wgf_screen_get(int index);

/* opaque: none under it is drawn (on to start; off for a pause menu over the game). pausing:
 * those under it are paused (on to start; off for a HUD over play). False for an actor that
 * isn't one. */
WGF_API bool wgf_screen_set_opaque(wgf_actor_t screen, bool opaque);
WGF_API bool wgf_screen_is_opaque(wgf_actor_t screen);
WGF_API bool wgf_screen_set_pausing(wgf_actor_t screen, bool pausing);
WGF_API bool wgf_screen_is_pausing(wgf_actor_t screen);

/* Draw what is shown, into this frame: each shown screen's stages, then the cover. */
WGF_API void wgf_screen_draw(void);

/* The transition the changes run through: its kind and seconds (0 or less: at once), its
 * color (black to start), and each half's ease (linear to start). False for a kind that isn't
 * one, or seconds that aren't finite. */
WGF_API bool wgf_screen_set_transition(wgf_transition_t kind, float seconds);
WGF_API wgf_transition_t wgf_screen_get_transition(void);
WGF_API float wgf_screen_get_transition_seconds(void);
WGF_API void wgf_screen_set_transition_color(wgf_color_t color);
WGF_API wgf_color_t wgf_screen_get_transition_color(void);
WGF_API bool wgf_screen_set_transition_ease(wgf_easing_t ease);
WGF_API wgf_easing_t wgf_screen_get_transition_ease(void);

/* Whether a change is running, and how far: 0 to 0.5 covering, 0.5 covered (the change made
 * there, or waiting on a load), 0.5 to 1 uncovering; 0 with none. */
WGF_API bool wgf_screen_is_changing(void);
WGF_API float wgf_screen_get_change_progress(void);

/* What `screen` waits for before it is changed to: an asset task (a group of its files, made
 * local), 0 for none. False for an actor that isn't one, or a handle that isn't a task. */
WGF_API bool wgf_screen_set_assets(wgf_actor_t screen, wgf_asset_task_t task);
WGF_API wgf_asset_task_t wgf_screen_get_assets(wgf_actor_t screen);

/* Whether a change to `screen` waits for every load pending in the program too (off to
 * start). False for an actor that isn't one. */
WGF_API bool wgf_screen_set_wait_loads(wgf_actor_t screen, bool wait);
WGF_API bool wgf_screen_is_wait_loads(wgf_actor_t screen);

/* The screen drawn over the cover while a change waits on loads (0 for none), and how far the
 * loads have come, 0 to 1: its assets' progress, else the share of the loads pending as the
 * wait began that have settled; 1 with none waiting. */
WGF_API bool wgf_screen_set_loading_screen(wgf_actor_t screen);
WGF_API wgf_actor_t wgf_screen_get_loading_screen(void);
WGF_API float wgf_screen_get_load_progress(void);

#ifdef __cplusplus
}
#endif

#endif
