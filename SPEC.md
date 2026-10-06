# libwgf

We're building libwgf: a game framework for the web first and the desktop second, with a C core and games written in Haxe. It is production software, not a demo: stable, tested, measured, and used to ship real games. No hacks, no shortcuts.

libwgf is a clean start, not a port. Six earlier libraries came before it (below), and they are its north star: the agent reads them, carries over what works, and redesigns what doesn't, recording why. Matching them is never the goal. Shipping the games below is.

## Done means games

In this order. Each milestone leaves everything before it working, and every game stays playable in CI.

1. **Asteroids** (2D). Ship, rocks that split, bullets, wraparound, score, lives, audio, particles, a title screen and a game-over screen built with the UI. This proves the loop, input, 2D drawing, audio, UI, entities, and export.
2. **A chase-camera racing game** (3D). One track from glTF, a car with real vehicle physics, a chase camera with smoothing, lap timing, a HUD, shadows, and an environment map. This proves 3D rendering, physics, asset streaming, and performance.
3. **An ARPG vertical slice with co-op** (Diablo / Torchlight / Darksburg style). One dungeon level, one player class with three abilities, three enemy types with navmesh pathing, loot drops, an inventory, and 2 to 4 players in co-op through a central server. This proves skinned animation with blending, many lit instances, navmesh and AI, an authoritative server, and replication.

   The slice's characters are **modular**:
   - **Shared skeletons.** Several skinned meshes (body, head, chest, legs, and so on) are driven by one skeleton and one animation state, matched by joint name, and swapped at runtime. A piece whose skeleton doesn't match is refused, never drawn wrong.
   - **Sockets.** Named attachment points from the glTF or from data (`hand_r`, `hand_l`, `back`), each with an offset. Equipping an item from the inventory hangs the item's model on its socket: a weapon, a shield.
   - **Animation.** Crossfades between clips, and layers: an upper-body attack over lower-body movement.
   - **Co-op.** Equipment is replicated as item ids, never as meshes, so every player sees what the others wear and wield.

   - **Source.** The character comes from `~/media/models/woman/woman-src.blend` (Quaternius's Ultimate Modular Women, CC0). It has 10 outfits split into Body, Head, Legs and Feet, all on one 62-bone armature. A Sword and a Pistol are parented to the bone `Middle1.R`, which is the socket. It has 24 clips, including Sword_Slash, Punch, Kick, Roll, HitRecieve, Die and directional runs. `gen_woman.py` beside it is the export pattern: headless Blender. Write a new export script for the slice (one skeleton with every clip, each part in its own file, the weapons with their sockets) that writes into the game's assets, never into `~/media`. A shield, and enemies, come from elsewhere in `~/media/models/` (for example `cultist/`) or from other CC0 sources, credited.

   - **Pieces as separate files.** A character is dressed from per-piece files, never from one file holding every piece, so a player downloads only what they wear.
     - The rig file holds the skeleton and the clips, in sets (locomotion, combat) fetched when needed.
     - Each piece's glTF holds just its skinned mesh, with its own joint list and inverse bind matrices, and no animation.
     - A piece is bound to the character's skeleton instance by joint name: a remap table built once per piece and skeleton, and the skeleton posed once per character per frame and shared by every piece.
     - A rig id in the glTF extras, plus a bind-pose check within a tolerance, refuses a piece from another rig.
     - Pieces are listed in the asset manifest (rig id, slot, palette, file) and fetched on demand. The old piece stays drawn until the new one is ready, so equipping never pops.
     - Pieces follow the body-region convention (each piece *is* its region of the body, as Quaternius's are). Masks for clothing over a full body wait until an asset needs them.
     - Rigid items (weapons, shields) carry no skin; they hang on sockets.
     - Retargeting animation to a different rig (other proportions, a second character) is out of v1: later, when a second rig exists.
     - The exporter writes the rig with its clip sets, plus one file per piece, from `woman-src.blend`. Later, artists will author pieces as separate .blend files that link the rig file.

   Done when equipping a different helmet, chest piece and weapon from the inventory changes the character for every player in the session, mid-animation, with no pop.

The full ARPG is a game, not a framework milestone. The vertical slice is what libwgf must be able to carry.

Each game lives in `games/<name>/`, uses only libwgf's public API, is deployed to GitHub Pages on every push to `main`, and also exports as a desktop build.

## Dev workflow

- Work autonomously. Ask questions only when truly blocked. Start by proposing the plan for milestone 1 in `docs/ROADMAP.md`, then build it.
- Read the reference repos (below) freely, but treat them as read-only. Look nowhere else on this machine.
- Before building any feature, read how the references did it, along with their `docs/HISTORY.md` entries for it. Carry over what works, redesign what doesn't, and record the decision in libwgf's `docs/HISTORY.md` with the reason.
- Testing comes first. Unit tests for logic. A headless smoke run of every game and example that fails on a crash, a hang, or an error log. A browser check that loads every game in headless Chromium. Image comparison for rendering once a regression slips past smoke.
- A feature-test scene that exercises every component and the whole public API, run in CI on Linux, Windows, and the web.
- Test machines, besides CI:
  - This machine (Linux) runs the web builds with Emscripten 5.0.7 from `$EMSDK`, and the browser checks in headless Brave.
  - Windows builds cross-compiled with MinGW run their tests under Wine, found as libwgt's `tools/wine.py` finds it (Steam's Proton).
  - MSVC builds and native Windows runs go over ssh to `sightblinder` (Windows 11, Visual Studio 2026 Community, CMake, Python), as libwgt's `tools/run_remote_windows.py` does.
- Before every commit: review the diff against `docs/CONVENTIONS.md`, run the full verification, and make sure tests pass. A skipped check is reported as skipped, never as passed.
- Commit small, and push each reviewed commit to https://github.com/whirlinggizmo/libwgf (public, so Actions runners are free).
- Create `AGENTS.md`, `docs/CONVENTIONS.md`, `docs/ARCHITECTURE.md`, `docs/HISTORY.md` and `docs/ROADMAP.md`, plus skills for repeated work (adding a component, adding a binding call, adding an example). Each rule is stated once, in one doc.
- Code style and doc style: follow libwgt's CONVENTIONS.md unless a decision in libwgf's HISTORY says otherwise.

## Reference repos (read-only north star)

- `~/projects/github/whirlinggizmo/libwgt`: the latest. Layers, nodes (keep the idea), handles checked by clang, linking only what a program uses, the tools, and the Haxe binding plan. Its `docs/LINEAGE.md` is the summary of the whole line.
- `~/projects/github/whirlinggizmo/wgrender-c`: the most complete. glTF with skinning, scenes, picking, audio, async assets, the web harness, the JS and Haxe bindings, and Clay UI. Its `docs/HISTORY.md` is the deep record. It was the proving ground: what succeeded there was fed into libwgt, and some of it hasn't arrived yet (the JS binding, for one). When wgrender-c has something libwgt lacks, treat it as proven, not as abandoned.
- `~/projects/github/whirlinggizmo/wgrender-hx`, `hotreload-hx`, `hotreload-nim`: bindings and hot reload.
- `~/projects/github/whirlinggizmo/flecs_wrapper-c`, `wgutils-c` (WebSocket client for desktop and wasm), `experiments/`.
- `~/media/models/`: source art (Blender files, textures), read-only. Exports go into libwgf.
- `~/projects/github/robknopf/librl`: the raylib-era library, for its hot-reload state stash and its binding version stamps.

## Invariants (proven by measurement; change only with a new measurement)

- **Handle-only public C API.** Values and handles cross the API, never pointers into the library's memory. No backend identifier (sokol, flecs, Jolt) appears in a public header.
- **A program links only what it uses.** Optional subsystems never get named by the core. Web size is measured per example in CI, with a budget per game.
- **Bulk calls for hot paths.** A call that reads or writes many objects takes a caller-owned array and a count, filled or read in one call. This means no shared scratch area (librl's, dropped: results overwrote each other between calls, and views broke when wasm memory grew), and no command stream within a process (wg-vf's, which wins only across a process or network boundary; libwgt's LINEAGE.md). Heavy per-entity loops run as C systems (ECS, physics). Scripts set intent; they don't drive every transform every frame.
- **One async model.** Tasks polled from the frame. No synchronous twins, and no callbacks across a binding.
- **Bindings generated from the headers.** One name per C call, with sugar on top. Exports are called by quoted key so minification can't break them. Binding marshalling never piles up on the wasm stack.
- **Web first.** WebGL2 is the default, with WebGPU behind a build option until the condition in libwgt's ROADMAP holds. Threads are optional, and every game runs without cross-origin isolation.
- **Failures are visible.** A failed load logs once and draws a placeholder. Nothing fails silently.
- **"Later" has a checkable condition**, never "when we need it".

## Tech stack

- **Core: C (C11).** It gives the tightest wasm, the C ABI that bindings need, and the best tooling (sanitizers, clang checks). Nim and Zig were considered; C won on wasm size, ABI simplicity, and reliability for the agent.
- **Platform and graphics: sokol** (app, gfx, audio, fetch), vendored. One backend layer, never our own abstraction over several.
- **Games: Haxe.** JS on the web against a prebuilt wasm host, hxcpp natively and for the server. Haxe never goes into the core.
- **JS/TS binding: the one way JS reaches the wasm host.** It is generated from the headers: ES modules, `.d.ts` declarations, and each header comment as its JSDoc. It is a public API in its own right, so JS and TypeScript developers can use libwgf without Haxe. Haxe's JS target is built on it, dogfooding it, so every Haxe game, test and autopilot run exercises it. When Haxe needs something the JS binding can't express cheaply, extend the JS binding; never bypass it. Native Haxe still goes through hxcpp to C directly.
  - Source: wgrender-c's `bindings/js` (generator, runtime, guest, type tests, examples), proven there and not yet carried into libwgt. Its runtime rules carry over: records through one fixed slot, strings released per call, getters that fill a caller's object or array, quoted keys.
  - Checked by: a TypeScript type test, JS examples in the browser checks, the JS-to-wasm call benchmark, and the binding's size in the size table. Its cost was about 3% of download in wgrender; it stays measured, not assumed.
  - Versioned like the C API, with the version stamps checked at startup.
- **ECS: flecs**, wrapped behind libwgf's API. Nodes stay as the transform hierarchy, and entities attach to nodes. Decide the exact split in milestone 1 and record it.
- **Physics:** Box2D v3 (C) for 2D. Jolt (through its C API) for 3D, which brings vehicles for the racing game. Each is an optional module.
- **Game UI: Clay for layout, plus libwgf's own widget layer.** Clay only lays out boxes and hit-tests them; it has no widgets. libwgf's ui module adds:
  - buttons, sliders, toggles, and text fields with the clipboard
  - focus, with keyboard and gamepad navigation
  - drag and drop (the ARPG inventory) and tooltips
  - per-game styling
  It is immediate-mode, drawn by libwgf, and small enough for the web budget. Each widget is added when a milestone needs it, starting with Asteroids' menus.
- **Developer UI: Dear ImGui through cimgui**, as an optional module linked only into development builds. Use it for inspectors and debugging; it never ships in a game.
- **Assets:** cgltf, stb, dr_libs and fontstash, as in the references. Fetch over HTTP on the web, and a fetch hook on the desktop.
- **Networking: an optional module.** WebSocket client (browser and desktop) plus a native headless server: the same Haxe simulation, built with hxcpp, linked against libwgf with no window or GPU. The server is authoritative, clients predict their own player, and everything else is interpolated.

## Architecture

- **Layers with dependencies pointing down:** core, platform, gfx, audio, asset, then the optional modules (ecs, physics2d, physics3d, ui, net), then app. Each is listed in `docs/ARCHITECTURE.md`.
- **Scenes as data:** a text scene format (diffable) holding entities, components and prefabs. Prefabs get spawned at runtime.
- **Behavior in Haxe**, attached to entities. Lifecycle: create, tick (fixed rate), frame (interpolated), destroy, trigger enter and exit.
- **Hot reload is the everyday loop:** save Haxe or an asset, and the running browser page picks it up with its state kept.
- **Export:** one command builds a game for the web (a static folder) and for the desktop, then smoke-tests the result.

## Agent-drivable

An agent must be able to build and check a game with no human looking:

- `wgf` CLI: create a project from a template, build, run headless for N frames, screenshot, dump the scene as text, and run a scripted input sequence.
- Each game ships a scripted playthrough (inputs over time with assertions) that runs in CI.
- If a change can only be checked by a human looking at it, that is a missing tool. Build the tool first.

## Not in v1

- **No editor GUI.** Scenes are text, and the CLI plus hot reload are the authoring loop. Build an in-game inspector on the Dear ImGui module when a milestone needs one. A web editor comes after milestone 3, designed from what the ARPG slice needed.
- Also out: consoles, native mobile, a scripting language other than Haxe, our own abstraction over multiple render backends, and accounts, matchmaking or persistence on the server.
