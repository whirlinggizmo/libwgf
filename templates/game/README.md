# @NAME@

A game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe.

```sh
wgf serve                   # in a browser, reloading as you save, its state kept
wgf run                     # on the desktop
wgf autopilot autopilot/smoke.autopilot   # an autopilot run, headless (its format: libwgf's BUILDING.md, "Autopilot files")
wgf export                  # export/web (a static folder) and export/desktop, each smoke-tested
```

`src/Main.hx` sets an 800 by 600 design, fitted to any window or screen with bars (`Presentation.set`; libwgf's `wgf_presentation.h` has the other modes): draw in those coordinates whatever the window's size.

`wgf.json` is the game's: its name, its title (its page's), its main class, where its sources, assets, and autopilot files are, its web size budget (`web_budget_kb`, gzipped; 400 KB fits a 2D or 3D game, and physics (`wgf_physics.h`) adds about 225 KB, so a game with bodies wants about 550), which `wgf export` holds it to, and `defines`, if it has any: names (or name=value) given to every build of it as Haxe's `-D` (a bot or a telemetry build, read with `#if`), and `playthrough`, the autopilot file a game in libwgf's own `games/` is checked and measured by (`playthrough.autopilot` by default).
