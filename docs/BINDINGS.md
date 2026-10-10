# How a binding maps libwgf

The rules a binding follows to carry libwgf's C calls into another language, stated here only (CONVENTIONS.md, "Bindings"). libwgf has two bindings, both generated from the headers by `tools/gen_binding.py`:

- **The JS binding** (`bindings/js/`): an ES module, `wgf.js`, with TypeScript's declarations beside it, `wgf.d.ts`. It is the one way JS reaches the wasm host, libwgf linked for the web with no `main` (`hosts/web/`), and a public API of its own, for JS and TypeScript programs.
- **The Haxe binding** (`bindings/haxe/`, haxelib `wgf`): on the web a Haxe program compiles to JS and calls the JS binding, so every Haxe program, test, and autopilot run exercises it; natively it compiles through hxcpp and links libwgf's staged archive, calling C itself.

How each piece works is ARCHITECTURE.md's ("The bindings"); why, HISTORY.md's.

## What a binding sees

- Only exported functions (`WGF_API`) and enums. A macro or a `static inline` function in a header only renames or wraps exported ones, so a binding needs none of them; libwgf's math is header-only, and a binding has its own.
- Every parameter and result is one of the hard rule's kinds (CONVENTIONS.md, "Public API"): a handle, a number, a bool, an enum, text, a math value returned, a byte span, or a caller-owned array with its count. Each maps as one rule below, so a new call needs no hand work.

## One name per C call

- Every exported call is exactly one member of the binding's API, or is listed, with its reason, as left to the binding's runtime (`wgf_app_run`, whose callbacks are C function pointers).
- A member calls exactly one C function; sugar (`isNone`) calls none, and reaches C, when it does, only through a member.
- **On the web, a call that is arithmetic on its arguments alone is worked by the JS binding itself**, where a crossing into wasm costs more than the work: the colors' calls and the trigonometry (`LOCAL` in `tools/jsbinding.py`, each with its reason). Its `wgf.js` function is written by hand in `bindings/js/src/local.js` to the same bits as C's (rounding to a 32-bit float wherever C's float arithmetic does, the trigonometry's polynomials in double), so a JS program and Haxe's JS target, which reaches C through this binding alone, share the one path, and a trimmed host links none of their C. Natively Haxe calls C, a function call. `tools/check_js_binding.py`'s `local` step holds every one to the host's C (every stock color, colors made and lerped over a grid, the trigonometry's sweep to `wgf_math_test`'s hash, and its edges), and the Haxe binding's test holds the trigonometry to the same hash on hxcpp, under node, and in a browser.
- So each call's documentation, its "false for ..." sentence included, has one home, coverage is checked call by call (`tools/check_binding.py`, "coverage"), and nothing reaches C by a second path that skips a check. Haxe's JS target reaches C through the JS binding alone: when Haxe needs something the JS binding can't express cheaply, the JS binding grows it.
- Names: in JS, a call keeps its C name (`wgf_actor_set_position`), its header comment its JSDoc, so the header is the documentation for both. In Haxe, a member is its C name less its prefix, in camel case (`wgf_actor_set_position` is `Actor.setPosition`).

## Where a call goes

- **In JS**, every call is a function of the one module, and every enum value a constant of it (`WGF_KEY_UP`); beside it the typed layer (`wgf-typed.js`) puts the same functions in the typed Haxe API's names and places (`Actor.setPosition(actor, ...)`, `World.takeEvents`), each type a frozen namespace of the raw functions themselves, no wrapper objects, its enums objects of their values, so a JS program and a Haxe one read alike (bindings/js/README.md). Haxe's JS target keeps the raw layer. In TypeScript a handle kind is a branded number of its own (`wgf_texture_t`, so a texture is not an actor, and a plain number is neither; 0, none, is any kind), an enum the union of its values, and a returned vector `wgf_vec2_t`, `wgf_vec3_t`, or `wgf_vec4_t`.
- In Haxe:
  - **A handle kind** (`wgf_<kind>_t`) is a type of its own, an abstract over the 32-bit handle, with `isNone()`: 0 is none, never null (on hxcpp a nullable handle boxes; on JS an unset field is `undefined`, which `== 0` doesn't call none); and `isAlive()` (`wgf_handle_is_alive`), what code that expects a handle may be dead asks first. A member's name is its C name's, but where `MEMBER_NAMES` in the generator says why (`scene.prefab("bullet")`, `wgf_scene_find_prefab`); a type's sugar over its calls is written in the generator once for both bindings (`scene.spawnPrefab(name, parent, ?x, ?y, ?z, ?angle)`). A call named for a kind is that kind's: a method when it takes one first (`actor.setPosition`), a static otherwise (`Texture.create`).
  - **A section over a kind** -- every call of it takes that kind first, or makes one -- is a type over the kind that keeps all of the kind's methods: a `Shape2d` is an `Actor` with the shape's calls, and `Shape2d.create()` makes one. Components are the same: `(ship : Motion).setVelocity(...)`.
  - **Any other call** is a static of its header's section (`Draw.text`, `Ui.button`), and a call taking any handle (`wgf_handle_t`) stays a static (`Resource.release`).
  - **A section named like a runtime type** takes another name, listed with its reason in the generator (`TYPE_NAMES`): `wgf_behavior_*`, an actor's behaviors' calls, is `BehaviorComponent`, since `Behavior` is the runtime's base class for a game's behaviors (SPEC's name), which reaches its own parameters through it (`getParamNumber("size")`).
  - **An enum** is an enum abstract over its int, its values less their shared prefix (`ActorKind.STAGE2D`; a value that would start with a digit keeps the prefix's last word: `KEY_0`).

## How values cross

On the web the JS binding does the crossing, for a JS program and a Haxe one alike; Haxe's JS target hands it Haxe's values as they are, and keeps only what is Haxe's own (a vector into a `Vec`, `Bytes` to and from a `Uint8Array`).

- **Numbers, bools, enums, handles, colors:** as they are. A color is its 32 bits as an int. In JS a `uint32_t` (a handle, a color) comes back unsigned, so it equals the constant it was made from; Haxe makes it its 32-bit Int again (`| 0`).
- **Text in** is copied for the call: on the web onto the wasm stack, released as the call returns; natively, the string's own UTF-8. Null passes as `NULL`, which some calls tell from "".
- **Text out** is copied as it returns. C's text is valid only until it changes, so a binding never keeps the pointer.
- **A returned math value** comes back as the binding's own vector type. Its getter takes one to fill (`getPosition(into)`; in JS an object, filled by `x`, `y`, `z`, or an array or typed array, filled by index), so a loop calling it makes none. On the web it comes through one result slot, `_malloc`'d once and read out at once. Haxe's JS target passes one kept array, read by index, which no minifier can rename.
- **A byte span in** is copied for the call (in JS a `Uint8Array` or `ArrayBuffer`); **out** (`_get_data` beside `_get_size`) is copied out whole as the call returns (a `Uint8Array`).
- **A caller-owned array** is the binding's own array, its length the count (in JS an array or a typed array). On the web it is copied into a slot of its own (a call's first array slot 0, its second slot 1), each `_malloc`'d once and grown when too small, and an array C fills is copied back before the call returns. Natively, ints and handles pass as the array holds them; floats are copied, since a Haxe array of floats holds doubles.

## The web host

- **Nothing piles up on the wasm stack.** A string is on the stack for its call alone; vectors and arrays use the slots. An arena held for a whole frame overflowed the 64 KB stack at 5,000 vector getters (wgrender-c, 2026-10-02); the binding's test calls one 100,000 times in a frame.
- **Never hold a heap view.** The host grows its memory, which replaces its views; every read names `HEAPF32` (or the rest) where it reads.
- **Every name into another module is a quoted key** -- the JS binding's into the host (`host["_wgf_node_set_position"]`, `host["HEAPF32"]`), into an object a caller passed (`into["x"]`), and Haxe's into the JS binding (`Raw.binding["wgf_actor_set_position"]`) -- so a minifier can't rename it. A call before the host is attached says so, naming the call.
- **The full host** exports every call (`hosts/web/exports.json`, which the generator writes from the headers), with the whole JS binding beside it, for development and hot reload; a game's export links a trimmed one, exporting only what its program calls, and the JS binding beside it is trimmed to the same calls (`tools/jsbinding.py`). Either keeps what the binding's run needs: `wgf_app_run`, `wgf_app_quit`, the version's major and minor, `_malloc`, and `_free`.

## The runtime

- **One run, the one callback.** `wgf_app_run(init, tick, frame, shutdown)` -- the JS binding's, which takes JS functions, and Haxe's `Runtime.run`, which calls it on the web and C itself natively -- installs four trampolines in C once (on the web, four `addFunction`s); each reads the handler it dispatches to when it fires, so a handler set later, or swapped by a hot reload, is the one that runs, and nothing more enters the wasm table. On the web each trampoline restores the wasm stack when its handler ends.
- **A throw stops at the trampoline.** Unwinding through C would leave libwgf mid-call, so the trampoline catches, logs the error and its stack, and ends the run.
- **No callback otherwise: polled events.** What C reports (a behavior added to an actor, its actor's triggers, its end) a program takes in bulk each tick and frame (`wgf_world_take_events`). The Haxe binding's runtime dispatches them to the behaviors registered by their name (`Behavior.register`); a JS program dispatches its own (`examples/js/asteroids/behaviors.js`).
- **Version stamps.** Each binding holds the version and a digest of the headers it was made from (`BUILT_VERSION` in `wgf.js`, the generated `BuiltVersion` in Haxe). The run checks the host's version first and refuses another major or minor (a patch apart is compatible), since a page's host, binding, and program are cached apart and a returning visit can pair an old one with a new one; Haxe's run on the web also refuses a JS binding made from other headers than its own.

## Checks

`tools/check_binding.py` (the Haxe binding) and `tools/check_js_binding.py` (the JS binding) run them all, each step skipped, and said so, where this machine can't:

- **generated:** `tools/gen_binding.py --check` -- nothing generated is stale against the headers.
- **coverage:** every exported call reached exactly once; no member calls C twice.
- **hxcpp**, **node**, **browser:** the Haxe binding's test (`bindings/haxe/test/Main.hx`) on hxcpp against the staged headless archive, under node on the headless web host, and in a browser on the full host in its page, through the JS binding. Verification runs hxcpp on Windows too (MSVC).
- **types:** the JS binding's declarations under TypeScript `--strict` (`bindings/js/tests/types.ts`): what a program should write compiles, and each mistake marked as one is caught.
- **examples:** each JS example (`examples/js/<name>/`) in a browser, flown by its autopilot to a PASS: hello, and Asteroids ported from the Haxe game, flying the game's own playthrough.
- **The cost:** `tools/bench/measure_calls.py` times a call from Haxe and from JS (both through the JS binding), and the size table gives the binding's share of each program (`docs/benchmarks.md`).
