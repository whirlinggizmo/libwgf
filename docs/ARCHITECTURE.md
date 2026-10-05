# libwgf architecture

How libwgf is put together now: its layers and modules, what each provides, and how they work together. The rules -- layout, naming, the public API -- are [CONVENTIONS.md](CONVENTIONS.md)'s; this file describes how they are met. What is still to come is [ROADMAP.md](ROADMAP.md)'s.

## Layers

libwgf is one C library, `libwgf.a`, built in layers (CONVENTIONS' table says what each may depend on). A program links the one archive and pulls from it only the objects it calls.

| Layer | Provides | State |
|-------|----------|-------|
| math | vectors, quaternions, 4 by 4 matrices, and the math on them | built |
| core | version, logging, handles, time, file storage, the load pipeline and resources, the program's identity, probes, random numbers | built |
| platform | the window, its events, input, scripted input | to come (ROADMAP, step 3) |
| asset | where a resource's file comes from | to come (step 5) |
| gfx | 2D drawing | to come (step 4) |
| audio | sounds and voices | to come (step 5) |
| ecs | entities, components, systems, scenes | to come (step 6) |
| ui | layout and widgets | to come (step 7) |
| app | the runtime | to come (step 3) |

### math

Headers alone: value types and the math on them, depending on nothing.

| Section | Header | Provides |
|---------|--------|----------|
| vec2, vec3, vec4 | `wgf_vec2.h` ... | `wgf_vec3_t` and its kin, floats in order with no padding; make, add, sub, scale, dot, length, normalize, lerp, and for vec3, cross |
| quat | `wgf_quat.h` | `wgf_quat_t`, a rotation; identity, from an axis and angle, to and from euler angles, look rotation (-z to a direction), mul, conjugate, normalize, rotate a vec3, slerp |
| mat4 | `wgf_mat4.h` | `wgf_mat4_t`, 16 floats column by column; identity, mul, transpose, determinant, invert, from translation, rotation, and scale and back, transforming a point or a direction, and projections (perspective, orthographic, look at) |

These are the only structs the public API passes, and only as return values. Every operation is `static inline`, C's own: a binding does its math in its own language, which on the web is faster than a call into the wasm.

### core

The base every other layer uses. It has no window and no loop: app's runtime starts it, updates it at the start of each frame, and stops it.

| Section | Header | Provides |
|---------|--------|----------|
| version | `wgf.h` | `wgf_version_get`, "MAJOR.MINOR.PATCH" from `VERSION` |
| identity | `wgf_identity.h` | the company and product the program's files on the desktop are kept under |
| log | `wgf_log.h` | levels, `wgf_log_message`, and for C the `wgf_log_<level>` macros, which format |
| time | `wgf_time.h` | `wgf_time_get_seconds`, monotonic seconds since core started, on sokol_time |
| handle | `wgf_handle.h` | `wgf_handle_t`, and `wgf_handle_get_kind_name`; the pools are private |
| fs | `wgf_fs.h` | read, write, exists, remove, mkdir, rmdir, each a task (`wgf_fs_task_t`); the root; `user:` paths for the program's own files that last |
| probe | `wgf_probe.h` | named numbers a program publishes about itself, read by scripted runs, tools, and tests |
| random | `wgf_random.h` | one seeded generator (PCG32): a seed's sequence is the same on every platform |
| resource | `wgf_resource.h` | what every resource kind shares: status, path, release; the load budget |
| play state | `wgf_play_state.h` | `wgf_play_state_t`, the state of anything that plays over time |
| load | private | the load pipeline behind a resource's load on create: make the file local, prepare it on worker threads, finish it on the main thread within a per-frame budget |
| part | private | the list of optional parts, each installed by its first create (below) |
| os, thread | private | the few OS calls that differ between POSIX and Windows; threads, mutexes, and condition variables (none on the web) |

## Handles

Everything libwgf owns and hands out -- a texture, a node, an entity, a task -- is named by a 32-bit handle:

```
[ kind: 6 bits ][ generation: 10 bits ][ index: 16 bits ]
```

- 0 is never a valid handle.
- Each kind has its own pool, one array of items; a handle's index is its slot, so resolving one is an array lookup, inline in core's private header.
- Freeing a slot bumps its generation, so a stale handle stops resolving, even after its slot is reused; freed slots are reused oldest first, so a stale handle could match again only after its slot has been reused 1023 times.
- A pool stores each slot's generation with a free bit no handle's generation has, so a handle matching its slot's stored generation is a live item's: resolving reads one array, checking kind, range, and generation.
- The kinds are one list (`core/src/wgf_core_handle_priv.h`), each with its number and name (`"gfx.texture"`), a block of numbers per layer; `wgf_handle_get_kind_name` names any handle's kind, even a stale one's.
- In the public headers each kind has its own type, a typedef of `wgf_handle_t` (`wgf_fs_task_t`), so a binding's generator gives each its own type from the headers alone.

Pools start small and double as needed, up to 65,535 slots.

## Resources and objects

- **A resource is data, shared**: created from a path, it loads on create (`wgf_<kind>_create(path)` returns its handle at once, PENDING), through core's load pipeline; creating the same path again gives the same resource with one more reference; `wgf_resource_release` drops one, and the last frees it.
- **An object is owned**, by whoever created it, and ends with its own destroy. It holds a reference to each resource it uses.

## Asynchronous work

What could wait follows CONVENTIONS' rules: a task handle, polled; its status an enum per kind (NONE 0, PENDING 1, then its outcomes); a status changes only in core's update, which the runtime runs at the start of each frame. fs's tasks are the pattern: `wgf_fs_read(path)` returns a task, `wgf_fs_task_get_status` answers PENDING until an update has run it, then DONE, NOT_FOUND, or FAILED, and `wgf_fs_task_get_data` and `_get_size` give the bytes until `wgf_fs_task_destroy`. Destroying a pending task lets it finish and discards the result, so a write still lands.

## Optional parts

CONVENTIONS' rule, "An optional part is reached through its hook", is met by core's part list (`wgf_core_part_priv.h`): each part is a static record (its layer, its order, and any of an update, a flush, an end of frame, and a stop) put on the list by the first create of what it serves. The runtime and the frame walk the list and never name a part, so a program that never creates one links none of its code. A layer's stop runs its parts' stops and takes them off the list, so the next run installs them again.

## Platforms

Native (Linux, Windows) and web (wasm32), split by file suffix (CONVENTIONS, "Naming").

### File storage (fs)

- **Native**: real files under the root, the program's own directory by default. A file's metadata is kept in a sidecar under the root's `.meta/`.
- **Web**: files live in memory (MEMFS) under the root (`/wgf` by default), kept between visits in an IndexedDB database, one record per file; startup reads only the list of files, and a file is read into memory the first time it is needed.
- **The program's own files**, `user:` paths: natively under the user's data directory by the program's identity (`<data>/<company>/<product>`), found the first time one is asked for; on the web under the root's `.user/`, so the browser keeps them as it keeps the rest.
- **The root is a jail.** A path is normalized (`\` read as `/`, `.` and empty parts dropped, `..` taking back the part before it), and one that is absolute, has a `:` or a control character, climbs above the root, or names nothing is refused.

### Logging

Logs go to stderr, which is the browser console on the web, as UTF-8. The exported calls take finished text, so a binding formats in its own language; C formats through the `wgf_log_<level>` macros, which skip the formatting when the level filters a message out.

## Build

- CMake, with the variants in `CMakePresets.json`. `wgf_layer()` (`cmake/wgf_library.cmake`) defines each layer: its object target, which the archive `wgf` collects, its never-installed `wgf_<layer>_priv` target, and a check that compiles each public header on its own with only public include paths. A layer of headers alone (math) is an interface target.
- sokol's implementations are compiled once each, by the layer that owns them: sokol_time's in core, which owns the clock.
- `linux-x64-debug-headless` builds with no window or GPU (sokol's dummy backend), so its tests run anywhere; `-asan`, `-ubsan`, and `-tsan` run the headless tests under the sanitizers.
- MinGW builds for Windows cross-build on Linux, their tests run under Wine (`tools/run_wine.py`); MSVC builds through Visual Studio's generator, on Windows, or from Linux over ssh (`tools/run_remote_windows.py`).
- The web builds use the pinned Emscripten; their tests run under node, and the ones that need a browser in a headless Chromium-based browser (`tools/run_in_browser.py`).
- Every preset's tests include `check_api` (clang's parse of every public header, held to CONVENTIONS' API rules, and its self-test) and `check_tools`.
