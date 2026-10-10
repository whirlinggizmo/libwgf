# libwgf's JS binding

libwgf from JavaScript and TypeScript in the browser: one ES module, `wgf.js`, and TypeScript's declarations beside it, `wgf.d.ts`, generated from libwgf's public headers (`tools/gen_binding.py`). It is the one way JS reaches libwgf's wasm host (`hosts/web/`): the Haxe binding's JS target is built on it, so every Haxe game, test, and autopilot run exercises it too. Fifteen calls don't cross at all: the colors' and the trigonometry's are arithmetic on their arguments alone, which a call into wasm costs more than, so `wgf.js` works them itself, to the same bits as libwgf's C (`docs/BINDINGS.md`).

## Files

| | |
|---|---|
| `wgf.js`, `wgf.d.ts` | every exported C call under its C name, each header comment its JSDoc, and the enums; generated, never edited |
| `wgf-typed.js`, `wgf-typed.d.ts` | the typed layer: the same calls in the typed Haxe API's names (`Actor.setPosition(actor, ...)`, `Prefab.spawnAt(prefab, ...)`, `World.takeEvents(...)`), each type a frozen namespace of `wgf.js`'s own functions -- no wrappers, so nothing allocates -- each handle kind its branded type (`Actor`, `Prefab`), each enum an object of its values (`KeyboardKey.SPACE`), and `Scene.spawnPrefab`; generated |
| `src/runtime.js` | how a call crosses into the host and back; put at the top of `wgf.js` |
| `src/app.js` | `wgf_app_run` for JS, and the version check; put in `wgf.js` after the runtime |
| `src/wgf.d.ts` | the declarations of what `src/` writes by hand; put in `wgf.d.ts` |
| `tests/types.ts` | what TypeScript must accept, and the mistakes it must catch (`tools/check_js_binding.py`) |

The examples are `examples/js/`: `hello` (the C `app-hello`, call for call, on the typed layer) and `asteroids` (the Haxe game ported, flying the game's own playthrough, on the raw one).

## Use

```js
import createWgfHost from "./wgf-host.js";   // the host: tools/build_host.py, wgf.js beside it
import * as wgf from "./wgf.js";

wgf.attach(await createWgfHost({ canvas: document.getElementById("canvas") }));

let white = 0;
function init() {
    white = wgf.wgf_color_get(wgf.WGF_COLOR_STOCK_RAYWHITE);
    wgf.wgf_render_set_clear_color(white);
}
function frame() {
    wgf.wgf_draw_text(0, "hello", 40, 40, 32, wgf.wgf_color_get(wgf.WGF_COLOR_STOCK_DARKGRAY));
}

wgf.wgf_window_set_title("hello");
wgf.wgf_app_run(init, null, frame, null);
```

`attach` comes first: a call before it throws, naming the call. `wgf_app_run(init, tick, frame, shutdown)` takes JS functions, any of them null; it checks the host is the libwgf the binding was made from (`BUILT_VERSION`, major and minor) and refuses another, saying so. A throw out of a callback is logged and ends the run.

How each kind of value crosses -- text, arrays and typed arrays, bytes, vectors filled into the caller's object or array -- is [docs/BINDINGS.md](../../docs/BINDINGS.md)'s, as is how a handle kind is typed in TypeScript.

Or the same through the typed layer, its names the Haxe binding's:

```js
import createWgfHost from "./wgf-host.js";
import { App, Color, ColorStock, Draw, Render, Window, attach } from "./wgf-typed.js";

attach(await createWgfHost({ canvas: document.getElementById("canvas") }));
function init() {
    Render.setClearColor(Color.get(ColorStock.RAYWHITE));
}
function frame() {
    Draw.text(0, "hello", 40, 40, 32, Color.get(ColorStock.DARKGRAY));
}
Window.setTitle("hello");
App.run(init, null, frame, null);
```

Each member is the raw call itself, so it takes what the raw call takes, its handle first (`Actor.setPosition(actor, 1, 2, 3)`), and costs what it costs. In TypeScript an `Actor` can't go where a `Prefab` does. `Scene.spawnPrefab(scene, name, parent, x?, y?, z?, angle?)` finds the prefab and spawns it, placed when a place is given: it looks the prefab up on every call, so a hot path finds it once (`Scene.prefab`) and keeps the handle. A trimmed export keeps the members a program names (`Type.member`), and the calls they reach.

## Minifying

Everything the binding sends across a module boundary is a quoted key (`host["_wgf_..."]`, `into["x"]`), so a minifier that mangles property names leaves it alone. Two rules for a program's own code, as wgrender-c measured them for this binding's original: keep `wgf_` out of a mangling pattern (the calls are module exports, which a namespace import reads by name), and read vectors into an array if the program's own `x`, `y`, `z` get mangled.

## Release

A full host and the whole `wgf.js` are for development. A release ships a host trimmed to the calls its program makes, and `wgf.js` trimmed to the same (`tools/jsbinding.py`'s `trim`, which keeps whole sections, each marked `// wgf: `, and drops the comments): a Haxe game's export does it (`wgf export`), and a JS example's release build reads the calls and enums its modules name (`tools/webhost.py`'s `build_js_example`). What the binding costs, in time and in bytes, is measured: `tools/bench/measure_calls.py`, and the binding's share in `docs/benchmarks.md`.
