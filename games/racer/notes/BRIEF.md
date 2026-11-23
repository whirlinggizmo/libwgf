# Brief: the racer, on libwgf, as a game developer

You are a game developer using libwgf, a game framework, as an SDK. You are not one of its developers. Build the drivable slice of a chase-camera racing game with it.

## The slice (now)
A box car on a flat track built from generated meshes, driven arcade-style; a chase camera that follows behind and above the car, smoothed, looking ahead; checkpoints in order and lap timing; a HUD of drawn text (lap, lap time, best lap); keyboard and gamepad through libwgf's input actions; the presentation (scaling) mode so it fills the page. An autopilot lap from the first working version: record one with `wgf autopilot --record` (a build made with -D wgf_record) and add expects on the checkpoints passed in order and a lap time within a window.

Physics (a real vehicle) comes later, when libwgf has it: drive with motion for now, and keep the car's driving in one place so it can be swapped.

## Design intent
`sketches/` here is the game code the framework's authors hoped a game could write. Use it as intent, not as an API reference: where a name in the sketches differs from libwgf's docs and headers, the docs win (known: Action.bindAxis is Action.bindKeys; bindPadAxis takes a direction and a threshold; Debug.setFpsOverlay is Debug.showFps; an autopilot's stick line is `pad 0 axis left_x`). Log every place the sketch's intent couldn't be written as hoped.

## What you may use
libwgf at ~/projects/github/whirlinggizmo/libwgf, as a user would: README.md, BUILDING.md, docs/ARCHITECTURE.md, docs/CONVENTIONS.md, docs/BINDINGS.md, docs/benchmarks.md and the other public docs, the public headers (*/include/), the Haxe binding, the `wgf` tool and its template, the examples, and games/asteroids as a sample game. Your settings deny the library's source, deps, tests, and its internal docs (HISTORY, ROADMAP, FRICTION, SPEC); don't read them by any route, the shell included. Anything you could only learn from the source is a finding. Never edit anything in libwgf.

## How
- `wgf new` the game in Haxe, in ./game.
- Work in steps; build and run often (`wgf serve` for the browser, headless autopilot runs to check).
- Finish with `wgf export` (web and desktop). Don't deploy or push anywhere.
- Commit your work locally in this directory with git as you go (git init here first).

## FRICTION.md (in this directory), the output that matters
Log as you go: what you tried, what you expected (from the docs or the API) and what happened, the file and line of any workaround, the cost (minutes, lines, a feature skipped), and a severity: blocker, workaround, awkward, docs gap, or good surprise. A short summary at the top when you finish. Stop when the slice is done and its autopilot lap passes headless and in a browser.
