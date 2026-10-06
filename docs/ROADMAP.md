# libwgf roadmap

What is left to build, in order. libwgf is done when it ships three games ([SPEC.md](../SPEC.md), "Done means games"); each milestone leaves everything before it working, and every game stays playable in CI. A finished step is deleted from this file, not ticked off, and its record moves to [HISTORY.md](HISTORY.md) in the same commit. What exists now is [ARCHITECTURE.md](ARCHITECTURE.md).

## Milestone 2: a chase-camera racing game

One track from glTF, a car with real vehicle physics, a chase camera with smoothing, lap timing, a HUD, shadows, and an environment map. It proves 3D rendering, physics, asset streaming, and performance. Planned in full before it starts (milestone 1 closed on 2026-10-06: HISTORY.md); its pieces will come from libwgt's 3D (scenes, meshes from glTF, materials, lights, shadows, environments) and a physics3d module on Jolt's C API.

## Milestone 3: an ARPG vertical slice with co-op

One dungeon level, one player class with three abilities, three enemy types with navmesh pathing, loot drops, an inventory, and 2 to 4 players in co-op through a central server. It proves skinned animation with blending, many lit instances, navmesh and AI, an authoritative server, and replication. Planned in full once milestone 2 closes.

## Later, when its condition holds

Each starts when something anyone can check is true.

- **physics2d (Box2D v3)**: when a game or example on the ROADMAP needs 2D rigid bodies, joints, or contact resolution; no game in milestones 1 to 3 does, and Asteroids' overlaps are the ecs collider's.
- **Dear ImGui (cimgui), the developer UI**: when a milestone's game needs an in-game inspector (SPEC.md, "Not in v1").
- **WebGPU behind a build option**, then as the default: libwgt's condition (its ROADMAP, "Later"): Firefox ships WebGPU on Linux, Chrome on Linux covers the common GPUs, and every example passes on WebGPU in CI, with an automatic fall back to WebGL2 and a persisted switch.
- **Hot reload of assets**: when a game on the ROADMAP loads a file its developer edits while it runs (milestone 2's track and car, as glTF): `wgf serve` picks up a saved asset as it picks up Haxe, the resource loaded again in place, its handle kept. Asteroids' sounds are generated, its scene instantiated once, and its art drawn as shapes.
- **Native hot reload** (hxcpp or C): when a game's desktop-only bug needs the reload loop to find; the browser is the everyday loop. Not through cppia as it measures now (HISTORY.md, "Native hot reload stays deferred").
- **Threads on the web**: when a benchmark shows a load or a system that a worker would take off the main thread by more than a frame's budget.
