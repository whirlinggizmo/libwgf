# Racer

The drivable slice of a chase-camera racing game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe: a box car on a circuit of Kenney's Racing Kit pieces, driven on physics (physics3d's Jolt wheeled vehicle), three laps of checkpoints in order, lap times, a HUD. Up (W, a pad's right trigger or south button) starts the race and drives, Left and Right (A and D, the left stick) steer, Down (S, left trigger) brakes, Space (east) is the handbrake, T or Backspace (Back) puts the car back on the track at the last checkpoint, R (Start) puts it back on the grid, and B shows the physics bodies.

```sh
wgf serve                                  # in a browser, reloading as you save
wgf autopilot autopilot/lap.autopilot      # the recorded lap, headless (--web: in a browser)
wgf export --autopilot autopilot/lap.autopilot   # export/web and export/desktop, each flying the lap
```

- **The track** is written by `tools/gen_track.py` from its layout: straights and corners on the kit's 16 m grid (the same bytes every time and on every system: its trigonometry is its own, from IEEE arithmetic alone; `--check` fails when a file is stale). It writes:
  - `assets/track/section_0.scene` to `section_7.scene`: what streams in, each eighth of the loop's kit pieces (road, barriers, gates, trees), its trackside props (cones along every corner, flags, light posts, banner towers, the grandstands and checkered flags at the start), and its signs (chevrons, distance boards, turn numbers, sponsors' boards, the gantry's name and lap board), sprites of `assets/signs/signs.png` and text.
  - `assets/track/collision.glb`: the whole loop's collision, loaded first: the ground (drawn), the road, and the walls (`visible=false` in the scene). The walls are taller than the drawn barriers, so a car can't roll over them.
  - `assets/scenes/track.scene`: the collision file's bodies and the checkpoints' sensors.
  - `src/TrackData.hx`: the centerline every 2 m, the checkpoints, and each section's first sample and files.
  - `assets/signs/signs.png` and `assets/fx/puff.png`, drawn by it (compared by their pixels, since zlib's bytes may differ between systems).
- **The pieces,** `assets/kit/*.glb`, are written by `tools/gen_kit.py` from the kit's download (`python3 tools/gen_kit.py kenney_racing-kit.zip`). Their triangles are unchanged; the importer drops the node's offset, makes the colors linear and gives each a roughness (credited in `assets/CREDITS.md`).
- **The manifests** (so a returning browser reads everything from its cache) are written by `wgf export` into what it exports; under `wgf serve` and `wgf autopilot --web` there are none, and the files are cached as the cache mode says.
- **Streaming** (`src/Stream.hx`): the loading screen's bar is the start's files (the scenes, the collision, the sky, the smoke's puff, and the grid's two sections). The rest streams in once the start is pressed: a section at a time, nearest first, once it is within 280 m ahead of the car. Its files (the section's scene, and the pieces and the signs' atlas it places first) are fetched as one asset group.
- **Tire smoke:** an `emitter3d` at each rear tire's contact patch, on the car's body (a wheel turns, and its emitter's particles would turn with it), its rate from that wheel's slip (`Car.smokeRates`), its curves (fading in, growing, greying) set in `Car.curves`.
- **The start gantry's board** says the lap (`Main.gantryBoard`), the last one called, then the finish.
- `src/Drive.hx` is how the car drives: the vehicle's input each tick, with steering that narrows with speed and grass that drags. The engine, gears, springs and tires are the car prefab's `vehicle` line in `assets/scenes/racer.scene`. `Car.hx` turns the actions into its intent, and puts the car back on the track by itself after 1 s upside down or past the barriers, or 2.5 s stuck with the throttle or brake held.
- The sun that casts is `src/Main.hx`'s (a scene has no light), aimed along the sun in `assets/sky/sky.hdr` (libwgf's generated sky), which lights the stage and is drawn behind it.
- **Autopilots:**
  - `lap.autopilot` was recorded with `wgf autopilot --record`. `tools/drive.py` typed the keys into the record build's window under Xvfb (`xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/drive.py autopilot/lap.autopilot`), counted the inputs from the grid's first frame, and wrote its expectations. As it records the lap it also writes:
    - `bench.autopilot`: the lap's first 600 frames, for libwgf's frame benchmark.
    - `stream.autopilot`: the lap with `log streaming` at the race's start, for libwgf's `tools/check_racer_stream.py --game .`.
    - `wall.autopilot`: the car flat out into a barrier, then put back by the key and by itself.
  - `slide.autopilot` (by hand): a wheelspinning start and a handbrake turn, the smoke rising and then gone.
  - `race.autopilot` is three laps and the results (`RACER_LAPS=3`, then `drive.py --race`).
  - `pad.autopilot` covers the pad controls.
  - `RACER_TRACE=file` makes drive.py write the car's line, a frame a line.
- `python3 tools/frames.py` measures the bench run's frame times as libwgf's `measure_frames.py` does, and holds them to libwgf's game budget (a median of 6 ms, a 95th percentile of 10). `--autopilot slide` measures the slide instead, the most smoke a car makes.
- Probes:
  - The race: `racer.state` (0 loading, 1 ready, 2 countdown, 3 racing, 4 results), `racer.gantry` (the gantry board's text), `racer.lap`, `racer.checkpoint`, `racer.passed`, `racer.lap_time`, `racer.last`, `racer.best`.
  - The car: `racer.smoke_rate` (puffs a second, both tires), `racer.smoke` (puffs alive), `racer.speed`, `racer.steer`, `racer.grass`, `racer.across`, `racer.farthest`, `racer.upright`, `racer.recoveries`, `racer.y`, `racer.heading`, `racer.yaw_rate`, `racer.gear`, `racer.rpm`, `racer.slip0` to `racer.slip3`.
  - Streaming: `racer.stream_ready`, `racer.stream_pending`, `racer.stream_pending_at_start`, `racer.stream_late`.

What it took, and what libwgf couldn't do yet: [../FRICTION.md](../FRICTION.md).
