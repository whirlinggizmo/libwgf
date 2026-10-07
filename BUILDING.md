# Building libwgf

CMake (3.21 or newer), Ninja, and Python 3 (3.12 or newer) build everything: the library, its tests, and the web builds. There is no make and nothing in shell; every tool is Python, standard library only.

## Requirements

What every build on a platform needs is under Required; the rest is only for its row, and without it that part is skipped, which `tools/verify_builds.py` says (`SKIPPING ...`).

### Linux

Required: CMake 3.21 or newer, Python 3.12 or newer, Ninja, a C compiler (gcc or clang, with their sanitizer runtimes for the sanitizer presets), and for the desktop presets the X11, OpenGL, and ALSA dev packages: `python3 tools/setup_system_packages.py install` installs them with apt, dnf, or pacman.

| For | Needs |
| --- | --- |
| the web presets | [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), exactly the version in `cmake/emscripten-version.txt` (5.0.7): `emsdk install 5.0.7 && emsdk activate 5.0.7`, then `$EMSDK` set or `emcc` on PATH. It brings node, which runs the web tests, and the clang the API check uses |
| the browser tests | a Chromium-based browser: Chrome, Chromium, Brave, or Edge |
| the native window tests | Xvfb and Mesa (OpenGL on the CPU) |
| the Windows presets, cross-built | MinGW-w64 (`x86_64-w64-mingw32-gcc`), and to run their tests Wine (`wine64`, or Proton from Steam) |
| the API check without Emscripten | clang on PATH; with neither, `check_api` is skipped |
| the Haxe binding's checks | [Haxe](https://haxe.org) 4.3.7 on PATH, and for its hxcpp test hxcpp (`haxelib install hxcpp 4.3.2`); the web steps need Emscripten and node too, and a browser for the page |

### Windows

Required: CMake 3.21 or newer, Python 3.12 or newer, and Visual Studio with the "Desktop development with C++" workload, for the MSVC presets: they use Visual Studio's generator, which finds the compiler, so no developer prompt. The MinGW presets need Ninja; their compiler is set up for you (`tools/setup_mingw.py`). The Haxe binding's hxcpp test needs Haxe and hxcpp, as on Linux; hxcpp uses MSVC, against the MSVC presets' archive.

### Set up by the tools

Nothing is fetched at build time. What the tools set up the first time it is needed is downloaded, checked against its SHA-256, and kept in a per-user cache shared by every checkout -- `~/.cache/libwgf` on Linux, `%LOCALAPPDATA%\libwgf` on Windows, or `$LIBWGF_CACHE_DIR` -- and is safe to delete: on Windows the pinned MinGW-w64 (`tools/setup_mingw.py`), and on Linux the Wine prefix the MinGW presets' tests run in (`tools/run_wine.py`).

## Presets

Every build is a preset in `CMakePresets.json`, named `<platform>-<config>[-<feature>]` (CONVENTIONS, "Build"). Each machine lists the ones it can build: `cmake --list-presets`.

| Host | Preset | What it makes |
| --- | --- | --- |
| Linux | `linux-x64-debug` | the library and its tests, windowed |
| | `linux-x64-release` | optimized: benchmarks, and a game's desktop export |
| | `linux-x64-debug-headless` | no window or GPU (sokol's dummy backend), so every test runs anywhere |
| | `linux-x64-debug-tsan`, `-asan`, `-ubsan` | the headless tests under ThreadSanitizer, AddressSanitizer with LeakSanitizer, and UndefinedBehaviorSanitizer |
| Linux, Windows | `windows-x64-mingw-debug`, `-debug-headless`, `-release` | Windows with MinGW-w64: cross-built on Linux, tests under Wine |
| Windows | `windows-x64-msvc-debug`, `-debug-headless`, `-release` | Windows with MSVC, through Visual Studio's generator |
| Linux, Windows | `wasm32-debug`, `wasm32-release` | the web, WebGL2; tests under node, and in a browser where they need one |
| Linux, Windows | `wasm32-debug-headless` | the web with no canvas, Web Audio, or fetch: the native mixer (no device), asset layer, and file storage, in the wasm's own file system; every headless test, the binding's, and the feature test, under node |

A preset's CMake work goes to `build/<preset>/`. What it makes for others goes to `out/<platform>/<config>[-<feature>]/` when it is staged (`python3 tools/stage_variant.py <preset>`): the public headers in `include/`, the archive in `lib/` (`libwgf.a`; MSVC's `wgf.lib`), and libwgf's LICENSE and THIRD_PARTY_NOTICES.md in `share/wgf/`. A program builds against `out/`, never `build/`.

Every preset treats warnings as errors. A project that builds libwgf as part of its own doesn't, so a newer compiler's new warning can't break it; `-DWGF_WERROR=ON` or `OFF` decides either way.

## Linux

```sh
cmake --preset linux-x64-debug-headless
cmake --build --preset linux-x64-debug-headless
ctest --preset linux-x64-debug-headless          # unit tests, header checks, check_api, check_tools
python3 tools/stage_variant.py linux-x64-debug-headless
```

The same four steps for any preset. A headless build runs frames at 60 a second until the program quits, or for `LIBWGF_HEADLESS_FRAMES` frames when that is set.

The C examples, in `examples/c/<layer>-<name>/`, are built against a staged variant, as a program outside libwgf would be. Three tools build, stage, and run them all, or the ones named:

```sh
python3 tools/run_smoke.py        # headless, 180 frames each: fails on a crash, a hang, or an error log
python3 tools/check_desktop.py    # in a window on a virtual display (Xvfb): fails on an error or an early exit,
                                  # and saves a screenshot of each in build/<preset>/check_desktop/
python3 tools/check_desktop.py --variant windows-x64-mingw-debug   # the Windows build, under Wine, the same way
python3 tools/check_web.py        # in a headless browser: fails one that doesn't start or logs an error,
                                  # and saves a screenshot of each in build/<preset>/check_web/
```

A program flies an autopilot -- a file of inputs and expectations ("Autopilot files", below) -- when `LIBWGF_AUTOPILOT` names one; on the web a page hands the module the file's text as `Module["wgfAutopilot"]`.

## The web

Configuring a web preset finds Emscripten through `$EMSDK` or the `emcc` on PATH, and fails unless it is exactly the pinned version, naming the command that installs it: its patch releases change behavior and output, so a size means something only from the same compiler. `LIBWGF_EMSCRIPTEN_VERSION=<version>` allows another on purpose, said at every configure.

```sh
cmake --preset wasm32-debug && cmake --build --preset wasm32-debug && ctest --preset wasm32-debug
```

The tests run under node; the ones that need a real browser (IndexedDB, WebGL2's pixels) run in a headless Chromium-based browser through `tools/run_in_browser.py`, several visits in one browser context, and are skipped where there is none. `wasm32-debug-headless` builds the web with no canvas, Web Audio, or fetch, so every headless test runs under node, as the binding's does.

## Shaders

gfx's shaders (`gfx/src/shaders/*.glsl`) are written once in sokol-shdc's annotated GLSL and compiled into headers for GL 4.1 and WebGL2 by `gen_shaders.py`, which are committed: building never runs the compiler, the pinned sokol-shdc that `setup_shdc.py` downloads.

```sh
python3 tools/gen_shaders.py           # every shader's header again, after changing its GLSL (--check: fail if stale)
python3 tools/setup_shdc.py            # the pinned sokol-shdc, downloaded once into the per-user cache; prints its path
```

## The bindings

```sh
python3 tools/gen_binding.py           # both bindings' generated files, from the headers (--check: fail if stale)
python3 tools/build_host.py            # the full web host for a variant (default wasm32-release), wgf.js beside it
python3 tools/check_js_binding.py      # the JS binding: generated, its types, and every JS example in a browser
python3 tools/check_binding.py         # the Haxe binding: generated, coverage, and its test on hxcpp, node, a browser
python3 tools/check_features.py        # the feature test, reaching every public call, on the same three
python3 tools/bench/measure_calls.py   # what a call costs from Haxe and from JS, through the JS binding
python3 tools/bench/measure_frames.py --write   # each game's frame times, on the reference machine, into the benchmarks
```

How the bindings map the C calls is [docs/BINDINGS.md](docs/BINDINGS.md); how to use them, [bindings/js/README.md](bindings/js/README.md) and [bindings/haxe/README.md](bindings/haxe/README.md). The JS binding's type test needs TypeScript 7.0.2 (`npm install -g typescript@7.0.2`, or `TSC` naming a tsc), the version CI installs; without it the step is skipped, and says so.

## Games: the `wgf` tool

`wgf` (at the repository's root; `python wgf` on Windows) makes and works on a game, run in the game's directory. To run it as `wgf` from anywhere, put the repository on PATH (`export PATH="$PATH:$HOME/path/to/libwgf"`, in your shell's profile to keep it); on Windows, `python <libwgf>\wgf` is the command wherever `wgf` is written below. `wgf new` says which to type.

```sh
./wgf new ~/games/rocks             # a game from templates/game/
cd ~/games/rocks
wgf serve                           # in a browser, reloaded as its Haxe is saved, its state kept
wgf build --web | --desktop | --headless    # into build/<target>/
wgf run [--headless] [--frames N | --autopilot FILE]   # the desktop build
wgf autopilot autopilot/smoke.autopilot [--web]   # an autopilot run: PASS or FAIL
wgf screenshot --frame 60 [--autopilot FILE]   # the web build at a frame, as a PNG
wgf dump --frame 60 [--autopilot FILE]         # the ecs's world, as a scene's text
wgf export                          # export/web (a trimmed host) and export/desktop, smoke-tested
```

`wgf --help`, and each command's, says the rest. `wgf screenshot` and `wgf dump` with `--autopilot` leave out the file's own screenshot and dump lines, and say so, so the one asked for is the one made. A game names no libwgf: it is built against the libwgf whose `wgf` runs, from its staged variants. `wgf serve` builds through hotreload-hx (`deps/hotreload-hx`), vendored, so nothing is installed for it; Haxe 4.3.7 and hxcpp 4.3.2 are what the rest need.

### Autopilot files

An autopilot flies a program with no one at it: inputs at frames, and expectations on probes (`wgf_probe.h`), the numbers a program publishes. `wgf autopilot`, `wgf run --autopilot`, and the checks run them; natively the file `LIBWGF_AUTOPILOT` names is flown, and on the web a page hands the module the file's text as `Module["wgfAutopilot"]`. A line each, `#` starting a comment:

```
wgf-autopilot 1                 the first line: the format and its version
seed <int>                      wgf_random's seed, set before the program's init
at <frame> <command>            a command at a frame, frames counted from 0
```

where a command is one of

```
key down|up|tap <key>           a key by name (wgf_keyboard.h's, lower case: a, 1, space, enter,
                                left, left_shift, f1...); tap is down at this frame and up at the next
text <characters>               typed characters (UTF-8, the rest of the line)
mouse move <x> <y>              the pointer, in logical pixels
mouse down|up|click <button>    left, right, or middle; click is down, then up the next frame
mouse scroll <dx> <dy>
pad <n> connect|disconnect      pad 0 to 3 (an autopilot's pads replace the real ones)
pad <n> down|up|tap <button>    wgf_gamepad.h's, lower case: south, dpad_up, start...
pad <n> axis <axis> <value>     left_x, left_y, right_x, right_y (-1 to 1), left_trigger,
                                right_trigger (0 to 1)
expect <probe> <op> <number>    after the frame: ==, !=, <, <=, >, or >= against a probe;
                                a probe not set fails
wait <probe> <op> <number>      after the frame, until the probe holds: the autopilot stays at this
                                frame while the program's frames go on, so every later line keeps its
                                distance from the wait (a load's end, which takes real time, comes at
                                the same point on every machine); past 30 real seconds, a failure,
                                and the autopilot goes on
log <text>                      the text, logged, to mark a point in the run
screenshot <name>               "wgf_autopilot: SCREENSHOT <name>" logged, for a tool watching the
                                run to save the frame (wgf screenshot does)
dump                            after the frame, each optional part's state as text (the ecs's
                                world, as a scene), logged a line at a time
end                             after the frame: the run's result logged, and quit
```

For example, a game started, a thrust and a shot, and what should follow:

```
wgf-autopilot 1
seed 1
at 1 wait ready == 1            # the game's files loaded, however long that takes here
at 30 key tap enter             # start the game
at 40 key down up               # thrust...
at 100 key up up
at 100 key tap space            # ...and fire
at 101 expect bullets >= 1
at 120 end
```

Inputs at a frame are delivered before its ticks; expectations are checked after it. While an autopilot runs, time is the autopilot's: every frame lasts a sixtieth of a second, whatever the display does, so ticks, and the random numbers a seed gives, make the same run everywhere, and a headless run doesn't wait for a display. The run's result is logged, "wgf_autopilot: PASS (0 of <n> expectations failed, <frames> frames)", or FAIL with how many failed, with an error for each expectation that failed, naming the probe and its value; an autopilot that can't be read, or a program that quits before its end, is an error too. Errors are what the tools judge a run by (on the web there is no exit code).

### Scene files

A scene is a text file of actors -- their kinds, components, and behaviors -- and prefabs, the trees a game makes at run time (`wgf_scene.h`). `wgf_ecs_dump` and `wgf dump` write the simulated actors in the same format, so a dump loads again as a scene. Like Godot's `.tscn` it is a line per fact, so a scene diffs and merges line by line; and like `.tscn` a tree is written flat, each actor a block naming its parent by path, never blocks inside blocks. A line each, `#` starting a comment, words split by spaces:

```
wgf-scene 2                     the first line: the format and its version
prefab <path>                   a prefab's actor, until its end: made by wgf_scene_spawn
actor [<path>]                  an actor made by wgf_scene_instantiate, until its end
  from <prefab>                 (first) starting as that prefab's tree, the lines after it
                                changing its top actor
  <kind> key=value ...          what the actor is: one kind line at most, none a plain actor
  <component> key=value ...     a component, with its settings
  behavior name=<name> key=value ...   a behavior added, the other keys its parameters;
                                without name=, the last one's parameters changed
end
```

A path is the actor's name, or its parent's path, `/`, and its name: `prefab ship` and then `prefab ship/flame` is the ship with an actor named `flame` under it. A parent comes before its children in the file, and the two are both prefabs or both actors; an `actor` with no name has no children. An actor's name is the one `wgf_actor_find` and `wgf_stage2d_find` look for. Settings not given keep their defaults; a kind or component line given twice changes the first.

A value is a number, a list of numbers (`1,2,3`), a color (`#RRGGBB` or `#RRGGBBAA`), a word (`true`, `wrap`), or text in double quotes (`"a \"b\" \\ c"`, with `\"` and `\\` escapes). A behavior's parameter starting with `@` refers to another actor: `@start_gate` is the one actor so named on the stage, `@car/wheel_rl` a path from it, `@./flame` a path from the actor itself, `@../gun` from its parent. It is found once, when the scene's actors are all made (or the prefab is spawned), and kept: the behavior reads it with `wgf_behavior_get_param_actor`; one that finds none is warned. The kinds and their keys:

```
shape2d    rectangle=w,h | circle=r | line=x0,y0,x1,y1 | polygon=x,y,x,y,...
           outline=t  color=#..  pivot=x,y
sprite     texture="path"  source=x,y,w,h  size=w,h  pivot=x,y  tint=#..
text       string="..."  font="path"  size=s  color=#..  wrap=w
           align=left|center|right,top|middle|bottom
emitter2d  rate=r  emitting=true|false  capacity=n  life=min,max  direction=a  spread=s
           speed=min,max  radius=r  gravity=x,y  drag=d  size=start,end  color=#..,#..
           stretch=s  burst=n (that many at once, as it is made)
model      a generated mesh (wgf_mesh.h), its create call's parameters in order:
           plane=w,l,subdivisions | cube=w,h,l | sphere=r,rings,segments |
           cylinder=r,h,segments | cone=r,h,segments | capsule=r,h,rings,segments |
           torus=r,thickness,rings,segments; and tint=#.. (made once a stage3d has been,
           as it is drawn on one: before, a plain actor, warned)
```

and the transform and the components:

```
transform  position=x,y,z  rotation=x,y,z (radians)  scale=x,y,z
motion     velocity=x,y,z  spin=x,y,z  damping=d  max_speed=s
bounds     rect=x,y,w,h  mode=wrap|clamp|destroy  margin=m  visible=true|false
lifetime   seconds=s
collider   radius=r  layer=bits  mask=bits  enabled=true|false
voice      sound="path"  streamed=true|false  volume=v  pitch=p  pan=p  loop=true|false
           play=true|false (played as it is made)
```

For example, a ship with its flame, a rock prefab and a smaller one from it, and a ship placed:

```
wgf-scene 2
prefab ship
  shape2d polygon=18,0,-12,-11,-6,0,-12,11 outline=2 color=#7FD4FF
  motion damping=0.35 max_speed=420
  behavior name=Ship lives=3 target=@start_gate
end
prefab ship/flame
  emitter2d rate=70 emitting=false life=0.12,0.3 direction=3.14159
end

prefab rock
  shape2d circle=40 outline=2
  collider radius=40 layer=2 mask=5
  behavior name=Rock size=3
end
prefab rock_small
  from rock
  collider radius=11
  behavior size=1
end

actor start_gate
  transform position=400,300,0
end
```

Paths in values name files as `wgf_texture_create` and the others take them, through the asset layer. A file of more than 4 MB, a line of more than 4096 bytes, an unknown line, kind, component, or key, a second kind, a value of the wrong shape, a duplicate or unknown prefab, a path whose parent isn't above it, a block inside another, a tree deeper than 32, or a block left without its `end`, is refused: the scene FAILED, each bad line logged with its number.

Room is kept for structured values (milestone 3's lists and nested data) without giving up a line per fact: a key with `.`, `[`, or `]` in it (`wheels[0].radius=0.34`) is refused now, so it can mean a field of a list or a record later and no file written today reads differently then.

**Version 1** (`wgf-scene 1`, before actors) is refused with a line saying so; `tools/update_scene.py FILE...` updates a file in place. The rule: `entity` becomes `actor`; a block's first kind line is its actor's own, and each further one becomes an actor under it named after its kind (`actor ship/emitter2d`), as a version 1 entity drew each of its kinds from its own place; an unnamed entity with such a part is named `_<n>`; everything else stays the actor's. A block `from` a prefab that adds a kind its prefab hasn't is left for a hand to write, the line named. A JSON form of a scene, made and read outside the runtime, is ROADMAP's.

## Windows

From Linux, with MinGW-w64:

```sh
cmake --preset windows-x64-mingw-debug-headless && cmake --build --preset windows-x64-mingw-debug-headless
ctest --preset windows-x64-mingw-debug-headless   # under Wine
```

The tests run through `tools/run_wine.py`: `$WINE`, else `wine64` or `wine` on PATH, else the newest Proton in a Steam library; its prefix is in the per-user cache unless `WINEPREFIX` says otherwise. On Windows, the MinGW presets build with the pinned gcc (`tools/setup_mingw.py`, which configuring runs), or `LIBWGF_MINGW_BIN`'s on purpose.

To build and test on a Windows machine from Linux without pushing, over ssh:

```sh
python3 tools/run_remote_windows.py HOST           # the MinGW debug presets
python3 tools/run_remote_windows.py HOST --msvc    # the MSVC debug presets
python3 tools/run_remote_windows.py HOST --msvc --then "tools/run_smoke.py --variant windows-x64-msvc-debug-headless"
```

It copies the working tree, committed or not, builds and tests there, runs what `--then` names, and deletes its copy afterwards (`--keep` leaves it).

## Before calling a change done

```sh
python3 tools/verify_builds.py                         # this machine's presets, MinGW under Wine
python3 tools/verify_builds.py --web                   # and the web presets
python3 tools/verify_builds.py --web --windows HOST    # and MSVC and MinGW on a Windows machine over ssh
python3 tools/verify_builds.py --only linux-x64-debug-asan   # just these steps (--list shows them all)
```

## Continuous integration

`.github/workflows/pages.yml` builds every game's web export on every push to `main` (`tools/build_pages.py`) and deploys it to GitHub Pages, at <https://whirlinggizmo.github.io/libwgf/>. `.github/workflows/ci.yml` runs on every push and pull request: the Linux presets (debug, release, headless, and the three sanitizers) with every example headless and in a window and the binding on hxcpp, the web presets with every example in the runner's Chrome, the Haxe binding under node and in Chrome, and the JS binding's types and examples, and the MSVC presets on Windows with every example headless, the binding on hxcpp, and each game's playthrough headless (the runner has no GPU, so nothing runs in a window there; a game's desktop export runs in one on Windows in `verify_builds.py --windows HOST`), each through `tools/verify_builds.py --only`, so CI runs exactly what runs locally. The MinGW builds under Wine run locally only (`verify_builds.py`'s `smoke-mingw` and `desktop-mingw`, and `--windows HOST`).

## The tools

Every tool answers `--help` with what it does; `tools/check_tools.py` checks that they do.

| Tool | What it does |
| --- | --- |
| `check_api.py` | checks the public API's shape against CONVENTIONS through clang's parse of every public header (`headers.py`); `--self-test` runs it against a header that breaks every rule |
| `gen_binding.py` | writes both bindings' generated files from the headers, the JS binding's and the Haxe binding's (`--check`: writes nothing, fails when one is stale) |
| `build_host.py` | links the web host a JS or Haxe program runs on, the full one or a trimmed one (`--exports`), with the JS binding beside it, whole or trimmed alike |
| `measure_sizes.py` | measures every example's (C and JS) and game export's release web size (wasm and JS, raw, gzip, brotli, and the JS binding's share), beside libwgt's and wgrender-c's (`--references`), into `docs/benchmarks.md` (`--write`); `--check` fails a program grown past the baseline |
| `check_games.py` | checks every game in `games/`: its generated files current, its playthrough headless and in a browser, its web export within budget, its desktop export |
| `build_pages.py` | builds the GitHub Pages site: every game's web export, smoke-tested and within budget, and a page linking them |
| `gen_sounds.py` | writes Asteroids' sounds (`games/asteroids/assets/sounds/`) from their synthesis, the same bytes every time (`--check`) |
| `check_cli.py` | runs each `wgf` command on a game it makes from the template, judging what each made and said; `serve` is edited while it runs, and must keep its state |
| `check_features.py` | runs the feature test (`examples/haxe/feature-test/`) on hxcpp, under node, and in a browser, failing a call it never reached |
| `check_binding.py` | checks the Haxe binding: generated, every call reached once, and its test on hxcpp, under node, and in a browser |
| `check_js_binding.py` | checks the JS binding: generated, its declarations under TypeScript (`bindings/js/tests/types.ts`), and every JS example (`examples/js/`) in a browser, flown by its autopilot |
| `measure_frames.py` | (in `tools/bench/`) flies each game's web export with its autopilot in a browser and traces each frame's main-thread work and the garbage collections; `--write` records them in `docs/benchmarks.md` beside the sizes, from the reference machine (the GPU under Xvfb, the CPU throttled 4 times) |
| `measure_calls.py` | (in `tools/bench/`) times a call into the host from Haxe and from JS, both through the JS binding, in a browser on the release host (not a check: timing) |
| `check_docs.py` | checks the docs against the code: every link resolves, ARCHITECTURE names every public header, this file every tool and preset, README every doc, deps/README every vendored directory (ctest runs it) |
| `check_tools.py` | checks every tool is named for what it does, imports no command, answers `--help` and does nothing else, and refuses an argument it doesn't take |
| `stage_variant.py` | stages a built preset into `out/`, fresh |
| `verify_builds.py` | every build and check this machine can run, in one command |
| `run_in_browser.py` | runs a wasm test in a real browser, over several visits (ctest uses it) |
| `run_in_xvfb.py` | runs a native test in a real window on a virtual display (ctest uses it) |
| `run_wine.py` | runs a Windows program under Wine (the MinGW presets' test runner), starting it again when Wine's launcher failed |
| `run_remote_windows.py` | builds and tests the working tree on a Windows machine over ssh, then runs a tool there with `--then` |
| `run_smoke.py` | runs every example headless, failing a crash, a hang, an error log, or a sokol panic; a Windows variant on Linux under Wine |
| `check_desktop.py` | runs every example in a window on Xvfb, a Windows variant under Wine there, with screenshots |
| `check_web.py` | runs every example in a headless browser, each in a context of its own, checking it once its loads are done, with screenshots |
| `check_asset_cache.py` | visits the asset cache's test page again and again in one browser context, judging each visit by its requests, log, and screen (ctest runs it on the web presets) |
| `check_stream.py` | serves a streamed sound slowly to its test page and checks each case: played as it arrives, from the cache, after a 304, through a redirect (ctest runs it on the web presets) |
| `update_scene.py` | updates scene files from `wgf-scene 1` to `wgf-scene 2`, in place, by the rule in "Scene files" (`--check`: fails naming a file still at version 1) |
| `gen_manifest.py` | writes the asset manifests for a directory tree (`wgf_asset_set_manifest`) |
| `finish_site.py` | finishes a web build's site: each example's page stamped with its program's version (`name.js?v=<hash>`), `examples.json`, and the launcher |
| `watch_browser.py` | the browser tools' watchdog: stops what a run started if the run can't |
| `setup_mingw.py` | sets up the pinned MinGW-w64 on Windows |
| `setup_system_packages.py` | checks for, or installs, the packages a Linux desktop build links |

Modules the tools share, with nothing to run: `examples.py` (building the examples against a staged variant), `headers.py` (the public API as clang reads it, in one parse), `jsbinding.py` (the JS binding's text, and its trimming for an export), `webhost.py` (linking the web host, the JS binding beside it, and a JS example's site), `browser.py` (finding and driving a Chromium-based browser over the DevTools protocol), `server.py` (serving a site as a static host would), `variants.py` (the presets, read from `CMakePresets.json`), `wine.py` (finding Wine), and `usercache.py` (the per-user cache).
