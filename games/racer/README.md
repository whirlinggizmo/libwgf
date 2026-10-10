# Racer

The drivable slice of a chase-camera racing game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe: a box car on a generated circuit, driven on physics (physics3d's Jolt wheeled vehicle), three laps of checkpoints in order, lap times, a HUD. Up (W, a pad's right trigger or south button) starts the race and drives, Left and Right (A and D, the left stick) steer, Down (S, left trigger) brakes, Space (east) is the handbrake, R (Start) puts the car back on the grid, and B shows the physics bodies.

```sh
wgf serve                                  # in a browser, reloading as you save
wgf autopilot autopilot/lap.autopilot      # the recorded lap, headless (--web: in a browser)
wgf export --autopilot autopilot/lap.autopilot   # export/web and export/desktop, each flying the lap
```

- `src/Drive.hx` is how the car drives: the vehicle's input each tick, with steering that narrows with speed and grass that drags. The engine, gears, springs and tires are the car prefab's `vehicle` line. `Car.hx` turns the actions into its intent.
- `assets/track/track.glb` is the circuit, written by `tools/gen_track.py` from its centerline (the same bytes every time; `--check` fails when it or `src/TrackData.hx`, the centerline it writes for the game, is stale): named nodes for the ground, the road with its curbs and line, the barriers, and the gates. `assets/scenes/racer.scene` places it: each surface node's static mesh body and shadow settings (the car, barriers, gates and trees cast; the flat surfaces only receive), and a checkpoint's sensor under each gate. The sun that casts is `src/Main.hx`'s (a scene has no light), its shadow distance tuned for the chase camera. `assets/scenes/racer.scene` holds the car (a dynamic body with a vehicle) and the checkpoint (a sensor) prefabs.
- `autopilot/lap.autopilot` was recorded with `wgf autopilot --record`. `tools/drive.py` typed the keys into the record build's window under Xvfb (`xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/drive.py autopilot/lap.autopilot`) and wrote its expectations. `bench.autopilot` is the lap's first 600 frames (drive.py cuts it as it records the lap), for libwgf's frame benchmark. `race.autopilot` is three laps and the results (in a browser it needs `--timeout`), `pad.autopilot` covers the pad controls, and `bench.autopilot` is the lap's first 600 frames (the start and two checkpoints), the run libwgf's frame-time tool flies: cut from `lap.autopilot`, and cut again when the lap is re-recorded. `RACER_TRACE=file` makes drive.py write the car's line, a frame a line. `python3 tools/frames.py` measures the bench run's frame times as libwgf's `measure_frames.py` does, and holds its 95th percentile to a 16.7 ms frame.
- Probes: `racer.state` (0 loading, 1 ready, 2 countdown, 3 racing, 4 results), `racer.lap`, `racer.checkpoint`, `racer.passed`, `racer.lap_time`, `racer.last`, `racer.best`, `racer.speed`, `racer.steer`, `racer.grass`, `racer.models`, and the vehicle's: `racer.y`, `racer.heading`, `racer.yaw_rate`, `racer.gear`, `racer.rpm`, `racer.slip0` to `racer.slip3`.

What it took, and what libwgf couldn't do yet: [../FRICTION.md](../FRICTION.md).
