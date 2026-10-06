// The declarations of what is written by hand (runtime.js, app.js): tools/gen_binding.py
// puts them in wgf.d.ts, before the generated ones.

/** The Emscripten module createWgfHost() resolves to: libwgf's web host. */
export interface WgfHost { readonly [name: string]: unknown }

/** Hand the binding its host: the module createWgfHost() resolved to. */
export declare function attach(module: WgfHost): void;

/** Whether attach() has been given a host. */
export declare function isAttached(): boolean;

/**
 * Whether the host is the libwgf this binding was generated from, major and minor; a
 * patch apart is compatible. False says why on the console.
 */
export declare function matchesHost(): boolean;

/**
 * Open the window and run, calling `init` once, `tick` at the tick rate (wgf_loop.h),
 * `frame` every frame, and `shutdown` after the last; any may be null. False when the
 * host is another major or minor version than the binding (said on the console), or C
 * refuses (a run already going). On the web it returns at once and the browser runs
 * the frames. A throw out of a callback is logged and ends the run.
 */
export declare function wgf_app_run(init: (() => void) | null, tick: (() => void) | null,
                                    frame: (() => void) | null, shutdown: (() => void) | null): boolean;
