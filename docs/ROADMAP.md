# libwgf roadmap

What is left to build, in order. libwgf is done when it ships three games ([SPEC.md](../SPEC.md), "Done means games"); each milestone leaves everything before it working, and every game stays playable in CI. A finished step is deleted from this file, not ticked off, and its record moves to [HISTORY.md](HISTORY.md) in the same commit. What exists now is [ARCHITECTURE.md](ARCHITECTURE.md).

## Milestone 1: Asteroids

Ship, rocks that split, bullets, wraparound, score, lives, audio, particles, a title screen and a game-over screen built with the UI. It proves the loop, input, 2D drawing, audio, UI, entities, and export.

The plan, decided before building (HISTORY.md, "Milestone 1's plan"). Each step is a few small commits, each reviewed against CONVENTIONS.md, verified, and pushed.

1. **The repository.** Docs (AGENTS.md, CONVENTIONS.md, ARCHITECTURE.md, HISTORY.md, this file), CMake in layers (`wgf_layer()`), the presets (Linux debug, release, headless, asan, ubsan; wasm32 debug and release; MinGW debug and headless under Wine; MSVC debug and headless over ssh), the vendored dependencies, and the tools every later step leans on: `headers.py` and `check_api.py` (the API read by clang), `check_tools.py`, `variants.py`, `stage_variant.py`, `verify_builds.py`, and Wine and remote Windows runners. CI on GitHub Actions for Linux, Windows, and the web from the first commit that builds.
   Done when: an empty library builds and tests on every preset here, on sightblinder, and in CI.
2. **math and core.** Vectors and the 3x3 and 4x4 math a 2D game needs; handles (kind, generation, index), logging, time, the version, file storage as tasks (the high score), the load pipeline and resource core, probes (named numbers a game publishes for scripts and tests to read), and a seedable random generator.
   Done when: each has unit tests passing on every preset, asan and ubsan included.
3. **platform and app.** The window (sokol_app), keyboard, mouse, gamepads, touch; the headless platform; the runtime (`wgf_app_run`), its fixed-rate ticks and frame loop; scripted input (a text script of inputs over frames with assertions on probes, run in deterministic time).
   Done when: the loop and input are tested headless, a scripted run passes and a failing assertion fails it, and a window opens on Linux, Windows, and the web.
4. **gfx, 2D.** The frame and its immediate mode (sokol_gl), color, textures, fonts and text (fontstash, the built-in JetBrains Mono), nodes in a tree with cached transforms, canvases with a 2D camera, 2D shapes (rectangle, circle, line, polygon, filled or outlined), sprites, text nodes, and CPU particle emitters.
   Done when: each section has tests (headless, and pixel checks in a browser and under Xvfb), and the C examples pass the headless smoke run and the browser check.
5. **asset and audio.** Where a file comes from: fetched over HTTP on the web, read from disk natively, with a fetch hook a program can answer; failed loads logged once with a placeholder drawn. Sounds (WAV and MP3) and voices: the browser's Web Audio on the web, libwgf's mixer on sokol_audio natively.
   Done when: tests pass on every preset, and the audio example plays on the web and the desktop.
6. **ecs.** flecs behind libwgf's handles: entities with a simulated transform and the built-in components (motion, bounds, lifetime, collider, shape, sprite, text, emitter, sound, behavior); the systems that move them at the tick rate and draw them interpolated through nodes; triggers; lifecycle events polled by the binding; bulk reads and writes of many entities through caller-owned arrays; scenes as text, with prefabs spawned at run time, and the scene dumped as text.
   Done when: each component and system has unit tests, a scene file round-trips through load and dump, and the feature test's first scene runs headless.
7. **ui.** Clay for layout, and libwgf's widget layer on it, immediate mode, drawn by gfx: panels, labels, buttons, focus with keyboard and gamepad navigation, the pointer captured from the game, per-game styling. Widgets past these come with the milestone that needs them.
   Done when: a UI example is tested headless (focus moves, a button activates by key, pad, and pointer) and passes the browser check.
8. **The Haxe binding.** The externs and the typed API generated from the headers by clang, one name per C call, with a coverage check; bulk calls taking a Haxe array, copied for the call's length; the JS target calling a prebuilt wasm host by quoted keys, with no marshalling on the wasm stack; hxcpp natively, linking a staged archive; behaviors (create, tick, frame, destroy, trigger enter and exit) dispatched from polled events; version stamps checked at start.
   Done when: the binding's tests pass on JS (node and a browser) and hxcpp (Linux, Windows), the coverage check passes, and a binding getter called 100,000 times in a frame doesn't fault.
9. **The feature test.** One program exercising every component and the whole public API through the binding, run in CI on Linux, Windows, and the web; a tool fails the build when a public call isn't reached by it.
   Done when: it passes on all three, and the reach check passes.
10. **The `wgf` CLI and the dev loop.** `wgf new` (from a template), `build`, `run --headless --frames N`, `screenshot`, `dump`, `play` (a scripted input sequence), `serve` (hot reload: save Haxe or an asset, and the running page picks it up with its state kept), and `export` (the web as a static folder with a trimmed host, and the desktop, then smoke-tested).
    Done when: a new project reaches a hot-reloading page in one command, and each command has a test.
11. **Asteroids.** `games/asteroids/`, on the public API only: the game, its sounds (generated by a tool, committed), its scene file, its title and game-over screens, and a scripted playthrough.
    Done when: it builds for the web and the desktop, its playthrough passes headless and in a browser in CI, it is deployed to GitHub Pages on every push to `main` within its web size budget, and its desktop export runs on Linux and Windows.
12. **Close the milestone.** Skills for repeated work (adding a component, a binding call, an example), the docs checked against the code, and this section moved to HISTORY.md.

## Milestone 2: a chase-camera racing game

One track from glTF, a car with real vehicle physics, a chase camera with smoothing, lap timing, a HUD, shadows, and an environment map. It proves 3D rendering, physics, asset streaming, and performance. Planned in full once milestone 1 closes; its pieces will come from libwgt's 3D (scenes, meshes from glTF, materials, lights, shadows, environments) and a physics3d module on Jolt's C API.

## Milestone 3: an ARPG vertical slice with co-op

One dungeon level, one player class with three abilities, three enemy types with navmesh pathing, loot drops, an inventory, and 2 to 4 players in co-op through a central server. It proves skinned animation with blending, many lit instances, navmesh and AI, an authoritative server, and replication. Planned in full once milestone 2 closes.

## Later, when its condition holds

Each starts when something anyone can check is true.

- **physics2d (Box2D v3)**: when a game or example on the ROADMAP needs 2D rigid bodies, joints, or contact resolution; no game in milestones 1 to 3 does, and Asteroids' overlaps are the ecs collider's.
- **Dear ImGui (cimgui), the developer UI**: when a milestone's game needs an in-game inspector (SPEC.md, "Not in v1").
- **WebGPU behind a build option**, then as the default: libwgt's condition (its ROADMAP, "Later"): Firefox ships WebGPU on Linux, Chrome on Linux covers the common GPUs, and every example passes on WebGPU in CI, with an automatic fall back to WebGL2 and a persisted switch.
- **Native hot reload** (hxcpp or C): when a game's desktop-only bug needs the reload loop to find; the browser is the everyday loop.
- **Threads on the web**: when a benchmark shows a load or a system that a worker would take off the main thread by more than a frame's budget.
