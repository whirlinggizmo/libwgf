#ifndef WGF_TWEEN_H
#define WGF_TWEEN_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_ease.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A tween is an object: a handle of kind "ecs.tween". */
typedef wgf_handle_t wgf_tween_t;

/* Tweens: move a number, or an actor's position, turn, or scale, to a value over seconds on an
 * ease (wgf_ease.h).
 *
 * Tweens step on the tick clock (wgf_loop.h), so an autopilot's run is the same each time.
 *
 * - An actor's tween starts from where the actor is when the tween begins, after its delay.
 * - It ends with the actor: a tween of a destroyed actor is let go of.
 * - A tween that ends, its repeats done, is let go of too. Its handle is then dead (see
 *   wgf_handle_is_alive), which is how your game knows it ended.
 * - There can be at most 4096 tweens.
 *
 * Each call below creates a tween, running. Each returns 0 for `seconds` not above 0 or not
 * finite, an actor that isn't one, or no room for another tween. */
WGF_API wgf_tween_t wgf_tween_create(float from, float to, float seconds, wgf_easing_t easing);
WGF_API wgf_tween_t wgf_tween_move(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing);
WGF_API wgf_tween_t wgf_tween_turn(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing);
WGF_API wgf_tween_t wgf_tween_scale(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing);

/* Lets go of a tween at once, leaving its value, or its actor, where it is. Does nothing for
 * a handle that isn't a tween. */
WGF_API void wgf_tween_destroy(wgf_tween_t tween);

/* Returns a tween's value now.
 *
 * - For a number's tween: the value, from `from` to `to` or between.
 * - For an actor's tween: its eased progress, 0 to 1.
 *
 * Returns 0 for a handle that isn't a tween. */
WGF_API float wgf_tween_get_value(wgf_tween_t tween);

/* Sets or gets the seconds a tween waits before it begins (default: 0). Set it before the
 * tween begins. The setter refuses a negative or non-finite delay, and a tween already
 * begun. */
WGF_API bool wgf_tween_set_delay(wgf_tween_t tween, float seconds);
WGF_API float wgf_tween_get_delay(wgf_tween_t tween);
/* Sets or gets how many more times a tween runs after the first (default: 0), and whether each
 * run after the first goes back the way it came (yoyo).
 *
 * A repeat of -1 runs it for ever. set_repeat refuses a count below -1. */
WGF_API bool wgf_tween_set_repeat(wgf_tween_t tween, int count);
WGF_API int wgf_tween_get_repeat(wgf_tween_t tween);
WGF_API bool wgf_tween_set_yoyo(wgf_tween_t tween, bool yoyo);
WGF_API bool wgf_tween_is_yoyo(wgf_tween_t tween);
/* Pauses a tween where it is, or runs it again, or gets whether it is paused. Returns false
 * only for a handle that isn't a tween. */
WGF_API bool wgf_tween_set_paused(wgf_tween_t tween, bool paused);
WGF_API bool wgf_tween_is_paused(wgf_tween_t tween);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TWEEN_H */
