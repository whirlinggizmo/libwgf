---
name: add-component
description: Add a built-in ecs component to libwgf (data any actor can carry, which a system runs), with its calls, its system, its scene-file key, its dump, its binding, and its tests. Use when a game needs actors to have something new (a health, a homing target).
---

# Adding a component

A component is a `wgf_component_t` value, its calls in a header of its own, and -- usually -- a system that runs it each tick. Any actor can carry it. Read docs/ARCHITECTURE.md's ecs section and docs/HISTORY.md's "One kind of object, the actor" first: what a record holds, what a tick runs, and why. Something drawn is not a component: it is an actor kind, gfx's (a `wgf_actor_kind_t` with its kind's hooks, `gfx/src/actor/wgf_gfx_actor_priv.h`), and a scene's kind line.

1. **Decide where it lives.** Plain data the systems walk (motion, bounds, lifetime, collider) is a flecs component, in `ecs/src/wgf_ecs_priv.h`, its id in `wgf_ecs_priv_ids_t`. State that isn't plain data (handles to keep, text) goes in the actor's record, `wgf_ecs_priv_record_t`, with a flecs tag in the ids so finding by it stays an index (the voice's way).
2. **The enum value**: append to `wgf_component_t` in `ecs/include/wgf_component.h`, never renumbering; describe it on its line.
3. **Its header**, `ecs/include/wgf_<section>.h`: a header comment saying what it is, its defaults, and that every call is false (or 0) for an actor without it; a setter and getter per value, each "false for ..." sentence naming every refusal (CONVENTIONS.md, "Shape of calls": clamp or refuse). Run `python3 tools/check_api.py`.
4. **Add and remove**: `ecs/src/wgf_ecs_actor.c` (`wgf_actor_add_component`, `_remove_component`, `_has_component`): the flecs component set with its defaults (or the record's state made, and its tag added); and `component_id` in `ecs/src/wgf_ecs.c`, so `wgf_ecs_find_component` finds it.
5. **Its calls**: `ecs/src/wgf_ecs_components.c` (or a file of its own, added to `ecs/CMakeLists.txt`), each through `wgf_ecs_priv_get`.
6. **Its system**, for a data component: a query made in `wgf_ecs_priv_start` and a function `tick` calls in `ecs/src/wgf_ecs.c`, in the order that makes sense (lifetimes, motion, bounds, colliders). Destroy actors only after the query that found them (`doom`, `destroy_all`). Events go through `wgf_ecs_priv_raise`.
7. **The scene format**: its line and keys in BUILDING.md's "Scene files", parsed in `ecs/src/wgf_ecs_scene.c`, and written by the dump (`ecs/src/wgf_ecs_dump.c`), so a dumped world loads back the same.
8. **Tests**: its calls, refusals, and finding in `ecs/tests/wgf_ecs_actor_test.c`, its system in `wgf_ecs_systems_test.c` (ticked through core's part list), its scene line and the dump's round trip in `wgf_ecs_scene_test.c`.
9. **The binding**: `python3 tools/gen_binding.py` (a new section over `wgf_actor_t` becomes `wgf.<Section>`, a type over `Actor`), then the add-binding-call skill's checks; reach it in the feature test (`examples/haxe/feature-test/Main.hx`'s `ecs()`).
10. **Docs**: ARCHITECTURE.md's ecs table; HISTORY.md, why it exists. Then `python3 tools/verify_builds.py --web --windows sightblinder`.
