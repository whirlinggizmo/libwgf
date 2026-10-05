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

### Windows

Required: CMake 3.21 or newer, Python 3.9 or newer, and Visual Studio with the "Desktop development with C++" workload, for the MSVC presets: they use Visual Studio's generator, which finds the compiler, so no developer prompt. The MinGW presets need Ninja; their compiler is set up for you (`tools/setup_mingw.py`).

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

A preset's CMake work goes to `build/<preset>/`. What it makes for others goes to `out/<platform>/<config>[-<feature>]/` when it is staged (`python3 tools/stage_variant.py <preset>`): the public headers in `include/`, the archive in `lib/` (`libwgf.a`; MSVC's `wgf.lib`), and libwgf's LICENSE and THIRD_PARTY_NOTICES.md in `share/wgf/`. A program builds against `out/`, never `build/`.

Every preset treats warnings as errors. A project that builds libwgf as part of its own doesn't, so a newer compiler's new warning can't break it; `-DWGF_WERROR=ON` or `OFF` decides either way.

## Linux

```sh
cmake --preset linux-x64-debug-headless
cmake --build --preset linux-x64-debug-headless
ctest --preset linux-x64-debug-headless          # unit tests, header checks, check_api, check_tools
python3 tools/stage_variant.py linux-x64-debug-headless
```

The same four steps for any preset.

## The web

Configuring a web preset finds Emscripten through `$EMSDK` or the `emcc` on PATH, and fails unless it is exactly the pinned version, naming the command that installs it: its patch releases change behavior and output, so a size means something only from the same compiler. `LIBWGF_EMSCRIPTEN_VERSION=<version>` allows another on purpose, said at every configure.

```sh
cmake --preset wasm32-debug && cmake --build --preset wasm32-debug && ctest --preset wasm32-debug
```

The tests run under node; the ones that need a real browser (IndexedDB, WebGL2's pixels) run in a headless Chromium-based browser through `tools/run_in_browser.py`, several visits in one browser context, and are skipped where there is none.

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
```

It copies the working tree, committed or not, builds and tests there, and deletes its copy afterwards (`--keep` leaves it).

## Before calling a change done

```sh
python3 tools/verify_builds.py                         # this machine's presets, MinGW under Wine
python3 tools/verify_builds.py --web                   # and the web presets
python3 tools/verify_builds.py --web --windows HOST    # and MSVC and MinGW on a Windows machine over ssh
python3 tools/verify_builds.py --only linux-x64-debug-asan   # just these steps (--list shows them all)
```

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request: the Linux presets (debug, release, headless, and the three sanitizers), the web presets (with the runner's Chrome for the browser tests), and the MSVC presets on Windows, each through `tools/verify_builds.py --only`, so CI runs exactly what runs locally.

## The tools

Every tool answers `--help` with what it does; `tools/check_tools.py` checks that they do.

| Tool | What it does |
| --- | --- |
| `check_api.py` | checks the public API's shape against CONVENTIONS through clang's parse of every public header (`headers.py`); `--self-test` runs it against a header that breaks every rule |
| `check_tools.py` | checks every tool is named for what it does, imports no script, answers `--help` and does nothing else, and refuses an argument it doesn't take |
| `stage_variant.py` | stages a built preset into `out/`, fresh |
| `verify_builds.py` | every build and check this machine can run, in one command |
| `run_in_browser.py` | runs a wasm test in a real browser, over several visits (ctest uses it) |
| `run_in_xvfb.py` | runs a native test in a real window on a virtual display (ctest uses it) |
| `run_wine.py` | runs a Windows program under Wine (the MinGW presets' test runner), starting it again when Wine's launcher failed |
| `run_remote_windows.py` | builds and tests the working tree on a Windows machine over ssh |
| `watch_browser.py` | the browser tools' watchdog: stops what a run started if the run can't |
| `setup_mingw.py` | sets up the pinned MinGW-w64 on Windows |
| `setup_system_packages.py` | checks for, or installs, the packages a Linux desktop build links |

Modules the tools share, with nothing to run: `headers.py` (the public API as clang reads it, in one parse), `browser.py` (finding and driving a Chromium-based browser over the DevTools protocol), `server.py` (serving a site as a static host would), `variants.py` (the presets, read from `CMakePresets.json`), `wine.py` (finding Wine), and `usercache.py` (the per-user cache).
