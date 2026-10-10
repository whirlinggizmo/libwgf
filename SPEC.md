# libwgf

We're building libwgf: a game framework for the web first and the desktop second, with a C core and games written in Haxe. It is production software, not a demo: stable, tested, measured, and used to ship real games. No hacks, no shortcuts.

libwgf is a clean start, not a port. Six earlier libraries came before it (below), and they are its north star: the agent reads them, carries over what works, and redesigns what doesn't, recording why. Matching them is never the goal. Shipping the games below is.

## Done means games

In this order. Each milestone leaves everything before it working, and every game stays playable in CI.

1. **Asteroids** (2D). Ship, rocks that split, bullets, wraparound, score, lives, audio, particles, a title screen and a game-over screen built with the UI. This proves the loop, input, 2D drawing, audio, UI, entities, and export.
2. **A chase-camera racing game** (3D). One track from glTF, a car with real vehicle physics, a chase camera with smoothing, lap timing, a HUD, shadows, and an environment map. This proves 3D rendering, physics, asset streaming, and performance.
2.5. **Jam-ready: the framework's breadth.** libwgf is a framework, not one game's library: a game jam must not have to stop and add a feature under a deadline. Games drive the order of work, but breadth defines done. After the racer, restore what was trimmed from wgrender-c and libwgt and no game has brought back yet, each feature with a showcase example (wgrender-c's suite is the model).
   - **Already trimmed** (keep this list in ROADMAP's catalog, and re-check it each milestone):
     - render targets and post-process effects
     - rounded rectangles, borders, nine-slice textures
     - text slices without copying (`draw_n`, `measure_n`) and the FPS overlay
     - particle color and size keyframes, palettes, spawn shapes, prewarm, sprite-sheet frames, spin, inherited velocity, alpha modes
     - a settable placeholder texture
     - public pointer capture, and cursor capture
     - debug drawing
     - whatever 3D milestone 2 didn't need
   - **An exception, to look into later and not restore by default:** Clay used directly, as wgrender-c's `clay` example did. libwgf's ui owns Clay, so a program driving Clay itself would be a second way to do UI. Whether to expose it, wrap it, or leave it out is an open question, not a catalog item.
   - Breadth stays cheap: only what a program calls is linked, and the size table shows that a small game doesn't pay for it.
   - **Done when:**
     - every catalog feature has an example in the browser check and the size table
     - `wgf new` offers a few starter templates (top-down, platformer, menu-driven)
     - a jam game can go from `wgf new` to deployed on Pages in one sitting, which is checked by doing it
3. **An ARPG vertical slice with co-op** (Diablo / Torchlight / Darksburg style). One village at night, one player class with three abilities, three enemy types with navmesh pathing, loot drops, an inventory, and 2 to 4 players in co-op through a central server. This proves skinned animation with blending, many lit instances, navmesh and AI, an authoritative server, and replication.

   **The slice is the first scene of Rob's game design "Whispers in the Shadows"** (WITS, an ARPG with in-world puzzles): a small town near a university, at night, whose seemingly normal townsfolk put on red hooded cloaks and try to sacrifice you (Rob's decision, 2026-10-10). A village rather than a dungeon level, since lantern- and window-lit streets prove many lit instances far better than corridors, the navmesh gets streets, alleys, and squares, and it is WITS's own setting.
   - **The player** is the modular character below dressed as a noir investigator, the gumshoe, through wearables on the shared skeleton: a fedora, a trench coat, perhaps a lantern on a socket. (Rob's earlier gumshoe was a Unity Asset Store model, not redistributable, so it isn't used.)
   - **The enemies** (three types) include the cultists: Rob's cultist model (`~/media/models/cultist`, his to publish), re-rigged to Quaternius's Universal Animation Library's skeleton and animated with its clips (CC0, `~/media/models/quaternius/universal-animation-library`). Its Mixamo rig and animations (`cultist-mixamo-reference.glb`) stay out of every public repository, a game's import into libwgf's `games/` included: Adobe's terms allow Mixamo's animations in a shipped project but not as redistributed files, and the auto-rig's output is a grey area, so the retarget removes both; the Mixamo file may serve as a private reference for how it should move. A townsperson turning cultist by putting on the red hooded cloak is a wearable swap, replicated as an item id, so the modular-character and replication work shows in play.
   - **One co-op puzzle**, chosen because it tests the replication of shared world state rather than of entities: a rune-locked crypt door at the town's edge that the players open together (runes found, placed in order; two players reaching for the same rune resolved by the server). The crypt's inside is outside the slice.
   - **Art:** CC0 kits, credited. The village's period is open (Quaternius's Medieval Village MegaKit is CC0 but medieval, and WITS suggests a 1920s New England town), chosen at planning, with Rob.
   - The full WITS (more kinds of puzzle, the investigation loop) is a candidate game after milestone 3, written outside libwgf by a game session as the racer is; not part of the slice.

   The slice's characters are **modular**:
   - **Shared skeletons.** Several skinned meshes (body, head, chest, legs, and so on) are driven by one skeleton and one animation state, matched by joint name, and swapped at runtime. A piece whose skeleton doesn't match is refused, never drawn wrong.
   - **Sockets.** Named attachment points from the glTF or from data (`hand_r`, `hand_l`, `back`), each with an offset. Equipping an item from the inventory hangs the item's model on its socket: a weapon, a shield.
   - **Animation.** Crossfades between clips, and layers: an upper-body attack over lower-body movement.
   - **Co-op.** Equipment is replicated as item ids, never as meshes, so every player sees what the others wear and wield.

   - **Source.** The character comes from `~/media/models/woman/woman-src.blend` (Quaternius's Ultimate Modular Women, CC0). It has 10 outfits split into Body, Head, Legs and Feet, all on one 62-bone armature. A Sword and a Pistol are parented to the bone `Middle1.R`, which is the socket. It has 24 clips, including Sword_Slash, Punch, Kick, Roll, HitRecieve, Die and directional runs. `gen_woman.py` beside it is the export pattern: headless Blender. Write a new export script for the slice (one skeleton with every clip, each part in its own file, the weapons with their sockets) that writes into the game's assets, never into `~/media`. A shield, and the other enemies, come from elsewhere in `~/media/models/` or from other CC0 sources, credited (the cultist as above).

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

   **Gameplay is behaviors first; ECS is the engine's inside and an escape hatch.** ARPG logic (buffs, procs, abilities, bosses, loot) is varied, rule-heavy and small in count. Writing it as pure ECS bends it out of shape (two poisons become effect entities with relationships and table fragmentation), so it is written as behaviors:
   - **More than one of the same behavior per entity.** Two `Poison`s, each with its own state.
   - **Typed fields, not string params.** Declared in Haxe or TS, with the layout generated, so scene files (`poison dps=4 remaining=6`), `dump`, autopilot `expect` and co-op replication work for game state with no hand-written code.
   - **Event hooks:** `onHit`, `onDamaged`, `onEquipped`, `onTrigger`, and so on. A sword's proc is a behavior on the sword, listening for its wielder's hits.
   - **Effects belong to their target, not their source.** A poison on the player, or a slow on a car, runs until its own duration ends or it's cured, whatever happens to whoever applied it. The source is held as a weak handle (its generation checked, so a dead or reused entity reads as gone) plus a snapshot taken when the effect was applied: damage per tick, team, display name, and who gets kill credit. Players are referred to by a stable player id, never by an entity handle, so dying, respawning or logging out changes nothing.
   - **Behaviors talk through a typed pub/sub bus, which lives in the Haxe/JS runtime, not in C.**
     - An event is a typed class (`Hit { @:subject attacker, @:subject target, amount, kind }`). Fields marked `@:subject` are indexed, so `bus.subscribe(Hit, {attacker: wielder}, …)` checks only that entity's subscribers. A predicate filters further (`h -> h.amount > 100`). A subscription with no subject filter is global.
     - Delivery is queued by default, at phase boundaries, in a deterministic order, so the co-op server matches the browser. Immediate delivery is an opt-in, mainly for UI.
     - A subscription is owned by its subscriber and ends when that behavior or its entity goes away. A publisher dying never cancels anyone's subscription; it just stops publishing.
     - Events have typed fields, so they can be logged, checked by autopilot (`expect event Hit{target: player} count >= 3`), and replicated when co-op needs a game event to reach the server.
     - Engine events (hit, trigger, spawned, destroyed, animation markers, sound ended) still come through C's polled queue, and the runtime publishes them on the bus.
     - Why not in C: wgrender-c removed its C event bus (`wgr_event_on`/`emit`: callbacks across the binding, untyped `void *` payloads, nothing used it). See its HISTORY, "The event bus goes". Its verdict stands for the C core. What's new is game code that needs to talk, and that code all lives in one language.
   - **Phases.** Each tick runs Input (engine), Update (game), Simulate (engine: motion, physics, collision), React (game: hits and triggers), and Finalize (engine: queued spawns and destroys applied, nodes synced). Structural changes inside a phase are queued until it ends.
   - **The dispatch stays in one language.** The Haxe or JS runtime owns its behavior instances and is entered once per game phase. Inside, it's a Haxe loop calling Haxe methods. C never calls into script code once per entity.
   - **The engine's hot, uniform work stays data-oriented in C:** transforms, motion, collision, particles, physics, animation.
   - **The escape hatch:** batch query systems (components with field layouts, read and written a column at a time through caller-owned arrays) for the rare case of thousands of similar things. A system moves there, or into C, only when a measurement forces it, and HISTORY records the numbers.
   - **Prior art:** `~/projects/github/whirlinggizmo/flecs_wrapper-c` and `~/projects/github/robknopf/flecs_wrapper-hx` (read its `docs/PLAN-component-storage.md`). Keep their runtime components and per-table batching. Avoid what broke them: raw column pointers and callbacks handed to the host language.
   - **flecs is kept or replaced by measurement, decided right after the actor unification and before the racer** (Rob, 2026-10-07). Today libwgf uses only flecs' core, the case that flecs may be more than it needs (an open choice, not a reason to keep it): components by size, set, get, has and remove, and queries of up to 3 terms. Its hierarchy, prefabs, events, reflection and scheduling are all libwgf's own, and flecs is the ladder's biggest step (+87 KB gzipped, ecs included). The alternative is plain C sparse-set storage behind the same internal interface. The benchmark must not flatter either side:
     - **sparse sets' worst case:** a query whose rarest component is common but whose intersection is small (e.g. 50k actors with A, 50k with B, 100 with both), which archetype tables answer without scanning;
     - **archetype tables' worst case:** churn that adds and removes components on many actors each tick, which moves them between tables;
     - the ordinary cases: the actor benchmark's static and moving actors, spawn and destroy churn, lookups, and Asteroids' and the racer's real queries;
     - size: the ladder's "+ ecs" step for each.
     Keep the winner on the real load, with the worst cases recorded either way, and the reasons in HISTORY. Asteroids' autopilot runs are the regression test.
   - **The proof:** a stress autopilot run for the slice (4 players, 200 enemies with AI behaviors, about 100 active effects with stacked poisons, heals over time and procs firing, over a fixed number of frames). It holds 60 fps in the browser on a mid-range machine, and its frame time is recorded in the benchmarks beside the size table.

The full ARPG is a game, not a framework milestone. The vertical slice is what libwgf must be able to carry.

Each game lives in `games/<name>/`, uses only libwgf's public API, is deployed to GitHub Pages on every push to `main`, and also exports as a desktop build.

**Games test the framework only if they can't bend it.** Isolated examples prove a feature works; they don't prove it works with other features, over a session's lifetime, under load, or comfortably. A game built by the framework's own author tends to route around gaps quietly. So:

- **A separate game developer.** Each game is built, or rebuilt clean-room, by a session that can use only the public API, the docs and `wgf new`. It works in its own directory, and edits to libwgf are denied to it.
  - **The public surface only, enforced.** This tests libwgf as an SDK, not whether a determined developer could finish a game. The session may read what ships to a user: the docs, the public headers, the bindings (what users import, with their generated declarations), the `wgf` tool and its template, and the examples. Its settings deny reads of the library's source (`*/src/**`), `deps/`, the tests, HISTORY, ROADMAP and SPEC, and any existing game that would give the answer away.
  - **Anything only the source could answer is a failure.** It is logged with a severity: a docs gap at least, or a missing API when no documentation would fix it. When something is missing or awkward, it files the gap. It may work around it only by logging the workaround.
- **A friction log.** `docs/FRICTION.md` lists each workaround and each awkward use: the game, the file, what was missing, and what it cost. A milestone closes only when each entry has become a framework task or a recorded decision that the workaround is fine.
- **Game code first.** Each new feature in a milestone's plan starts with the game-side code you'd want to write, committed in the plan. The framework is then made to fit that code, and any gap is recorded. The framework bends to the game, not the other way round.
- **An adversarial review at each milestone's end.** A fresh session reads the game looking for framework work done in game code (hand-written hit tests, timers, tweens, pools, collision, layout). Each finding is justified or becomes a task.
- **Combination scenes** sit beside the isolated examples: small realistic scenes mixing features (UI over 3D with particles, rendered to a target), between an example and a game.
- **Jam simulations** test breadth (milestone 2.5). A fresh, isolated session gets only the docs, `wgf new` and a random jam theme, with a time limit, and must ship to Pages. Its friction log is the result. Run several, across genres.
- **Asteroids is the first case, applied retroactively:** a clean-room rebuild from the docs and API alone, without seeing `games/asteroids/` and with a time limit, plus an adversarial review of the existing game. Both are triaged before milestone 2's building starts.

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
- **One kind of object: the actor** (Rob's decision, before the racer). This is Godot's tree with Unity-style components, named like the theatre it plays in: **actors** on a **stage**, loaded from **scene** files. Unreal's Actor-with-components is the same model, and wg-core's `actor` the same word. The roots are `stage2d` and `stage3d`, like the other 2d/3d pairs (camera, shape, emitter) and Flash's Stage and Stage3D. The old names (`wgf_node`, `canvas`, `stage`) go, with no aliases.
  - Everything is an actor in one tree, with a transform and a place in the hierarchy. There is no separate entity object.
  - Visuals are actor kinds: sprite, shape, text, emitter, model, light, camera.
  - Any actor can carry behaviors (several, even of one type) and data components (motion, collider, game data), UI actors and cameras included.
  - Simulation and drawing are split inside the actor: an actor with simulated components keeps a transform at the tick rate and draws an interpolated one, so determinism, interpolation and co-op replication keep what they need.
  - A headless server runs the same tree with no GPU resources behind the visual actors.
  - Scene files and prefabs describe actor trees with their components.
  - The pub/sub bus's subjects are actors, and an event may bubble up the actor tree, Flash-style (capture down, then bubble up, either one stoppable), so a click inside a panel reaches the panel and a hit on a weapon reaches its wielder. Bubbling is opt-in per event type.
  - **Actors stay light, by rule and by measurement.** Godot's nodes are known to struggle at scale for four reasons: each node is a heavy object (signals, notifications, name, metadata); the engine calls into a script once per node, every frame; transforms propagate through the tree; and its C# binding allocated on every call. libwgf avoids all four by design and holds itself to that:
    - an actor is a handle into data-oriented C storage, with optional fields stored only when used;
    - C never calls into a script once per actor;
    - the binding doesn't allocate;
    - the scale escape hatches are still actors (instancing, emitters, batch query systems).
  - **Finding an actor:**
    - keep the handle `spawn` returns (a behavior always has its own actor);
    - a path relative to an actor (`car.find("wheel_rl/smoke")`) for a prefab's parts;
    - a name unique within its stage (`stage.find("player")`);
    - tags (`tagged("enemy")`);
    - by component or behavior (`with(Health)`);
    - spatial queries (an area, a ray);
    - and, in co-op, a stable id.
    Lookups are indexed, never a walk of the tree, and many results go into a caller-owned array. The docs teach finding once and keeping the handle, never a find every frame. Scene files refer to other actors by name or path (`target=@start_gate`), resolved once when the scene loads. The unification step covers handles, paths, names, and lookup by component or behavior; tags, spatial queries and stable ids come where the ROADMAP has them.
  - **An actor benchmark** is recorded beside sizes and frame times: 10k and 50k actors, static and moving, in deep and flat trees, with and without behaviors, and churn (1,000 spawned and destroyed a second). It records bytes and time per actor, and CI fails a regression the way the size table does.
  - The internals stay data-oriented C behind handles (Godot's servers are the same shape). flecs, or plain arrays, store the components; milestone 3 settles which by measurement.
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
