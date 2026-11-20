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

/* A tween, an object: a handle of kind "ecs.tween". */
typedef wgf_handle_t wgf_tween_t;

/* Tweens, helix-legacy's Tweens: a number, or an actor's position, turn, or scale, moved to
 * a value over seconds on an ease (wgf_ease.h), stepped on the tick clock (wgf_loop.h), so
 * an autopilot's run is the same each time. An actor's starts from where the actor is when
 * it begins (after its delay), and ends with the actor: a tween of an actor destroyed is let
 * go of. A tween that ends (its repeats done) is let go of too, its handle dead
 * (wgf_handle_is_alive): what a program asks to know it ended. At most 4096.
 *
 * Each makes a tween, running; 0 for seconds not above 0 or not finite, an actor that isn't
 * one, or no room. */
WGF_API wgf_tween_t wgf_tween_create(float from, float to, float seconds, wgf_easing_t easing);
WGF_API wgf_tween_t wgf_tween_move(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing);
WGF_API wgf_tween_t wgf_tween_turn(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing);
WGF_API wgf_tween_t wgf_tween_scale(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing);

/* Let go of at once, where it is. Nothing for a handle that isn't a tween. */
WGF_API void wgf_tween_destroy(wgf_tween_t tween);

/* A number's tween's value now (from, to, or between); an actor's tween's eased progress, 0
 * to 1. 0 for a handle that isn't a tween. */
WGF_API float wgf_tween_get_value(wgf_tween_t tween);

/* The seconds it waits before it begins (0 by default), set before it begins. Refuses a
 * negative or non-finite one, and one begun. */
WGF_API bool wgf_tween_set_delay(wgf_tween_t tween, float seconds);
WGF_API float wgf_tween_get_delay(wgf_tween_t tween);
/* How many times more it runs after the first (0 by default; -1 for ever), and whether each
 * run after the first goes back the way it came (yoyo). Refuses a count below -1. */
WGF_API bool wgf_tween_set_repeat(wgf_tween_t tween, int count);
WGF_API int wgf_tween_get_repeat(wgf_tween_t tween);
WGF_API bool wgf_tween_set_yoyo(wgf_tween_t tween, bool yoyo);
WGF_API bool wgf_tween_is_yoyo(wgf_tween_t tween);
/* Held where it is, and on again. False only for a handle that isn't a tween. */
WGF_API bool wgf_tween_set_paused(wgf_tween_t tween, bool paused);
WGF_API bool wgf_tween_is_paused(wgf_tween_t tween);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TWEEN_H */
