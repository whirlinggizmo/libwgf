# FRICTION: the racer slice on libwgf, as a game developer

The racer's drivable slice (`game/`), built on libwgf 0.1.0 as an SDK, from its public docs, headers, Haxe binding, `wgf` tool, template, and asteroids. 2026-10-07, about 4 hours.

Eight parts:
- **Part 1** (below): the slice, on arcade motion.
- **[Part 2](#part-2-the-car-on-physics-physics3d-jolt):** the car on physics3d's Jolt vehicle, the track a static body, the checkpoints sensors.
- **[Part 3](#part-3-the-track-as-a-gltf-file):** the track as a glTF file the game generates and loads, its nodes the collision.
- **[Part 4](#part-4-a-sun-that-casts-shadow-maps):** a sun that casts, set in the scene where it can be, and what it costs.
- **[Part 5](#part-5-a-sky-and-reflections-environments):** a sky and reflections, and gen_track.py the same bytes on every system.
- **[Part 6](#part-6-the-track-streamed-as-racing-kit-pieces):** the track as Kenney's Racing Kit pieces, streamed in sections behind a loading screen, with a manifest; and recovery from Rob's roll over a barrier.
- **[Part 7](#part-7-many-things-on-the-track-instancing-particles-sprites-and-text-in-3d):** trackside props, signs and a start gantry in sprites and text, and tire smoke.
- **[Part 8](#part-8-a-rear-view-mirror-and-a-speed-effect-render-targets-custom-shaders-screen-effects):** a rear-view mirror drawn into a render target, and a speed effect of the game's own shader; Part 7's two workarounds undone.

# Part 1: the slice, on arcade motion

## Summary

**Done:** a box car (a scene prefab of generated meshes) on a flat circuit built in code from a Catmull-Rom centerline (asphalt planes, curbs, gates, 140 trees), driven arcade-style by Motion (`ArcadeDrive.hx`, the one class a vehicle replaces), a yaw-sprung chase camera, 8 checkpoints in order as collider triggers, lap timing over 3 laps with a results screen and Race again, a drawn-text HUD, keyboard and pad through actions, EXPAND presentation. Four autopilots: `lap` (recorded with `wgf autopilot --record`: the start, all 9 checkpoint passes in order at their frames, lap 2 reached, the lap time in a window) **passes headless, in a browser, and against both release exports** (`wgf export --autopilot`). `pad` passes headless and in a browser. `smoke` is the export smoke run. `race` (3 laps, results, Race again) passes headless only. Web export: 258 KB gzipped.

**What mattered most:**

1. **Blocker, worked around: `wgf serve` can't build a game with an instance `final` field that has an initializer.** That includes libwgf's own asteroids (`Ship.hx`'s `final heading = new Vec3()`). The error points only into hotreload-hx's macro (a dep, `Builder.hx:565`) and never at the game's code. It took 25 minutes to bisect.
2. **There's no way to read a simulated actor's drawn (interpolated) transform**, so the sketch's "camera from the car's interpolated transform" means re-deriving the ecs's interpolation in game code.
3. **Recording a lap needs a window and a human.** Here there was neither. I drove the record build through Xvfb and xdotool from a script that reads telemetry the game logs (`game/tools/drive.py`). The recorder then did exactly its job, and that recording replayed frame-exact on every target: the best part of the session.
4. **A recording counts frames from 0, but loading takes real time.** The recorder writes no wait, so a game that starts when loading ends replays out of step. I redesigned the start to come from the throttle, and the docs don't warn about this.
5. **A browser autopilot gets 120 s of wall-clock time**, with no option to raise it. When it runs out, the CLI prints just `FAIL` with no reason. A full 3-lap race (3,700 frames, at the headless browser's 30 to 40 fps) can't be flown in a browser.
6. **Missing from the sketch, as the brief expected:** physics3d and the vehicle, glTF, shadows, the sky, render targets (mirror), custom shaders (speed vignette), and 3D particles (tire smoke). One gap I hadn't expected: **no mesh from vertices**, so the track is 763 separate static models.

Severity counts: 1 blocker (with a workaround), 5 workaround, 6 awkward, 5 docs gap, 7 good surprises. Details below, in the order I found them.

---

## Log

### 1. The sketch vs. what libwgf has (read before building)
- **What I tried:** to write the sketches' intent as it stands.
- **What I expected:** the brief says physics comes later. The rest of the sketches' calls I checked against the headers.
- **What happened:** these don't exist, and each one changed the slice:
  - `Vehicle`, a `vehicle` scene line, `body` (physics3d), sensors with `shape=box`. The car drives with Motion (`ArcadeDrive.hx`), and the checkpoints are sphere colliders (see 8).
  - glTF models (`track/*.glb`, `car/car.glb`), `AssetGroup` streaming. Everything is generated meshes, and the car is a prefab of `model cube=`/`cylinder=` lines (`assets/scenes/racer.scene`).
  - `Environment.create`, `stage.setEnvironment` and `setBackground`. The sky is the clear color.
  - `Light.setShadowCasting` and `setShadowDistance`. The light header says shadows are "milestone 2, step 8's". No shadows.
  - `Texture.createTarget`, `Render.beginTarget`, `stage.drawWith(camera)`. **The rear-view mirror is skipped.**
  - `Shader.create`, `Material.createCustom`, `Render.addEffect`. **The speed vignette is skipped.**
  - `Emitter3d` (`emitter2d` only). **Tire smoke is skipped.**
  - `Ui.progress` (the sketch knew). The loading screen is drawn text.
  - Known renames (the brief): `Action.bindKeys`, `bindPadAxis(axis, direction, threshold)`, `Debug.showFps(font, x, y, size, color)`.
- **Cost:** features skipped (mirror, vignette, smoke, shadows, sky). This was 0 minutes of work.
- **Severity:** docs gap, as expected. This is what milestone 2 still owes.

### 2. The sketch's own slips (sketch bugs, not libwgf: HISTORY candidates)
- `camera.setFov(65)`: the header says radians (clamped to 3.13), so 65 is a half-turn fish-eye. I wrote `65 * Math.PI / 180` (`ChaseCamera.hx:24`).
- `Laps.passed`: `if (next == 1 && index == 0 && current > 0)` counts a lap on the first crossing of the line if a tick has gone by since the start. I rewrote the rule: a lap ends at the line only after all the others (`Laps.hx`).
- `ChaseCamera`'s critically damped spring on the camera's *position* trails a moving target by about 2v/ω. At 40 m/s and the sketch's stiffness that is 28 m. I saw 13 m behind at stiffness 40 in a screenshot. Rewritten to spring the yaw only and keep the distance fixed (`ChaseCamera.hx`).
- `target.getRotation().y` as the heading: see 4.
- **Cost:** 15 minutes.
- **Severity:** awkward (the sketch's intent, not the API).

### 3. A simulated actor's drawn transform can't be read
- **What I tried:** the sketch's camera, "per frame, from the car's interpolated transform, so it is smooth at any frame rate".
- **What I expected:** a call for the transform as drawn this frame.
- **What happened:** ARCHITECTURE says "the transform the program sets and reads is the simulation's". Nothing gives the blend the ecs draws. A camera on `getWorldPosition` in the frame callback moves in tick steps while the car it follows is drawn smoothly: judder whenever frames and ticks differ.
- **Workaround:** `Car.hx:33-60`. In `onTick` I keep the position and heading as the tick begins. That matches the ecs's kept transform only because behaviors tick before the systems move the car, which I learned from Behavior.hx's comment and wgf_world.h's ordering. In `onFrame` I blend toward the current transform with `Loop.getTickFraction()`. The camera reads `Car.drawn`.
- **Cost:** 15 minutes, about 15 lines. It relies on an ordering the docs state, but in three different places.
- **Severity:** workaround. `wgf_actor_get_drawn_position` (or rotation), or a flag on the getters, would remove it.

### 4. `getRotation` past a half turn, and Motion's spin
- **What I tried:** the sketch's `getRotation().y` for the car's heading.
- **What happened:** wgf_actor.h warns, correctly, that the angles read back "may not be the ones given (past a half turn)". A yaw past ±90° comes back as x=π, y=π−yaw, z=π. Motion's spin is "added to its rotation's angles", so I couldn't tell from the docs whether a y spin past ±90° turns the right way. I didn't use it.
- **Workaround:** `ArcadeDrive` keeps the heading itself and sets `setRotation(0, heading, 0)` and the Motion velocity each tick (`ArcadeDrive.hx:9-13, 89-90`).
- **Cost:** 5 minutes.
- **Severity:** awkward. A yaw-only getter, or a heading on Motion, would fit a vehicle game. The docs could also say what spin does past a half turn.

### 5. Recording a lap with no hands and no display
- **What I tried:** `wgf autopilot --record autopilot/lap.autopilot`, as the brief asks.
- **What I expected:** BUILDING: "run in a window ... played by hand".
- **What happened:** this machine has no `$DISPLAY`, and the session has no hands. Nothing in libwgf makes a recording any other way. That is fair: a recording is meant to come from a person.
- **Workaround:** `game/tools/drive.py` (197 lines). Under `xvfb-run` it starts `wgf autopilot --record`, reads telemetry the record build logs (`Main.hx:137-162`, `#if wgf_record`: the centerline once, then the car each frame), steers by pure pursuit, and types arrow keys into the window with `xdotool`. The recorder writes whatever reaches the program, so the file is a real recording. The tool then edits it (see 6) and adds the expectations from the frames where the driver saw each checkpoint pass.
- **Cost:** 45 minutes, plus a game-side telemetry block.
- **Severity:** workaround (for this environment). The good part is in 13.

### 6. A recording starts at frame 0, but loading takes real time
- **What I tried:** flying the first recording back.
- **What I expected:** the same race. BUILDING's "Recording one" says it replays the same.
- **What happened:** the recorder writes `seed`, the inputs, and `end`, with no `wait`. The inputs are frame-numbered from the program's first frame. The countdown started when the scene finished loading, and that takes real time: different frames natively, headless, and in a browser that fetches. So the recorded keys would land at a different point in the race on replay. The sketch's lap has the same flaw (`at 240 expect racer.state == 2`).
- **Workaround:** the race now starts on the throttle (`Main.hx:212`, state READY; `Main.hx:118`), so the inputs themselves make the start. `drive.py` adds `at 1 wait racer.state >= 1`, so a slower load shifts every later line together. It also takes out the driver's Escape: a recorded quit ends the replay before its `end`, which is an error.
- **Cost:** 20 minutes, one game state, one re-record.
- **Severity:** docs gap. BUILDING's recording section could say "a recording counts frames from the start: add a `wait` for your load, and start play on an input".

### 7. The recorder records the machine's own gamepad
- **What happened:** the first recording began `at 0 pad 0 connect`, `at 0 pad 0 axis left_x 0.17`. This machine has an Xbox 360 pad with a drifting stick. The recording is faithful, and the replay gets the same drift, so it is still the same run. But with the steer action bound to `LEFT_X` with threshold 0, the drift was steering the car at 0.17.
- **Workaround:** a 0.2 threshold on the stick (`Main.hx:70`).
- **Cost:** 5 minutes.
- **Severity:** awkward. It's worth a line in BUILDING ("the pads connected while recording are recorded too"). The round dead zone in wgf_gamepad.h didn't cover a worn stick.

### 8. Checkpoints as spheres: the grid touched the line
- **What I tried:** a checkpoint as a collider sphere across the track (radius 7.5 over a 12 m track; the car's 1.2), since there is no box sensor.
- **What happened:** the grid 8 m behind the line was inside the line's sphere (7.5 + 1.2 = 8.7). The car "passed" checkpoint 0 while standing on the grid. The autopilot caught it: one expectation off by two frames, the rest exact.
- **Workaround:** the grid moved to 12 m (`Main.hx:103`). A sphere reaches past the line ahead as well as behind, so the line triggers about 8 m early. Lap times are consistent, just not timed at the painted line.
- **Cost:** 10 minutes, one re-record.
- **Severity:** workaround. A box (or plane) trigger is what a gate is. physics3d's sensors are the sketch's answer.

### 9. No mesh from vertices: the track is 763 models
- **What I tried:** "a flat track built from generated meshes".
- **What I expected:** to build one ribbon mesh along the centerline.
- **What happened:** meshes are the 7 primitives only (wgf_mesh.h; glTF is step 6). The track is one scaled unit cube per 2 m sample, and that showed seams: each box's lit sides poke through its neighbour. Alternating heights made dark dashes.
- **Workaround:** asphalt from scaled unit planes overlapping coplanar. One tint and one normal, so where they fight they fight to the same color (`Track.hx:105, 119-121`). Curbs alternate two heights so a corner's inside overlap doesn't fight (`Track.hx:132`). That's 763 static models, the 140 trees' 280 included: a draw each, no batching.
- **Cost:** 20 minutes. The headless browser runs about 40 fps here (software GL). I didn't measure on a GPU.
- **Severity:** workaround. A `wgf_mesh_create(vertices, indices)` call, or merging static models, is what a generated track wants.

### 10. `wgf serve` fails on instance `final` fields with initializers (asteroids too)
- **What I tried:** `wgf serve`, the documented dev loop.
- **What I expected:** the game in a browser, reloading on save.
- **What happened:** `wgf: the build failed:` and five copies of `deps/hotreload-hx/src/hotreload/Builder.hx:565: characters 67-75 : This expression cannot be accessed for writing`. The errors carry no position in the game's code. I reproduced the hot build with Haxe by hand (the command from `tools/wgf/game.py`) and bisected. The cause is every instance field written `final x = <initializer>` (4 in `Car`, 1 in `ArcadeDrive`). Static finals, finals assigned in a constructor, and `(default, null)` properties are fine. **A scratch copy of `games/asteroids` fails `wgf serve` the same way** (`Ship`'s `final heading = new Vec3()`). `wgf build`, `autopilot`, and `export` don't use the macro, so they all worked. Nothing but serve shows it.
- **Workaround:** those fields are `var` (`Car.hx:15-26`, `ArcadeDrive.hx:93`). After that, serve built in 273 ms on a save. I checked the rebuild from serve's log, not by watching a page.
- **Cost:** 25 minutes.
- **Severity:** **blocker**, worked around. Serve is the documented first step, and the sample game fails it. `check_cli.py` only edits the template, which has no instance finals.

### 11. `wgf autopilot --web`: a 120 s cap, and a FAIL with no reason
- **What I tried:** the full race (3 laps, results, Race again; 3,704 frames) with `--web`.
- **What happened:** `wgf: autopilot ../scratch/race.autopilot: FAIL` after 2 minutes, with nothing before it but the page's start-up lines: no expectation failed, no error shown. The cause is in `tools/wgf/game.py`: `run_page(..., timeout=120)`. It appends "[ERROR] wgf: nothing ended the run within 120 s", but the CLI doesn't print it. The headless browser here draws 30 to 40 frames a second (300 frames in 13 s, start-up included), so about 3,600 frames is the ceiling. The 1-lap autopilot (1,456 frames) takes 57 s.
- **Workaround:** `race.autopilot` is flown natively only (its header says why). `lap.autopilot` is the browser check.
- **Cost:** 15 minutes. The browser check covers one lap, not the race.
- **Severity:** workaround. A `--timeout` option, a timeout scaled to the file's last frame, and printing the reason would each fix it.

### 12. Smaller things
- **`wgf --help` leaves out `autopilot --record` and `export --autopilot`.** Both are in BUILDING and in each subcommand's own `--help`. Docs gap.
- **wgf.json's `title` doesn't reach the page.** The page's `<title>` is the game's name, written once into `web/index.html`. I set both by hand. Docs gap.
- **No defines from wgf.json.** There's no way to pass a `-D` to a game's build (a bot or telemetry build, say). I hung the telemetry on `wgf_record`, the record build's own define. Awkward.
- **One random generator.** Placing the trees with `wgf_random` and restoring its seed would replay the sequence, not continue it. The trees use a generator of their own (`Track.hx:182`), so an autopilot's seed stays the game's. Awkward.
- **D-pad steering.** A pad button binds as +1 only, so the D-pad can't be the two sides of `steer` the way `bindKeys` pairs two keys. It would need two actions and code. I skipped it. Awkward.
- **Input timing in an autopilot.** "Inputs at a frame are delivered before its ticks; expectations are checked after it." BUILDING says it exactly, and I still wrote my first pad test off by one frame. A one-line example of the consequence would help. Docs gap.

### 13. Good surprises
- **Determinism, everywhere.** A lap recorded on the debug desktop build replayed frame-exact: every checkpoint at the recorded frame, ±0, headless (hxcpp, debug), in a browser (JS on wasm), and against both **release** exports (`wgf export --autopilot autopilot/lap.autopilot`: web and desktop PASS). That held across hxcpp and JS float math, with a hand-rolled steering spring in the game.
- **The recorder is honest.** It took XTEST key events from xdotool like any keyboard and captured the real pad. The probes' last values as comments made writing expectations quick.
- **Prefabs of generated meshes.** The whole car, wheels and lamps included, and the checkpoint gates are data (`racer.scene`). No model code, and `spawnPrefab(name, parent, x, y, z, angle)` placed and turned them in one call.
- **Collider triggers just worked on a 3D stage.** One `onTriggerEnter` per checkpoint, a layer and mask, nothing else.
- **Actions read well.** `Action.getBindingText("throttle", 0)` made the "press Up to start" line for free, and pad trigger and stick bindings with threshold and direction needed no game code.
- **EXPAND with `Presentation.getVisible`.** The HUD anchored to the page's corners at any size in two lines, and the 3D view filled the page.
- **It all compiled and ran on the first build.** The web export is 258 KB gzipped (77 calls in its trimmed host), and `wgf export` smoke-tested both builds in 23 s.

## Files (as of part 1)
- `game/src/`: `Main.hx` (states, controls, HUD), `Track.hx` (centerline and generated world), `ArcadeDrive.hx` (the driving; swap for a vehicle; replaced by `Drive.hx` in part 2), `Car.hx` (input to intent, drawn transform), `ChaseCamera.hx`, `Laps.hx` (checkpoints, timing, probes).
- `game/assets/scenes/racer.scene`: the car and checkpoint prefabs.
- `game/autopilot/`: `lap` (recorded; headless, web, exports), `pad`, `smoke`, and `race` (native only).
- `game/tools/drive.py`: the keyboard driver for `--record` under Xvfb, and the expectation writer.

---

# Part 2: the car on physics (physics3d, Jolt)

2026-10-07, about 3 hours, against libwgf 0.1.0 at `5309723` (physics3d's public side in `42fc45e`). Read as a user would: `wgf_physics.h`, `wgf_body.h`, `wgf_vehicle.h`, ARCHITECTURE's physics3d section, BUILDING's scene lines, and `examples/c/physics3d-vehicle`.

## Summary

**Done:** the car is a dynamic box body with a Jolt wheeled vehicle (a `body` and a `vehicle` line in the car prefab, with wheels as axle actors under it). `Drive.hx` replaces `ArcadeDrive.hx` and only sets the vehicle's input. The asphalt, curbs, and line are one static mesh body (500-odd scaled planes under one actor) on a static ground box. The checkpoints are box sensors at the line, with solid posts. It drives well:
- no rollovers or spins;
- about 1.2 g of grip at 18 m/s;
- 0 to 100 km/h in about 4 s, through 4 or 5 gears to about 35 m/s on the long straight;
- grass that slows the car but lets it back.

I re-recorded the lap with `tools/drive.py` (37.75 s from a standing start, 33.15 s flying). `lap.autopilot` **passes headless, in a browser, and against both release exports**. So do `pad`, `smoke`, and `race` (3 laps and the results); `race` also passes in a browser now, in 4.9 minutes. The web export grew to 485.6 KB gzipped (Jolt), so the game's budget is now 550 KB.

**What mattered most:**
1. **Good surprise: five of part 1's frictions were already fixed** (P1). Car.hx's hand-rolled interpolation is gone.
2. **Workaround: the car rolled onto its roof.** A box body's center of mass is its center, and there's no offset, anti-roll bar, or inertia setting to lower it. I fixed it with geometry instead (P3).
3. **Docs gap: `wgf_vehicle_get_wheel_slip` reads 1/60 on a free-rolling wheel and exactly 1 on a driven one under throttle**, too coarse for traction control. I dropped traction control (P4).
4. **Workaround: a low-friction ground is ice, not grass.** Friction takes grip but not speed, so the car couldn't steer back onto the road. The grass drag is game code (P6).
5. **Awkward: most of the time went to tuning**, as expected: the driver aids (P5), and teaching drive.py to drive a car that has inertia (P14).

Severity counts: 0 blockers, 2 workaround, 5 awkward, 3 docs gap, 5 good surprises.

## Log

### P1. Part 1's frictions, fixed upstream
- **What happened:** five of part 1's frictions were already fixed:
  - **Drawn transform (#3):** `wgf_actor_get_drawn_position` and `wgf_actor_get_drawn_direction`, and `get_world_direction`. Car.hx's kept-and-blended transform is gone (`Car.hx:39-43`), and Drive reads the heading from `getWorldDirection(0, 0, 1)` instead of keeping its own (part 1's #4).
  - **Loading wait (#6):** the recorder writes `at 0 wait core.loading == 0` itself.
  - **Browser timeout (#11):** `wgf autopilot --timeout`, whose browser default grows with the file's last frame. The 6,600-frame race flew in a browser.
  - **`wgf serve` and instance finals (#10):** fixed. Instance `final` fields with initializers are back in Car and Drive, and serve built in 287 ms on an edit.
  - **CLI help (#12):** `wgf --help` lists `--record`.
- **Cost:** none; I deleted about 20 lines.
- **Severity:** good surprise.

### P2. The sketch's physics, nearly as written
- **What I tried:** the sketch's `body type=dynamic mass=1200 shape=convex` and `vehicle engine_torque=420 max_rpm=7000 gears=... differential=rear wheels=... radius= width= suspension= stiffness= grip=` lines.
- **What happened:** the sketch's car prefab maps onto the real keys almost one for one: `drive=rear` for `differential=rear`, and one `vehicle` line where the sketch had two. The keys are listed in a single long sentence in BUILDING ("`body` and `vehicle` are physics'...") rather than in the kinds table. The C example was the clearest reference.
- **Two things the sketch didn't show:**
  - **A wheel actor's transform belongs to the vehicle.** The vehicle sets each wheel's transform every tick. A cylinder model needs an axle actor above it carrying the 90° turn; put on the wheel itself, the turn is overwritten. wgf_vehicle.h says so ("a model under it with its axle along x shows the wheel"), and the example does it.
  - **Physics must start before the scene loads.** `Physics.setGravity` before `Scene.create`, or the body lines are refused. wgf_physics.h says so.
- **Sketch items still out of reach:** `Vehicle.getWheelSlip` for smoke works, but there's still no emitter3d, and a `convex` body would have taken in the wheels' cylinders, so it's a box.
- **Cost:** 20 minutes.
- **Severity:** good surprise (with a docs gap: the vehicle keys deserve the table).

### P3. Rollover: no center of mass to lower
- **What I tried:** the car as the example builds it: a 1.8 × 0.6 × 4.0 box body, wheels attached 0.25 m below its center, then grip 1.7 for sharper cornering.
- **What happened:** in the first fast corner the car went onto its roof, and lay there (screenshot: wheels up, heading frozen, speed bleeding off with throttle held). A box's center of mass is its middle, about 0.6 m up on a 1.9 m track: a static rollover threshold near 1.6 g, below grip 1.7. wgf_body.h has no center-of-mass offset, inertia, or anti-roll setting, and wgf_vehicle.h has no anti-roll bars. (Jolt has all three.)
- **Workaround:** geometry. The body box is flat (0.4 m tall) and wheels attach just below its middle (−0.05), on a 2.0 m track, with grip 1.5 (`racer.scene:10-14`, wheels at `:36-63`). The center of mass is now about 0.4 m up, and the visual body model is drawn taller than the physics box.
- **Cost:** 30 minutes (the trace, the screenshot, and three tries).
- **Severity:** workaround. A `com=x,y,z` offset on `body`, or anti-roll stiffness on `vehicle`, would let the shape be the car's.

### P4. Wheel slip: 1/60 rolling, 1 driving
- **What I tried:** traction control, easing the throttle while the rear tires slide (`getWheelSlip(2)`, `(3)` above 0.2), since 650 Nm spun the rear wheels and spun the car.
- **What I expected:** wgf_vehicle.h: "0 gripping, 1 sliding: tire smoke above about 0.3".
- **What happened:** probed per wheel on a straight launch:
  - **A free-rolling front wheel reads 0.01667 (1/60, the tick) at every speed**, from 0.2 m/s to 35, never 0.
  - **The driven rear wheels read exactly 1 at full throttle through first and second gear**, up to 21 m/s, even while the car accelerated at 4 to 8 m/s². So 1 is either real wheelspin that still pulls (possible at 420 Nm through 3.2 × the differential) or a clamp.
  - The readings flip between 1 and 0.0167 from tick to tick.
- **What I couldn't tell:** without the source I can't tell which. As a signal it's binary, so traction control on it held the car in first gear (16.8 m/s at frame 600 against 27.6 without).
- **Workaround:** no traction control. Torque back to 550 Nm (420 was slow; 650 spun), and steering that narrows with speed (P5) keeps it pointed.
- **Cost:** 25 minutes.
- **Severity:** docs gap (what slip measures, and why a rolling wheel isn't 0), possibly a bug.

### P5. A vehicle on keys needs driver aids
- **What happened:** with full lock on a key (Car eases it in over 0.25 s), a 650 Nm rear-drive car spun at speed: the heading swung left with Right held, at frame 821 of the first trace. With 0.5 rad of steering at any speed, a key is a yank.
- **Workaround:** steering that narrows with speed, in Drive (`Drive.hx:16, 57-61`). Full lock below 19 m/s, then lock ∝ 1/speed², about what the tires hold. Measured: 1.2 g at 18 m/s, and 0.9 g at 30 m/s with the lock at 16 m/s (19 now gives more). The car settles at once after a step steer (an open-loop probe at an earlier lock: yaw rate back to 0 within 0.3 s, no overshoot).
- **Cost:** 20 minutes.
- **Severity:** awkward (expected: arcade feel is the game's). A `steering` that took a speed curve would save every keyboard racer the same lines.

### P6. Grass: friction is grip, not drag
- **What I tried:** the grass as a slippery ground: the ground box at friction 0.45 and the road at 1.0.
- **What happened:** off the road at 26 m/s, the car slid along the grass at speed, Left held all the way, and never came back. Low friction took away the grip to turn but not the speed. There's no rolling resistance per surface, and changing a body's settings remakes it "at rest" at the next tick (wgf_body.h), so damping can't be a per-surface drag. I didn't try it.
- **Workaround:** grass friction 0.7, and a game-side drag in Drive while on grass above 8 m/s: half the throttle and a 0.12 brake (`Drive.hx:51-56`). It is the arcade model's grass, on the vehicle's input.
- **Cost:** 15 minutes.
- **Severity:** workaround.

### P7. The road as a static mesh of 500-odd scaled planes: it just worked
- **What I tried:** every asphalt, curb, and line model under one plain actor with `body type=static shape=mesh`, on a static 800 × 1 × 800 box under the grass (`Track.hx:117, 157`).
- **What I expected:** to be unsure. wgf_body.h says "A body's shape isn't scaled with its actor", and every plane is a 1 × 1 mesh scaled to 12 × 2.6.
- **What happened:** the triangles came out where the models are drawn, scale included (the debug view, B, outlines each plane on the road). The car sits at y = 0.59 and rolls over the curbs' 10 to 12 cm.
- **Notes:** the debug view is depth-tested, so its lines fight with the coplanar asphalt and come out dashed. A docs line would help: "a mesh or convex body reads each model's transform, scale included".
- **Cost:** 0.
- **Severity:** good surprise (and a small docs gap).

### P8. Sensors at the line, centered on their actor
- **What happened:** a `body type=sensor shape=box size=15,4,1` is the gate the spheres of part 1 couldn't be (#8): the line triggers at the line now.
- **Awkward:** the shape is centered on its actor with no offset, so the checkpoint spawns at y = 2 to sit on the road (`Main.hx:99`), and its posts and banner are offset by −2 in the scene. The posts are static box bodies under the sensor's actor. Bodies under a body worked, each in the stage's space.
- **Cost:** 5 minutes.
- **Severity:** awkward.

### P9. `wgf screenshot --autopilot` stops at the first failed expectation
- **What I tried:** a screenshot at frame 860 of a recording whose drive.py expectations partly failed (the car had rolled, so later checkpoints never came).
- **What happened:** `wgf: no screenshot: the page never reached frame 860`. The page did reach it; the same file without its `expect` lines screenshots fine. The browser run ends at the first `wgf_autopilot: FAIL ...` line, since `run_page`'s `until` matches the prefix of a failed expectation as well as the run's result. The message names the wrong cause.
- **Workaround:** strip `expect` lines before screenshotting a broken run.
- **Cost:** 15 minutes.
- **Severity:** awkward.

### P10. The template's web budget doesn't fit a physics game
- **What happened:** `wgf export`: `FAIL: 485.6 KB is over the budget of 400 KB`. The host wasm went from 219 to 445 KB. benchmarks.md had warned (225 KB gzip for Jolt and the layer).
- **Workaround:** `web_budget_kb` 550 in wgf.json.
- **Cost:** 2 minutes.
- **Severity:** docs gap (the template README could say what physics costs).

### P11. Determinism held with Jolt
- **What happened:** the recording made on the debug desktop build replayed with every checkpoint on its recorded frame, ±0, on:
  - hxcpp headless;
  - wasm in a browser;
  - the release web export;
  - the release desktop export.
- **What that covers:** Jolt's steps, the vehicle's gearbox, and a game-side controller in the loop, across C++ and wasm. So did 6,600 frames of 3 laps in a browser.
- **Severity:** good surprise: what wgf_physics.h promises.

### P12. Smaller things
- **The vehicle's steer sign matches the actions.** −1 is full left in both, so `Action.getAxis("steer")` goes straight in. Good.
- **`Vehicle.reset` after `setTransform` put the car back on the grid at rest.** Restart and Race again needed no settling time. Good.
- **Units:** `stiffness` is in Hz and `suspension` is travel in metres. Both are clear in the header, but the scene keys don't say so. Docs gap, minor.

### P13. Settings take effect "made again, at rest"
- **What happened:** wgf_body.h and wgf_vehicle.h both say changed settings remake the body or vehicle at rest at the next tick. That rules out live tuning (grip on a surface, a wing's downforce, damping as drag) from game code during a race. I designed around it and never tried, so this is unmeasured.
- **Severity:** awkward.

### P14. Tuning, and the driver tool
- **What happened:** most of the part's time. My `tools/drive.py` (not libwgf) needed:
  - a corner-speed model from lateral grip instead of the arcade turn rate;
  - a rate controller (pure pursuit asks for a turn rate, and the keys push the car's measured rate toward it), because bang-bang on the heading error made a car with inertia fishtail;
  - a whole-track search when far off;
  - a trace (`RACER_TRACE`), which I plotted over the track to see where it was slow.
- **Result:** with those, the same tool records a clean physics lap on the asphalt the whole way, with no game changes for it.
- **Cost:** about 70 minutes, including the car's tuning (torque 420 → 650 → 550, grip 1.3 → 1.7 → 1.5, lock speed 13 → 19, grass 0.45 → 0.7).
- **Severity:** awkward (expected).

## Files (part 2)
- `game/src/Drive.hx`: the vehicle's input and the driver aids (replaced `ArcadeDrive.hx`). `Car.hx` reads the drawn transform from libwgf now. `Track.hx`: the road's static mesh body and the ground box. `Main.hx`: physics started, the sensors placed, B for the debug view.
- `game/assets/scenes/racer.scene`: the car's `body` and `vehicle`, the checkpoint's sensor and solid posts.
- `game/autopilot/`: `lap` (re-recorded on physics), `race` (re-recorded, 3 laps), `pad` and `smoke` (retuned).
- `game/tools/drive.py`: the rate controller, grip-limited corners, `RACER_TRACE`, `RACER_LAPS`, and `RACER_GIVE_UP`.

---

# Part 3: the track as a glTF file

2026-10-09, about 2 hours, against libwgf at `961e09b` (glTF in `6b282a7`). Read as a user would: `wgf_mesh.h` (`wgf_mesh_create`, and the new `wgf_mesh_create_triangles`), `wgf_model.h`, ARCHITECTURE's mesh, model, and glTF paragraphs, BUILDING's scene keys (`model path=`, and the body note that a mesh body reads glTF nodes, scale included), and `measure_frames.py --help` for the benchmark autopilot.

## Summary

**Done:**
- **The generator:** `game/tools/gen_track.py` (standard library only) writes `assets/track/track.glb` from the centerline, the same bytes every time (`--check` fails when stale). It has named nodes: `ground`, `road` (with `curbs` and `line` under it), `barriers`, and `gates` (`gate_0` to `gate_7`, each with `post_l`, `post_r`, `banner`). It also writes `src/TrackData.hx`, the centerline itself, which the game drives by.
- **In the game:** `Track.hx` loads the file with `Mesh.create`. Once it is READY, each surface node gets a static mesh body of its own (ground 0.7, road with its curbs and line 1.0, barriers and gates 0.3), and each gate node a checkpoint sensor. The sensor uses part 2's new `offset=` key, so it is no longer spawned 2 m up.
- **New on the track:** barriers 15 m out on both sides. They are new to the track and are real walls.
- **Re-recorded:** the lap (37.03 s standing), the race (33.07 s best), and a new `bench.autopilot` (the lap's first 600 frames, cut by drive.py as it records).
- **Results:** lap, bench, pad, race, and smoke all **pass headless and in a browser**, and the lap passes against both release exports. The web export is 524.1 KB gzipped against part 2's 485.6: glTF's +28 KB of wasm, and +11 KB of racer.js for the centerline's literals. The file is 458 KB raw, 88 KB gzipped, and isn't in that count.

**What mattered most:**
1. **Good surprise: loading a glTF and making collision from its nodes was a few lines and worked the first time** (G1). Nodes are found by path, a mesh body takes a node's subtree, and a sensor under a node works.
2. **Docs gap: the file can carry no game data.** There's no glTF `extras`, so the centerline is written twice by one tool, as triangles and as Haxe literals (G3).
3. **Awkward: a scene can't put a body on a node of a glTF file.** Per-surface bodies are code that polls for the file's nodes after load (G2).
4. **Awkward: `wgf dump` gets the file's nodes wrong once they have components.** The bodied nodes come out as top-level `model` actors with no mesh, and the 8 sensors under the gates are left out entirely (G4).
5. **A smoother road made a faster car.** drive.py's braking assumption no longer held, and the first recording went wide and missed a gate (G5).

Severity counts: 0 blockers, 0 workaround, 4 awkward, 2 docs gap, 3 good surprises.

## Log

### G1. glTF in, collision out: it just worked
- **What I tried:** `Mesh.create("track/track.glb")`, a `Model` of it on the stage. Then, once `Resource.getStatus(mesh) == READY`, `root.find("road")`, `addComponent(BODY)`, `setShape(MESH)`, for each surface node (`Track.hx:53, 64-87`). The checkpoints are `scene.spawnPrefab("checkpoint", Track.gate(i))`, under `gates/gate_i` (`Main.hx:98`).
- **What happened:** all of it, first build:
  - the nodes named and placed as written;
  - each mesh body built from its node's subtree (the road's includes its curbs and line, the gates' every post and banner);
  - the car on the road at the same height as before (0.586 m);
  - the sensors triggering where they hang under nodes the file made.
- **Linking:** glTF linked for hxcpp (desktop, record build, and export) and for the trimmed web host without my doing anything. The call is in the code, not a scene.
- **Draw count:** the stage draws 280 tree models and 15 file nodes, down from 763 static models.
- **Cost:** 30 minutes, the generator included.
- **Severity:** good surprise.

### G2. Bodies on a file's nodes are code, not scene
- **What I tried:** to say it in `racer.scene`, as the car's physics is.
- **What happened:** a scene's `model path="track/track.glb"` loads the file, but its nodes appear only once it loads, and a scene block can't name one (`actor track/road` would be a new actor, not the file's node). One `body type=static shape=mesh` on the root would take the whole file as one body, at one friction: grass, asphalt, and walls alike.
- **Workaround:** `Track.ready()` (`Track.hx:64-76`) polls READY and `find("road")` each tick of loading, then adds the four bodies. Main waits on it before spawning the car.
- **Cost:** 15 lines.
- **Severity:** awkward. A scene line that addresses a file's node (`node road` under a `model path=` block, say), or per-material friction from the file, would keep the track's physics in data.

### G3. No game data in the file
- **What I tried:** to make the glb the one source of the track: the gates are nodes, but the centerline (what the grid, the grass test, the HUD's checkpoint, and drive.py use) isn't geometry.
- **What happened:** `wgf_mesh_create` reads meshes, materials, nodes, and lights. Nothing reads glTF `extras` or custom properties, and a node's numbers can only be had by making it an actor. 371 samples as 371 empty nodes would be 371 actors.
- **Workaround:** the tool writes `src/TrackData.hx` beside the file, from the same centerline in the same run, and `--check` covers both. Track.hx's spline code moved into the tool (`gen_track.py`'s `centerline`).
- **Cost:** 10 minutes, plus 11 KB of racer.js.
- **Severity:** docs gap (what a glTF file can't carry) and awkward. A node's `extras` as text (`wgf_actor_get_extras`), or an accessor read back, would let a track carry its own racing line.

### G4. `wgf dump` and a file's nodes with components
- **What I tried:** `wgf dump` at frame 30, to check the tree.
- **What happened:** ARCHITECTURE: "the nodes are marked as the file's, so a world dump writes the root's path alone". But the root (no component) isn't written at all, and the four nodes I gave bodies are written as top-level actors `ground`, `road`, `barriers`, `gates` with `model tint=#FFFFFFFF` and no mesh or path. Loaded again, that would make bodies with no triangles. The 8 checkpoint actors (a sensor body and a Checkpoint behavior each, under `gates/gate_i`) aren't in the dump at all; the car is.
- **Cost:** 10 minutes. The game is unaffected, since dump is a debugging tool.
- **Severity:** awkward (a dump that doesn't load back as the same world, against the docs).

### G5. The surface changed the car
- **What happened:** the same car on the one-ribbon road reached 39.6 m/s on the long straight, against 35.3 m/s on part 2's 371 overlapping planes. The planes' seams cost something; the ribbon doesn't. drive.py's braking model (7 m/s²) then came into turn 1 too hot, ran wide on the grass, and passed beside gate 2, so the rest of the lap was out of order.
- **Workaround:** the driver assumes 5.5 m/s² of braking now. The barriers (new, 15 m out) catch a car that runs wide; the game didn't change.
- **Cost:** 10 minutes, one bad recording.
- **Severity:** not libwgf's: the expected effect of better geometry.

### G6. A `Model` handle can't start at 0
- **What happened:** `public static var root:Model = 0;` is a compile error: "Int should be wgf.Model". `Mesh` takes 0, as `Actor` does, but a section type over a kind (`Model`, `Camera3d`, `Body` ...) is `from Actor` only, and Haxe doesn't chain implicit casts.
- **Workaround:** `root:Actor = 0`, used as a Model (`Track.hx:24`).
- **Cost:** 2 minutes.
- **Severity:** awkward (a `from Int` on each section type would fix it).

### G7. The benchmark autopilot
- **What happened:** the brief named one, and the only docs that say what it is are `measure_frames.py --help` ("the game's autopilot/bench.autopilot, else its playthrough (wgf.json's "playthrough")") and benchmarks.md's row. The template and BUILDING don't mention either name.
- **I missed one already here.** This repo already had one, committed on 2026-10-08 (`fc620eb`: the lap's first 600 frames, with a header on why 600). I only found it after regenerating over it.
- **What I did:** the lap changed, so it had to be cut again. drive.py now writes it from each lap it records, with that header kept (`--bench FILE` cuts it again). The header's "within 15% of the busiest stretch" was measured on part 2's track, not re-measured on this one. wgf.json now names `lap.autopilot` as the playthrough.
- **Cost:** 15 minutes.
- **Severity:** docs gap.

### G8. Smaller things
- **Writing a glb by hand was easy.** The one judgment call was winding: wgf_mesh_create_triangles' header says counterclockwise from the front, and glTF says the same. gen_track.py orders each quad by its normal so it can't get it wrong. Unsigned 32-bit indices, flat normals, and linear `baseColorFactor` all loaded as glTF says.
- **The asset isn't counted.** The 458 KB glb (88 KB gzipped) is outside the web budget, which counts only the program. That's fair, but it's where the track's weight went.
- **Part 2's frictions, answered:** `offset=` (used for the sensors), `mass_offset=`, and `anti_roll=`. I didn't change the car (the brief keeps it a box), so its low-box workaround stays for now.

## Files (part 3)
- `game/tools/gen_track.py`: the track's glTF and `src/TrackData.hx`, from the centerline.
- `game/assets/track/track.glb` and `game/src/TrackData.hx`: generated, committed.
- `game/src/Track.hx`: loads the file, gives its nodes bodies, finds the gates; the trees are still made here, now 18 m clear of the centerline, outside the barriers.
- `game/assets/scenes/racer.scene`: the checkpoint is a sensor with `offset=0,2,0`; its gate is the file's.
- `game/autopilot/`: `lap`, `race` re-recorded; `bench` new; wgf.json's `playthrough`.
- `game/tools/drive.py`: braking 5.5 m/s², and `bench.autopilot` written beside a recorded lap.

---

# Part 4: a sun that casts (shadow maps)

2026-10-09, about 2 hours, against libwgf at `b181e3e` (shadows in `2cb3a36`). Read as a user would: wgf_light.h's and wgf_model.h's shadow calls, ARCHITECTURE's shadows paragraph, BUILDING's `model ... cast_shadows= receive_shadows=` keys and its new section on a glTF file's nodes as scene blocks, `examples/c/gfx-shadows`, and `measure_frames.py --help` with benchmarks.md's frame-time and shadowbench tables.

## Summary

**Done:**
- **The sun casts**, from `Main.hx`, since a scene has no light kind: a 2048 map, strength 0.85, distance **35 m**, lowered to about 34° so shadows read as shadows.
- **What casts:** the car (every part), the barriers, the gates, and the trees.
- **What only receives:** the ground, the road, its curbs, and its line, set `cast_shadows=false` in the scene: a flat surface casting onto itself is only a draw and a risk of acne.
- **The track moved into the scene file**, using libwgf's new node blocks: the file, each surface's body, the shadow flags, and the 8 checkpoints under their gate nodes (`from checkpoint`, `behavior index=N`). Part 3's code for bodies and polling is gone.
- **No re-recording needed:** physics came out identical (18.3941 m/s at frame 400, before and after), so the recordings stand.
- **Autopilots:** smoke, lap, pad, bench, and race **pass headless and in a browser**, and the lap passes against both release exports. The web export is 533.9 KB gzipped (+9.8 KB: the shadow part), within the 550 KB budget.
- **Frame times: past the budget.** On the bench run (reference machine: RTX 4080 under Xvfb, CPU throttled 4x), the mean went from 1.63 to 14.3 ms and the 95th percentile from 1.94 to **20.78 ms** (budget 16.7). 104 of 612 frames are over 16.7 ms.

**What mattered most:**
1. **Blocker for the budget, not worked around: the shadow pass costs a fixed ~12.5 ms of main-thread time a frame**, whatever casts, receives, or the map's size, and throttled or not. That points at a wait on the GPU, not work. libwgf's own shadowbench shows the same (S4).
2. **Good surprise: the track's physics and shadows are data now** (S1). Part 3's G2 and G4 are answered.
3. **Docs gap: the frame tool and the frame budget.** `measure_frames.py` measures only libwgf's own `games/`, and the docs never give the budget as a number (S5).
4. **Awkward: barriers 15 m out can't shadow the asphalt** with 8 m of grass in front of them. Their shadows land on the run-off (S6).

Severity counts: 1 blocker (frame budget, open), 0 workaround, 3 awkward, 3 docs gap, 3 good surprises.

## Log

### S1. The track as scene blocks: bodies, shadows, checkpoints
- **What I tried:** BUILDING's new "A glTF file's nodes are blocks too": `actor track` with `model path="track/track.glb"`, then `actor track/road` with `body type=static shape=mesh friction=1`, and so on.
- **What happened:** it worked as written, and the physics came out bit-identical to part 3's code-built bodies, so no recording changed.
- **More of it worked than the docs say:**
  - **Shadow flags on node blocks:** `model cast_shadows=false` on a node block was taken. BUILDING says a node's block has "no kind line (a `model tint=` aside)", so I wasn't sure it would be.
  - **Checkpoints as blocks under nodes:** `actor track/gates/gate_0/checkpoint` with `from checkpoint` and `behavior index=0` makes a new actor under a node, behavior parameter included.
- **What went:** `Track.ready()`, `Track.body()`, `Track.gate()`, and the spawn loop. Main instantiates the scene and waits for `countWithBehavior("Checkpoint") == 8` before the car.
- **Dump:** `wgf dump` writes the file's root as its `model path=` block and each changed node under it (part 3's G4, fixed), sensors included. See S7 for what it still loses.
- **Cost:** 20 minutes.
- **Severity:** good surprise (and a small docs gap: say a node block takes the model's shadow keys).

### S2. Every parent on a path needs a block, even a file node left as it is
- **What happened:** `actor track/gates/gate_0/checkpoint` was refused: `scenes/racer.scene:102: no block above at its parent's path ("track/gates/gate_0")`. `gate_0` is the file's node, and `track/gates` had a block, but the node in between needed one too. Eight empty blocks (`actor track/gates/gate_N` / `end`) fixed it.
- **Cost:** 5 minutes, 16 lines.
- **Severity:** awkward. The message was exact. A file's node could count as a block for its children, since the file is above it either way.

### S3. No light in a scene
- **What happened:** BUILDING's kinds have no `light`, so the sun, its shadow settings, and its aim are code (`Main.hx`). That's the part of the shadow setup "the scene file where you can" couldn't take. A glTF file's KHR_lights_punctual lights do come in, but a light from the track file wouldn't carry shadow settings either.
- **Cost:** none (the sun was already code).
- **Severity:** docs gap (a `light` kind with `cast_shadows=`, `shadow_distance=`, would complete the scene).

### S4. The shadow pass's cost is fixed, and past the frame budget
- **What I measured:** the bench run (the lap's first 600 frames), traced as `measure_frames.py` traces, on the reference setup. ms a frame, main thread:

| case | mean | median | 95th | over 16.7 ms |
|---|---:|---:|---:|---:|
| no shadows (part 3), 4x throttle | 1.63 | 1.20 | 1.94 | 3 of 605 |
| **sun casting, as shipped**, 4x | **14.30** | 13.78 | **20.78** | 104 of 612 |
| trees neither cast nor receive, 4x | 13.96 | 13.45 | 18.93 | 77 |
| and the barriers receive nothing, 4x | 13.80 | 13.32 | 19.05 | 64 |
| and a 1024 map, 4x | 14.39 | 13.99 | 20.62 | 83 |
| sun casting, **unthrottled** | 13.14 | 12.93 | 21.09 | 89 |
| no shadows, unthrottled | 0.58 | 0.36 | 0.63 | 2 |

- **What it means:**
  - **About 12.5 ms a frame is the shadow pass being on.** Taking away 280 tree casters, the barriers' receiving, or three quarters of the map's texels moves it under 0.5 ms.
  - **It is not CPU work: unthrottling the CPU 4x leaves it at 13 ms.** It reads like a wait on the GPU inside the frame (the frame's main-thread time is what's traced).
- **libwgf's own numbers agree:** benchmarks.md's shadowbench has the same step, "off" 1.45 ms against "sun 1024" 11.95 at 100 models. Its "no receive" case (0.41 ms) suggests the pass is skipped when nothing receives.
- **Nothing in the API addresses it:** no setting makes the shadow pass cheaper, so a game that wants shadows takes the 12 ms.
- **What I did:** kept the shadows, since the brief asks for a sun that casts, and recorded the miss here. `tools/frames.py` exits 1 on it.
- **Cost:** 40 minutes of measuring.
- **Severity:** **blocker** for the frame budget, open. A game can only turn shadows off. The pass needs to stop stalling, or a `wgf_light` setting to cast every other frame would do.

### S5. Measuring a game's frames, and what the budget is
- **What I tried:** "check the bench autopilot's frame times (wgf docs say how)".
- **What happened:** the docs say `measure_frames.py`, but it takes `game:<name>` from libwgf's own `games/` only, where `racer` is libwgf's imported copy, not this repo's game.
- **Workaround:** `game/tools/frames.py` exports this game as measure_frames does, then calls its `run()` with this game's `bench.autopilot`, and prints the same numbers.
- **The budget:** "libwgf's frame budget" is named in measure_frames' help but not given as a number anywhere I read. I took the table's own line, 16.7 ms (60 Hz), at the 95th percentile.
- **Cost:** 20 minutes, 80 lines.
- **Severity:** docs gap. A `measure_frames.py path/to/game`, and the budget stated (which percentile, at which throttle), would make this a one-line check.

### S6. Barriers too far out to shadow the track
- **What happened:** the brief asks for the barriers to cast onto the track. They cast (wall shadows on the grass), but they stand 15 m from the centerline, behind the curb and 8 m of grass run-off. A 0.9 m wall's shadow reaches about 1.3 m at a 34° sun (it was a thin line at the first sun's 58°), so it never crosses the run-off onto the asphalt.
- **What I did:** lowered the sun for longer shadows everywhere. I left the barriers where part 3 put them: moving them in would change the track, the driving, and every recording.
- **Cost:** 10 minutes.
- **Severity:** awkward (a design consequence, not libwgf's). Bringing the walls to ~1 m past the curbs (gen_track.py's `BARRIER_OFFSET`) would put their shadows on the road; that needs a re-record.

### S7. `wgf dump` drops a node's shadow flags
- **What happened:** the dump writes `track/road`, `track/ground` with their bodies but not their `model cast_shadows=false`, and leaves out `track/road/curbs` and `track/road/line` (whose only change is that flag). Loaded again, those four surfaces would cast.
- **Cost:** 5 minutes.
- **Severity:** awkward (a dump that doesn't load back as the same world).

### S8. Tuning the distance for the chase camera
- **What I tried:** the car's shadow at frame 700 (mid-corner, 92 km/h), cropped 2x, at shadow distances of 15, 25, 40, and 60 m with the 2048 map.
- **What happened:** 15 to 40 m look alike at the car: crisp edges, the wing's and cabin's steps their own silhouette, not texels. At 60 the edges soften. The default bias (1, 4) showed no acne on the road or the car, and the fade at the map's far edge is gentle.
- **Choice:** 35 m (`Main.hx`, `SHADOW_DISTANCE`), with margin because the map is fitted around the camera's view, so its texel size changes with the camera's heading against the sun. Shorter would sharpen nothing visible and lose the gates' shadows ahead.
- **Severity:** good surprise (sharp by default; one number to tune).

## Files (part 4)
- `game/assets/scenes/racer.scene`: the track's file, its surfaces' bodies and shadow flags, and the checkpoints under its gates.
- `game/src/Main.hx`: the sun casts (distance, map size, strength), lower; the scene instantiated and the checkpoints waited for. `game/src/Track.hx`: the centerline and the trees only.
- `game/tools/frames.py`: this game's frame times, as libwgf's measure_frames measures them, held to 16.7 ms at the 95th percentile.

---

# Part 5: a sky and reflections (environments)

2026-10-10, about 3 hours, against libwgf at `1cc552c` (environments in `a45f0f3`; part 4's shadow stall fixed in `44688c7`, and its sokol fork in `1cc552c`). Read as a user would: `wgf_environment.h`, wgf_stage3d.h's `set_environment` and `set_background`, ARCHITECTURE's environments paragraph, `examples/assets/CREDITS.md`, and `tools/gen_sky.py --help` for the sky's sun.

## Summary

**Done:**
- **Sky and light:** `assets/sky/sky.hdr` (libwgf's generated sky; see E2) lights the stage (`setEnvironment`, intensity 0.7) and is drawn behind it (`setBackground`, sharp). It takes the place of the flat ambient light. The sketch's three calls matched the binding exactly.
- **Sun:** aimed along the sky's own sun (25° up, 40° from −z toward +x, read from gen_sky.py), so the light, the shadows, and the bright disc agree. Intensity 3.5; I chose sky 0.7 / sun 3.5 / exposure 0 from three side-by-side crops for contrast in the shadows.
- **Reflections:** the car's paint (roughness 0.22) and cabin glass (0.08) have materials of their own, so the sky shows in them.
- **gen_track.py** now makes the same bytes on every system:
  - **Its own trigonometry**, from IEEE arithmetic alone: sin, cos, and atan2 agree with libm to 4.4e-16.
  - **Linear colors as literals,** since `pow` was also libm's.
  - **6 decimals** in `TrackData.hx`.
  - **Checked:** a run with every libm transcendental trapped to raise passes `--check`, and a `.gitattributes` keeps the generated files from CRLF.
  - **The glb came out byte-identical:** float32 hides the last-ulp changes.
- **Re-recorded:** `TrackData`'s 6 decimals moved the grid by under a micrometre, enough to diverge a 37 s replay. So the lap (37.30 s, bench re-cut) and the race (best lap 32.85 s) are new.
- **Autopilots:** smoke, lap, pad, bench, and race **pass headless and in a browser**, and the lap passes against both release exports.
- **Frame times are within budget.** The bench run's 95th percentile is **2.21 ms** (part 4 shipped at 20.78 before libwgf's stall fix; 2.07 with the fix and no sky). One 1.48 s frame while the sky prepares (E4) puts the mean at 4.19 ms.
- **Size:** the web export is 550.3 KB gzipped, 0.3 KB over the game's 550 budget, so the budget is now 600 (E6).

**When libwgf can turn the track check back on for Windows:** from this commit (re-imported). gen_track.py's output depends only on IEEE `+ − × ÷ √`, Python's own float formatting (the same on every system), and `struct`'s float32 rounding. The trapped run shows no C-library math is reached. **I could not run it on Windows here** (no Windows Python under Wine, no Windows host), so the first Windows `--check` is the proof. libwgf's copy needs its own `eol=lf` rule for `TrackData.hx`, as `f28323d` gave its other generated Haxe.

**What mattered most:**
1. **Awkward: an environment that loads last moved a recording's frame 0 past the game's start.** The recorder counts from where core's loads end, and inputs before that are all filed at frame 0. The start press landed before the replay's grid, so the lap never started. I hold the grid until the sky has loaded, and drive.py now anchors its frames on the start press (E3).
2. **Good surprise: the environment was three calls, as the sketch wrote them, and part 4's shadow stall is gone** (E1).
3. **Docs gap: "libwgf's examples carry CC0 HDRs"** is one generated sky under libwgf's MIT license. There's no photographic HDR (E2).
4. **Docs gap: on the web the sky's preparation is a 1.48 s frame** (E4).

Severity counts: 0 blockers, 0 workaround, 3 awkward, 3 docs gap, 2 good surprises.

## Log

### E1. The sky in three calls, and shadows fixed upstream
- **What I tried:** the sketch's `Environment.create("sky/....hdr")`, `stage.setEnvironment(sky, intensity, rotation)`, and `stage.setBackground(sky, blur)`.
- **What happened:** they matched the binding name for name and worked first time. The sky draws behind everything, the stage is lit from it, and glossy surfaces reflect it.
- **Shadows:** libwgf's stall fix (sokol's per-pass framebuffer check skipped in release web builds) took the racer's bench run from part 4's 20.78 ms at the 95th percentile back to 2.07 with the sun casting.
- **Severity:** good surprise.

### E2. The HDRs libwgf carries
- **What happened:** the brief says libwgf's examples carry CC0 HDRs. They carry one: `examples/assets/environments/sky.hdr`, made by `tools/gen_sky.py`, under libwgf's MIT license, not CC0, and not a photograph.
- **What I did:** used it, copied unchanged into `assets/sky/` (credited in `assets/CREDITS.md`). gen_sky.py's `--help` and source gave the sun's direction, which I needed to aim the light. Neither the header nor the docs say where an environment's sun is: a game must read the image, or know it.
- **Docs gap:** the sky's sun direction (or a call to find a panorama's brightest direction) would let a game aim its light from the data.
- **A related one:** copying the file with `cp` was refused by this session's settings (they read a libwgf path in the command as an edit there). Reading it from Python worked. That's my environment's rule, not libwgf's.
- **Cost:** 15 minutes.
- **Severity:** docs gap.

### E3. A slow load moved the recording's frame 0
- **What happened:** after the sky went in, a fresh recording began `at 0 key down up`, before its `at 1 wait racer.state >= 1`. Replayed, the press came before the grid, the countdown never started, and every expectation failed.
- **Why:** the recorder counts frames from where core's loads end ("its start's loads were in flight: what follows counts from their end"), and every input before then is written at frame 0. The sky is prepared on a worker and was the last load, so the game had reached the grid, and drive.py had pressed Up, during frames the recording doesn't count. In a replay the grid came a frame after the loads, after the press.
- **Workaround, two parts:**
  - **The grid waits for the sky** (`Main.hx`: the car spawns once `Resource.getStatus(sky)` isn't PENDING). Nothing the player can do comes before the loads end.
  - **drive.py anchors its frames on the start press.** It converted its telemetry's frames to the recording's with a fixed −1, true only while loading ended on the first frame. Its expectations now allow ±6 frames for the press's own latency.
- **Cost:** 45 minutes and three re-recordings.
- **Severity:** awkward. The rule is sound; a recording's first inputs filed at frame 0 is the trap. A recorder that wrote `wait` lines for inputs made while loads were in flight (the probe values they came at, `racer.state == 1` here) would keep them ordered.

### E4. On the web the sky's preparation is one long frame
- **What I measured:** the bench run's worst frame went from 106.7 ms to **1476.99 ms** (4x throttle), and the mean from 1.68 to 4.19 ms. The median and 95th barely moved (1.36, 2.21).
- **Why:** wgf_environment.h says an environment is "prepared on a worker thread (tens of milliseconds for a 1024 by 512 image)", and ARCHITECTURE says the web has no threads. In a browser the preparation (spherical harmonics, two cubemaps, a 96-sample GGX prefilter) is one frame on the main thread.
- **Impact:** it falls in the loading period, before the grid (E3), so play is unaffected. A sky changed mid-race would hitch.
- **Severity:** docs gap (say that on the web an environment's preparation is a frame of its own, and about how long).

### E5. A material per model, in code
- **What I tried:** a glossy body and glass for the car, to give the sky something to show in.
- **What happened:** the car's parts are generated meshes from the scene (`model cube=... tint=`), which share their mesh's one material (rough 0.5). A scene can tint a model but can't set a material's numbers.
- **Workaround:** `Car.gloss()` makes a PBR material per part and sets it on the model (`setMaterial(-1, m)`), the tint back to white (`Car.hx`).
- **Cost:** 10 minutes, 20 lines.
- **Severity:** awkward. `model roughness= metallic=` on a scene line would keep the car's look in its prefab.

### E6. Size: 0.3 KB over
- **What happened:** the web export is 550.3 KB gzipped, 0.3 KB over the game's 550 KB budget (wgf.json). The environment part added about 16 KB, and the 6-decimal TrackData took about 5 KB off racer.js.
- **Decision:** the budget is now 600, since the next feature would cross it anyway. The 0.3 KB could have come out of TrackData's precision instead.
- **Severity:** awkward (a budget that tracks libwgf's steps, not the game's).

### E7. The race in a browser: 13 minutes, and one unexplained FAIL
- **What happened:** the race (6,469 frames) in a browser now takes 13 minutes 23 seconds, about 8 frames a second on the headless browser's software rendering with the sky. That's within the default timeout (two minutes plus the last frame at 3 a second).
- **The FAIL:** of three runs, the first printed `FAIL` and nothing else I had kept; the other two passed with 0 of 8 expectations failed. I can't say what failed.
- **What would have caught it:** keeping the whole log of every browser run, which `tools/frames.py` and my loops didn't.
- **Severity:** docs gap (a FAIL's reason should be in its last line, as part 1's #11 asked).

### E8. `wgf screenshot` still has no `--timeout`
- **What happened:** with the sky, `wgf screenshot --frame 1150` failed twice with "the page never reached frame 1150", while `wgf autopilot --web` flew the same inputs past 1200. screenshot keeps a fixed 120 s, and at about 8 frames a second 1,150 frames don't fit. The message is part 3's misleading one again.
- **Workaround:** earlier frames (180, 700) for the lighting comparisons.
- **Cost:** 10 minutes.
- **Severity:** awkward (screenshot could take `--timeout`, as autopilot now does, and say it ran out of time).

## Files (part 5)
- `game/assets/sky/sky.hdr` (libwgf's, credited in `game/assets/CREDITS.md`).
- `game/src/Main.hx`: the environment and background, the sun aimed along the sky's, the tuning constants (`SKY_LIGHT`, `SUN`, `EXPOSURE`), the grid held until the sky loads. `game/src/Car.hx`: glossy paint and glass.
- `game/tools/gen_track.py`: its own trigonometry, linear colors as literals, 6 decimals. `game/src/TrackData.hx` regenerated. `.gitattributes`: the generated files kept LF.
- `game/tools/drive.py`: the start held 10 frames on the grid, frames anchored on the start press, ±6-frame windows, `--race RECORDING OUT`.
- `game/autopilot/`: `lap`, `bench`, `race` re-recorded. `game/wgf.json`: budget 600 KB.

---

# Part 6: the track streamed, as Racing Kit pieces

2026-10-10, about 5 hours, against libwgf at `675a439` (asset groups and manifests carried from libwgt, `Ui.progress` in `835ec8c`, compressed textures in `7ca8a01` and `c4d5b49`, `check_racer_stream.py` in `675a439`). Read as a user would: `wgf_asset.h`, `wgf_ui.h`'s progress bar, `wgf_resource.h`'s load budget, `wgf_fs.h`, `wgf_scene.h`, BUILDING's scene-file and tool tables, ARCHITECTURE's asset section, and the `--help` of `gen_manifest.py`, `compress_textures.py`, and `check_racer_stream.py`.

## Summary

**Done:**
- **The track's form: libwgf's recommendation, Kenney's Racing Kit** (CC0, version 2.0, credited in `assets/CREDITS.md`). gen_track.py stays the source:
  - **The layout:** 15 straights and corners on the kit's grid at 16 m a tile, walked as a turtle and checked to close. It's 754 m, about the old track's 742.
  - **Eight section scenes:** 275 pieces placed (21 road pieces, 156 barrier walls, 8 gantries over the checkpoints, 90 trees), 37 KB of scene text.
  - **One collision file** (`track/collision.glb`, 63 KB): the ground (drawn), the road and its shoulders, and the walls.
  - **TrackData:** the centerline, the checkpoints, and each section's first sample and files (its scene, then the pieces it places first).
  - **The manifests:** gen_track.py writes them last.
  - **The pieces** go through `tools/gen_kit.py` (P3).
- **I didn't choose the alternatives.** The generated track cut into sections ships about 448 KB where this ships 63 KB of collision, 84 KB of pieces and 37 KB of scenes. Building the meshes at start-up leaves nothing to stream. The kit also looks far better than my flat strips.
- **The start:** a loading screen with libwgf's progress bar (`Ui.progress`) over one asset group of the start's files: the two scenes, the collision, the sky, and the first two sections' scenes and pieces. Its last 30% is the pieces' meshes loading.
- **During the race** (`src/Stream.hx`): from the start's press, the rest comes a section at a time, nearest first, once its start is within 280 m ahead of the car. Each section's files are one asset group, then its scene loads and is placed, and it counts as ready once every piece it shows has loaded. When the race starts 4 of 8 sections are still to come, and none has been late.
- **The manifest** (`Asset.setManifest("manifest.json")`): a returning visit downloads nothing, and offline the race starts from the cache.
- **Compressed textures:** not used. The kit draws with material colors, not textures; the only image is the HDR sky, which `compress_textures.py` doesn't take.
- **Rob's bug:**
  - **Recovery** (`Main.recover`, `Car.lost`): the car goes back to the last checkpoint passed (the grid before the first), 4 m on, facing along the track, at rest. T, Backspace, or a pad's Back do it. So does the car itself after 1 s upside down (its up below 0.3), 1 s past the barriers (or below the world), or 2.5 s with the throttle or brake held and under 1 m/s.
  - **Barriers:** they collide 2.6 m tall and 0.7 m thick, where the drawn ones are 0.9 m and 0.5 m.
- **Autopilots:**
  - **New:** `stream.autopilot` (the lap with `log streaming` at the race's start; it expects `racer.stream_pending_at_start >= 1`, `racer.stream_late == 0`, and every section in by the lap's end) and `wall.autopilot` (into the barrier at 30 m/s, then put back by the key and by itself).
  - **Re-recorded:** lap (44.28 s) and race (best lap 40.82 s). bench is re-cut.
  - **Results:** smoke, lap, pad, bench, stream, wall, and race pass headless and in a browser, and the lap passes against both release exports.
- **`check_racer_stream.py --game game`: PASS** (final build). First visit: 26 files downloaded, worst frame from the race's start 4.2 ms, none over 33. Again: 0 files downloaded, worst 4.4 ms. Offline: 0 files, worst 4.5 ms, the race started from the cache.
- **Frame times are within libwgf's budget:** the bench run (the lap's first 600 frames) has a median of **2.01 ms** and a 95th percentile of **6.33 ms**, against 6 and 10. That's up from part 5's 1.36 and 2.21: the cost of 275 placed pieces, each its own draws (about 560 in the main pass and 500 in the shadow pass), until libwgf instances them.
  - Three frames over 33 ms (the worst 103 ms) fall in the loading; the stream check found none from the race's start.
  - A first measurement was past budget (18.89 ms) because of my own loading-screen load budget (P7).
- **Size:** the web export is 552.0 KB gzipped, within the game's 600.

**What mattered most:**
1. **Workaround: the root manifest was fetched before the browser's file store was ready, so it was lost.** The stream check failed its second visit (P1). The file store clears "a cache from an older build" after it opens, and that wiped the root manifest the game had just been sent. The game now makes one `Fs.exists` request and asks for its files once that answers.
2. **Awkward: a recording made while the start loads through asset groups counts from the program's first frame.** The recorder only waits for core's loads, and ensures aren't counted as loads, so the replay's grid lasted 23 frames where the recording's lasted 10, and the lap diverged at the first S-bend (P2). drive.py now counts every input from the grid's first frame.
3. **Awkward: a stale manifest fails a file outright, and the export doesn't write manifests** (P4). The game keeps them in `assets/`, written by gen_track.py, which has to be run after every asset change.
4. **Awkward: a model that collides but isn't drawn takes code.** Scene lines have no `visible=` (P5).
5. **Good surprises:** the progress bar, asset groups, and the manifest worked as their headers say. The kit's pieces loaded unchanged (lit, once my importer fixed their colors). The taller hidden walls held a car at 32 m/s, and nothing tunnelled (P6).

Severity counts: 0 blockers, 1 workaround, 5 awkward, 1 docs gap, 4 good surprises.

## Log

### P1. The root manifest lost to the file store's first clearing
- **What I tried:** `Asset.setManifest("manifest.json")` in `init`, before the first load, as wgf_asset.h says ("Set it before the loads it should cover").
- **What happened:** libwgf's check failed its second visit with "downloaded again: /assets/manifest.json". The third visit, offline, logged "no manifest at manifest.json; files are cached as the cache mode says", although every other file came from the cache.
- **Why (from the log):** the first visit logs `wgf_core_fs: cache from an older build, clearing it` after the autopilot has started, so after the root manifest had been fetched and kept. The clearing took the manifest; everything fetched after it survived.
- **Ensuring the manifest as a file as well** changed nothing.
- **Workaround:** `init` makes one file request (`Fs.exists("manifest.json")`; wgf_fs.h: "A request made before storage is ready waits for it"), and the game sets the manifest and starts its loads once that answers (`Main.load`).
- **Cost:** 3 check runs (4 minutes each) and 30 minutes.
- **Severity:** workaround. libwgf could hold manifest and asset fetches until storage has opened and cleared, as it holds fs requests. Without that, a program that follows the header's advice loses its root manifest on the first visit after every new build.

### P2. Asset groups aren't core's loads, so a recording counts from frame 0
- **What happened:** the new lap, replayed, failed from checkpoint 4. Headless and the browser agreed with each other, but not with the recording. The car's speed differed from frame 205, still on the grid.
- **Why:** the recording had no `wait core.loading == 0`. The start's files load through `Asset.ensure` and a group, which `core.loading` doesn't count, so at the recorder's start nothing was "in flight" and it counted from the program's first frame. The grid then came at a frame that takes real time to reach: the press came 10 frames after it in the recording and 23 in the replay, and a car held 13 frames longer on its springs isn't the same car.
- **Workaround:** drive.py (`rebase`) counts every input from the grid's first frame (its telemetry says when it was), behind the `at 1 wait racer.state >= 1` it already wrote, so the press keeps its distance from the grid in every run. The race's recording goes through it too.
- **Cost:** 40 minutes and two re-recordings.
- **Severity:** awkward. Part 5's E3 again, from the other side. A recorder that also waited for asset tasks, or wrote a `wait` on the first probe that changed, would make the recording's frame 0 the same point in every run.

### P3. The kit's pieces, imported
- **What I found:** every kit `.glb` has its node translated by (−0.35, −0.01, −0.65), an export's leftover. Each lists `KHR_materials_unlit` as used, though no material uses it. Its colors are the flat colors of its unlit previews written as linear factors, so lit by the sun the road came out pale grey and the grass mint.
- **What I did:** `tools/gen_kit.py` writes the nine pieces the track places, from the download (checked by its sha256), changing the JSON alone: the node's offset dropped, each color taken from sRGB to linear (written out, not computed, as gen_track's colors are), a roughness, no metal, and the unused extension gone. The binary chunks are the kit's.
- **libwgf:** it loaded every piece as glTF says, lit, and shared each piece's mesh among the models of its path. Placing them took the kit's own geometry: entry edges, corner radii of 1.5 and 2.5 tiles, the left turn as a right turn entered from its other end.
- **Cost:** an hour, mostly reading the kit.
- **Severity:** good surprise (for libwgf; the kit's quirks are the kit's).

### P4. A stale manifest fails the file, and nothing writes one at export
- **What happened:** after an edit to `racer.scene`, the browser refused it ("isn't what the manifest lists ... not kept", then "not found"). The loading screen waited for ever, since my game didn't yet treat a failed scene as an end.
- **Why:** gen_manifest.py says to run it "on what is deployed, after the last file is in place", but `wgf export` doesn't run it, and `check_racer_stream.py` exports for itself. So the manifests have to be in `assets/` before an export: gen_track.py writes them last, over the whole tree, and its `--check` fails a stale one.
- **What else it took:**
  - Every file under `assets/` is now committed without line-ending conversion (`.gitattributes`), since a CRLF checkout would change their hashes.
  - The loading screen now says when a scene failed.
- **Cost:** 20 minutes.
- **Severity:** awkward. `wgf export` writing the manifests into what it exports, and `wgf serve` leaving the manifest out, would keep them right without a step to remember.

### P5. A body's model that isn't drawn
- **What I tried:** the sketch's `model path=track/collision.glb visible=false`, with each node's body in the scene.
- **What happened:** a scene's `model` line has no `visible` key (BUILDING: `tint`, `cast_shadows`, `receive_shadows`). The bodies are made from the models as they are, drawn or not.
- **Workaround:** `Track.hideCollision` finds `track/road` and `track/walls` each tick until the file's nodes exist, then `setVisible(false)` on each. Hidden, they still collide and cast nothing (wgf_model.h).
- **Cost:** 15 minutes, 15 lines.
- **Severity:** awkward. `visible=false` on a scene line, or a body whose shape comes from a file and has no model, would keep the collision's setup in the scene.

### P6. The barriers: taller in collision, and no tunnelling
- **What I did:** the drawn barriers are kit walls (0.9 m tall, 0.5 m thick). Their collision is a box per wall, on the same chords, 2.6 m tall and 0.7 m thick, plus the gantries' posts.
- **How I tested it** (`wall.autopilot`): the car at 30 to 32 m/s, steered full right into the barrier. It hit, scraped along it at 10.3 m from the centerline (the barrier's face is at 11.65), stayed upright, and never got past 10.53 m.
  - Pinned there with the throttle down, it was put back after 2.5 s.
  - **Not tested:** a square hit at that speed. Steering that narrows with speed allows only a glancing one.
- **Tunnelling:** none seen, at any speed I reached.
- **Severity:** good surprise.

### P7. A loading screen's load budget, measured as frames
- **What I tried:** wgf_resource.h's advice: "a loading screen, with nothing else to draw, can give it more (16 ms, say) and set it back once loaded". The loading screen set 16 ms, and the car's spawn put back 4.
- **What happened:** the bench run's 95th percentile went to **18.89 ms**, past the budget. Measure_frames counts every frame of the run, the loading screen's too, and each of them now took its 16 ms. With the default 4 ms the same run is 6.33.
- **Also:** part 5's 1.48 s frame, the sky prepared all at once in a browser, is gone. libwgf now prepares an environment in steps, and the worst loading frame is 103 ms.
- **Decision:** the default budget. Headless, the 16 ms changed nothing either: the grid comes about 2,600 frames in, under a second, since a waiting headless run's frames are uncapped.
- **Cost:** one measurement run, 10 minutes.
- **Severity:** awkward. Either the budget's advice or the measurement should say which frames count: a loading screen spending its frames on loading is what the advice wants, and the frame budget reads it as slow frames.

### P8. Smaller things
- **Asset groups need their own lists:** wgf_asset.h says no libwgf format "names others yet" for an ensure, so ensuring a section's scene doesn't bring the pieces it places. TrackData lists each section's pieces beside its scene. Severity: docs gap (ARCHITECTURE's glTF paragraph says a lister names a glTF's buffers and images, which the header contradicts).
- **The frame budget is in the docs now:** BUILDING's measure_frames row gives a game's budget as a median of 6 ms, a 95th percentile of 10 ms, and no frame over 33 ms once play starts. `tools/frames.py` now holds the bench run to the first two (part 4's 16.7 was my guess). Severity: good surprise.
- **`wgf screenshot --timeout` exists now** (part 5's E8). Severity: good surprise.
- **`LogLevel.WARN`, not `WARNING`.** The compiler said so. Severity: none.
- **`wgf export --autopilot lap.autopilot`** (the file named without its folder) ended in a Python traceback (`FileNotFoundError` in cli.py's `export_web`), after the web export had been built. `autopilot/lap.autopilot` works. Severity: awkward (a line naming the missing file, or looking in the game's autopilot folder, as `playthrough` in wgf.json does).
- **The driver:** drive.py's corner speeds came from part 2's track. The kit's S-bends (two 24 m corners back to back) put it into the wall once; recovery put it back, which is how I first saw recovery work. Its grip is now 7 m/s² by default.

## Files (part 6)
- `game/tools/gen_kit.py` (new) and `game/assets/kit/*.glb`: the kit's pieces.
- `game/tools/gen_track.py`: the layout, the sections, the collision (shared corners, no normals for what isn't drawn), `scenes/track.scene`, TrackData, the manifests. `track.glb` is gone.
- `game/src/Stream.hx` (new): the start's group, the progress, and the sections streamed nearest first.
- `game/src/Main.hx`:
  - the storage wait, then the manifest;
  - the loading screen;
  - `recover()`.
- `game/src/Car.hx`, `game/src/Drive.hx`: what puts the car back by itself. `game/src/Track.hx`: the sections, and hiding the collision. `game/src/Laps.hx`: `racer.across`, `racer.farthest`, `racer.upright`.
- `game/assets/scenes/racer.scene`: the car alone.
- `game/tools/drive.py`:
  - `rebase` (inputs from the grid's first frame);
  - `stream()` and `wall()` beside `bench()`;
  - grip 7.
- `game/tools/frames.py`: libwgf's documented budget.
- `game/autopilot/`: `lap`, `race` re-recorded, `bench` re-cut, `stream` and `wall` new.
- `.gitattributes`: no line-ending conversion under `assets/`.

---

# Part 7: many things on the track (instancing, particles, sprites and text in 3D)

2026-10-11, about 4 hours, against libwgf at `239fe13`, exported at `a137a1f` (automatic instancing, GPU particles, sprites and text in 3D, and their scene keys). Read as a user would: `wgf_emitter3d.h`, `wgf_sprite.h`, `wgf_text.h`, `wgf_vehicle.h`'s slip, BUILDING's scene table (`emitter3d`, `sprite`, `text`, and the new `visible=`), and ARCHITECTURE's paragraphs on stage3d, sprites in 3D, and particles.

**A slip of mine first:** a `grep` over libwgf's `docs/*.md` for "instanc" matched its denied `docs/FRICTION.md`, `ROADMAP.md`, and `HISTORY.md`, and printed a few of their lines. I didn't use them (everything below comes from the headers, BUILDING, ARCHITECTURE, and my own measurements), and from then on named the allowed files one by one.

## Summary

**Done:**
- **Trackside props** from Kenney's Racing Kit (credited in `assets/CREDITS.md`). gen_kit.py imports eight more pieces: the pylon, three flags, two banner towers, the large light post, and the grandstand. gen_track.py places 376 of them in the streamed section scenes, and each section asks for its pieces before it is placed:
  - 288 cones along the barriers through every corner, from 20 m before it;
  - flags every 16 m on the straights, red and green, with a light post at every third;
  - a banner tower before each corner, on its outside;
  - checkered flags either side of the line;
  - eight grandstands along the start straight.
- **Signs, in the scenes:** sprites of one generated atlas (`assets/signs/signs.png`, 256 by 128) and text, all `alpha=mask`:
  - chevrons through each corner (`facing=free`);
  - distance boards at 100 m and 50 m before the corners, where the approach is straight;
  - turn numbers on discs (`facing=camera-y`);
  - the sponsors' boards along the start straight;
  - the start gantry: the race's name on its beam, and a board under it whose lap `Main.gantryBoard` writes ("LAP 2 / 3", "FINAL LAP", "FINISH"), published as the probe `racer.gantry`.
- **Tire smoke:** an `emitter3d` per rear tire in the car's prefab, on the body behind each contact patch, not on the wheel (R2). `Car.smokeRates` sets each rate from its wheel's `getWheelSlip`: none below 2, then 20 a second for each unit above it, at most 160. A puff texture (`assets/fx/puff.png`) is drawn by gen_track.py. The size and colour keys are set in Haxe (R3), the emitters are warmed on the grid (R4), and `skipTrail` is called on recovery.
- **`visible=false`** hides the collision in `track.scene`, and the export now writes the manifests, so `assets/` keeps none (R10).
- **Autopilots:**
  - **New:** `slide.autopilot`, a wheelspinning start and a handbrake turn. It expects smoke while sliding (`racer.slip > 2`, `racer.smoke >= 100`) and none once the car grips again.
  - **Changed:** `lap` and `race` also expect the gantry's lap.
  - **Re-recorded:** none. The props stand outside the barriers and the layout didn't change.
  - **Results:** smoke, lap, pad, bench, stream, wall, race, and slide **pass headless and in a browser**.
- **`check_racer_stream.py --game game`: PASS.**
  - First visit: the worst frame from the race's start was 8.2 ms, none over 33.
  - Again: 0 files downloaded, worst 8.1 ms.
  - Offline: the race started from the cache, worst 8.4 ms.
  - Before the emitters were warmed it was 32.5 ms (R4).
- **Draws, counted by a WebGL2 hook in a copy of the exported page, the car on the grid:** 62 draws before the props, 126 with them (296 instances in 49 instanced draws). The order of the scene's lines made no difference (R1).
- **Frame times** (`tools/frames.py`, in a browser, three runs each; the table holds the run reported):

  | Build | Median | p95 | p99 |
  |---|---|---|---|
  | Before the props (part 6's game, today's libwgf) | 1.81 to 2.74 ms | 6.49 to 8.36 ms | 9.0 to 11.2 ms |
  | The props and signs, no smoke (`SMOKE_MAX` 0) | 2.55 ms | 6.61 ms | 10.8 ms |
  | The props, signs, and smoke (bench) | 2.64 ms | 6.57 ms | 14.0 ms |
  | The slide, the most smoke (`--autopilot slide`) | 2.72 ms | 6.75 ms | 14.3 ms |

  - **All are within libwgf's budget** of a 6 ms median and a 10 ms p95.
  - **The props cost less than the run-to-run noise.** The two base runs differ by 0.9 ms of median.
  - **The smoke costs about 0.1 ms of median** and a few ms at the 99th percentile.
  - **The bench does slide:** its launch spins a rear wheel and its hardest corners slide, so the bench measures smoke. The slide autopilot measures the most.
  - These runs were made before the emitters were warmed (R4), which only changes the first spawn.
- **`wgf export --web --desktop --autopilot autopilot/lap.autopilot`** against libwgf `a137a1f`: the lap **passes against both release exports**. The web export is **571.8 KB gzipped** against the game's 600: wgf-host.wasm 524.7 KB, wgf-host.js 22.8, wgf.js 4.4, racer.js 17.2, index.html 2.7. That's up from part 6's 552.0, nearly all in libwgf's wasm (instancing, particles, text in 3D, render targets); the game's own racer.js grew 0.9 KB.
  - While I was finishing, libwgf's working tree was mid-change (render targets), and its web build didn't compile until `a137a1f` was committed. The export waited for it.

**What mattered most:**
1. **Workaround: an emitter's first particles cost a frame of about 25 ms** (R4). It pushed the stream check's worst frame from 4 ms to 32.5, a hair under its line, and it took an hour to find, because halving the sections came first. A puff from each emitter on the grid, cleared two frames later, fixed it.
2. **Good surprise: instancing needed nothing.** The props are model lines like the pieces, and libwgf gathers alike parts across the stage whatever the scene's order (R1). The props doubled the draws, from 62 to 126, but cost less than the noise in frame time.
3. **Awkward: the effect lives half in the scene and half in Haxe.** A scene line has two size keys and two colour keys, so the smoke's curves are code (R3). Slip pegs at 10, so its rate needed a threshold found by probing a lap (R6). The puffs are cut flat by the road, with no soft particles (R5).
4. **Awkward: text over a camera-facing sprite loses the depth test at a distance** (R7). A pair facing the camera can't be separated along the view in a scene, so the turn numbers are offset 0.3 m toward the track.
5. **Good surprise: sprites and text in 3D did what BUILDING says on the first try:** `alpha=mask`, `facing=free` and `camera-y`, a `source=` cell of one atlas, and `size=` in metres (R8). Part 6's `visible=` and the export's manifests landed, and two workarounds came out (R10).

Severity counts: 0 blockers, 1 workaround, 4 awkward, 0 docs gaps, 5 good surprises.

## Log

### R1. Instancing: nothing to do, and the order of a scene's lines doesn't matter
- **What I did:** placed the props as models in the section scenes, as the pieces are, and nothing else. Every prop is a kit file, so each one's mesh is shared by path and its parts are the same materials.
- **What I measured:** libwgf has no draw counter a game can read, so I wrapped WebGL2's draw calls in a copy of the exported page and counted them each frame, the car on the grid (`scratch`, not committed). The grid sees the first two sections.

  | Build | Plain draws | Instanced draws (instances) | Array draws | All draws |
  |---|---|---|---|---|
  | Part 6 (no props) | 26 | 27 (161) | 9 | 62 |
  | Props, each section's grouped by model | 58 | 49 (296) | 19 | 126 |
  | Props in the loop's order (a cone, a flag, a cone...) | 58 | 49 (296) | 19 | 126 |

- **What I expected:** the brief says a prop that differs from its neighbour breaks a run, so I wrote the props both ways. The counts are identical: libwgf gathers alike parts across the stage whatever order the scene gives them. gen_track writes them in the loop's order.
- **What still costs:** a kit prop is several parts (a flag 4, a banner tower 3, the light post 3, the grandstand 4), each its own run, in each pass. The 58 plain draws are runs of one: parts nothing else shares, the car's generated meshes among them.
- **Severity:** good surprise. A draw counter a program can read (a probe, `render.draws`) would have saved the page hook.

### R2. Tire smoke: where the emitter hangs
- **What I did:** an `emitter3d` line per rear tire in the car prefab, as children of the car, not of the wheels. A wheel actor turns about its x every tick, and an emitter's velocity turns with its actor (wgf_emitter3d.h says so plainly), so under a wheel the puffs would be flung round it. On the body, a few centimetres up and behind each contact patch, the puffs leave backwards and up and then stay where they were born as the car drives on, which is what smoke does.
- **`skipTrail()`** on recovery and on the grid's reset keeps a teleport from laying a line of puffs across the track. The header names the case.
- **Severity:** good surprise (the header said everything I needed).

### R3. The smoke's curves are code: a scene line has two keys
- **What I wanted:** a puff that fades in as it leaves the tire, grows fast then slowly, and greys as it thins: three or four keys each for colour and size.
- **What a scene line says:** `size=start,end[,variance]` and `color=#..,#..`, two keys each.
- **Workaround:** `Car.curves` clears the keys and adds four colour keys and three size keys in Haxe, 15 lines.
- **Severity:** awkward. A `size=` and `color=` that took `t:value` lists (`color=0:#E0E0E000,0.08:#E0E0E06E,...`) would keep the effect in the scene with its other settings.

### R4. An emitter's first particles cost a 25 ms frame
- **What happened:** after the smoke went in, `check_racer_stream.py`'s worst frame from the race's start rose from about 4.4 ms (part 6) to 16.7 to 32.5 ms, a hair under its 33 ms line.
- **What it wasn't:** halving the sections (16 of them, at most 73 actors placed in one frame instead of 124) changed nothing (32.5 ms).
- **What it was:** with the smoke's rate held at 0 the worst frame was 7.7 to 8.7 ms. The race's start is the car's wheelspinning launch, the first frame anything is spawned, so the first particles cost one long frame.
- **Workaround:** a puff from each emitter as the car is made, behind the loading screen, born transparent and cleared two frames on (`Car.onCreate`, `Car.onFrame`). The worst frame from the race's start is 8.1 to 8.4 ms.
- **Cost:** three stream checks (5 minutes each) and an hour.
- **Severity:** workaround. The emitter part could prepare what it needs when the first emitter is made, or a call could (a `prepare` alongside `prewarm`); at least the header could say the first spawn is slow.

### R5. Puffs cut by the road
- **What happened:** a puff is a camera-facing quad. As it grows past its height above the road, its lower part goes under the road and the depth test cuts it on a straight line: rows of puffs with flat bottoms behind a sliding car.
- **Workaround:** the emitters sit 20 cm up and behind the tires, and the puffs rise faster (an upward pull of 0.9 m/s²). That makes the edges rarer, not gone.
- **Severity:** awkward. Soft particles (fading where a particle nears what is behind it) are what smoke on a road wants.

### R6. Slip as a ratio: the sketch's threshold smoked everywhere
- **What happened:** the sketch's `slip > 0.3 ? 120 * slip : 0` (written for a 0 to 1 slip) made smoke from the first frame to the last. Probed through a lap, the rear slip read 10, the header's cap, in most corners and at the launch, where one rear wheel spins (the inside one, unloaded), and the fronts read about 8 while sliding.
- **What I did:** smoke from a slip of 2, 20 puffs a second for each unit above it, at most 160 per tire. The lap smokes at the launch and in its hardest corners; `slide.autopilot` makes the most.
- **Why it matters:** a ratio that pegs at 10 says a wheel is past its grip, not how fast its rubber is sliding over the road, which is what the amount of smoke follows. The header does say what slip is (1 the limit, smoke from about 1.5).
- **Severity:** awkward (the vehicle's, more than the emitter's). A sliding speed per wheel (m/s) beside the ratio would drive a smoke rate directly.

### R7. A turn's number lost against its disc
- **What I did:** each turn's number is a disc sprite and a text, both `facing=camera-y`, at one point: the text a child of the sprite.
- **What happened:** near, the number showed. From about 60 m only a few pixels of it did. The two are one plane turned to the camera; the sprite (MASK) is drawn with the opaque models, the text later among the see-through things, and as depth precision falls with distance the text loses the depth test to the disc.
- **Workaround:** the text 0.3 m nearer the track than its disc (gen_track), the side every camera sees it from.
- **Severity:** awkward. A camera-facing pair can't be separated along the view in a scene. A depth offset for text over its parent, or a text drawn as the sprite's label, would fix it. Boards facing `free` have no such problem: the text sits 3 cm in front along the board's own z.

### R8. Signs and text in 3D: mostly as the headers say
- **Scene keys:** `sprite ... alpha=mask cutoff=0.5 facing=free` and `text ... facing=free` did what BUILDING says, first time. A sprite's `source=` picks a cell of one atlas, so every sign is the same texture.
- **Sizes:** a 3D text's size is one unit a line unless `size=` says otherwise, as the brief warned. Every text in the scenes sets it: the race's name 0.9 m on the gantry's beam, the lap board's 0.9, the distance boards' 0.55, the sponsors' 0.75, the turn numbers 1.3.
- **The gantry at a distance:** screenshots from the lap's last straight (frames 2660 to 2850, at 82 to 117 km/h). The lap board (a dark `#202020` board, yellow text 0.9 m) reads from about 60 m. The race's name, white text 0.9 m tall on the kit's white overhead, read only from about 30 m: it was the colour, not the size or the facing. It's red now (`#C8282C`, the sponsors' red), and reads from about 60 m, as the board does. That's my mistake, not libwgf's; the text drew crisp at every distance, with no shimmer.
- **Smoke against text:** in no screenshot did a puff cross a sign. The header says why it could go wrong: BLEND emitters are sorted by the emitter's position, and a text in 3D is among the see-through things, so a puff born 20 m back sorts as if it were at the car. With the signs outside the barriers and the smoke on the road, the two rarely overlap on screen, and a puff lives under 2 s.
- **The gantry's lap:** `Main.gantryBoard` finds `gantry_board/lap` once its section is placed and sets its string when the lap changes ("LAP 2 / 3", "FINAL LAP", "FINISH"), published as the text probe `racer.gantry`, which the lap and race autopilots expect. That's code by nature.
- **Severity:** good surprise.

### R9. Emitter limits, for two tires
- **What the header gives:** at most `max` alive per emitter (default 1024, 1 to 65536, refused outside it, the oldest replaced when full), up to 8 size and 8 colour keys, BLEND sorted per emitter by its position and its particles unsorted, no shadow, no culling.
- **What tire smoke used:** two emitters, `max=384` each. At 160 puffs a second and a life of up to 1.8 s a tire could need 288, so none are replaced. The slide (`slide.autopilot`) has 100 or more alive at the handbrake's end.
- **None of it bit,** in this order of importance:
  - unsorted particles within one emitter are fine for a soft grey puff;
  - one sort point for a trail many metres long is the case above (smoke against text);
  - the 8 keys are plenty.
- **Severity:** good surprise.

### R10. Part 6's frictions, fixed upstream
- **`visible=false` on a scene line (part 6's P5):** the collision's road and walls are hidden in `track.scene`, and `Track.hideCollision` is gone (replaced by a check that the file's nodes exist, which spawning still needs).
- **The web export writes the manifests (part 6's P4):** BUILDING says so. gen_track.py no longer writes them, and `assets/` keeps none, so there is no stale manifest to forget. The stream check passes with the export's own.
- **An autopilot by its name (part 6's P8):** BUILDING now says `wgf autopilot lap.autopilot` finds it in the game's folder.
- **The root manifest and the file store (part 6's P1):** a libwgf commit says asset tasks now wait for the browser's store, but its header doesn't say so, and I kept the game's `Fs.exists` wait.
- **Severity:** good surprise.

### R11. Smaller things
- **The kit's props:** `gen_kit.py` takes eight more files (17 in all), with two new materials (`pylon`'s orange and `checkers`, the checkered flag's texture, kept). The importer's rule (the node's offset dropped, sRGB colours made linear) held for every one. A tent I imported and didn't place is gone again.
- **Two PNGs from gen_track.py:** the atlas and the puff are drawn in Python and written with zlib, whose bytes may differ between systems, so `--check` compares their pixels. Severity: none (gen_track's own rule).
- **Compressed textures:** still not used. The two images are 1.2 KB and 2.1 KB of PNG, and the kit's checkered flag's 437 bytes sits inside its `.glb`.
- **Frame budget at the slide:** `tools/frames.py --autopilot slide` measures the run with the most smoke.

## Files (part 7)
- `game/tools/gen_kit.py`: eight more kit pieces, and the `pylon` and `checkers` materials.
- `game/tools/gen_track.py`:
  - the props and the signs in the section scenes;
  - the gantry;
  - the signs' atlas and the smoke's puff, PNGs checked by their pixels;
  - `visible=false` on the collision;
  - no manifests.
- `game/src/Car.hx`: the smoke (its curves, its rate from the slip, the warm-up, `skipTrail`). `game/assets/scenes/racer.scene`: the two emitters in the car's prefab.
- `game/src/Main.hx`: the gantry's board. `game/src/Track.hx`: `collisionPlaced` in place of `hideCollision`. `game/src/Stream.hx`: textures among a section's files, and the puff among the start's.
- `game/autopilot/slide.autopilot` (new). `lap` and `race` expect the gantry. `game/tools/drive.py` writes those expectations. `game/tools/frames.py`: `--autopilot`.
- `game/assets/CREDITS.md`, `game/README.md`.
- `game/assets/**/manifest.json`: gone (the export writes them).

---

# Part 8: a rear-view mirror and a speed effect (render targets, custom shaders, screen effects)

2026-10-12, about 4 hours, against libwgf at `f87f0fe` (`29a9d3b` after it changes only CI; render targets in `688843d`, their read-back in `7b35885`). Read as a user would:
- the headers: `wgf_render.h` (targets, effects), `wgf_texture.h` (`create_target`, `read`), `wgf_stage3d.h` (`draw_with`), `wgf_shader.h`, `wgf_material.h`'s custom materials, `wgf_sprite.h`, `wgf_stage2d.h`, `wgf_camera3d.h`, `wgf_draw.h`;
- `wgf.glsl` (the staged copy, `share/wgf/shaders/`), BUILDING's "Shaders" and its scene table's `emitter3d`, and `pack_shader.py --help`;
- the Haxe binding's `Render`, `Texture`, `TextureRead`, `Shader`, `Material`, and `Sprite`;
- `examples/c/gfx-postprocess` and `examples/shaders/vignette.glsl`.

## Summary

**Done:**
- **The rear-view mirror** (`src/Mirror.hx`):
  - **Into a target:** the stage is drawn a second time each frame, into a 480 by 120 render target, through a camera parented to the car behind its cabin. Unturned, the camera looks back. It has a 0.36 rad vertical field (about 69 degrees across at 4:1) and sees 120 m (`setClip`).
  - **On the screen:** the target is shown at the top, 400 by 100 in a dark frame, left-right mirrored by a sprite on a 2D stage of its own, scaled -1 in x (S2). It shows during the countdown and the race, and not on the loading screen, the grid's "press to start", or the results.
- **The speed effect** (`src/SpeedEffect.hx`, `shaders/speed.glsl`, packed by `tools/pack_shaders.py` into `assets/shaders/speed.wgfshader`, committed):
  - **What it does:** toward the frame's edges, the picture is blurred along the line to the middle (eight taps) and darkened, and everything warms a little.
  - **How strong:** none below 8 m/s (29 km/h) and all of it at 32 m/s (115 km/h), about the lap's top speed. It is set from the car's speed every frame, and is off outside the race.
  - **What it leaves alone:** the HUD's two panels and the mirror are rectangles the effect leaves as they are (S3).
- **Part 7's workarounds undone:**
  - **The emitters' warm-up is gone.** The stream check's worst frame from the race's start, the first smoke's frame among them, is now 9.6, 9.5 and 11.2 ms over its three visits. Part 7 measured 32.5 ms without the warm-up and 8.1 to 8.4 ms with it. That's within 3 ms of the warm-up and far under the 33 ms line, so the warm-up stays out (S6).
  - **The smoke's curves are in the scene:** `size_keys=0:0.4,0.25:1.8,1:3.6 color_keys=0:#EBEBEB00,...`, exactly what `Car.curves` said. A `wgf dump` reads them back key for key. `Car.curves` is gone.
- **Probes and expectations:**
  - **Probes:** `racer.mirror` is 1 on a frame it is drawn. Once, half a second into the countdown, the mirror's pixels are read back (S8) (`Texture.read`): `racer.mirror_read` is 2 when DONE, 3 when FAILED (headless), and `racer.mirror_seen` is the share of pixels unlike its clear color, -1 when unread. `racer.effect` is the effect's strength.
  - **lap and stream** expect none of either on the grid, the mirror drawn by the race's start, its read answered, `mirror_seen != 0` (a share in a browser, -1 headless), and the effect above 0.5 approaching the line.
  - **race** expects neither on the results screen.
  - `drive.py` writes the same lines.
- **Autopilots:** smoke, lap, pad, bench, stream, wall, race and slide **pass headless and in a browser** (`--web`). The race takes 36 minutes in a browser on this machine. My own `--timeout 1800` cut it short once; the default (two minutes plus a third of a second a frame) is right.
- **`check_racer_stream.py --game game`:** **PASS**. The worst frame from the race's start is 9.5, 9.8 and 9.2 ms (the first visit, again, offline), none over 33. 39 files are downloaded the first time (the shader and its manifest among them), and none after. With the read-back in the race it failed (S8).
- **Frame times** (`tools/frames.py`, the bench in a browser, three runs a build):

  | Build | Median | p95 | p99 | Frames over 33 ms |
  |---|---|---|---|---|
  | Neither (`MIRROR` and `EFFECT` false) | 2.58 ms | 6.68 ms | 11.8 ms | 4 |
  | The mirror (480 by 120, 120 m) | 3.12 ms | 6.61 ms | 12.2 ms | 5 |
  | The mirror and the effect | **3.19 ms** | **6.80 ms** | 13.6 ms | 5 |
  | The mirror at 256 by 64, and the effect | 3.13 ms | 6.81 ms | 13.1 ms | 5 |
  | The mirror seeing 1000 m (the camera's default), and the effect | 3.25 ms | **11.39 ms** | 24.1 ms | 7 |

  - **The mirror costs about 0.5 ms of median, and the effect about 0.1.** With both, the game is within libwgf's budget of a 6 ms median and a 10 ms p95.
  - **How far the mirror sees is what matters, not its pixels.** At 256 by 64 it costs the same as at 480 by 120. Left at the camera's default 1000 m, the second view draws the whole loop's pieces and props behind the car, and the p95 goes past the budget. At 120 m it draws the near track only.
  - **frames.py measures main-thread work** (Chrome's trace), so it shows what drawing the stage twice costs to submit, not what it costs the GPU (S7).
  - **The extra frame over 33 ms** is the read-back, as `wgf_texture.h` says: it stalls the next frame's update until the GPU has drawn. It came once a race, in the race. It is in the countdown now (S8). These runs were made before that move.
- **`wgf export --web --desktop --autopilot autopilot/lap.autopilot`:** the lap **passes against both release exports**. The web export is **584.8 KB gzipped** against the game's 600: wgf-host.wasm 536.0 KB, wgf-host.js 23.0, wgf.js 4.6, racer.js 18.4, index.html 2.7, with 135 host calls. That's up 13.0 KB from part 7's 571.8: 11.3 in libwgf's wasm (targets, read-back, effects) and 1.2 in the game's racer.js.

**What mattered most:**
1. **Workaround: an effect redraws the HUD** (S3). The speed effect's blur and darkening are strongest at the frame's edges, where the lap panel, the speed and the mirror are. The shader takes three rectangles to leave alone, and the speed went on a panel so its patch reads as one.
2. **Workaround: a release export crashed where every other run passed** (S5). The trimmed web host lacked `get_size`, which `getData` uses, and only `frames.py`'s export run found it, at the read.
3. **Workaround: the read-back's stall broke the stream check's line** (S8). One frame over 33 ms, so the read moved to the countdown.
4. **Awkward: nothing that draws a target can flip it** (S2). A sprite scaled -1 in x on a 2D stage of its own does, though no header says so.
5. **Awkward: a game's tool has to find `pack_shader.py` itself** (S1). Writing and packing the shader was otherwise smooth: the errors cite my file and line, and the export and the stream carry the packed file like any asset.
6. **Good surprise: Part 7's two fixes held** (S6). The warm-up is gone with the stream check under 12 ms, and the smoke's curves are scene keys.
7. **How far the mirror sees is its cost, not its pixels.** At 120 m it adds about 0.5 ms of median; at the camera's default 1000 m the p95 is past the budget.

Severity counts: 0 blockers, 3 workarounds, 3 awkward, 0 docs gaps, 1 good surprise.

## Log

### S1. Writing and packing a shader as a game does
- **Finding things:**
  - **The tool and the header:** BUILDING's "Shaders" and `wgf_shader.h` both name `pack_shader.py` and `wgf.glsl`, in libwgf's checkout (`tools/`, `libwgf/gfx/shaders/`) and in each staged variant (`share/wgf/tools/`, `share/wgf/shaders/`). I read the staged `wgf.glsl`, a 90-line comment that says everything a shader gets, screen effects included.
  - **No `wgf` command:** `wgf` itself has nothing for shaders. A game's tool has to find libwgf on its own, so `tools/pack_shaders.py` takes `--libwgf`, then `$LIBWGF`, then a checkout beside the repository.
  - **Where the packed file goes:** wherever `-o` says. I chose `assets/shaders/` and committed it, as gen_track's output is, with a `--check`. The checkout's tool and the staged one pack the same bytes, so `--check` is exact.
- **Writing:** the effect packed on the first try. Its parameters are a uniform block, read back from the compiled SPIR-V, and they are named in the file's header (`param strength float fs 0`).
- **Errors:**
  - **A typo:** an undeclared name was reported against my file and line: `bad.glsl:47:0: error: 'darkk' : undeclared identifier`. Good.
  - **An `int` parameter used as a float:** it compiled, by glslang's implicit conversion.
- **The export and the stream:** the export carries `assets/shaders/speed.wgfshader` like any other file, and writes it into `assets/shaders/manifest.json`. The shader loads on the frame it's asked for (PENDING, then READY), and an effect is skipped until it's READY, so it needs no place in the loading screen's group.
- **What a game would want:**
  - `wgf build` and `wgf serve` packing `shaders/*.glsl` themselves, as they compile the Haxe. Without that a stale `.wgfshader` is easy to ship; my `--check` is the guard.
  - Or at least `wgf pack-shader` (a path the tool knows).
- **Severity:** awkward (finding libwgf from a game's own tool). Otherwise a good surprise.

### S2. A mirror needs flipping, and nothing that draws a target flips it
- **What I wanted:** the target drawn on the screen left-right mirrored.
- **What isn't there:**
  - `Draw.texture` has no flip, and `Draw.textureRegion` takes a source width of 0 or less as "the whole texture", not a flip.
  - A camera's scale is ignored (`wgf_camera3d.h`).
  - An effect redraws the screen, not a target, so a UV flip in a shader would need a 3D quad with a custom surface material in front of a camera.
- **What worked:** a 2D stage of its own with one sprite of the target, scaled -1 in x. `wgf_sprite.h` and `wgf_actor.h` don't say a negative scale mirrors a sprite, but it does. It's drawn in the HUD's coordinates, in order with the immediate-mode calls around it.
- **Severity:** awkward. `Draw.textureRegion` with a negative source width as a flip, or a flip flag on a sprite, would be one line.
- **The rest of the mirror:**
  - **Size and aspect:** the target's own size sets the aspect its camera sees (`wgf_render.h` says so), and the field is vertical, so a 4:1 mirror needs its field worked out from the width it should show.
  - **Drawing less than the main view:** the far clip (`setClip`) was enough, and it's the setting that mattered (the table above).
  - **Things a mirror might want to draw less of, which it can't:** the tire smoke and the signs, with no layer or mask on `drawWith`. It didn't need them.
  - **Shadows:** the frame's shadows are fitted around the main view (`wgf_stage3d.h` says so), so the mirror shows none behind the car. A mirror doesn't need them; nothing looked wrong.
  - **Text in the mirror reads backwards,** the sponsors' boards and the gantry's name, as in a real mirror. That's right.

### S3. An effect redraws the HUD
- **What happens:** `wgf_render.h` says it: the finished frame, stages, sprites, text, and the UI, is what an effect redraws. The speed effect's edge blur and darkening sat on the lap panel (top left), the speed (bottom right), and the mirror (top), all at the frame's edges where the effect is strongest.
- **Workaround:** three `vec4` parameters (`keep0` to `keep2`), the panels' and the mirror's rectangles in the frame's 0..1, which the shader leaves alone with a soft 3-pixel edge. `Main.hud` sets them each frame from the design's coordinates and the visible area.
  - A rectangle left alone is visible as a box against the blurred, darkened, warmer frame around it. That's fine for the lap panel and the mirror, which are boxes, but the speed's bare digits sat in a clear patch. They're on a shaded panel now.
- **What would fix it:** effects applied before the HUD, e.g. `Render.applyEffects()` called mid-frame, or a flag on `addEffect` for "the stages only". The racer's HUD and libwgf's UI would then draw over the effect untouched.
- **Severity:** workaround.

### S4. What the effect wanted and couldn't have
- **The previous frame:** a speed blur is motion blur, which wants the previous frame or motion vectors. The effect has only this frame, so the blur runs along the line to the middle as a stand-in (a zoom blur).
- **Depth:** a blur that left the car sharp and smeared the far trackside would want depth. The effect has none, so the car's corners blur where they reach the edges.
- **A second texture:** not wanted. The shader can declare textures, and the material sets them.
- **Severity:** awkward (by design for now; the header says what an effect gets, and it got that).

### S5. The web export's trimmed host lacked a call the binding uses
- **What happened:**
  - `frames.py` exports the release web build and flies the bench. The first time, with the mirror, it failed at frame 406: `uncaught exception in frame: host._wgf_texture_read_get_size is not a function`. The program quit.
  - The development web build (`wgf autopilot --web`) and headless runs had passed, because their hosts aren't trimmed.
- **Why, as far as a user can tell:**
  - The game called `TextureRead.getData`, never `getSize`.
  - The export keeps only the host calls a game makes (it says "135 calls in its host").
  - `getData` must ask for the size itself, so the trimmed host was missing `get_size`.
- **Workaround:** `Mirror.check` asks for the size by name (`read.getSize()`), and loops over it. The release export flies the lap.
- **Cost:** four frame runs (about 25 minutes), and only a run against the export found it.
- **Severity:** workaround. A crash in a release build that every other check passes is the worst kind. A binding call that uses another host call should count it.

### S6. Part 7's two, fixed upstream
- **The first spawn:** without the warm-up, the stream check's worst frame from the race's start is 9.6 / 9.5 / 11.2 ms (the first visit, again, offline). Part 7's 32.5 ms is gone. What's left is a few ms over the warm-up's 8.1 to 8.4, within the noise of a run.
- **The curves:** `size_keys=` and `color_keys=` say exactly what the Haxe said, four colour keys and three size keys. `wgf dump` writes them back, with `size=` and `color=` beside them (the first and last keys), which a reader could take for two settings.
- **Severity:** good surprise.

### S7. Smaller things
- **Measuring a second view:** `frames.py` (libwgf's `measure_frames.py`) traces main-thread work, so a second view's GPU time isn't in the budget's numbers. On this machine's GPU it doesn't matter; a weaker one could be GPU-bound with the CPU at 3 ms.
- **The read-back as an expectation:** DONE in a browser and FAILED headless, as `wgf_texture.h` says, so one expectation (`mirror_seen != 0`) holds in both, and says less in either. An autopilot line that ran only in a browser would say more.
- **My first threshold:** the effect read 0.29 at 63 km/h, barely seen, so its range moved down to 29 to 115 km/h.
- **The custom material's parameters set before its shader loads** are kept, as `wgf_material.h` says, so `SpeedEffect.create` sets them all at once with no wait.

### S8. The read-back's stall, and where a game can afford it
- **What happened:** the read was first made a second into the race. The stream check failed on all three visits with one frame over 33 ms (47.0, 41.1, 42.8). Its worst frame from the race's start had been 9.6 ms without the mirror.
- **Why:** `wgf_texture.h` says it: a read stalls the next frame's update until the GPU has drawn the frame asking. With the CPU throttled four times, that one frame is 40 ms.
- **What I did:** the read is in the countdown now, half a second in, with the car still on the grid and a hitch unseen. The stream check passes again (9.5, 9.8, 9.2 ms), and `mirror_read` and `mirror_seen` are still expected by frame 310.
- **What a game would want:** a read that answers a frame or two later without the stall (a fence, polled). A readback is for a test or a photo mode, not a race. The header is honest about the cost, so this is a choice of timing, not a surprise.
- **Severity:** workaround.

## Files (part 8)
- `game/src/Mirror.hx` (new): the target, its camera on the car, the mirrored sprite, the read-back in the countdown.
- `game/src/SpeedEffect.hx`, `game/shaders/speed.glsl` (new), and `game/assets/shaders/speed.wgfshader` (packed, committed).
- `game/tools/pack_shaders.py` (new): packs `shaders/*.glsl` into `assets/shaders/`, with `--check`.
- `game/src/Main.hx`: the `MIRROR` and `EFFECT` switches, the mirror drawn before the stage, the effect's strength each frame, and the HUD's kept rectangles and the speed's panel.
- `game/src/Car.hx`: the warm-up and `curves()` gone. `game/assets/scenes/racer.scene`: `size_keys=` and `color_keys=` on both emitters.
- `game/autopilot/lap`, `stream` and `race`: the mirror's and the effect's expectations. `game/tools/drive.py` writes them.
- `game/README.md`.

