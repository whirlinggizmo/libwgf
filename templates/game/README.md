# @NAME@

A game on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe.

```sh
wgf serve                   # in a browser, reloading as you save its code or an asset, its state kept
wgf run                     # on the desktop
wgf autopilot autopilot/smoke.autopilot   # an autopilot run, headless (its format: libwgf's BUILDING.md, "Autopilot files")
wgf export                  # export/web (a static folder, its assets' manifests written) and export/desktop, each smoke-tested
```

`src/Main.hx` sets an 800 by 600 design, fitted to any window or screen with bars (`Presentation.set`; libwgf's `wgf_presentation.h` has the other modes): draw in those coordinates whatever the window's size.

`web/index.html` is the game's page, written from libwgf's the first time it builds for the web, and the game's to change: `<body data-start="tap">` puts a tap to start over it (its title, then "Tap to start" once loaded), a tap before play that starts its sound and grants a fullscreen asked for at start (`Window.requestFullscreen` before `Runtime.run`), landing on the layer, not the game.

`wgf.json` is the game's: its name, its title (its page's), its main class, where its sources, assets, and autopilot files are, the optional parts its data needs and its code never names (`features`: `"gltf"` for a scene's `model path=`, `"emitters"` for its emitter lines, `"characters"` for a body of type character, in code or a scene, `"bitmap_fonts"` for a font made from a `.fnt` or an image of glyphs; a program that meets one it didn't declare says which, and an export's smoke run fails on it), a web size budget if it wants one (`web_budget_kb`, gzipped, which `wgf export` then holds it to; none by default), and `defines`, if it has any: names (or name=value) given to every build of it as Haxe's `-D` (a bot or a telemetry build, read with `#if`), and `playthrough`, the autopilot file a game in libwgf's own `games/` is checked by (`playthrough.autopilot` by default). Its frame times are measured on `autopilot/bench.autopilot` when it has one (a few hundred frames of its busiest play, short enough for a software renderer), else on its playthrough: `python3 <libwgf>/tools/bench/measure_frames.py <this folder>`.
