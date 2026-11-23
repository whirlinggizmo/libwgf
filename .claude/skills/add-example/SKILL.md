---
name: add-example
description: Add a C example to libwgf (examples/c/<layer>-<name>/) that builds against a staged variant, runs in the smoke run, a window, and the browser check; or a JS example under examples/js/, or a Haxe one under examples/haxe/. Use when a feature needs showing, or a check needs a program to run.
---

# Adding an example

Examples are programs outside libwgf, built against `out/` as anyone's would be (CONVENTIONS.md, "Tests and examples"). Every one runs headless (`tools/run_smoke.py`), in a window (`tools/check_desktop.py`), and in a browser (`tools/check_web.py`); none may log an error.

## A C example

1. **The directory**: `examples/c/<layer>-<name>/`, named for the layer it shows (`gfx-stage2d`, `ecs-scene`, `ui-menu`).
2. **`CMakeLists.txt`**, as the others:
   ```cmake
   cmake_minimum_required(VERSION 3.21)
   project(<layer>_<name> LANGUAGES C)
   include(../wgf_example.cmake)
   wgf_example(<layer>-<name> [ASSETS] main.c)
   ```
   `ASSETS` when it loads files from `examples/assets/`, which it then names with `wgf_asset_set_host("../assets")`.
3. **`main.c`**: public headers only (no sokol or Clay), a comment at its top saying what it shows and which keys do what, `wgf_window_set_size` before `wgf_app_run`, and Escape quitting only where `wgf_app_can_quit()`. It must start, draw, and keep running with no input, since the checks give it none.
4. **Its page**, `index.html`, is written from `examples/c/wgf_page.html` at the first web build; commit it, then it is the example's own.
5. **Assets** it needs go in `examples/assets/`, each one not made here credited in `examples/assets/CREDITS.md` with its license.
6. **Run the checks**: `python3 tools/run_smoke.py <name>`, `python3 tools/check_desktop.py <name>`, `python3 tools/check_web.py <name>`; read the screenshots they save (`build/<preset>/check_*/<name>.png`) to see it draws what it should.

## A JS example

`examples/js/<name>/`: `index.html` (a canvas and `<script type="module" src="./main.js">`, as `examples/js/hello/`'s), `main.js`, and any modules beside it, on the JS binding alone (`import * as wgf from "./wgf.js"`): make the host on the canvas, handing it `globalThis.wgfAutopilot` when there is one, `wgf.attach` it, and `wgf.wgf_app_run`. It has no files unless an `example.json` names others (`"assets"`), and its run is 120 frames unless it names an autopilot (`"autopilot"`), each a path from the root. `python3 tools/check_js_binding.py --only examples` flies it in a browser; `tools/measure_sizes.py` measures it as `js:<name>`, its host and binding trimmed to the calls and enums it names.

## A Haxe example

A Haxe example is a small game project, laid out as `wgf new` makes one, so a reader sees a real project: `examples/haxe/<name>/` with `wgf.json` (its `"playthrough": "smoke.autopilot"`), `src/Main.hx`, `autopilot/smoke.autopilot`, and `assets/` holding only the files it loads (a copy of what it needs from `examples/assets/`, credited in its `CREDITS.md`). Its `Main.hx` opens with a doc comment saying what it shows, written as CONVENTIONS.md's "Writing" asks, and publishes probes its smoke run checks. `python3 tools/check_games.py --examples <name>` flies its smoke run headless and in a browser and makes its web export; `tools/build_pages.py` puts it on the site, where the launcher offers it under Haxe. The feature test (`examples/haxe/feature-test/`) is not one of these: it is a single program the tools build. The set is curated, not one for each C example: add one where the feature reads differently in Haxe (HISTORY.md, "Examples in Haxe and JS").

Then verify, the tier CONVENTIONS.md's "Verifying" says (the quick tier, `python3 tools/verify_builds.py --quick`, before a commit).
