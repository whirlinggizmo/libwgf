---
name: add-binding-call
description: Carry a new or changed public C call into the bindings (JS and Haxe): regenerate, check coverage and staleness, reach it in the feature test, and run both bindings' tests on every target. Use after adding or changing any WGF_API function or public enum.
---

# Adding a call to the bindings

Both bindings, the JS binding and the Haxe binding on it, are generated from the headers (docs/BINDINGS.md says how each kind of call maps); this is never hand-written. A new public call needs, in order:

1. **The C call keeps the hard rule** (CONVENTIONS.md, "Public API"): handles typed by kind, math values returned only, text as `const char *`, a byte span or a caller-owned array with its count. `python3 tools/check_api.py`; a setter with no getter is either given one or listed in its `GETTERS_EXEMPT` with the reason.
2. **Its header comment** is its doc in JS and Haxe too: the first comment above it, or its group's. Say what it does now and every refusal.
3. **Regenerate**: `python3 tools/gen_binding.py`. It places the call by its name and first parameter -- a kind's method (`wgf_node_*` taking a node: `Node.method`), a section over a kind (`Shape2d`), an enum abstract, or a static of its header's section. If it stops with a call it can't map, the C call breaks a rule the binding relies on: fix the call, or (with a reason) add it to the generator's `SKIPPED`, which the runtime must then reach by hand.
4. **Check**: `python3 tools/check_binding.py` -- nothing stale, every exported call reached exactly once (coverage), and the Haxe binding's test on hxcpp, under node, and in a browser (through the JS binding); and `python3 tools/check_js_binding.py` -- the JS binding's declarations under TypeScript, and the JS examples in a browser.
5. **Reach it**: call it in the feature test (`examples/haxe/feature-test/Main.hx`), checking its answer where it is the same on every target; `python3 tools/check_features.py` fails naming any call never made.
6. **A new kind of value** (not a handle, number, bool, enum, text, vector, byte span, or array) needs the generator taught first: `Binding.params_of` and `result_of` in `tools/gen_binding.py`, the JS binding's (`tools/jsbinding.py`'s `Emitter.call`, and `bindings/js/src/runtime.js` for the crossing), both Haxe targets' raw code (`js_raw`, over the JS binding, and `cpp_raw`), `impl/Host.js.hx` and `Host.cpp.hx`, and docs/BINDINGS.md's "How values cross", with a test in `bindings/haxe/test/Main.hx` and a line in `bindings/js/tests/types.ts`.
7. **The web host's exports** (`hosts/web/exports.json`) are regenerated with the rest; commit the generated files with the header change, in one commit. Then `python3 tools/verify_builds.py --web --windows sightblinder`.
