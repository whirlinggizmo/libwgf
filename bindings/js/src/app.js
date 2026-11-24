// The JS binding's run: wgf_app_run for JS, the one call that takes callbacks. Written by
// hand; tools/gen_binding.py puts it in wgf.js after the runtime, so it is never loaded
// on its own.
//
// C takes four function pointers, so four trampolines enter the wasm table, once: each
// reads its handler when it fires, so a handler changed by a later wgf_app_run (a hot
// reload's) is the one that runs, and no second function enters the table, which can't
// take one back. Each also owns what C can't do for a JS program:
//
// - it catches: a throw unwinding into the host's frames freezes the page on an opaque
//   "Uncaught [object WebAssembly.Exception]". It is logged and ends the run (as the
//   Haxe binding's does), so it is seen rather than repeated every frame;
// - it runs the behaviors (behavior.js) before the program's tick, frame, and shutdown, once
//   the program has registered one;
// - it restores the wasm stack when the callback ends, fault or not, so a call that
//   threw between saving and restoring the stack can't leave it moved.

const handlers = [null, null, null, null];
const NAMES = ["init", "tick", "frame", "shutdown"];
let trampolines = null;
let faulted = false;
// The behaviors' runner (behavior.js), set at the first Behavior.register or Behavior.tag.
let behaviors = null;

function dispatch(which) {
    if (faulted && which !== 3) return;
    const mark = host["stackSave"]();
    try {
        if (behaviors !== null) {
            if (which === 1) behaviors.tick();
            else if (which === 2) behaviors.frame();
            else if (which === 3) behaviors.end();
        }
        const handler = handlers[which];
        if (handler) handler();
    } catch (e) {
        faulted = true;
        console.error(`wgf: uncaught exception in ${NAMES[which]}:`, e);
        host["_wgf_app_quit"]();
    }
    host["stackRestore"](mark);
}

/**
 * Whether the host is the libwgf this binding was generated from, major and minor; a
 * patch apart is compatible. A page fetches and caches the host and the binding apart,
 * so it can pair an old one with a new one: this says so, rather than calls landing on
 * the wrong functions.
 */
export function matchesHost() {
    const major = host["_wgf_version_get_major"](), minor = host["_wgf_version_get_minor"]();
    if (major === BUILT_VERSION["major"] && minor === BUILT_VERSION["minor"]) return true;
    console.error(`wgf: the JS binding is for libwgf ${BUILT_VERSION["major"]}.${BUILT_VERSION["minor"]}, `
        + `the host is ${major}.${minor}: load the host the binding was made with (or regenerate it, `
        + `tools/gen_binding.py)`);
    return false;
}

/**
 * Open the window and run, calling `init` once, `tick` at the tick rate (wgf_loop.h),
 * `frame` every frame, and `shutdown` after the last; any may be null. False when the
 * host is another major or minor version than the binding (said on the console), or C
 * refuses (a run already going). On the web it returns at once and the browser runs
 * the frames. C's `user` pointer has no use in JS, where a closure carries its state.
 */
export function wgf_app_run(init, tick, frame, shutdown) {
    handlers[0] = init || null;
    handlers[1] = tick || null;
    handlers[2] = frame || null;
    handlers[3] = shutdown || null;
    if (!matchesHost()) return false;
    if (trampolines === null) {
        trampolines = [0, 1, 2, 3].map((which) => host["addFunction"](() => dispatch(which), "vi"));
    }
    faulted = false;
    // a host with the autopilot's runner lets an autopilot fly the run; a game's web export
    // leaves it out (`wgf export`), so its host has none
    if (typeof host["_wgf_app_enable_autopilot"] === "function") host["_wgf_app_enable_autopilot"]();
    // a host that can load assets again in place keeps what a reload needs; a game's web
    // export leaves the call out (`wgf export`), so its host keeps nothing for one
    if (typeof host["_wgf_scene_enable_reload"] === "function") host["_wgf_scene_enable_reload"]();
    else if (typeof host["_wgf_asset_enable_reload"] === "function") host["_wgf_asset_enable_reload"]();
    return host["_wgf_app_run"](trampolines[0], trampolines[1], trampolines[2], trampolines[3], 0) !== 0;
}
