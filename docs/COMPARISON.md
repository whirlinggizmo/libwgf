# libwgf and helix compared

For a game developer choosing between them, and for Rob deciding which to carry on with. It says how the two are now; where a number is still to be measured, it says so. helix's side was read from its own docs (`../helix`: engines/raylib/README.md and api/, libs/*/README.md, docs/PLATFORM.md, docs/benchmarks.md) on 2026-11-27 and checked by helix's session; it is helix's pushed main unless a line says otherwise.

## What each is

- **libwgf** is a game framework in C, retained mode: a program makes actors (sprites, models, text, lights, cameras) in trees under stages, and the library keeps them, places them, culls them, and draws them each frame. Games are written in Haxe through its binding (on the JS target in a page, hxcpp natively), in JS or TypeScript, in C, or in Zen-C. One wasm module in a page; sokol underneath.
- **helix-raylib** is raylib 5.5's API in Haxe, immediate mode: a program draws what it wants each frame (`DrawTexture`, `DrawModel`) and keeps its own structure. In a page it is JS on WebGL2, no wasm; natively, hxcpp on SDL3 with sokol (GL 4.1) or SDL_GPU (Vulkan, D3D12, Metal). It is one engine over helix's libraries (platform, services, formats, math, geometry, tweens, Jolt, Box2D).
- **A retained engine on helix** would be a second helix engine beside helix-raylib, over the same libraries and adapters. It doesn't exist. helix-legacy was retained (a `Sprite2D` an object, its bunnymark the slow row), and helix-render, the drawing library such an engine would likely want, is set aside: helix-raylib's rebuild on it failed its size and speed gates. Its column below is what it would need building.

## What it comes with

Yes, partial, or none, a line each. "Libraries" is what helix offers beside helix-raylib, not through raylib's API.

| | libwgf | helix-raylib | a retained engine on helix |
|---|---|---|---|
| **Targets** | web (wasm, WebGL2), Linux, Windows (MSVC, MinGW); a headless build for tests | web (JS, WebGL2), native on SDL3 (GL 4.1, or SDL_GPU: Vulkan, D3D12, Metal) | helix-raylib's |
| **Languages** | Haxe, JS and TypeScript, C, Zen-C | Haxe | Haxe |
| **Window and display** | title, size, fullscreen, monitors, DPI; a design resolution with five fit modes; safe areas, orientation | raylib's window calls; the Screen extra's Godot-style stretch modes; the Device extra's safe area and orientation lock | helix-raylib's |
| **Input** | keyboard, mouse, 4 pads (rumble, SDL mappings), touch with gestures, tilt, an on-screen stick; actions bound across keys, pads, axes, and touch | keyboard, mouse, pads (vibration, SDL mappings), touch, raylib's gestures, the VirtualGamepad extra, tilt (web only); actions in helix-services, not raylib's API | helix-services' actions |
| **2D drawing** | immediate shapes and textures; sprites as actors (size, pivot, tint, alpha modes, nine-slice, trimmed atlas frames); sprite batches; 2D cameras; render targets; clip regions | raylib's shapes, splines, textures, `Camera2D`, render textures, blend modes; the Sprites extra's instanced draw; the Textures extra's fills, tiled draws, nine-patches; a bulk sprite batch in progress | to build |
| **Text and fonts** | TTF and OTF, bitmap fonts; text actors with wrapping and alignment, in 2D and 3D; a text buffer and view | TTF and OTF, BMFont, image fonts, SDF; raylib's measuring; no layout beyond it | to build (helix-render's `TextLayout2D`, set aside) |
| **Textures and images** | PNG, JPEG, BMP, TGA, GIF, and KTX (compressed, the variant the GPU samples); CPU images (raylib's Image calls); cubemaps; sprite sheets from a grid or TexturePacker or Aseprite JSON | PNG, JPEG, BMP, TGA, QOI, GIF (animated); raylib's CPU image editing; cubemaps; atlases from TexturePacker JSON and Starling XML; no compressed formats | helix-raylib's readers |
| **3D drawing** | models from glTF as actor trees; generated meshes; metallic-roughness materials and unlit; custom shaders; directional, point, and spot lights; shadows; environments (image-based light, a sky); instancing; frustum culling; 3D sprites and text | meshes, par_shapes, heightmaps; glTF and OBJ models; the Lighting extra's Phong and PBR shaders; shadows for up to 4 lights; skybox from a cubemap; instanced meshes; VR stereo; no fog or frustum culling (libwgf has no fog either) | to build |
| **Animation** | glTF skeletal on the GPU, layers, blends, shared clips; sprite sheet clips; tweens on the tick clock | glTF skeletal on the GPU, a two-clip blend and per-bone masks; the tweens extra; no sprite clip system | to build |
| **Particles** | 2D and 3D emitters on the GPU | the Particles extra: 3D on the GPU (lit), 2D on the CPU | helix-raylib's extra |
| **Audio** | WAV, MP3, Ogg, whole or streamed; voices with pitch and pan; buses (master, music, effects); spatial voices and a listener | WAV, Ogg, MP3, QOA; sounds, music, procedural streams; groups with fades in helix-services; no 3D audio; effects specified, their adapters not built | helix-services' |
| **UI** | an immediate UI on Clay with widgets (buttons, sliders, tabs, lists, dropdowns, modals, tooltips, a text field), skins, focus by keys and pads; RmlUi documents | raygui 4.5 and its styles; the GuiSkins extra; no key or pad focus documented | to build |
| **Physics and collision** | Jolt as components (bodies, sensors, characters, a vehicle); a 2D arcade body; 2D and 3D collision math; Box2D vendored, its calls in progress | raylib's collision checks; Jolt and Box2D as libraries, deterministic, each its own wasm in a page | the libraries |
| **Assets and saves** | resources loaded on create, on workers natively; an asset host with a cache, manifests, groups; `user:` saves; reload of changed files | the Assets extra's hosts and cache, async loads; saves per company and product | helix-services' |
| **Scene and game structure** | actors in trees, enabled, visible, paused; components and their systems (motion, bounds, lifetime, colliders); behaviors (a game's code on actors, by name); scene files and prefabs; a screen stack; follow and fly cameras | none: a game keeps its own structure, as in raylib | to build: the reason it would exist |
| **Tilemaps** | Tiled maps whole: four grids, infinite maps, terrain painting, objects, image layers, reload | none | to build |
| **Picking** | the topmost actor at a point (2D, texel-accurate), a ray against a 3D stage; the pointer's hover and press on actors | raylib's screen-to-world ray and ray tests; physics queries | to build |
| **Debug and testing** | a frame-rate overlay; probes; input recorded and replayed; autopilot runs (inputs and expectations, headless or in a browser) | the Debug extra's frame stats and recording, skeleton drawing; raylib's automation events; parity suites against raylib's C | helix-raylib's, plus its own |
| **Hot reload** | Haxe with hotreload-hx in a page (`wgf serve --hot`); Zen-C natively through tcc, its heap rule | helix-services' assets and shaders swapped in a page's dev build through hotreload-hx; none in helix-raylib's own API; none natively | as helix-raylib |
| **Platform layer** | sokol (app, gfx, audio); a new backend is whatever sokol has | adapters: an interface each for graphics, window, events, input, audio, decoders, fonts, http, sockets, files, export, each with a web and a native adapter; a program names one per interface | the same adapters |

## Size

A page's code gzipped (the page, its JS, its wasm), as helix counts it, no assets.

| | libwgf | helix-raylib |
|---|---:|---:|
| the smallest program (a window cleared) | 70.6 KB (app-skeleton) | 35.5 KB (1-window, main, 2026-11-25) |
| a game's features, one of each (helix's game rung, the same program on each) | 249.7 KB (game-rung: its wasm 207.6, the host's JS 24.9, the binding 4.2, the program 9.5, the page 3.4) | 142.0 KB (10-game, main) |
| everything linked | not measured | 363.8 KB (11-everything, main) |
| Asteroids (2D, UI, sound, particles) | 222.7 KB | not written |
| Wardens (the same spec on both) | 268.5 KB | **to measure** |
| the racer (3D, Jolt) | 565.5 KB | not written |

Physics weighs differently: libwgf links Jolt into its one module (about 264 KB gzip), helix's page fetches `jolt.wasm` (260.8 KB) or `box2d.wasm` (119.1 KB) beside the game.

## Speed

On coinspinner (i9-13900HX, RTX 4080 laptop), pages with helix's `run_page.cjs`, frame to frame in ms. The bunnymark rows were taken in one session on 2026-11-27, round-robin, the median of 3 runs: libwgf at 9378f49, helix-raylib on its robots-perf branch at 9bf08391, not yet pushed (its bulk sprite batch, `SetSpriteBatchPositions` and one `DrawSpriteBatch` a frame); the load 3.4 to 4.1, not a quiet window.

| | libwgf | helix-raylib |
|---|---:|---:|
| bunnymark, a sprite batch on each, 10k / 50k / 200k | 0.09 / 0.39 / 1.77 | 0.17 / 0.43 / 2.17 |
| bunnymark, a call a bunny: an actor each (libwgf) or `DrawTexture` each (helix), 10k / 50k / 200k | 0.31 / 1.79 / stops at 65k actors | 0.26 / 1.12 / 4.62 |
| robots 400, one session | 2.76 | 2.52 (before helix's culling, which is landing: 2.70 with every robot in view) |
| natively | about level | about level |

Batch against batch, libwgf's is 10 to 45% faster. A call a bunny, helix-raylib's immediate `DrawTexture` is 1.2 to 1.6 times faster than libwgf's actors: an actor is kept, placed in its tree, culled, and pickable, and costs that each frame.

## Limits

- **libwgf:** 65,535 live objects of each kind (actors, textures, voices); emitters, batches, and tile layers hold their many things inside one. One graphics layer, sokol's. The page is the larger.
- **helix-raylib:** no structure but what the game writes. Unsupported: IQM, VOX, and M3D models, morph targets, compressed images, loads from memory, shaders from GLSL at run time (they are compiled ahead), FLAC; 87 rlgl calls (raw GL handles and buffers). A call that waits (`EndDrawing`, a file read) is refused in a function used as a value (a lambda), pointing to its `...Async` call.

## An agent's learning cost

helix-raylib's API is raylib 5.5's, which a model already knows; libwgf's is learned from its headers and docs. The Wardens trial (2026-11-20, HISTORY.md, "The comparison") gave the same spec to fresh agents under the same conditions:

| | cost a run | cache read | failed builds | items met, of 37 |
|---|---:|---:|---:|---:|
| raylib C | $2.40 | | | 36.7 |
| helix-raylib | $2.51 | 7.3M | 4 to 5 | 35.0 |
| libwgf (Haxe) | $2.87 | 11.3M | 1 to 2 | 36 |

- **The known API saved reading:** helix-raylib's runs read about a third less and cost about 13% less.
- **It didn't save mistakes:** its runs failed to build most often and met the fewest items. Where helix-raylib differs from the raylib a model remembers (`...Async` loads, a waiting call refused in a lambda, shaders compiled ahead), what the model knows points the wrong way.
- **Told where the extras are, helix-raylib's agents were cheaper still.** helix's own trial (2026-11-13, `docs/trials/results/2026-11-13-wardens-extras.md`, its findings in helix's commit `65a19b02`): Wardens built natively by 18 fresh agents, 3 on raylib's docs alone and 15 with one line pointing at the extras and their API page. With the extras: $2.17 a run against $2.61, 18.4 minutes against 22.1, 66 turns against 77, every run using them (`Models`, `Lighting`, `GuiSkins`, `VirtualGamepad`, `Device`), all 18 built natively and in a page. Without the pointer, agents built their own; libwgf's did the same with libwgf's parts (bars over actors, skinned menus, a model's nodes walked by hand).

**The comparison still missing** is libwgf's under the same terms: its Wardens entrants given an index of what libwgf has, beside helix's $2.17. libwgf's three runs (2026-11-20) had its docs but no index, nearer helix's blind runs than its runs with the extras.

**The model for that index is PixiJS's** ([pixijs.com/llms](https://pixijs.com/llms)), better laid out than either engine's docs: `llms.txt`, an index of every page by group with a line on what each is for, loaded first; a medium file of the guides without the reference; the full reference; and skills for coding agents, its preferred route, installed in one command. Its site puts core concepts first, opens each area with an overview and then a page a class, and keeps guides, tutorials, examples, a playground, and the API apart. libwgf's own is on its ROADMAP (jam-ready's "Docs for agents", and Distribution's docs site).

## Worked examples

The same small task on each, in Haxe.

### An actor on a moving platform

**libwgf:** the platform is the rider's parent. Moved, turned, or scaled, the platform carries the rider and everything under it (a held item, a name tag); the world position is the library's to work out, so picking, culling, and collisions see the rider where it is.

```haxe
// landing: under the platform, where it was (today worked out by hand,
// for a platform that doesn't turn or scale; a call for it is on the ROADMAP)
final at = player.getWorldPosition(), base = platform.getWorldPosition();
player.setParent(platform);
player.setPosition(at.x - base.x, at.y - base.y, at.z - base.z);

// each frame: move only the platform
platform.setPosition(x, y, 0);

// leaving: back on the stage, where it is
final now = player.getWorldPosition();
player.setParent(stage);
player.setPosition(now.x, now.y, now.z);
```

**helix-raylib:** no tree, so the game carries its riders itself, each frame.

```haxe
// each frame: move the platform, and every rider by as much
final dx = platform.x - platform.lastX, dy = platform.y - platform.lastY;
for (rider in platform.riders) {
	rider.x += dx;
	rider.y += dy;
}
// a turning platform also turns each rider's offset about its center;
// a rider's own children (a held item) are the game's to carry too
DrawTexture(platformTexture, Std.int(platform.x), Std.int(platform.y), WHITE);
for (rider in platform.riders) DrawTexture(rider.texture, Std.int(rider.x), Std.int(rider.y), WHITE);
```

Or draw the riders under `rlPushMatrix` with the platform's transform: that moves what is drawn, not where the game thinks the rider is.

## Still to measure

1. What the game rung's 207.6 KB of wasm is, part by part, against helix's 142.0 KB for the whole page.
2. Wardens' page on helix-raylib, beside libwgf's 268.5 KB.
3. The robots again, in one session, once helix's culling is on its main.
4. More worked examples: a menu with pad focus, a Tiled level, a skinned character with a blended run, a save.
5. libwgf's Wardens entrants with an index of what libwgf has, as helix's had the extras' page (its 2026-11-13 trial), beside its $2.17 a run.
