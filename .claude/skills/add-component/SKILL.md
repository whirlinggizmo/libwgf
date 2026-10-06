---
name: add-component
description: Add a built-in ecs component to libwgf (a data component the systems run, or a node component drawn through gfx), with its calls, its system, its scene-file key, its dump, its binding, and its tests. Use when a game needs entities to have something new (a health, a homing target, a light).
---

# Adding an ecs component

A component is a `wgf_component_t` value, its calls in a header of its own, and -- for a data component -- a system that runs it each tick. Read docs/ARCHITECTURE.md's ecs section and docs/HISTORY.md's "Milestone 1, step 6" first: the entity and node split, what a tick runs, and why.

1. **Decide its kind.** Plain data the systems walk (motion, bounds, lifetime, collider) is a flecs component, in `ecs/src/wgf_ecs_priv.h`. Something drawn (a shape, a sprite) is a node of a gfx type under the entity's node: a slot in `WGF_ECS_PRIV_NODE_KINDS`. State that isn't plain data (text, handles to keep) goes in the entity's record, `wgf_ecs_priv_entity_t`.
2. **The enum value**: append to `wgf_component_t` in `ecs/include/wgf_entity.h`, never renumbering; describe it on its line.
3. **Its header**, `ecs/include/wgf_<section>.h`: a header comment saying what it is, its defaults, and that every call is false (or 0) for an entity without it; a setter and getter per value, each "false for ..." sentence naming every refusal (CONVENTIONS.md, "Shape of calls": clamp or refuse). Run `python3 tools/check_api.py`.
4. **Add and remove**: `ecs/src/wgf_ecs_entity.c` (`wgf_entity_add_component`, `_remove_component`, `_has_component`): the flecs component set with its defaults, or the node made under the entity's node.
5. **Its calls**: `ecs/src/wgf_ecs_components.c` (or a file of its own, added to `ecs/CMakeLists.txt`), each through `wgf_ecs_priv_get`.
6. **Its system**, for a data component: a query made in `wgf_ecs_priv_start` and a function `tick` calls in `ecs/src/wgf_ecs.c`, in the order that makes sense (lifetimes, motion, bounds, colliders). Destroy entities only after the query that found them (`doom`, `destroy_all`). Events go through `wgf_ecs_priv_raise`.
7. **The scene format**: its line and keys in `wgf_scene.h`'s header comment, parsed in `ecs/src/wgf_ecs_scene.c`, and written by the dump (`ecs/src/wgf_ecs_dump.c`), so a dumped world loads back the same.
8. **Tests**: its calls and refusals in `ecs/tests/wgf_ecs_entity_test.c`, its system in `wgf_ecs_systems_test.c` (ticked through core's part list), its scene line and the dump's round trip in `wgf_ecs_scene_test.c`.
9. **The binding**: `python3 tools/gen_binding.py` (a new section over `wgf_entity_t` becomes `wgf.<Section>`, a type over `Entity`), then the add-binding-call skill's checks; reach it in the feature test (`examples/haxe/feature-test/Main.hx`'s `ecs()`).
10. **Docs**: ARCHITECTURE.md's ecs table; HISTORY.md, why it exists. Then `python3 tools/verify_builds.py --web --windows sightblinder`.
