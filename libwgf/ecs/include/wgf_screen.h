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

/* Screens and scene flow: a game as a stack of screens, such as a title, play, a pause menu,
 * and game over. Nothing is called back.
 *
 * A screen is an actor, and your game's behaviors for it go on it. Its stages, 2D and 3D, are
 * added to it in the order they draw. Stages are always roots: they belong to the screen
 * without being its children. Every other actor of the screen hangs under its stages.
 *
 * The stack:
 * - push puts a screen on top.
 * - pop takes the top screen off.
 * - replace swaps the top screen for another.
 *
 * The top screen runs. The screens under it run or pause:
 * - A screen under one that pauses (the default) is paused, with its stages
 *   (wgf_actor_set_paused: drawn, but frozen).
 * - A screen under one that doesn't pause, such as a HUD laid over play, runs on.
 * - A screen off the stack is switched off and paused, with its stages (wgf_actor_set_enabled:
 *   not drawn, and nothing under them moving). It is kept for your game to push again or
 *   destroy.
 *
 * wgf_screen_draw draws what is shown. It draws from the bottom up, each screen's stages in
 * their order, but no screen under an opaque one (the default). It then draws the
 * transition's cover.
 *
 * A change runs through the transition that is set (none to start, so changes happen at once).
 * - It covers the frame over half its seconds, makes the change at the middle, and uncovers
 *   over the other half. Each half runs on the ease.
 * - A screen can wait for assets: a group (set_assets, wgf_asset.h), or every load pending
 *   (set_wait_loads: the textures and meshes made as it was built). The change to it happens
 *   only once they have settled.
 * - Meanwhile the stack is held covered. The loading screen (set_loading_screen) is drawn over
 *   the cover, and its progress can be read.
 * - While a change runs, another change is refused (false).
 *
 * Each change is told as world events (wgf_world.h):
 * - SCREEN_ENTER for the screen that comes to the top.
 * - SCREEN_EXIT for the one that leaves the top: pushed under, popped, or replaced.
 *
 * Each event carries the other screen (0 for none) as its second value. A binding's behaviors
 * on a screen hear them. */

typedef enum wgf_transition_t {
    WGF_TRANSITION_NONE = 0,  /* at once */
    WGF_TRANSITION_FADE = 1,  /* fades to the color and back */
    WGF_TRANSITION_SLIDE_LEFT = 2, /* the color slides in from the right, across, and out to the left */
    WGF_TRANSITION_SLIDE_RIGHT = 3,
    WGF_TRANSITION_SLIDE_UP = 4,
    WGF_TRANSITION_SLIDE_DOWN = 5,
    WGF_TRANSITION_WIPE = 6, /* the color wipes over from the left, then off to the right */
    WGF_TRANSITION_IRIS = 7  /* a circle closes on the middle, then opens */
} wgf_transition_t;

/* Creates a screen: an actor at the top of its own tree, off the stack and switched off.
 * Returns 0 when there is no room for another actor. */
WGF_API wgf_actor_t wgf_screen_create(void);

/* Adds, removes, or lists a screen's stages, drawn in the order added (at most 8).
 *
 * - add_stage returns false for an actor that isn't a screen, one that isn't a stage, or a
 *   stage the screen already has.
 * - remove_stage returns false for a stage the screen doesn't have.
 *
 * A stage that is destroyed leaves its screen. */
WGF_API bool wgf_screen_add_stage(wgf_actor_t screen, wgf_actor_t stage);
WGF_API bool wgf_screen_remove_stage(wgf_actor_t screen, wgf_actor_t stage);
WGF_API int wgf_screen_get_stage_count(wgf_actor_t screen);
WGF_API wgf_actor_t wgf_screen_get_stage(wgf_actor_t screen, int index);

/* Changes the stack, through the transition.
 *
 * - push returns false for an actor that isn't a screen, or one already on the stack.
 * - pop returns false with no screen, or with the bottom screen alone.
 * - replace returns false as push does, or with no screen to replace.
 *
 * All of them return false while a change runs. */
WGF_API bool wgf_screen_push(wgf_actor_t screen);
WGF_API bool wgf_screen_pop(void);
WGF_API bool wgf_screen_replace(wgf_actor_t screen);

/* Returns the stack's screens.
 *
 * - get_top returns the top screen, or 0 with none.
 * - get_count returns how many are on the stack.
 * - get returns the one `index` up from the bottom, or 0 for an index off the stack.
 *
 * Each is as of the last change made. A change still running isn't counted yet. */
WGF_API wgf_actor_t wgf_screen_get_top(void);
WGF_API int wgf_screen_get_count(void);
WGF_API wgf_actor_t wgf_screen_get(int index);

/* Sets or gets whether a screen is opaque, and whether it is pausing.
 *
 * - opaque: no screen under it is drawn (default: on). Turn it off for a pause menu over the
 *   game.
 * - pausing: the screens under it are paused (default: on). Turn it off for a HUD over play.
 *
 * Returns false for an actor that isn't a screen. */
WGF_API bool wgf_screen_set_opaque(wgf_actor_t screen, bool opaque);
WGF_API bool wgf_screen_is_opaque(wgf_actor_t screen);
WGF_API bool wgf_screen_set_pausing(wgf_actor_t screen, bool pausing);
WGF_API bool wgf_screen_is_pausing(wgf_actor_t screen);

/* Draws what is shown, into this frame: each shown screen's stages, then the cover. */
WGF_API void wgf_screen_draw(void);

/* Sets or gets the transition the changes run through.
 *
 * - Its kind, and its length in seconds. 0 or less means at once.
 * - Its color (default: black).
 * - Each half's ease (default: linear).
 *
 * Returns false for a kind that isn't one, or seconds that aren't finite. */
WGF_API bool wgf_screen_set_transition(wgf_transition_t kind, float seconds);
WGF_API wgf_transition_t wgf_screen_get_transition(void);
WGF_API float wgf_screen_get_transition_seconds(void);
WGF_API void wgf_screen_set_transition_color(wgf_color_t color);
WGF_API wgf_color_t wgf_screen_get_transition_color(void);
WGF_API bool wgf_screen_set_transition_ease(wgf_easing_t ease);
WGF_API wgf_easing_t wgf_screen_get_transition_ease(void);

/* Returns whether a change is running, and how far it has come.
 *
 * - 0 to 0.5: covering.
 * - 0.5: covered. The change is made there, or it is waiting on a load.
 * - 0.5 to 1: uncovering.
 *
 * The progress is 0 with no change running. */
WGF_API bool wgf_screen_is_changing(void);
WGF_API float wgf_screen_get_change_progress(void);

/* Sets or gets what `screen` waits for before it is changed to: an asset task (a group of its
 * files, made local), or 0 for none. Returns false for an actor that isn't a screen, or a
 * handle that isn't a task. */
WGF_API bool wgf_screen_set_assets(wgf_actor_t screen, wgf_asset_task_t task);
WGF_API wgf_asset_task_t wgf_screen_get_assets(wgf_actor_t screen);

/* Sets or gets whether a change to `screen` also waits for every load pending in your game
 * (default: off). Returns false for an actor that isn't a screen. */
WGF_API bool wgf_screen_set_wait_loads(wgf_actor_t screen, bool wait);
WGF_API bool wgf_screen_is_wait_loads(wgf_actor_t screen);

/* Sets or gets the loading screen, drawn over the cover while a change waits on loads (0 for
 * none). get_load_progress returns how far the loads have come, 0 to 1.
 *
 * - With assets to wait for, it is their progress.
 * - Otherwise it is the share of the loads pending when the wait began that have settled.
 * - It is 1 with nothing waiting. */
WGF_API bool wgf_screen_set_loading_screen(wgf_actor_t screen);
WGF_API wgf_actor_t wgf_screen_get_loading_screen(void);
WGF_API float wgf_screen_get_load_progress(void);

#ifdef __cplusplus
}
#endif

#endif
