# The racer, game code first

The game-side code milestone 2's racer should be able to write (SPEC.md, "Games test the framework only if they can't bend it": game code first), committed with the plan before any of the framework under it exists. It is not compiled and not a game: `games/racer/` is built later by a separate game-developer session, from the docs and the public API alone.

Each step of the plan (docs/ROADMAP.md, milestone 2) makes part of this real, and says which part. Where the framework built differs from what is written here, the difference is recorded: in [docs/FRICTION.md](../../FRICTION.md) when the game has to work around it, in docs/HISTORY.md when the sketch was wrong and the framework's way is better.

- `Main.hx`: the stage, the sun and its shadows, the sky, the track and car from the scene file, the loading screen, the race's states, and the HUD with a rear-view mirror and a speed effect.
- `Car.hx`: the player's intent (throttle, brake, steering, handbrake) into the vehicle; tire smoke from the wheels' slip.
- `ChaseCamera.hx`: a camera behind the car, smoothed by a spring, looking ahead.
- `Laps.hx`: checkpoints in order, lap times, and the probes the autopilot checks.
- `racer.scene`: the car's vehicle, the checkpoints, and the trackside props, as data.
- `lap.autopilot`: the lap, as `wgf autopilot --record` would write it, and what it expects.
