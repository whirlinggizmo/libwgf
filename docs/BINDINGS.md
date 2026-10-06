# How a binding maps libwgf

The rules a binding follows to carry libwgf's C calls into another language, stated here only (CONVENTIONS.md, "Bindings"). libwgf has one binding, Haxe's (`bindings/haxe/`, haxelib `wgf`): on the web a Haxe program compiles to JS and calls a prebuilt wasm host, libwgf linked with no `main` (`hosts/web/`); natively it compiles through hxcpp and links libwgf's staged archive. How each piece works is ARCHITECTURE.md's ("The Haxe binding"); why, HISTORY.md's.

## What a binding sees

- Only exported functions (`WGF_API`) and enums. A macro or a `static inline` function in a header only renames or wraps exported ones, so a binding needs none of them; libwgf's math is header-only, and a binding has its own.
- Every parameter and result is one of the hard rule's kinds (CONVENTIONS.md, "Public API"): a handle, a number, a bool, an enum, text, a math value returned, a byte span, or a caller-owned array with its count. Each maps as one rule below, so a new call needs no hand work.

## One name per C call

- Every exported call is exactly one member of the binding's API, or is listed, with its reason, as left to the binding's runtime (`wgf_app_run`, whose callbacks are C function pointers).
- A member calls exactly one C function; sugar (`isNone`) calls none, and reaches C, when it does, only through a member.
- So each call's documentation, its "false for ..." sentence included, has one home, coverage is checked call by call (`tools/check_binding.py`, "coverage"), and nothing reaches C by a second path that skips a check.
- Names: a member is its C name less its prefix, in camel case (`wgf_node_set_position` is `Node.setPosition`).

## Where a call goes

- **A handle kind** (`wgf_<kind>_t`) is a type of its own, an abstract over the 32-bit handle, with `isNone()`: 0 is none, never null (on hxcpp a nullable handle boxes; on JS an unset field is `undefined`, which `== 0` doesn't call none). A call named for a kind is that kind's: a method when it takes one first (`node.setPosition`), a static otherwise (`Texture.create`).
- **A section over a kind** -- every call of it takes that kind first, or makes one -- is a type over the kind that keeps all of the kind's methods: a `Shape2d` is a `Node` with the shape's calls, and `Shape2d.create()` makes one. Components are the same: `(ship : Motion).setVelocity(...)`.
- **Any other call** is a static of its header's section (`Draw.text`, `Ui.button`), and a call taking any handle (`wgf_handle_t`) stays a static (`Resource.release`).
- **An enum** is an enum abstract over its int, its values less their shared prefix (`NodeType.CANVAS`; a value that would start with a digit keeps the prefix's last word: `KEY_0`).

## How values cross

- **Numbers, bools, enums, handles, colors:** as they are. A color is its 32 bits as an int.
- **Text in** is copied for the call: on the web onto the wasm stack, released as the call returns; natively, the string's own UTF-8. Null passes as `NULL`, which some calls tell from "".
- **Text out** is copied as it returns. C's text is valid only until it changes, so a binding never keeps the pointer.
- **A returned math value** comes back as the binding's own vector type. Its getter takes one to fill (`getPosition(into)`), so a loop calling it makes none. On the web it comes through one result slot, `_malloc`'d once and read out at once.
- **A byte span in** is copied for the call; **out** (`_get_data` beside `_get_size`) is copied out whole as the call returns.
- **A caller-owned array** is the binding's own array, its length the count. On the web it is copied into a slot of its own (a call's first array slot 0, its second slot 1), each `_malloc`'d once and grown when too small, and an array C fills is copied back before the call returns. Natively, ints and handles pass as the array holds them; floats are copied, since a Haxe array of floats holds doubles.

## The web host

- **Nothing piles up on the wasm stack.** A string is on the stack for its call alone; vectors and arrays use the slots. An arena held for a whole frame overflowed the 64 KB stack at 5,000 vector getters (wgrender-c, 2026-10-02); the binding's test calls one 100,000 times in a frame.
- **Never hold a heap view.** The host grows its memory, which replaces its views; every read names `HEAPF32` (or the rest) where it reads.
- **Every name into the host is a quoted key** (`host["_wgf_node_set_position"]`, `host["HEAPF32"]`), so a minifier can't rename it. A call before the host is attached says so, naming the call.
- **The full host** exports every call (`hosts/web/exports.json`, which the generator writes from the headers), for development and hot reload; a game's export links a trimmed one, exporting only what its program calls. Either keeps the calls the runtime needs: the version, `wgf_app_run`, `_malloc`, `_free`.

## The runtime

- **One run, the one callback.** `Runtime.run(init, tick, frame, shutdown)` installs four trampolines in C once (on the web, four `addFunction`s); each reads the handler it dispatches to when it fires, so a handler set later, or swapped by a hot reload, is the one that runs, and nothing more enters the wasm table.
- **A throw stops at the trampoline.** Unwinding through C would leave libwgf mid-call, so the trampoline catches, logs the error and its stack, and ends the run.
- **No callback otherwise: polled events.** What C reports (a script's entity made, its triggers, its end) the runtime takes in bulk each tick and frame (`Ecs.takeEvents`) and dispatches in Haxe, to the scripts registered by their behavior's name (`Script.register`).
- **Version stamps.** The generated `BuiltVersion` holds the version and a digest of the headers the binding was made from. The run checks the library's version first and refuses another major or minor (a patch apart is compatible), since a page's host and program are cached apart and a returning visit can pair an old one with a new one.

## Checks

`tools/check_binding.py` runs them all, each step skipped, and said so, where this machine can't:

- **generated:** `tools/gen_binding.py --check` -- nothing generated is stale against the headers.
- **coverage:** every exported call reached exactly once; no member calls C twice.
- **hxcpp**, **node**, **browser:** the binding's test (`bindings/haxe/test/Main.hx`) on hxcpp against the staged headless archive, under node on the headless web host, and in a browser on the full host in its page. Verification runs hxcpp on Windows too (MSVC).
