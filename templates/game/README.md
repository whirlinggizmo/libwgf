# @NAME@

A game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe.

```sh
wgf serve                   # in a browser, reloading as you save its code or an asset, its state kept
wgf run                     # on the desktop
wgf autopilot autopilot/smoke.autopilot   # an autopilot run, headless (its format: libwgf's BUILDING.md, "Autopilot files")
wgf export                  # export/web (a static folder, its assets' manifests written) and export/desktop, each smoke-tested
```

`src/Main.hx` sets an 800 by 600 design, fitted to any window or screen with bars (`Presentation.set`; libwgf's `wgf_presentation.h` has the other modes): draw in those coordinates whatever the window's size.

`wgf.json` is the game's: its name, its title (its page's), its main class, where its sources, assets, and autopilot files are, a web size budget if it wants one (`web_budget_kb`, gzipped, which `wgf export` then holds it to; none by default), and `defines`, if it has any: names (or name=value) given to every build of it as Haxe's `-D` (a bot or a telemetry build, read with `#if`), and `playthrough`, the autopilot file a game in libwgf's own `games/` is checked by (`playthrough.autopilot` by default). Its frame times are measured on `autopilot/bench.autopilot` when it has one (a few hundred frames of its busiest play, short enough for a software renderer), else on its playthrough: `python3 <libwgf>/tools/bench/measure_frames.py <this folder>`.
