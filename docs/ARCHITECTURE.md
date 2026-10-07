# libwgf architecture

How libwgf is put together now: its layers and modules, what each provides, and how they work together. The rules -- layout, naming, the public API -- are [CONVENTIONS.md](CONVENTIONS.md)'s; this file describes how they are met. What is still to come is [ROADMAP.md](ROADMAP.md)'s.

## Layers

libwgf is one C library, `libwgf.a`, built in layers (CONVENTIONS' table says what each may depend on). A program links the one archive and pulls from it only the objects it calls.

| Layer | Provides | State |
|-------|----------|-------|
| math | vectors, quaternions, 4 by 4 matrices, and the math on them | built |
| core | version, logging, handles, time, file storage, the load pipeline and resources, the program's identity, probes, random numbers | built |
| platform | the window, its events, input: keyboard, mouse, touch, gamepads | built: sokol_app natively and on the web, or none in a headless build |
| asset | where a resource's file comes from: local, fetched and cached on the web, revalidated, manifests, redirects, ensured ahead of a load, a native download hook | built |
| gfx | 2D drawing: the frame, immediate mode, textures, fonts and text, nodes in canvases, shapes, sprites, particles | built |
| audio | sounds (decoded or streamed) and voices, mixed natively by libwgf on sokol_audio's thread, on the web by the browser's Web Audio | built |
| ecs | entities with a simulated transform, the built-in components and their systems, triggers, polled events, scenes as text | built: on flecs's core |
| ui | game UI, immediate mode: boxes, panels, labels, buttons, focus by keys and pads, the pointer's capture, a style | built: on Clay |
| app | the runtime: run, the frame loop, ticks, autopilot runs | built |

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
| probe | `wgf_probe.h` | named numbers a program publishes about itself, read by autopilot runs, tools, and tests |
| random | `wgf_random.h` | one seeded generator (PCG32): a seed's sequence is the same on every platform |
| resource | `wgf_resource.h` | what every resource kind shares: status, path, release; the load budget |
| play state | `wgf_play_state.h` | `wgf_play_state_t`, the state of anything that plays over time |
| load | private | the load pipeline behind a resource's load on create: make the file local, prepare it on worker threads, finish it on the main thread within a per-frame budget |
| part | private | the list of optional parts, each installed by its first create (below) |
| os, thread | private | the few OS calls that differ between POSIX and Windows; threads, mutexes, and condition variables (none on the web) |

### platform

The window, its events, and input, under gfx and audio, which ask it for what they draw into and play through.

| Section | Header | Provides |
|---------|--------|----------|
| window | `wgf_window.h` | title, size, fullscreen (and whether it can be), visible, resizable, decorated, focused; position and monitors; transparent, high DPI, vsync, and MSAA, fixed when it opens |
| input | `wgf_input.h` | `wgf_input_state_t` (UP, PRESSED, DOWN, RELEASED); the characters typed, as UTF-8; the capture flags: whether game controls should leave the pointer or keyboard to a UI |
| keyboard | `wgf_keyboard.h` | sokol's keys as `WGF_KEY_*`, by place on a US layout; each key's state, and down, pressed, released |
| mouse | `wgf_mouse.h` | position, movement, and scrolling in logical pixels; each button's state; locking the pointer; hiding the cursor |
| gamepad | `wgf_gamepad.h` | up to 4 pads, each keeping its number while connected: buttons by position on an Xbox-style pad; sticks with a round dead zone, rescaled to reach 1; triggers 0 to 1. Read once a frame, before its ticks: evdev on Linux, XInput on Windows, the Gamepad API on the web |
| touch | `wgf_touch.h` | fingers, oldest first, by an id that holds while each is down; the two-finger gesture's center, pan, pinch, and twist; the first finger driving the mouse |
| (the window system) | private | sokol_app, or none in a headless build, which runs frames at a display's rate (or as fast as it can, for an autopilot run), for `LIBWGF_HEADLESS_FRAMES` frames when that is set; the device and each frame's swapchain for gfx, through sokol_glue |

**Input** is read, never called back. Held state is shared; edges (pressed, released, movement, scrolling, typing) are kept twice, since they are relative to whoever reads them: inside a tick, since the previous tick; inside a frame, since the previous frame. A frame that runs no tick carries the tick edges over, so every press is seen by exactly one tick. A key pressed and released within one read is PRESSED, so no tap is missed.

sokol's implementations are compiled once each, by the layer that owns them: gfx, gl, app, app_utils, and glue in platform's `wgf_platform_sokol_impl.c`, time in core's.

### asset

Where a resource's file comes from (`wgf_asset.h`), libwgt's asset layer carried whole: `wgf_asset.c` (the part, the host, the cache's settings, and core's hooks), `wgf_asset_task.c` (ensures, groups, pings, a load's file), `wgf_asset_url.c` (paths, URLs read against the host as a browser reads them, redirects), `wgf_asset_fresh.c` (Cache-Control's freshness), `wgf_asset_sha256.c`, the manifests (`wgf_asset_manifest.c`, `wgf_asset_manifest_reader.c`, a strict JSON reader), and per platform `wgf_asset_web.c` and `wgf_asset_native.c`.

- **A path is an asset.** gfx's and audio's creates from a path go through `wgf_asset_priv_resource_create`, which installs the part, so a program of generated content links none of it. Installed, it sets core's load hooks: an update moving its tasks on, and a locate hook core asks, in place of looking for a request's file itself, until it answers LOCAL (fs has the file), PENDING, or FAILED; a refetch when a file that was local fails to load (on the web a cached copy is dropped and fetched once more).
- **The host** needs no setting: natively the storage root, so files are read where they are; on the web the directory of the document's base URL. A program names another (`wgf_asset_set_host("../assets")`, as the examples do).
- **On the web** a file not in this visit's store is read from the cache when the cache mode says it is current, else asked about (a conditional GET) or fetched, and kept with its response's validators; a 304 keeps the copy, a 4xx drops it, no answer or a 5xx uses it, so offline works. The fetch is polled: its answer waits in JS, under quoted keys of `Module`, until the task's step takes it, so nothing is exported.
- **Manifests** (`wgf_asset_set_manifest`; `tools/gen_manifest.py` writes the tree): a listed file whose cached hash matches is used with no request, and a download is hashed before it is kept, so a host still serving an old file fails the load rather than filling the cache.
- **Natively** libwgf has no HTTP client: the program downloads, when it turns that on (`wgf_asset_set_fetching`). Each download libwgf wants is a request the program polls for (`wgf_asset_fetch_next`), with a URL and a destination, and answers from any thread (`wgf_asset_fetch_done`), the answers applied at the next update.
- `tools/check_asset_cache.py` visits a page (`asset/tests/wgf_asset_cache_page.c`) again and again in one browser context -- first, unchanged, offline, changed, again, cleared, gone, gone offline, and with manifests -- judged by the requests, the log, and the screen; ctest runs it on the web presets.

### gfx

2D and 3D drawing, on sokol_gfx and sokol_gl.

| Section | Header | Provides |
|---------|--------|----------|
| color | `wgf_color.h` | `wgf_color_t`, 8-bit RGBA packed as `0xRRGGBBAA`; the stock colors (`wgf_color_get`, and for C `WGF_COLOR_SKYBLUE`); make, make_float, with_alpha, the component getters, lerp |
| render | `wgf_render.h` | the clear color, and the presentation's bars' color; the frame's size and DPI scale; the clip stack (`wgf_render_push_clip` / `pop_clip`): nested, intersecting rectangles in logical pixels |
| presentation | `wgf_presentation.h` | a design resolution and the mode fitting it to the window or the screen (stretch, fit with bars, fill, expand, integer); what is visible now in logical coordinates, and the scale |
| draw | `wgf_draw.h` | immediate mode 2D in logical pixels from the top-left, y down: rectangles, lines, circles, triangles, outlines with a thickness, polylines with mitred (or bevelled) corners, polygons filled by ear clipping, both from a caller-owned array of points; text, at a point or aligned to one; textures and regions of them; and in 3D, through a 3D camera between `wgf_draw_begin_3d` and `end_3d`, depth tested: lines, cubes and their edges, spheres, a grid, rectangles, circles, and text facing the camera |
| texture | `wgf_texture.h` | a resource loaded on create: PNG, JPEG, BMP, TGA, or GIF, decoded with stb_image on a worker, its mipmaps built there, uploaded on the main thread; size and sampling. PENDING draws nothing; FAILED draws a magenta and black checker |
| font | `wgf_font.h` | a resource loaded on create: a TrueType or OpenType font, checked whole before it is read; font 0 is the default, the built-in JetBrains Mono (printable ASCII); measuring text |
| node | `wgf_node.h` | every placed thing: the tree (parent, children in order, index), the transform (position, rotation as three angles, scale, all at once), world position, a name and finding by it, enabled and visible, aiming (`wgf_node_look_at`), and `wgf_node_destroy` with `DESTROY_CHILDREN` or `KEEP_CHILDREN` |
| canvas | `wgf_canvas.h` | the root of a 2D tree, drawn when asked, with an optional 2D camera |
| camera2d | `wgf_camera2d.h` | a node a canvas is viewed from, centered on its world position, turned, and zoomed |
| camera3d | `wgf_camera3d.h` | a node the world is seen from, down its -z: perspective (a field of view) or orthographic (a height), near and far planes; what it sees fills the visible area |
| shape2d | `wgf_shape2d.h` | a node drawing a rectangle, a circle, a line, or a polygon (its points from a caller's array, read back into one), filled or outlined |
| sprite | `wgf_sprite.h` | a node showing a texture or a region of one: size, pivot (its center by default), tint; it holds a reference to its texture |
| text | `wgf_text.h` | a node drawing a UTF-8 string in a font: font size, color, wrapping to a width, alignment on each axis; it holds a reference to its font |
| mesh | `wgf_mesh.h` | a resource: a shape's triangles, generated (plane, cube, sphere, cylinder, cone, capsule, torus) and shared by their parameters, uploaded when first drawn; a material a slot |
| material | `wgf_material.h` | a resource made from numbers: glTF metallic-roughness or unlit, its parameters by name (colors, factors, five textures with their transforms and sampling), alpha mode, double sided |
| emitter2d | `wgf_emitter2d.h` | a node owning many particles, simulated on the CPU: a rate and bursts, a capacity, life, direction and spread, speed, a birth radius, gravity, drag, size and color over life, and squares or streaks along their motion; their randomness from `wgf_random` |

app's runtime starts gfx once the window exists, and stops it, freeing every node, texture, and font. gfx draws with what the platform gives it: sokol_gfx on the window's device, each frame at the window's size and DPI scale, into the window's swapchain. A frame is recorded first and drawn at its end: immediate mode records into a sokol_gl context of gfx's own, starting with room for 65536 vertices and 16384 commands, doubled for the frames after one that runs out; the frame's end runs its parts' flushes (text's glyphs into the atlas, which can't happen inside a pass), then draws the recording in one pass into the window. A full glyph atlas grows between frames. Text is laid out and rasterized at the frame's pixel density and drawn scaled back to logical pixels, so it stays sharp. A clip becomes a scissor in the frame's pixels, recorded in the immediate mode stream. Immediate mode in 3D records into the same stream through a depth-tested pipeline, made by the first 3D draw, with the camera's view and projection and a viewport of the visible area, its file of its own (`wgf_gfx_draw3d.c`) so a 2D program links none of it.

**The presentation** is one transform, logical to framebuffer pixels (an offset and a scale on each axis), and a visible area; platform owns it, and NONE's (the DPI scale alone, the window visible) is the default. Everything that meets the framebuffer goes through it: the frame's projection (its edges in logical coordinates, so a bar is where nothing logical is), clips, a canvas's view (centered on the visible area), the UI's layout (laid out in the visible area), the pointer and touches (mapped back), and an autopilot's mouse. `wgf_presentation_set` installs the fitting into platform and the bars' fill into render, as function pointers, so a program that never sets a mode links neither; under FIT and INTEGER the frame is cleared to the bars' color and the visible area filled with the clear color.

**Nodes.** A plain node (`wgf_node_create`) is only a transform; the other types are nodes with content. All nodes share one pool (`"gfx.node"`), and every type shares the node calls. A node keeps its rotation as a quaternion, and its local and world matrices, rebuilt only when dirty: changing a node's transform or parent marks it and everything under it world-dirty, stopping at a node already dirty, so moving a node costs its subtree once and a frame where nothing moved does no matrix math. A node's children are an array in drawing order, and a child that leaves leaves a hole, closed in one pass when the array is next read whole, so a node leaving a parent with thousands of children costs nothing for its siblings. A tree is walked, drawn, and destroyed with a list rather than C recursion, so any depth is safe. A canvas draws its tree depth first, each node's world matrix cleaned on the way down, through its camera; each type with something to draw draws through its kind's hook (`wgf_gfx_priv_node_kind_t`), set by its first create, so a canvas names no type and a program links only the types it makes. Shapes become their outline's points, placed through the node and drawn by immediate mode's fill or thick polyline; a sprite a placed textured quad; text a fontstash block through the node's matrix; an emitter its particles, born and kept in canvas units and moved on by the particles part's update every frame, through the canvas's view.

sokol_gfx's backend is the build's: OpenGL core (4.1 or later) natively, WebGL2 on the web, and sokol's dummy backend, with no GPU, in a headless build.

### audio

Sounds and voices, libwgt's audio carried whole: a part installed by the first sound created, so a program that makes none links none of it.

| Section | Header | Provides |
|---------|--------|----------|
| audio | `wgf_audio.h` | the master volume, and pausing everything |
| sound | `wgf_sound.h` | a resource: WAV, MP3, or Ogg Vorbis, mono or stereo, decoded whole as it loads (`wgf_sound_create`), or decoded as it plays and played while its file arrives (`wgf_sound_create_streamed`, for music); its duration; named segments of it |
| voice | `wgf_voice.h` | one playing of a sound, an object: play, pause, resume, stop, and `wgf_play_state_t`; loop, volume, pitch (a negative one plays backwards), pan, position, and a segment to play as the whole; it holds a reference to its sound |

**Natively** libwgf decodes (dr_wav, dr_mp3, Xiph's libvorbis, each pinned past its last release for its fuzz fixes) and mixes on sokol_audio's device thread; voices' settings go to the mixer through a lock it holds briefly, and a slow frame doesn't stop the sound (`wgf_audio_stall_test`, in a window). **On the web** the browser does all of it: a decoded sound is `decodeAudioData`'s buffer, a streamed one an `<audio>` element, each voice a gain and a panner into a master gain, mixed on the browser's own audio thread, so a slow frame doesn't stop it there either and no decoder is in the wasm. One AudioContext, made by the first sound, waits suspended until the page's first input. The same tests run on both (`wgf_audio_voice_checks.c`'s steps), and `tools/check_stream.py` serves a streamed file slowly to a page and checks each case.

### ecs

Entities, their components, and the systems that run them at the tick rate, on flecs (v4.0.5, its core alone): a part installed by the first entity made, so a program that makes none links none of it, flecs included.

| Section | Header | Provides |
|---------|--------|----------|
| entity | `wgf_entity.h` | `wgf_component_t`; an entity made under a node and destroyed with everything under its node; a name, and finding by it; what it draws hidden or shown (its component nodes' visible, one added later taking it); its transform, and positions read and written in bulk through caller-owned arrays; snapping (drawn where it is, not swept there); components added, removed, asked about; a node component's node, the voice |
| (the world) | `wgf_ecs.h` | the events, polled: CREATED, DESTROYED, TRIGGER_ENTER, TRIGGER_EXIT, three ints each; the entities of a behavior found and counted; clearing; the world dumped as a scene |
| motion | `wgf_motion.h` | velocity, spin, damping, a top speed |
| bounds | `wgf_bounds.h` | a rectangle wrapped around, clamped to, or died outside, with a margin; or the presentation's visible area, read each tick, in place of a rectangle |
| lifetime | `wgf_lifetime.h` | seconds left |
| collider | `wgf_collider.h` | a circle, a layer and a mask; switched off and on; what it overlaps |
| behavior | `wgf_behavior.h` | the program's own code, by name, with text parameters |
| scene | `wgf_scene.h` | a resource: entities and prefabs in a text file (its header has the format), instantiated, and prefabs spawned |

**The split.** An entity's simulated state -- its transform and the systems' components -- is flecs components, plain data the systems' queries walk in flecs's tables. What isn't plain data is in libwgf's record for its handle (`"ecs.entity"`): its node, the nodes of its visual components, its voice, its name, and its behavior's name and parameters, with the flecs id. Its node is a plain node it owns, under the parent it was made with; a shape, sprite, text, or emitter component is a node of that type under it, so drawing stays gfx's, through canvases, and an entity or node put under an entity's node goes when it does.

**A tick** runs the systems, after the program's tick callback: lifetimes count down (at 0, destroyed), motion moves (damping, then the top speed, then velocity and spin), bounds wrap, clamp, or destroy, colliders compare, and the probes (`ecs.entities`, `ecs.behavior.<name>`) are published. Entities are destroyed after the query that found them, never inside it. As each tick begins, each transform is kept as it was; each frame, every entity's node is given its transform between the last two ticks at the frame's tick fraction, its rotation the shortest way round. A wrap moves the kept position by the same span, so a rock leaving the right edge is drawn coming in at the left, not swept back across the screen; `wgf_entity_snap` keeps the transform as it is now, for one put somewhere new.

**Colliders** are triggers, nothing pushed apart: each tick, the colliders under each parent node, sorted along x, are swept for overlaps where either side's mask has the other's layer (so clearing one side's mask doesn't stop the other meeting it), a switched-off collider (`wgf_collider_set_enabled`) skipped, its settings kept; the pairs found are sorted and walked against the last tick's, so a pair new this tick raises TRIGGER_ENTER and one gone raises TRIGGER_EXIT, told to each. A destroyed entity's pairs are dropped without an exit; its DESTROYED (for one with a behavior) carries its now-stale handle.

**Events** queue in a ring of 65,536 and are taken by the program, or the binding, with `wgf_ecs_take_events`: one async model, polled, and no callback crosses into a behavior. A behavior's code is the program's: the binding's runtime makes each behavior's object (`wgf.Behavior`) from these events, tells it of its triggers, ticks and frames it, and ends it ("The bindings").

**Scenes** load through core's load pipeline, parsed on a worker into a plan of entities and prefabs, each a list of component lines; `from` copies a prefab's lines first. Instantiating or spawning applies the lines through the same calls a program makes. `wgf_ecs_dump` writes every live entity with every component as it is, so a dumped world loaded again makes the same world, and dumps the same text.

### ui

Game UI, immediate mode, on Clay (the Whirling Gizmo fork): a part installed by the first `wgf_ui_begin`, so a program that draws none links none of Clay.

| Section | Header | Provides |
|---------|--------|----------|
| ui | `wgf_ui.h` | a frame's UI between `wgf_ui_begin` and `wgf_ui_end`: boxes (a column or a row) and panels, the open one's size, padding, gap, alignment, and color; labels, spacers, and buttons, a button true in the frame it is activated; the focus, by id; the style (colors, sizes, a font) |

**A frame's UI.** The program describes the UI as it is now, in the frame callback; each call goes to Clay as it is made, a box's layout held back until its first child or its close, since Clay takes an element's declaration once. The presentation's visible area is the root, a centered column, so a UI anchored to its edges stays at the window's edges under EXPAND. Text and ids are copied into blocks kept until the next frame's begin, because Clay points at them until it draws. `wgf_ui_end` closes what was left open, has Clay lay it all out, and draws its commands through gfx's immediate mode, at that point in the frame: a rectangle as a filled outline with rounded corners, a border as a closed thick line along it, text through `wgf_draw_text` in the style's font (measured with `wgf_font_measure`, so Clay and the drawing agree), and clips through gfx's clip stack. Nothing of Clay is in a public header, and no callback crosses the API.

**Input.** As the UI begins, it reads the frame's input once: the pointer (the mouse, which touch drives too) and its left button's edges, and the keys and pad buttons that activate and navigate. A button is answered as it is described, against where Clay laid it out in the frame before: the pointer activates the button it was pressed and released over, and the focused button activates on Enter, Space, or a pad's south button. After the layout, the arrow keys, Tab and Shift+Tab, and the D-pad move the focus through the frame's buttons in order, wrapping; a focus moved so is drawn as a line around the button. The UI sets the input's pointer capture while the pointer is over a panel, a button, or a colored box, or a press on a button is held, and the keyboard capture while a button has the focus; the part's end of frame lets both go after a frame that drew no UI.

**Memory.** One Clay context in one arena, sized by Clay for 2048 elements and 8192 measured words, made at the first begin and freed at gfx's stop with the text blocks.

### app

The runtime: it opens the window, starts and drives the layers below it, and runs the frame loop.

| Section | Header | Provides |
|---------|--------|----------|
| (lifecycle) | `wgf_app.h` | `wgf_app_run(init, tick, frame, shutdown, user)`, `wgf_app_quit`, `wgf_app_can_quit` (false on the web), `wgf_app_is_running`. `wgf_app_run` is the one public call that takes callbacks |
| loop | `wgf_loop.h` | the frame delta; the tick rate, delta, and fraction; the time scale (0 pauses ticks while frames go on); a target fps; frames per second; a frame's own cost, in real time |
| debug | `wgf_debug.h` | the frame-rate overlay (the frames a second and a frame's cost), drawn by the runtime over the program's frame through a hook its show call sets, so a program that never shows it links none of it |
| (autopilot runs) | private | an autopilot: inputs at frames and expectations on probes (BUILDING.md, "Autopilot files", has the format), flown in its own time |

**The run.** `wgf_app_run` opens the window, starts core and gfx, and calls init. Each frame, it takes the window's events, updates core (tasks and loads move on), delivers an autopilot's inputs for the frame, runs the ticks due -- after each, the parts' ticks (a module's systems) -- then the parts' updates, then the frame callback between gfx's begin and end, and checks an autopilot's expectations for the frame. After quitting, it calls shutdown and stops gfx and core. On the desktop `wgf_app_run` returns when the program has quit; on the web it returns at once, and the browser runs the frames, so it is the last call in main everywhere.

**Ticks** run on the runtime's tick clock, fed the frames' real time times the time scale, at most 5 a frame. The frame delta is real time, clamped to 0.1 s. A target fps sleeps natively, and on the web skips the frames the browser offers too early.

**An autopilot run** takes its file natively from the file `LIBWGF_AUTOPILOT` names, on the web from `Module["wgfAutopilot"]`, the text the page gave the module. Its inputs at a frame are delivered as the window's events would be, before the frame's ticks, and its expectations are checked after the frame, each failure an error in the log; its end logs PASS or FAIL and quits. While it runs, every frame lasts exactly a sixtieth of a second, so ticks, and the random numbers its seed gives, make the same run on every machine; a headless run doesn't wait for a display. Its results are in the log, which is what the tools judge, since a page has no exit code.

## The bindings

Games are Haxe (`bindings/haxe/`, haxelib `wgf`); JS and TypeScript programs use the JS binding (`bindings/js/`), which Haxe's JS target is built on. How each maps each kind of C call is [BINDINGS.md](BINDINGS.md). `tools/gen_binding.py` generates both from clang's parse of the headers (`tools/headers.py`), with `hosts/web/exports.json`, the full host's exports; a header change is a regeneration, checked by `--check`.

**The JS binding** is one ES module, `wgf.js` (written by `tools/jsbinding.py`), and its declarations, `wgf.d.ts`:

- **Generated:** every exported call under its C name, its header comment its JSDoc, each crossing into the host by quoted key; the enums as constants; `BUILT_VERSION`, the version and the headers' digest. In the declarations, each handle kind a branded type, each enum the union of its values.
- **By hand**, put at its top whole: the runtime (`src/runtime.js`: `attach`, text on the wasm stack for its call alone, vectors and arrays through slots `_malloc`'d once, no heap view held) and the run (`src/app.js`: `wgf_app_run` taking JS functions, through four trampolines installed once, each reading its handler when it fires, catching a throw, and restoring the wasm stack; the version check against the host).
- **Sections**, each marked `// wgf: `, so `tools/jsbinding.py`'s `trim` keeps only the calls a program makes, for its export, without reading the JS. A JS example (`examples/js/<name>/`) makes the host itself, attaches the binding, and runs; `tools/check_js_binding.py` flies each in a browser.

**The Haxe binding**, in three parts:

- **Generated:** `impl/Raw.js.hx` and `impl/Raw.cpp.hx`, the same Haxe signature for each exported call on each target; the typed API over them, `wgf.<Type>` -- an abstract per handle kind, one over a kind for each section whose calls take it (a `Shape2d` is a `Node`), an enum abstract per enum, and statics for the rest -- each member one inline call of one `Raw` function; and `impl/BuiltVersion.hx`.
- **The runtime**, by hand: `wgf.Runtime.run` (C's `wgf_app_run`: on the web the JS binding's; natively four trampolines installed once, each reading its handler when it fires), `wgf.Behavior` (per-entity code made, told, and ended from the ecs's events, taken in bulk each tick and frame; the behavior component's calls are `wgf.BehaviorComponent`'s), the version checks, and the vector types.
- **The C calls.** On JS, `Raw` calls the JS binding's functions by quoted key (`Raw.binding["wgf_..."]`), found at `globalThis.wgfJs`; `impl/Host.js.hx` keeps what is Haxe's own (a vector read through one kept array, `Bytes` as the binding's `Uint8Array`s, handles back from unsigned). On hxcpp, `Raw`'s functions write each C call out with its arguments cast to their C types, in one compiled file that includes every header, and `project/Build.xml` links the staged archive (`-D wgf_out`).

**The feature test** (`examples/haxe/feature-test/`) is one program reaching every public call through the binding, every component in a scene among them. Built with `-D wgf_reach`, each `Raw` function first counts its call (`impl/Reach.hx`, generated with the list of every call), and the program fails naming any it never made; `tools/check_features.py` runs it on hxcpp, under node, and in a browser.

**The web host** (`hosts/web/`) is libwgf linked for the web with no `main`, as an ES module whose `createWgfHost()` resolves to it: `tools/build_host.py` links a staged web variant, exporting every call (the full host) or a list (a trimmed one), and the runtime methods the binding uses; a release host is linked as the examples' release pages are, -O2 with Closure for its JS. The JS binding is put beside every host (`tools/webhost.py`): whole beside the full host, trimmed to the same calls beside a trimmed one. A Haxe program's page (`hosts/web/page.html`) imports the host, makes it on the canvas, imports the JS binding and attaches it to the host, leaves the binding at `globalThis.wgfJs`, and imports the program, whose every call goes through it. A headless web variant's host is for node, where the binding's test runs (`bindings/haxe/test/node.mjs`, which does the same).

## The `wgf` tool

`wgf` (the root's launcher, `tools/wgf/`) works on a game: a directory with a `wgf.json` (its name, main class, sources, assets, autopilot files, and web size budget), made from `templates/game/` by `wgf new`. A game names no libwgf: the tool builds it against the libwgf it belongs to, from that libwgf's staged variants, with the binding on the class path. Its builds put the program's assets beside it everywhere (`build/<target>/assets`), as `Asset.setHost("assets")` finds them: a page's directory on the web, the executable's natively.

- **build** and **run**: the web (the full host, the game's page written once into `web/index.html`, the program), the desktop and headless (hxcpp against the native debug or release, or the headless, variant).
- **autopilot**, **screenshot**, **dump**: autopilot runs (BUILDING.md, "Autopilot files"), headless natively or in a headless browser, which a page hands the host (`globalThis.wgfAutopilot`, the module's `wgfAutopilot`); judged by what they log. A screenshot is the browser's capture when the autopilot logs its SCREENSHOT; a dump, each part's dump hook (the ecs's world, as a scene), logged a line at a time. `run`, `screenshot`, and `dump` take `--autopilot` to fly one to a point first.
- **serve** (`tools/wgf/devserver.py`): a hot build (hotreload-hx, vendored in `deps/`) on the full host, served with a long poll at `/__hotreload`; each save rebuilds the program through a compilation server, stamps it, and the page swaps its classes in between two frames, every static and object kept. The runtime's trampolines read their handlers when they fire, so the swapped code is what runs; a reload's bundle doesn't start the run again.
- **export**: the web as a static folder -- a release program, its host and its JS binding trimmed to the calls the program makes (the quoted keys in its JS that are calls), its assets copied in, libwgf's notices -- smoke-tested in a browser with the game's smoke autopilot and held to its budget; and the desktop, a release build with its assets, smoke-tested in a window (Xvfb's when Linux has no display).

## Games

Each game is a directory in `games/` the `wgf` tool works on, on the public API alone, through the binding: Asteroids (`games/asteroids/`) now. Its world is a scene file of prefabs; its behaviors set intent and the ecs does the per-entity work; its screens are the UI; its sounds are generated (`tools/gen_sounds.py`) and committed. Each game ships an autopilot playthrough, which `tools/check_games.py` runs headless and in a browser beside its exports, and every push to `main` deploys its web export to GitHub Pages (`tools/build_pages.py`, `.github/workflows/pages.yml`), held to its size budget.

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
- The C examples (`examples/c/<layer>-<name>/`) are each a CMake project of their own, built against a staged variant as a program outside libwgf would be (`examples/c/wgf_example.cmake`), into the variant's `bin/`, or on the web its `site/`, one folder an example with its own page. `tools/run_smoke.py` runs each headless and fails one that exits non-zero, runs past its time, or logs an error; `tools/check_desktop.py` runs each in a window on a private Xvfb display with OpenGL on the CPU, a Windows build under Wine there, and saves a screenshot; `tools/check_web.py` loads each in a headless browser, fails one that doesn't start or logs an error, and saves a screenshot.
- Every preset's tests include `check_api` (clang's parse of every public header, held to CONVENTIONS' API rules, and its self-test) and `check_tools`.
