# Racer

The drivable slice of a chase-camera racing game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe: a box car on a circuit of Kenney's Racing Kit pieces, driven on physics (physics3d's Jolt wheeled vehicle), three laps of checkpoints in order, lap times, a HUD. Up (W, a pad's right trigger or south button) starts the race and drives, Left and Right (A and D, the left stick) steer, Down (S, left trigger) brakes, Space (east) is the handbrake, T or Backspace (Back) puts the car back on the track at the last checkpoint, R (Start) puts it back on the grid, and B shows the physics bodies.

```sh
wgf serve                                  # in a browser, reloading as you save
wgf autopilot autopilot/lap.autopilot      # the recorded lap, headless (--web: in a browser)
wgf export --autopilot autopilot/lap.autopilot   # export/web and export/desktop, each flying the lap
```

- **The track** is written by `tools/gen_track.py` from its layout: straights and corners on the kit's 16 m grid (the same bytes every time and on every system: its trigonometry is its own, from IEEE arithmetic alone; `--check` fails when a file is stale). It writes:
  - `assets/track/section_0.scene` to `section_7.scene`: the kit pieces of each eighth of the loop (road, barriers, gates, trees), what streams in.
  - `assets/track/collision.glb`: the whole loop's collision, loaded first: the ground (drawn), the road, and the walls (hidden by the game). The walls are taller than the drawn barriers, so a car can't roll over them.
  - `assets/scenes/track.scene`: the collision file's bodies and the checkpoints' sensors.
  - `src/TrackData.hx`: the centerline every 2 m, the checkpoints, and each section's first sample and files.
  - `manifest.json` in every directory under `assets/` (libwgf's `gen_manifest.py`), so a returning browser reads everything from its cache. **Run gen_track.py again after changing any file under `assets/`**: a stale manifest makes the browser refuse the changed file.
- **The pieces,** `assets/kit/*.glb`, are written by `tools/gen_kit.py` from the kit's download (`python3 tools/gen_kit.py kenney_racing-kit.zip`). Their triangles are unchanged; the importer drops the node's offset, makes the colors linear and gives each a roughness (credited in `assets/CREDITS.md`).
- **Streaming** (`src/Stream.hx`): the loading screen's bar is the start's files (the scenes, the collision, the sky, and the grid's two sections). The rest streams in once the start is pressed: a section at a time, nearest first, once it is within 280 m ahead of the car. Its files (the section's scene and the pieces it places first) are fetched as one asset group.
- `src/Drive.hx` is how the car drives: the vehicle's input each tick, with steering that narrows with speed and grass that drags. The engine, gears, springs and tires are the car prefab's `vehicle` line in `assets/scenes/racer.scene`. `Car.hx` turns the actions into its intent, and puts the car back on the track by itself after 1 s upside down or past the barriers, or 2.5 s stuck with the throttle or brake held.
- The sun that casts is `src/Main.hx`'s (a scene has no light), aimed along the sun in `assets/sky/sky.hdr` (libwgf's generated sky), which lights the stage and is drawn behind it.
- **Autopilots:**
  - `lap.autopilot` was recorded with `wgf autopilot --record`. `tools/drive.py` typed the keys into the record build's window under Xvfb (`xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/drive.py autopilot/lap.autopilot`), counted the inputs from the grid's first frame, and wrote its expectations. As it records the lap it also writes:
    - `bench.autopilot`: the lap's first 600 frames, for libwgf's frame benchmark.
    - `stream.autopilot`: the lap with `log streaming` at the race's start, for libwgf's `tools/check_racer_stream.py --game .`.
    - `wall.autopilot`: the car flat out into a barrier, then put back by the key and by itself.
  - `race.autopilot` is three laps and the results (`RACER_LAPS=3`, then `drive.py --race`).
  - `pad.autopilot` covers the pad controls.
  - `RACER_TRACE=file` makes drive.py write the car's line, a frame a line.
- `python3 tools/frames.py` measures the bench run's frame times as libwgf's `measure_frames.py` does, and holds them to libwgf's game budget (a median of 6 ms, a 95th percentile of 10).
- Probes:
  - The race: `racer.state` (0 loading, 1 ready, 2 countdown, 3 racing, 4 results), `racer.lap`, `racer.checkpoint`, `racer.passed`, `racer.lap_time`, `racer.last`, `racer.best`.
  - The car: `racer.speed`, `racer.steer`, `racer.grass`, `racer.across`, `racer.farthest`, `racer.upright`, `racer.recoveries`, `racer.y`, `racer.heading`, `racer.yaw_rate`, `racer.gear`, `racer.rpm`, `racer.slip0` to `racer.slip3`.
  - Streaming: `racer.stream_ready`, `racer.stream_pending`, `racer.stream_pending_at_start`, `racer.stream_late`.

What it took, and what libwgf couldn't do yet: [../FRICTION.md](../FRICTION.md).
