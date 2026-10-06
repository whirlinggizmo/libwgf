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
- **Triage:** task: milestone 2, step 2, the presentation mode, which the template then uses (fit by default)

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
- **Triage:** task: milestone 2, step 2 (the presentation mode, `bounds` taking the design area); the `Window.setSize` sentence fixed in step 0 (`wgf_window.h`)

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
- **Triage:** task: milestone 2, step 2 (anchors to the visible area, and `Draw.text`'s alignment)

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

