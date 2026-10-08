# Friction

Each workaround a game had to make, and each use of libwgf that was awkward, as the game's developer found it (SPEC.md, "Games test the framework only if they can't bend it"). A game developer works from the public API, the docs, and `wgf new` alone, and may work around a gap only by logging it here. A milestone closes only when each of its entries is triaged: it became a framework task (in ROADMAP.md), or a decision was recorded that the workaround is fine (in HISTORY.md, with why).

An entry is added by the game's developer, under its game, newest last, and is never rewritten; its triage is added to it. A triaged entry stays until its milestone closes, then moves to HISTORY.md with the milestone's record.

## Format

```
### <game>: <what was missing or awkward, in a few words>

- **Where:** <file and line, or the function, in the game>
- **Missing:** <what the framework lacked, or what made the use awkward>
- **Workaround:** <what the game did instead; "none" if it went without>
- **Cost:** <what it cost: lines, time, a measured number, a behavior the player sees>
- **Found by:** <the session: the game's developer, a clean-room rebuild, an adversarial review, a jam simulation>
- **Triage:** <empty until triaged; then "task: <ROADMAP step or item>" or "fine: <HISTORY entry>">
```

## Entries

Asteroids (milestone 1), from step 0 of milestone 2: the clean-room rebuild (`../libwgf-cleanroom/asteroids/FRICTION.md`, its entries numbered as there), the adversarial review (`../libwgf-review/asteroids/REVIEW.md`, its findings numbered as there), and what the triage itself found.

### Asteroids: `wgf` isn't on PATH, and the docs write it bare

- **Where:** BUILDING.md's "Games" block; `wgf new`'s closing message ("`cd ... && wgf serve`")
- **Missing:** a step putting `wgf` on PATH, or the docs saying how; after `cd` into a game, `wgf serve` is written bare
- **Workaround:** called it by its absolute path the whole session
- **Cost:** about a minute
- **Found by:** the clean-room rebuild (its #1)
- **Triage:** fixed in step 0: BUILDING.md says how to put the repository on PATH (and on Windows, `python <libwgf>\wgf`), and `wgf new` names the `wgf` it ran as

### Asteroids: the autopilot format is documented only in a private header

- **Where:** BUILDING.md and ARCHITECTURE.md, pointing to `app/src/wgf_app_autopilot_priv.h`; the template's `smoke.autopilot`, which shows only `expect` and `end`
- **Missing:** the format in a doc a game's developer reads: how to press, hold, and release a key, the comparison operators, the pad and mouse lines, the key names
- **Workaround:** probed the parser with junk lines until its errors gave the syntax away; the pad and mouse lines, and the operators, stayed unknown
- **Cost:** about 8 minutes, a dozen throwaway runs
- **Found by:** the clean-room rebuild (its #2)
- **Triage:** fixed in step 0: the format is BUILDING.md's ("Autopilot files"), the private header points there, and the template's smoke autopilot presses a key as an example

### Asteroids: no sound without bringing files

- **Where:** the game's sound effects
- **Missing:** a way for a first-time developer with no assets to make a sound: the template ships none, `tools/gen_sounds.py` writes the repository's own Asteroids' sounds, and no call makes a sound from samples in code
- **Workaround:** a game-side `tools/gen_sounds.py` (standard library `wave`), writing WAVs into the game's assets
- **Cost:** about 10 minutes, 60 lines
- **Found by:** the clean-room rebuild (its #4)
- **Triage:** task: milestone 2.5, "Sounds without files" (a sound from samples made in code, and a starter sound in the templates)

### Asteroids: hiding an entity's node does nothing

- **Where:** the game's `showShip` (`game/src/Main.hx`)
- **Missing:** a way to hide what an entity shows. Its node draws nothing itself, so `entity.getNode().setVisible(false)` returns true and changes nothing (as `wgf_node.h` says, `visible` is a node's own output); `enabled` would also stop its emitter's live particles
- **Workaround:** hid the SHAPE2D component's node instead
- **Cost:** about 5 minutes, 5 lines
- **Found by:** the clean-room rebuild (its #7)
- **Triage:** fixed in step 0: `wgf_entity_set_visible` and `_is_visible` hide or show everything an entity draws (its component nodes' output), while it moves, ticks, and its emitters keep simulating

### Asteroids: an autopilot's own `screenshot` line overrides `wgf screenshot --frame`

- **Where:** `wgf screenshot --frame 250 --autopilot autopilot/playthrough.autopilot`
- **Missing:** `--frame` winning over the file's own `screenshot` lines, or a warning; and `wgf autopilot --web` saving its named shots anywhere
- **Workaround:** filtered the file's screenshot lines out into a scratch copy
- **Cost:** about 5 minutes, two wasted browser runs
- **Found by:** the clean-room rebuild (its #8)
- **Triage:** the override fixed in step 0 (`wgf screenshot` and `wgf dump` drop the file's own screenshot lines, and say so); saving the named shots is a task: milestone 2, step 4, with the slice's autopilot tooling

### Asteroids: arrow keys drive both the game and the UI's focus, and the autopilot can't expect on text

- **Where:** the game-over screen (`game/src/Main.hx`, `gameOverScreen`)
- **Missing:** nothing in libwgf for the keys (the UI moves focus on arrows, as `wgf_ui.h` says); but probes are numbers only, so the autopilot couldn't check which button had focus, and finding it took a log line a frame
- **Workaround:** the game-over buttons appear 1.2 s after the panel; the playthrough stops steering before the game ends
- **Cost:** about 8 minutes, 12 lines
- **Found by:** the clean-room rebuild (its #9)
- **Triage:** fine for the keys: the game's design, a delay before a menu takes input, as the record says (HISTORY.md, "Step 0, Asteroids' friction triaged"); task for the text: milestone 2, step 4, an autopilot `expect` on the UI's focus and on text probes, which the racer's menus need too

### Asteroids: the template pins the web canvas to 800 by 600 in the page's corner

- **Where:** the template's `Main.hx` (`Window.setSize(800, 600)`)
- **Missing:** a design resolution with a fitting mode; on the web, `Window.setSize` pins the canvas, so the game sat in a corner of the page, and fitting, letterboxing, clipping, and the HUD's placement were left to the game
- **Workaround:** `Window.setSize` only off the web; a `Camera2d` zoomed to fit the field; `Render.pushClip` to the letterboxed field; the HUD placed by hand in the fitted rectangle
- **Cost:** about 10 minutes, 25 lines
- **Found by:** the clean-room rebuild (its #11)
- **Triage:** fixed in milestone 2, step 2: the presentation mode (`wgf_presentation.h`); the template sets an 800 by 600 design, fit, and no window size

### Asteroids: an export is smoke-tested only, and leaves a file in the desktop folder

- **Where:** `wgf export`; `export/desktop/.wgf-run.autopilot`
- **Missing:** a way to fly the game's own playthrough against the export; and the smoke run's copy of its autopilot left beside the shipped binary
- **Workaround:** ran `wgf autopilot --web` on the development build instead
- **Cost:** about a minute
- **Found by:** the clean-room rebuild (its #12)
- **Triage:** the leftover fixed in step 0 (a native run's autopilot file goes in a scratch folder, removed after, never beside the program); flying a chosen autopilot against the export is a task: milestone 2, step 4 (`wgf export --autopilot`)

### Asteroids: what worked (five notes)

- **Where:** the clean-room rebuild's #3, #5, #6, #10, #13
- **Missing:** nothing. `wgf new` and a headless autopilot worked first try; the headers are the docs, and thorough; the game compiled and ran first try, the generated Haxe reading like the headers; an autopilot's failure prints the probe's value, which with `wgf dump` made it a debugger; and exports are small (273 KB) and quick
- **Workaround:** none
- **Cost:** none
- **Found by:** the clean-room rebuild
- **Triage:** fine: kept as they are, and kept so (a change that loses one of them is a regression)

### Asteroids: not verified by the rebuild

- **Where:** the clean-room rebuild's "Not verified"
- **Missing:** a check of the sounds by ear (no audio device there), of the gamepad (the format it found had no pad lines), and of the desktop window by eye
- **Workaround:** none
- **Cost:** none in the build; the three are unchecked in that game
- **Found by:** the clean-room rebuild
- **Triage:** fine: the pad lines exist and are now documented (the autopilot format entry above); sound and the desktop window are checked by libwgf's own audio tests and window checks, not a game's

### Asteroids: spawn protection by `setMask(0)` doesn't protect (a live bug)

- **Where:** `games/asteroids/src/Ship.hx` (`setMask(0)` while it blinks, `setMask(2)` after); the scene's ship `layer=1 mask=2`, rock `layer=2 mask=5`
- **Missing:** the collider's rule where it was needed: `wgf_collider.h`'s opening comment states it (a pair meets when either side's mask has the other's layer), but the mask's own line said only "what it is, and what it meets", and the game read it as one-sided, so a rock (mask 5) still met a ship whose mask was 0, and a ship respawned onto a rock died blinking; and a way to switch a collider off without editing bits the scene file owns
- **Workaround:** none that works: the game believed the mask protected it
- **Cost:** a bug a player meets, which no autopilot checked
- **Found by:** the adversarial review (its #1)
- **Triage:** fixed in step 0: the rule stated again on the mask's line and in ARCHITECTURE.md; `wgf_collider_set_enabled` and `_is_enabled` switch a collider off, its settings kept; Asteroids' ship uses it; the ecs's tests hold that a switched-off collider meets nothing from either side, and that one switched on again meets as its settings say. An area query (anything on these layers within a radius, no entity needed) is a task: milestone 2.5

### Asteroids: no logical resolution or screen fitting; on the web the canvas is pinned to the top-left

- **Where:** `games/asteroids/src/Main.hx` (`WIDTH`, `HEIGHT` in five places, `Window.setSize`); the scene's `bounds rect=0,0,960,720`, three times
- **Missing:** a design size with a fit mode, bounds that can take the design area, the UI and `Draw` sharing its coordinates; and the docs saying that `Window.setSize` on the web fixes the canvas's size over the page's fill
- **Workaround:** none: the game is 960 by 720 at the top-left of any browser
- **Cost:** cut off on a phone, a corner of a large monitor; on a resized desktop window the menus and the field drift apart
- **Found by:** the adversarial review (its #2); the clean-room rebuild's #11 too
- **Triage:** fixed in milestone 2, step 2: Asteroids sets a 960 by 720 design, fit, and its scene's bounds take the visible area (`bounds visible=true`); the `Window.setSize` sentence fixed in step 0 (`wgf_window.h`), and pointed at the presentation in step 2

### Asteroids: input mapped by hand

- **Where:** `games/asteroids/src/Ship.hx` (turn, thrust, fire as `||` chains of keys, pad 0's buttons, and stick and trigger thresholds); the title's help line
- **Missing:** named input actions bound to keys, pad buttons, axis directions, and touch regions, read across every pad, their bindings listable for help text and a remap screen
- **Workaround:** the chains; pad 0 only; no touch
- **Cost:** about 15 lines; unplayable on a phone; help text out of step with the bindings
- **Found by:** the adversarial review (its #3)
- **Triage:** task: milestone 2, step 4 (input actions, which the racer's `Car.hx` sketch now uses)

### Asteroids: four timers by hand, one already wrong

- **Where:** `games/asteroids/src/Main.hx` (`waveDelay`, `respawnDelay`), `Ship.hx` (`cooldown`, `safe` and its blink)
- **Missing:** timers on the tick clock owned by a behavior or the runtime (`after`, `every`, a cooldown), ended with their owner, cleared with a state; and a blink for a node
- **Workaround:** floats counted down by `dt`; `start()` resets neither delay, so a partial wave delay carries into the next game
- **Cost:** about 20 lines, and the stale-delay bug
- **Found by:** the adversarial review (its #4)
- **Triage:** task: milestone 2.5 ("Timers"); the stale delay fixed in the game in step 0 (`start()` resets both)

### Asteroids: a voice pool for one-shot sounds; the `voice` component unused

- **Where:** `games/asteroids/src/Sounds.hx` whole, copied in `examples/js/asteroids/sounds.js`; `Ship.hx`'s thrust loop
- **Missing:** a fire-and-forget sound (`Audio.playOneShot`, a mixer-owned pool with a polyphony limit); and the docs saying the ecs's `voice` component holds a loop that ends with its entity
- **Workaround:** voices preallocated per sound and rotated; a global thrust voice the ship switches and must stop in `onDestroy`
- **Cost:** about 40 lines, twice
- **Found by:** the adversarial review (its #5)
- **Triage:** task: milestone 2, step 14 (the racer's engine, tires, and impacts need one-shots, and the component voice shown and documented there)

### Asteroids: prefab variants by name and string parameters; numbers twice

- **Where:** `games/asteroids/src/Rock.hx` (`PREFABS`, `RADII`, `SPEEDS`, `POINTS` by size; `setParam("size", ...)` after the spawn); the scene's collider radii and margins
- **Missing:** typed behavior fields (milestone 3's); and the docs saying when `onCreate` runs (at the runtime's next poll, after the code that spawned it returns, so a parameter set right after `spawn` is there)
- **Workaround:** the size set as text after the spawn and read back as a number; radii kept twice
- **Cost:** a hidden ordering contract, and tuning that has to happen in two files
- **Found by:** the adversarial review (its #6)
- **Triage:** the `onCreate` timing documented in step 0 (`Behavior.hx`); typed fields are milestone 3's (SPEC); the game reading its prefabs' parameters and collider radius is fine as a later cleanup, not a framework task

### Asteroids: collider bits, the gun's offset, and the lives icon kept in code

- **Where:** `Ship.hx` (`setMask(2)`, the muzzle at 18 units, `drawIcon`'s outline)
- **Missing:** named layers in the scene format; named points on a prefab (markers, sockets)
- **Workaround:** the numbers in code
- **Cost:** a few constants in step with the scene by hand
- **Found by:** the adversarial review (its #7)
- **Triage:** named layers: task, milestone 2.5; markers: milestone 3 (SPEC's sockets); `setMask(2)` goes with the collider's enable in step 0; the icon: fine

### Asteroids: a one-shot effect is an entity with a lifetime tuned by hand

- **Where:** `Rock.hx`'s `explosion`; the scene's `explosion` prefab (`lifetime seconds=1.2` against particles living up to 1.0)
- **Missing:** an emitter whose entity ends when it is empty; an owner's particles allowed to finish when it goes
- **Workaround:** a lifetime longer than the particles'
- **Cost:** two numbers in step by hand; the ship's exhaust vanishing with it
- **Found by:** the adversarial review (its #8)
- **Triage:** task: milestone 2, step 11 (the emitters, 2D and 3D, ending their entity when empty; particles finishing after their owner)

### Asteroids: spawning is four lines each time, and a missed `snap` is a trap

- **Where:** `Main.hx`, `Rock.hx` (twice), `Bullet.hx`: `spawn`, `setPosition`, `snap`
- **Missing:** a spawn at a place, snapped (`scene.spawnAt`), and a scene's default parent
- **Workaround:** the pattern, four times
- **Cost:** 12 lines; a forgotten snap draws a sweep from the origin
- **Found by:** the adversarial review (its #9)
- **Triage:** task: milestone 2, step 4 (`wgf_scene_spawn_at`; the racer spawns its car and props)

### Asteroids: trigger handling by type tests, a stale-entity guard, and an empty behavior class

- **Where:** `Rock.hx`'s `onTriggerEnter`; `Bullet.hx` (a class with no code); `Main.hx`'s registration
- **Missing:** the runtime dropping a trigger whose entity or other died earlier in the batch (or the docs saying it can arrive); the other side's layer in the trigger; and a behavior name used as a tag allowed without a class (today it logs a warning)
- **Workaround:** an `isAlive` guard; `Std.isOfType` dispatch; an empty `Bullet` class registered to be found
- **Cost:** a few lines, and a class that does nothing
- **Found by:** the adversarial review (its #10)
- **Triage:** task: milestone 2, step 4 (the racer's checkpoints are triggers: the runtime drops stale triggers, a trigger carries the other's layer, and a tag-only behavior name is documented and not warned about)

### Asteroids: UI focus by hand on every screen change; a held fire key can restart the game

- **Where:** `Main.hx` (`Ui.setFocus("again")` from the tick, `setFocus("play")` twice)
- **Missing:** a panel's default focus when the last input was keys or a pad; a guard so a press begun before a screen appears doesn't activate it; confirm keys apart from a game's fire key (input actions)
- **Workaround:** `setFocus` by string id on each screen
- **Cost:** ids duplicated; a player hammering fire as the last ship dies skips the game-over screen
- **Found by:** the adversarial review (its #11); the clean-room rebuild's #9 met the same thing
- **Triage:** task: milestone 2.5 (the UI's default focus and its input guard), with input actions from milestone 2, step 4

### Asteroids: the HUD placed by guessed coordinates

- **Where:** `Main.hx` (score at 24, 16; `wave` at `WIDTH - 120` to look right-aligned)
- **Missing:** alignment on `Draw.text`; anchors for HUD drawing to the visible area's corners
- **Workaround:** a guessed offset
- **Cost:** a label that overflows with another string or font
- **Found by:** the adversarial review (its #12)
- **Triage:** fixed in milestone 2, step 2: `Draw.textAligned`, and the visible area (`Presentation.getVisible`) to anchor to; the wave right-aligned at the design's edge

### Asteroids: the load gate, polled by hand

- **Where:** `Main.hx` (`ready` when the scene is READY; Play guarded by it)
- **Missing:** nothing structural (polling is the one async model); the template doesn't show the gate, and `Scene.spawn` returning 0 before READY says nothing
- **Workaround:** the `ready` flag
- **Cost:** a trap every game meets once
- **Found by:** the adversarial review (its #13)
- **Triage:** fine: polling is right (SPEC's one async model); a spawn before READY logging once is a task, milestone 2.5, and the template's gate comes with step 2's template change

### Asteroids: what the game is right to own

- **Where:** the review's #14: the states, the wave layout, extra lives and score, the jagged rocks, the thrust's intent, the bullet cap, the probes; and `Ship.current`, set and never read
- **Missing:** nothing
- **Workaround:** none
- **Cost:** none
- **Found by:** the adversarial review (its #14)
- **Triage:** fine: the game's to own, as the review says; `Ship.current`, dead code, removed in step 0

### Asteroids: an emitter under a disabled parent still simulates

- **Where:** `gfx/src/emitter/wgf_gfx_emitter2d.c`'s update; any emitter under an entity's node or a subtree switched off
- **Missing:** the update stepping an emitter only when it and every node above it are enabled, as `wgf_node.h` promises ("skipped altogether, with everything under it"). It read the emitter's own flag alone, so particles were born, moved, and aged unseen, and showed aged when the parent came back. libwgt's emitter checks the chain (`enabled_in_tree`); libwgf's CPU emitter, written new in milestone 1, lost it
- **Workaround:** none in the game (Asteroids never disables a parent)
- **Cost:** none seen yet; a pause menu over a scene with emitters would have shown it
- **Found by:** Rob's question during step 0's triage
- **Triage:** fixed in step 0 (libwgt's check, with a test); the CPU emitter itself is retired at milestone 2, step 11, by libwgt's GPU emitters, which have the check

### Asteroids: the particles are on the CPU; libwgt's were on the GPU

- **Where:** `gfx/src/emitter/wgf_gfx_emitter2d.c`, milestone 1's own emitter
- **Missing:** libwgt's GPU emitter core (2D and 3D in one, each particle written once at birth and moved by the GPU) and the breadth it had; milestone 1 carried no shaders, so it wrote a CPU emitter instead
- **Workaround:** none: Asteroids' few hundred sparks don't feel it
- **Cost:** wgrender-c measured 16,000 particles at 1.1 ms of CPU a frame against 0.1 on the GPU (6.2 against 0.6 on a phone)
- **Found by:** Rob's question during step 0's triage
- **Triage:** task: milestone 2, step 11, which carries libwgt's emitter core whole and retires the CPU one; a CPU mode for particles that react after birth waits in "Later" on its condition


### Asteroids: a playthrough raced its game's load

- **Where:** `games/asteroids/autopilot/playthrough.autopilot`, flown by the JS Asteroids in CI (0bb429e's run): `at 30 key tap enter`, then `at 31 expect asteroids.state == 1`
- **Missing:** a way for an autopilot to wait for the program: its frames are virtual time, but a load takes real time, so on CI's slower runner the scene hadn't loaded by frame 30, the title ignored Play, and the run failed; the Haxe game's playthrough had the same race and hadn't lost it yet
- **Workaround:** none: the run failed
- **Cost:** a CI failure on a commit that had passed locally; a flake in every playthrough that acts before its loads end
- **Found by:** CI
- **Triage:** fixed in milestone 2's step 1: `at <frame> wait <probe> <op> <number>` holds the autopilot's clock while the program's frames go on (BUILDING.md, "Autopilot files"); Asteroids publishes `asteroids.ready`, and its playthrough and smoke autopilots wait for it

Racer (milestone 2), from step 4: the game-developer session that wrote the drivable slice outside libwgf (`../libwgf-racer/FRICTION.md`, its log entries numbered as there; the game in `../libwgf-racer/game/`), 2026-10-07.

### Racer: the sketch calls what milestone 2 hasn't built yet

- **Where:** the sketches' `Vehicle`, `body`, box sensors, glTF and `AssetGroup`, `Environment`, shadow casting, render targets, custom shaders and effects, `Emitter3d`, `Ui.progress`
- **Missing:** each of them, as the brief expected; the mirror, the speed vignette, tire smoke, shadows, and the sky skipped; the car a prefab of generated meshes, the sky the clear color, the loading screen drawn text
- **Workaround:** Motion for the driving (`ArcadeDrive.hx`), sphere colliders for checkpoints
- **Cost:** none in time; the features skipped
- **Found by:** the racer's session (its #1)
- **Triage:** task: milestone 2's later steps, each where it already is: the vehicle, bodies, and sensors step 5; glTF step 6; shadows step 8; the environment step 9; asset groups and a progress bar step 10; 3D particles step 11; targets, effects, and shaders step 12

### Racer: the sketch's own slips

- **Where:** the sketches' `ChaseCamera.setFov(65)` (radians are wanted), `Laps.passed` (a lap counted on the first crossing), `ChaseCamera`'s spring on the camera's position (it trails a fast car by about 2v/ω), and `getRotation().y` as a heading
- **Missing:** nothing in libwgf: the sketch was wrong
- **Workaround:** degrees converted, the lap rule rewritten, the spring on the yaw only at a fixed distance, the heading kept by the game
- **Cost:** 15 minutes
- **Found by:** the racer's session (its #2)
- **Triage:** fine: HISTORY.md, "Milestone 2, step 4, the racer's friction": the sketch's bugs, recorded as the roadmap says, and the sketch left as it was written

### Racer: a simulated actor's drawn transform can't be read

- **Where:** the game's `Car.hx:33-60`, read by its chase camera
- **Missing:** the transform the ecs draws (between the last two ticks, blended by the tick fraction); a camera on `getWorldPosition` in the frame moves in tick steps while the car it follows moves smoothly
- **Workaround:** the car's transform kept as each tick begins, and blended in the frame by `Loop.getTickFraction()`, matching the ecs only because behaviors tick before the systems move the car
- **Cost:** 15 minutes, about 15 lines, on an ordering stated in three places
- **Found by:** the racer's session (its #3)
- **Triage:** fixed in step 4: `wgf_actor_get_drawn_position` and `wgf_actor_get_drawn_direction` read where an actor is drawn this frame; and `wgf_actor_get_world_position`, which read the drawn matrix (between the ticks in a frame, and in a tick a blend at the last frame's fraction), is now the simulation's everywhere, as ARCHITECTURE.md said the transform was

### Racer: `getRotation` past a half turn, and what Motion's spin does there

- **Where:** the game's `ArcadeDrive.hx:9-13, 89-90`
- **Missing:** a heading read back as it was set (a yaw past a quarter turn reads back as x=π, y=π−yaw, z=π, as `wgf_actor.h` warns), and the docs saying what a y spin does past it
- **Workaround:** the game keeps its heading and sets the rotation and the velocity each tick
- **Cost:** 5 minutes
- **Found by:** the racer's session (its #4)
- **Triage:** fixed in step 4, and a bug found by it: Motion's spin added to the angles read back, so a spin about y turned back at a quarter turn and never got past it; it now turns by each tick's share about the parent's axes, steadily past any half turn, with a test. A heading is read with `wgf_actor_get_world_direction(actor, 0, 0, -1)` (or the drawn one), with no angles; the angles themselves stay as `wgf_actor.h` says (a rotation, given back as some angles that make it)

### Racer: recording a lap needs a window and a person

- **Where:** `wgf autopilot --record`; the game's `tools/drive.py` (197 lines) and its `#if wgf_record` telemetry (`Main.hx:137-162`)
- **Missing:** a way to make a recording with no display and no hands: an agent's session, or CI
- **Workaround:** the record build under Xvfb, driven by a script that reads the game's logged telemetry, steers by pure pursuit, and types keys with xdotool
- **Cost:** 45 minutes, a telemetry block in the game
- **Found by:** the racer's session (its #5)
- **Triage:** task: ROADMAP's "Later", "AI players, an autopilot `set` command, and a supported way to record with no one there", when a game needs AI opponents or recording becomes a burden; until then the racer's `drive.py` stays the game's own tool: it steers by feedback, so a lap re-recorded after the driving changes (step 5's vehicle) is a re-run of it, not a person's time (HISTORY.md, "Milestone 2, step 4, the racer's friction")

### Racer: a recording counts frames from the start, but loading takes real time

- **Where:** the game's start (`Main.hx:118, 212`) and its `lap.autopilot`; BUILDING.md's "Recording one"
- **Missing:** a recording that waits for its load: the recorder writes the inputs by frame from the program's first, and a countdown begun when the scene loaded starts at a different frame natively, headless, and in a browser, so the keys land elsewhere in the race on replay; the sketch's lap has the same flaw
- **Workaround:** the race starts on the throttle, and the driver adds `at 1 wait racer.state >= 1`
- **Cost:** 20 minutes, one game state, a re-record
- **Found by:** the racer's session (its #6)
- **Triage:** fixed in step 4: every program's autopilot reads `core.loading` (the loads in flight) as a probe, and a recording made while the init's loads are in flight starts with `at 0 wait core.loading == 0`, counts its frames from their end, and writes what came during them at 0, so its inputs land at the same point after the load on any machine; BUILDING.md says so, and that a game whose play starts on its own at the load's end should start it on an input where it can, as the racer did

### Racer: the recorder records the machine's pads

- **Where:** the first recording's `at 0 pad 0 connect`, `pad 0 axis left_x 0.17`: a worn stick's drift, steering the car through a threshold of 0
- **Missing:** BUILDING.md saying a recording takes in every pad connected
- **Workaround:** a 0.2 threshold on the stick (`Main.hx:70`)
- **Cost:** 5 minutes
- **Found by:** the racer's session (its #7)
- **Triage:** fixed in step 4: BUILDING.md's "Recording one" says the pads connected are recorded, drift and all, and flown back in place of the real ones, and to give a stick's binding a threshold above its drift

### Racer: checkpoints as spheres trigger before their line

- **Where:** the game's checkpoints (`Main.hx:103`): a 7.5 m sphere across a 12 m track; the grid 8 m behind the line was inside it
- **Missing:** a box or plane trigger, which is what a gate is
- **Workaround:** the grid moved to 12 m; the line triggers about 8 m early, the same every lap
- **Cost:** 10 minutes, a re-record
- **Found by:** the racer's session (its #8)
- **Triage:** fixed in step 5: a body of type sensor (`wgf_body.h`), a box among its shapes, raising the ecs's trigger events to both; the ecs's colliders stay spheres

### Racer: no mesh from vertices; the track is 763 models

- **Where:** the game's `Track.hx:105-132`
- **Missing:** a mesh made from the game's own vertices and indices (meshes are the generated primitives; glTF is step 6), so a ribbon of road along a centerline is unit boxes and planes, seams between them, and a draw each
- **Workaround:** asphalt from overlapping coplanar planes of one tint and normal, curbs at alternating heights
- **Cost:** 20 minutes; 763 static models (the 140 trees' 280 included) with no batching; measured only in a software-GL browser (about 40 fps)
- **Found by:** the racer's session (its #9)
- **Triage:** task: milestone 2, step 6: a mesh from a program's own vertices and indices (`wgf_mesh_create`), which glTF's loader makes its meshes with, so a track or a road generated along a centerline is one mesh; the track itself becomes glTF in the same step

### Racer: `wgf serve` fails on an initialized instance `final`, Asteroids' `Ship` too

- **Where:** any instance field written `final x = <initializer>` (the game's `Car` and `ArcadeDrive`; libwgf's `games/asteroids/src/Ship.hx`, `final heading = new Vec3()`)
- **Missing:** a hot build that takes them: hotreload-hx's macro wrote each initializer into a method run on an object a reload carries over, `this.x = e`, which Haxe refuses for a final; every error pointed into `deps/hotreload-hx`, none at the game, and only `wgf serve` uses the macro, so check_cli's template game (no instance finals) never showed it
- **Workaround:** the fields made `var`
- **Cost:** 25 minutes to bisect; the documented first step failed on the sample game
- **Found by:** the racer's session (its #10)
- **Triage:** fixed in step 4: the macro sets a final's initializer with `Reflect.setField` (a change to the vendored copy, listed in `deps/hotreload-hx/VERSION` until it goes upstream), and check_cli's serve step builds a class with an initialized instance final

### Racer: a browser autopilot gets 120 s, and its FAIL gives no reason

- **Where:** `tools/wgf/game.py`'s `run_page(..., timeout=120)`; the CLI's `wgf: autopilot ...: FAIL`
- **Missing:** a time that fits the file (a 3-lap race, 3,704 frames, at a headless browser's 30 to 40 fps), a way to give one, and the reason printed ("nothing ended the run within 120 s" was kept but never shown)
- **Workaround:** the full race flown natively only; the browser flies one lap
- **Cost:** 15 minutes; the browser covers a lap, not the race
- **Found by:** the racer's session (its #11)
- **Triage:** fixed in step 4: a browser run gets two minutes, its last frame's at 3 frames a second, and 30 s for each `wait` (the lap: about 11 minutes, for CI's software drawing; HISTORY.md, "The browser autopilot's allowance"), `--timeout` gives it another, and every FAIL prints why under it (the errors and FAIL lines, or that the run said nothing of a verdict); headless the same, and `wgf export --autopilot` takes the same time

### Racer: `wgf --help` leaves out `autopilot --record` and `export --autopilot`

- **Where:** the top-level help
- **Missing:** the two options, which BUILDING.md and each command's own `--help` have
- **Workaround:** read the command's help
- **Cost:** none measured
- **Found by:** the racer's session (its #12)
- **Triage:** fixed in step 4: the help lists `--record`, `--screenshots`, `--timeout`, and `export --autopilot`

### Racer: wgf.json's `title` doesn't reach the page

- **Where:** the game's `web/index.html`, whose `<title>` `wgf new` wrote once from the name
- **Missing:** the page's title from wgf.json
- **Workaround:** both set by hand
- **Cost:** none measured
- **Found by:** the racer's session (its #12)
- **Triage:** fixed in step 4: a web build's page takes its `<title>` from wgf.json's title (the game's `web/index.html` left as it is), so a page's title follows wgf.json

### Racer: no defines from wgf.json

- **Where:** the game's telemetry build, hung on `wgf_record`, the record build's own define
- **Missing:** a way to give a game's build a `-D` of its own (a bot, telemetry, a cheat build)
- **Workaround:** the record build's define
- **Cost:** none measured
- **Found by:** the racer's session (its #12)
- **Triage:** fixed in step 4: wgf.json's `defines` (a list of names, or name=value) are given to every build of the game as `-D`, hot builds included

### Racer: one random generator

- **Where:** the game's tree placement (`Track.hx:182`)
- **Missing:** a second generator: placing with `wgf_random` and restoring its seed would replay its sequence rather than go on with it
- **Workaround:** a generator of the game's own, so an autopilot's seed stays the game's
- **Cost:** none measured
- **Found by:** the racer's session (its #12)
- **Triage:** task: milestone 2.5, "Random generators of a game's own" (a generator as a handle, seeded, beside `wgf_random`, so a world's layout can draw its own numbers and the autopilot's seed stays the game's); the game's own generator is fine until then

### Racer: the D-pad can't be the two sides of an axis action

- **Where:** the game's `steer` action
- **Missing:** a pad-button pair bound as -1 and +1, as `bindKeys` pairs two keys
- **Workaround:** none: D-pad steering skipped
- **Cost:** a control the game doesn't have
- **Found by:** the racer's session (its #12)
- **Triage:** fixed in step 4: `wgf_action_bind_pad_buttons(action, negative, positive)`, a pair of pad buttons as -1 and 1 across the pads, as `bind_keys` pairs two keys

### Racer: an autopilot's input timing, written off by a frame

- **Where:** the game's first pad autopilot
- **Missing:** an example of what "inputs at a frame are delivered before its ticks; expectations are checked after it" means for a line's frame (BUILDING.md says it exactly)
- **Workaround:** found by running it
- **Cost:** a run
- **Found by:** the racer's session (its #12)
- **Triage:** fixed in step 4: BUILDING.md shows what it means for a line's frame (`at 10 key down space` is seen by frame 10's ticks, so `at 10 expect` holds and `at 9` doesn't)

### Racer: what worked (seven notes)

- **Where:** the racer's session's #13
- **Missing:** nothing. A lap recorded on the debug desktop build replayed frame-exact headless, in a browser, and against both release exports, across hxcpp's and JS's float math; the recorder took synthetic and real input alike, its probe comments making expectations quick to write; the car and gates are prefabs of generated meshes, placed by `spawnPrefab`; collider triggers worked on a 3D stage with a layer and mask; actions read well (`getBindingText` made the prompt); EXPAND with `Presentation.getVisible` anchored the HUD in two lines; and it all compiled and ran first build, its web export 258 KB gzipped (77 calls in its trimmed host), both exports smoke-tested in 23 s
- **Workaround:** none
- **Cost:** none
- **Found by:** the racer's session
- **Triage:** fine: kept as they are, and kept so (a change that loses one of them is a regression)
