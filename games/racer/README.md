# Racer

The drivable slice of a chase-camera racing game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe: a box car on a generated circuit, three laps of checkpoints in order, lap times, a HUD. Up (W, a pad's right trigger or south button) starts the race and drives, Left and Right (A and D, the left stick) steer, Down (S, left trigger) brakes, Space (east) is the handbrake, R (Start) puts the car back on the grid.

```sh
wgf serve                                  # in a browser, reloading as you save
wgf autopilot autopilot/lap.autopilot      # the recorded lap, headless (--web: in a browser)
wgf export --autopilot autopilot/lap.autopilot   # export/web and export/desktop, each flying the lap
```

- `src/ArcadeDrive.hx` is how the car drives, and the only class a real vehicle (physics3d) replaces. `Car.hx` turns the actions into its intent.
- `src/Track.hx` builds the circuit from its centerline out of generated meshes. `assets/scenes/racer.scene` holds the car and checkpoint prefabs.
- `autopilot/lap.autopilot` was recorded with `wgf autopilot --record`. `tools/drive.py` typed the keys into the record build's window under Xvfb (`xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/drive.py autopilot/lap.autopilot`) and wrote its expectations. `race.autopilot` is three laps and the results (native only), and `pad.autopilot` covers the pad controls.
- Probes: `racer.state` (0 loading, 1 ready, 2 countdown, 3 racing, 4 results), `racer.lap`, `racer.checkpoint`, `racer.passed`, `racer.lap_time`, `racer.last`, `racer.best`, `racer.speed`, `racer.steer`, `racer.grass`, `racer.models`.

What it took, and what libwgf couldn't do yet: [../FRICTION.md](../FRICTION.md).
