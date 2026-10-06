# Building libwgf

CMake (3.21 or newer), Ninja, and Python 3 (3.9 or newer) build everything: the library, its tests, and the web builds. There is no make and no shell script; every tool is Python, standard library only.

## Requirements

What every build on a platform needs is under Required; the rest is only for its row, and without it that part is skipped, which `tools/verify_builds.py` says (`SKIPPING ...`).

### Linux

Required: CMake 3.21 or newer, Python 3.9 or newer, Ninja, a C compiler (gcc or clang, with their sanitizer runtimes for the sanitizer presets), and for the desktop presets the X11, OpenGL, and ALSA dev packages: `python3 tools/setup_system_packages.py install` installs them with apt, dnf, or pacman.

| For | Needs |
| --- | --- |
| the web presets | [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), exactly the version in `cmake/emscripten-version.txt` (5.0.7): `emsdk install 5.0.7 && emsdk activate 5.0.7`, then `$EMSDK` set or `emcc` on PATH. It brings node, which runs the web tests, and the clang the API check uses |
| the browser tests | a Chromium-based browser: Chrome, Chromium, Brave, or Edge |
| the native window tests | Xvfb and Mesa (OpenGL on the CPU) |
| the Windows presets, cross-built | MinGW-w64 (`x86_64-w64-mingw32-gcc`), and to run their tests Wine (`wine64`, or Proton from Steam) |
| the API check without Emscripten | clang on PATH; with neither, `check_api` is skipped |
| the Haxe binding's checks | [Haxe](https://haxe.org) 4.3.7 on PATH, and for its hxcpp test hxcpp (`haxelib install hxcpp 4.3.2`); the web steps need Emscripten and node too, and a browser for the page |

### Windows

Required: CMake 3.21 or newer, Python 3.9 or newer, and Visual Studio with the "Desktop development with C++" workload, for the MSVC presets: they use Visual Studio's generator, which finds the compiler, so no developer prompt. The MinGW presets need Ninja; their compiler is set up for you (`tools/setup_mingw.py`). The Haxe binding's hxcpp test needs Haxe and hxcpp, as on Linux; hxcpp uses MSVC, against the MSVC presets' archive.

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

A program runs a script of inputs and expectations (`app/src/wgf_app_script_priv.h` says the format) when `LIBWGF_SCRIPT` names one; on the web a page hands the module the script's text as `Module["wgfScript"]`.

## The web

Configuring a web preset finds Emscripten through `$EMSDK` or the `emcc` on PATH, and fails unless it is exactly the pinned version, naming the command that installs it: its patch releases change behavior and output, so a size means something only from the same compiler. `LIBWGF_EMSCRIPTEN_VERSION=<version>` allows another on purpose, said at every configure.

```sh
cmake --preset wasm32-debug && cmake --build --preset wasm32-debug && ctest --preset wasm32-debug
```

The tests run under node; the ones that need a real browser (IndexedDB, WebGL2's pixels) run in a headless Chromium-based browser through `tools/run_in_browser.py`, several visits in one browser context, and are skipped where there is none. `wasm32-debug-headless` builds the web with no canvas, Web Audio, or fetch, so every headless test runs under node, as the binding's does.

## The Haxe binding

```sh
python3 tools/gen_binding.py           # the binding's generated files, from the headers (--check: fail if stale)
python3 tools/build_host.py            # the full web host for a variant (default wasm32-release)
python3 tools/check_binding.py         # generated, coverage, and the test on hxcpp, under node, and in a browser
python3 tools/check_features.py        # the feature test, reaching every public call, on the same three
```

How the binding maps the C calls is [docs/BINDINGS.md](docs/BINDINGS.md); how to use it, [bindings/haxe/README.md](bindings/haxe/README.md).

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

`.github/workflows/ci.yml` runs on every push and pull request: the Linux presets (debug, release, headless, and the three sanitizers) with every example headless and in a window and the binding on hxcpp, the web presets with every example in the runner's Chrome and the binding under node and in Chrome, and the MSVC presets on Windows with every example headless and the binding on hxcpp, each through `tools/verify_builds.py --only`, so CI runs exactly what runs locally. The MinGW builds under Wine run locally only (`verify_builds.py`'s `smoke-mingw` and `desktop-mingw`, and `--windows HOST`).

## The tools

Every tool answers `--help` with what it does; `tools/check_tools.py` checks that they do.

| Tool | What it does |
| --- | --- |
| `check_api.py` | checks the public API's shape against CONVENTIONS through clang's parse of every public header (`headers.py`); `--self-test` runs it against a header that breaks every rule |
| `gen_binding.py` | writes the Haxe binding's generated files from the headers (`--check`: writes nothing, fails when one is stale) |
| `build_host.py` | links the web host a Haxe program runs on, the full one or a trimmed one (`--exports`) |
| `check_features.py` | runs the feature test (`examples/haxe/feature-test/`) on hxcpp, under node, and in a browser, failing a call it never reached |
| `check_binding.py` | checks the binding: generated, every call reached once, and its test on hxcpp, under node, and in a browser |
| `check_tools.py` | checks every tool is named for what it does, imports no script, answers `--help` and does nothing else, and refuses an argument it doesn't take |
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
| `gen_manifest.py` | writes the asset manifests for a directory tree (`wgf_asset_set_manifest`) |
| `finish_site.py` | finishes a web build's site: each example's page stamped with its program's version (`name.js?v=<hash>`), `examples.json`, and the launcher |
| `watch_browser.py` | the browser tools' watchdog: stops what a run started if the run can't |
| `setup_mingw.py` | sets up the pinned MinGW-w64 on Windows |
| `setup_system_packages.py` | checks for, or installs, the packages a Linux desktop build links |

Modules the tools share, with nothing to run: `examples.py` (building the examples against a staged variant), `headers.py` (the public API as clang reads it, in one parse), `browser.py` (finding and driving a Chromium-based browser over the DevTools protocol), `server.py` (serving a site as a static host would), `variants.py` (the presets, read from `CMakePresets.json`), `wine.py` (finding Wine), and `usercache.py` (the per-user cache).
