#ifndef WGF_WORLD_H
#define WGF_WORLD_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The simulation as a whole: libwgf's systems over the actors' components (wgf_component.h),
 * the events they raise, and the simulated actors written out as text.
 *
 * To find the actors with a behavior or a component, see wgf_actor_find_with_behavior
 * (wgf_behavior.h) and wgf_actor_find_with_component (wgf_component.h).
 *
 * Each tick, as it begins, every simulated actor's transform is kept as it is. After your
 * game's tick, the systems run in this order:
 * - Lifetimes count down. An actor whose time is up is destroyed.
 * - Motion moves each actor by its velocity and spin, damped, with its speed capped.
 * - Bounds wrap, clamp, destroy, or bounce an actor that has left its rectangle.
 * - Colliders find the overlaps.
 *
 * Each frame, a simulated actor is drawn between its last two ticks' transforms.
 *
 * Events are what your game's behaviors are told (wgf_behavior.h). They are queued as they
 * happen and taken by your game, in order. Each is four ints: the event and three more.
 *
 *   CREATED          a behavior added: its actor, its id, 0
 *   DESTROYED        a behavior removed, or its actor destroyed: its actor (stale by then,
 *                    a name for what it was), its id, 0
 *   TRIGGER_ENTER    two colliders starting to overlap, told to each: the actor, the other,
 *                    and the other's collider layer as they met
 *   TRIGGER_EXIT     two colliders ceasing to overlap, as TRIGGER_ENTER
 *   CONTACT_BEGIN    two solid bodies beginning to touch (wgf_body3d.h), told to each: the
 *                    actor, the other, and the other's body layer
 *   CONTACT_END      two solid bodies ceasing to touch, as CONTACT_BEGIN
 *   SCREEN_ENTER     a screen coming to the top of the stack (wgf_screen.h): the screen,
 *                    the one it covers or replaced (0 for none)
 *   SCREEN_EXIT      a screen leaving the top: the screen, the one now there (0 for none)
 *
 * Notes:
 * - An actor destroyed while it overlaps another raises no TRIGGER_EXIT.
 * - Events taken in a batch can name an actor your game destroyed while handling an earlier
 *   event of the batch. A binding's runtime drops a trigger whose actor or other actor is gone
 *   by then.
 * - Nothing is called back. A binding takes the events at its tick's start and calls its
 *   behaviors itself.
 * - At most 65536 events wait. Past that the oldest are dropped, with a warning once. */
typedef enum wgf_world_event_t {
    WGF_WORLD_EVENT_NONE = 0,
    WGF_WORLD_EVENT_CREATED = 1,
    WGF_WORLD_EVENT_DESTROYED = 2,
    WGF_WORLD_EVENT_TRIGGER_ENTER = 3,
    WGF_WORLD_EVENT_TRIGGER_EXIT = 4,
    WGF_WORLD_EVENT_CONTACT_BEGIN = 5,
    WGF_WORLD_EVENT_CONTACT_END = 6,
    WGF_WORLD_EVENT_SCREEN_ENTER = 7, /* a screen came to the top; the other is the one it covers or replaced */
    WGF_WORLD_EVENT_SCREEN_EXIT = 8   /* a screen left the top; the other is the one now there */
} wgf_world_event_t;

/* Returns how many events wait, or takes the oldest off the queue.
 *
 * take_events fills `out` with four ints an event (the event, then the three above). It takes
 * as many whole events as fit in `count` ints, and returns how many ints it filled. */
WGF_API int wgf_world_get_event_count(void);
WGF_API int wgf_world_take_events(int *out, int count);

/* Returns how many actors have a component or a behavior: the simulated ones. An actor with
 * neither, such as a text or a sprite that only draws, isn't counted. For the actors a scene
 * file holds, see wgf_scene_get_actor_count. */
WGF_API int wgf_actor_get_count(void);

/* Destroys every actor with a component or a behavior, with every actor under it, oldest
 * first. */
WGF_API void wgf_world_clear(void);

/* Sets or gets the actor that spatial audio is heard from (wgf_audio.h's listener).
 *
 * Each frame the listener is put where the actor is drawn, facing its -z, as a camera views.
 * 0 sets none, and the listener stays as last set. Returns false for a handle that isn't an
 * actor. */
WGF_API bool wgf_world_set_listener(wgf_actor_t actor);
WGF_API wgf_actor_t wgf_world_get_listener(void);

/* Returns the simulated actors as a scene's text, in wgf_scene.h's format.
 *
 * - Each top actor is written, oldest first, as an `actor` block. A top actor has a component
 *   or a behavior, and its parent has none.
 * - Each block holds the actor's kind, its transform, its components, its behaviors, and the
 *   actors under it, as they are now. Loading the text as a scene and instantiating it makes
 *   the same actors again.
 * - An actor under a glTF file's model is written with the file's root, as its `model path=`
 *   block.
 * - A node of the file is written only with what it has beyond what the file gives it: its
 *   transform, components, behaviors, and the actors under it. Its kind is left to the file.
 *
 * libwgf owns the string, valid until the next dump. Returns "" when no actor ever had a
 * component or a behavior. */
WGF_API const char *wgf_world_dump(void);

#ifdef __cplusplus
}
#endif

#endif
