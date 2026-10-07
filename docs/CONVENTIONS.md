# libwgf conventions

The rules for changing libwgf: layout, naming, the public API, builds, dependencies, tools, and docs. For developers and agents. This file states current rules only; why each is so is in [HISTORY.md](HISTORY.md). libwgf follows libwgt's conventions (its `docs/CONVENTIONS.md`) except where HISTORY.md records a decision otherwise; this file is libwgf's own statement of them, and the one to follow.

## Layout

```
libwgf/
  CMakeLists.txt  CMakePresets.json  VERSION
  README.md  AGENTS.md  BUILDING.md  LICENSE  THIRD_PARTY_NOTICES.md
  wgf             the command-line tool's launcher (tools/wgf/)
  include/        wgf_api.h, the export macro every public header uses
  math/ core/ platform/ asset/ gfx/ audio/      the layers
  ecs/ ui/                                      the optional modules
  app/                                          the runtime
    include/      public headers
    src/          implementation and private headers
    tests/
  hosts/web/      the web host JS and Haxe programs run on
  bindings/js/    the JS binding (wgf.js, wgf.d.ts), the one way JS reaches the host
  bindings/haxe/  the Haxe binding (haxelib wgf), built on the JS binding on the web
  examples/c/<layer>-<name>/ C examples; examples/js/<name>/ and examples/haxe/<name>/
  games/<name>/   the games
  templates/      what `wgf new` copies
  deps/           vendored third-party code
  tools/          Python tools
  docs/           CONVENTIONS, ARCHITECTURE, HISTORY, ROADMAP
  .claude/skills/ skills for repeated work
  build/          scratch (Build, below)
  out/            staged artifacts (Build, below)
```

## Layers and dependencies

libwgf is one library, `libwgf.a` (`wgf.lib` with MSVC), built in layers and modules: directories that depend on each other in one direction. A program runs through app's runtime (`wgf_app_run`), which starts and drives everything below it.

| Layer | Role | May depend on |
|------|------|---------------|
| math | vectors, matrices, and the math on them | nothing |
| core | handles, logging, time, files, loading, resources, probes, random numbers | math |
| platform | the window, its events, input, an autopilot's input | core, math |
| asset | where a resource's file comes from | core, math |
| gfx | drawing | platform, asset, core, math |
| audio | sounds and voices | platform, asset, core, math |
| ecs (module) | components and behaviors on actors, systems, scenes | gfx, audio, platform, asset, core, math |
| ui (module) | layout and widgets | gfx, platform, asset, core, math |
| app | the runtime: run, the loop, ticks | any other |

- Dependencies point down only. A layer never includes a header of a layer it may not depend on; each layer is one CMake object target (`wgf_layer()`) that sees only its dependencies' headers, so a forbidden include fails to build.
- A layer may use the private headers of the layers it depends on, through their never-installed `wgf_<layer>_priv` targets.
- A layer asks the layers under it for what it needs; nothing is pushed down into it from above.
- **An optional part is reached through its hook.** The frame, the runtime, and the stops never call an optional part by name: the part installs itself (`wgf_core_priv_part_install`, and a layer's own hooks) the first time a program creates one of it, and they call through the hook, so a program links only the parts it creates. Nothing runs before `main` except a constructor that only fills a static table (`WGF_CORE_PRIV_ON_LINK`), in an object only its part references. A module is a part.
- core has no window. sokol's implementations are compiled once, in the platform layer (`wgf_platform_sokol_impl.c`), and audio's in audio; sokol's log goes to core's log.

## Naming

| Thing | Form | Example |
|-------|------|---------|
| Public function | `wgf_<section>_<action>` | `wgf_actor_set_position` |
| Private function | `static`, or `wgf_<layer>_priv_*` | `wgf_gfx_priv_end_frame` |
| Type | `wgf_<section>[_<name>]_t` | `wgf_log_level_t` |
| Handle type | `wgf_<kind>_t`, a typedef of `wgf_handle_t` | `wgf_texture_t` |
| Macro, constant, enum value | `WGF_<SECTION>_*` | `WGF_LOG_LEVEL_INFO` |
| Include guard | `WGF_<FILE>_H` | `WGF_NODE_H` |
| Public header | `wgf_<section>.h`; `wgf.h` for the version | `wgf_actor.h` |
| Source file | `wgf_<layer>_<section>.c`, in the layer's `src/` | `wgf_gfx_actor.c` |
| Private header | `wgf_<layer>_<section>_priv.h` | `wgf_gfx_actor_priv.h` |
| Platform source | `wgf_<layer>_<section>_<platform>.c` | `wgf_core_fs_web.c` |
| Haxe type | `wgf.<Section>` | `wgf.Actor` |
| C example dir | `<layer>-<name>` | `gfx-shapes` |

- Public names don't carry the layer, except the layer-wide calls of app, asset, audio, ecs, and ui (`wgf_app_run`, `wgf_ui_begin`). Section names are unique across layers, since they are the public names. A section named after its layer lives in `<layer>/src/wgf_<layer>.c`.
- Platform-specific code goes in its own file with a platform suffix: `_web`, `_native`, `_posix`, `_windows`, `_linux`, `_macos`, `_headless` (the build with no window), `_none` (a section with nothing to use). CMake picks the files. Shared files contain no platform `#if`.
- Inside libwgf, a pointer resolved from a handle is `<noun>_ptr`.
- No double underscores, and no leading underscore followed by a capital.
- Repo and directory names group with dashes; code identifiers use underscores.
- An archive's file name never carries the platform, config, or feature; the variant is in its directory.

## Public API

**The hard rule.** Every public parameter and return value is one of:

- a handle (`wgf_handle_t`, or a handle type of it);
- an integer, float, bool, or enum;
- a `const char *` for a path or text (UTF-8, NUL-terminated);
- a math value (`wgf_vec2_t`, `wgf_vec3_t`, `wgf_vec4_t`, `wgf_mat3_t`, `wgf_mat4_t`), **as a return value only**; a setter takes its components;
- a byte span: `const unsigned char *data, int size` in, or a `_get_data` beside a `_get_size` out;
- a caller-owned array of numbers or handles, followed by its `int count` (`int <name>_count` for a call's second): `const float *` or `const int *` or a handle type's `const wgf_<kind>_t *` read, or the same without `const` filled, within the call.

No other pointer, no struct, no function pointer, no `void *`, no variadic call. A byte span is opaque: the layer never reads it as a structure. In: copied before the call returns. Out: owned by the task or handle that produced it, valid until that is destroyed. An array is the caller's: the layer reads or fills it during the call and never keeps it, and a call that fills one returns how many it filled.

**Bulk calls for hot paths.** A call that reads or writes many objects takes a caller-owned array and a count, filled or read in one call (SPEC's invariant): never a scratch area the library shares between calls, whose results the next call overwrites, and never a command stream within a process. Per-actor work every tick runs as a C system (ecs); a binding sets intent, it doesn't drive every transform every frame.

**One exception: `wgf_app_run`**, whose callbacks are `wgf_app_callback_t`, `void (*)(void *user)`, because the window system owns the loop. `tools/check_api.py` lists it in `CALLBACKS_ALLOWED`; another is a decision recorded in HISTORY.md, not a convenience.

**Handles are typed by kind.** A public handle parameter or return uses the kind's typedef (`wgf_texture_t`), never bare `wgf_handle_t`, except a call that takes any kind (`wgf_handle_get_kind_name`, `wgf_resource_release`). Actors of every kind are `wgf_actor_t`. A handle is valid only in the running program: never saved, never sent. 0 is none.

**Shape of calls.**

- Every value a setter stores has a getter: `set_<value>` with `get_<value>`, or `is_<value>` for a bool; a setter of several values has a getter per value or one returning their vector. A getter returns 0 (or its documented none) for a handle that isn't valid. An exception is listed with its reason in check_api's `GETTERS_EXEMPT`.
- A transform is set part by part (position, rotation, scale), each leaving the others as they are, and all at once with `set_transform`.
- What plays over time has one state, `wgf_play_state_t` (STOPPED, PLAYING, PAUSED, COMPLETE), and four calls: play, pause, resume, stop, each false where it doesn't apply.
- **Say clamp or refuse.** A setter clamps when every value in range is the same request at another fidelity; it refuses, returning false, when the value would change what was asked or has no meaning. Its comment says which, and a "false for ..." sentence names every refusal.
- Predicates: `is_<state>` (now), `has_<noun>` (whether something exists), `can_<verb>` (whether an action is possible), all `bool`.
- **Resources and objects.** A resource is shared data (a texture, a font, a sound, a scene file), created from a path, loading on create, reference counted, released with `wgf_resource_release`; `wgf_resource_get_status` answers NONE, PENDING, READY, or FAILED, and a resource is usable while PENDING. An object is owned (an actor, a voice), created from a resource handle or from nothing, and destroyed with its own `_destroy`; it holds a reference to each resource it uses. No `_create_from_memory`.
- **Asynchronous work** returns a task handle, polled for its status (an enum per kind of task: NONE 0, PENDING 1, then its outcomes), read through getters, then destroyed. A status changes only in the runtime's update at the start of a frame. An operation that could wait anywhere is asynchronous everywhere. Nothing calls back. A section exposes no readiness check: a request made early is queued.
- **Failures are visible.** A failed load logs one error naming the file, and what uses it draws a placeholder. Nothing fails silently.

**Other rules.**

- Public headers and examples include no backend header (sokol, Clay) and name no backend identifier.
- Symbols are hidden by default; only `wgf_*` marked `WGF_API` are exported. Every public header compiles on its own.
- A binding sees only exported functions and enums. Macros and `static inline` functions only rename or wrap exported functions; constants are enum values. A public function may be `inline` in its header with one `extern inline` in the layer's source.
- The public API behaves the same on every platform; differences stay inside.

## Code

- C11, compiled with `-Wall -Wextra -Wpedantic` (`/W4` with MSVC), warnings as errors.
- Four spaces, no tabs; lines up to 120 columns; a function's opening brace on its own line, a block's on the line of its statement. Comments are `/* */`, sentences, saying what and why, not how.
- A public header comment says what the call does now, and is changed in the commit that changes the behavior. When a header and the code disagree, the header is the bug.
- Untrusted input (files, autopilot files, scene text) is checked before it is read: sizes and offsets bounded, numbers parsed with their range checked. A parser refuses rather than guessing.

## Build

- CMake, with every variant defined once in `CMakePresets.json`, named `<platform>-<config>[-<feature>]`: platforms `linux-x64`, `windows-x64-mingw`, `windows-x64-msvc`, `wasm32`; configs `debug`, `release`; features `headless` (no window or GPU), `asan`, `ubsan`, `tsan`.
- `build/<preset>/` holds scratch trees: delete freely. `out/<platform>/<config>[-<feature>]/{include,lib,share}` holds staged artifacts, written by `tools/stage_variant.py <preset>` after a successful build. Bindings, examples, and games read `out/`, never `build/`.
- The web uses exactly the Emscripten in `cmake/emscripten-version.txt`.
- MSVC builds use the static C runtime (`/MT`, `/MTd`).

## Dependencies

- Third-party source is copied into `deps/`, pinned, and never fetched at build time. `deps/README.md` lists each with its version, license, and how to update it; `THIRD_PARTY_NOTICES.md` has each license in full and what a binary must ship.
- An altered vendored file says so at its top and is listed in THIRD_PARTY_NOTICES.md; a change of libwgf's is marked `[libwgf]`, one carried from another project keeps its mark.
- Haxe dependencies are pinned in `bindings/haxe/haxelib.json`. The JS binding depends on nothing; TypeScript, a tool its type test runs, is pinned in `tools/check_js_binding.py` (`TYPESCRIPT`), which CI installs.

## Versioning

- One version for the repository, in `VERSION`; the library and its bindings change together in one commit.

## Bindings

- How a binding maps the C calls is [BINDINGS.md](BINDINGS.md)'s, stated only there.
- Binding code that can be generated from the headers is generated, and the generated files are committed; `--check` fails when one is stale.

## Tests and examples

- A layer's tests are in `<layer>/tests/`, through its public headers, and its private ones for private pieces. A test that waits on a load gives up after a while and fails.
- Every example and game runs headless in the smoke run and in a browser in the browser check; a game also ships an autopilot playthrough.
- Example files are under `examples/assets/`; a game's under `games/<name>/assets/`. Each asset not made here is credited, with its license, in a `CREDITS.md` beside it.

## Tooling

- Tools are Python 3.12 or newer, standard library only, and run on Linux, macOS, and Windows. Nothing shell-only.
- Every tool answers `--help` with its usage, from its docstring, and does nothing else; it refuses an argument it doesn't take. `tools/check_tools.py` checks every one.
- A tool you run is named `<verb>_<noun>.py`, its verb from check_tools' `VERBS`; a module that tools import is one word, listed in `MODULES`. No file imports a command.
- A check that can't run here (no compiler, browser, Wine, Xvfb) says `<tool>: SKIPPING <what> (<why>)` before running anything, and a driver of checks repeats every skip in its last line. A skipped check is reported as skipped, never as passed.
- Rules are enforced by the compiler and CMake where possible. A check of the API reads headers through clang (`tools/headers.py`), never regular expressions over source.
- Before a change is done, every preset this machine can build passes its tests: `tools/verify_builds.py` runs them, `--web` adds the web, `--windows HOST` a Windows machine.

## Guards

A guard is a check that holds a number or a rule: a tolerance, a threshold, a baseline (`docs/benchmarks.json`), a budget, or the presets and steps a check runs on.

- A timing guard holds every row to an absolute limit on the machine that recorded its baseline (the baseline names it), so a slowdown of every row alike fails there; elsewhere, a runner of another speed, it holds rows relative to the run's own speed.
- A guard is never loosened in the commit whose code needs it loosened: a looser tolerance, a higher threshold, a baseline re-recorded upward, or a check narrowed to fewer presets goes in a commit of its own, with its reason, so it is judged on its own.
- Every change to a guard, either way, is reported in the step's report under "Guards changed": what it was, what it is, and why.

## Docs

- Every rule lives in one developer doc, exactly once: this file, BINDINGS.md, ARCHITECTURE.md, BUILDING.md, or the binding's README. AGENTS.md links to rules and adds only what an agent needs.
- `README.md` is where a reader starts, and links every doc.
- ARCHITECTURE.md describes the design as it is now; a change to it updates it in the same commit. BUILDING.md says how to build and test, and what every tool does; a change to a preset, requirement, or tool updates it in the same commit.
- ROADMAP.md lists what is left. An item leaving it, done or dropped, is deleted, and its record moves to HISTORY.md in the same commit; a dropped item says why. A deferred item has a condition anyone can check, never "when we need it".
- HISTORY.md is the record, never rewritten. It says why things are as they are, and what was tried.
- FRICTION.md logs what a game's developer had to work around or found awkward, each entry triaged into a ROADMAP task or a HISTORY decision before its milestone closes, then moved to HISTORY.md (SPEC.md, "Games test the framework only if they can't bend it"). A plan's game code first is in `docs/sketches/<game>/`, never compiled, and stays as written once its milestone closes.
- Every other doc describes how things are now: rewrite what changed rather than adding history.
