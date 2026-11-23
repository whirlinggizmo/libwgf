// The declarations of what is written by hand (runtime.js, app.js, behavior.js): tools/gen_binding.py
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

/**
 * A behavior: the program's code on an actor. Extend it, and register the class by name:
 *
 *     class Rock extends Behavior {
 *         onTick(dt: number) { Motion.setVelocity(this.actor, ...); }
 *     }
 *     Behavior.register("Rock", Rock);
 *
 * An actor gets a behavior from wgf_actor_add_behavior (the typed layer's Actor.addBehavior) or a
 * scene's `behavior name=...` line. The run makes one object for each, before the next tick or
 * frame, and calls what the class overrides:
 *
 * - `onCreate` once, before the next tick or frame, never inside the call that added it.
 * - `onTick` and `onFrame` before the program's own, skipped while the actor is frozen.
 * - `onTriggerEnter` and `onTriggerExit` for its actor's collider.
 * - `onContactBegin` and `onContactEnd` for its actor's body.
 * - `onScreenEnter` and `onScreenExit` when its actor is a screen.
 * - `onDestroy` when the behavior is removed, its actor is destroyed, or the run ends.
 *
 * The run takes the ecs's events itself (wgf_world_take_events) from the first `register` or
 * `tag` on. A program with behaviors doesn't take them too. A class with its own constructor
 * passes its arguments on: `constructor(...args: [wgf_actor_t, number, string]) { super(...args); }`.
 */
export declare class Behavior {
    constructor(actor: wgf_actor_t, id: number, name: string);
    /** The actor this behavior is on. */
    readonly actor: wgf_actor_t;
    /** Its id among the actor's behaviors, for the wgf_behavior_* calls. */
    readonly id: number;
    /** The name it was made for. */
    readonly name: string;
    /** The behavior was added. */
    onCreate(): void;
    /** Each tick, with its length in seconds. */
    onTick(dt: number): void;
    /** Each frame, with its length in seconds. */
    onFrame(dt: number): void;
    /** The actor started to overlap `other`, whose collider's layer was `layer`. Both are alive. */
    onTriggerEnter(other: wgf_actor_t, layer: number): void;
    /** The actor stopped overlapping `other`. */
    onTriggerExit(other: wgf_actor_t, layer: number): void;
    /** The actor's body began to touch `other`'s, whose body layer was `layer`. Both are alive. */
    onContactBegin(other: wgf_actor_t, layer: number): void;
    /** The actor's body stopped touching `other`'s. */
    onContactEnd(other: wgf_actor_t, layer: number): void;
    /** The actor, a screen, came to the top of the stack. `from` is the screen it covers or replaced, or 0. */
    onScreenEnter(from: wgf_actor_t | 0): void;
    /** The actor, a screen, left the top of the stack. `to` is the screen now there, or 0. */
    onScreenExit(to: wgf_actor_t | 0): void;
    /** The behavior was removed or its actor is gone. `actor` may be dead from here. */
    onDestroy(): void;
    /** Returns this behavior's parameter `key` as text, or "" if it has none. */
    getParam(key: string): string;
    /** Returns this behavior's parameter `key` as a number, or 0 if it has none or it isn't a number. */
    getParamNumber(key: string): number;
    /** Returns the actor this behavior's parameter `key` names ("@name"), or 0 if it names none. */
    getParamActor(key: string): wgf_actor_t;
    /** Returns whether this behavior has the parameter `key`. */
    hasParam(key: string): boolean;
    /** Makes a `type` for each behavior named `name`. A second call for a name replaces the first. */
    static register(name: string, type: new (actor: wgf_actor_t, id: number, name: string) => Behavior): void;
    /** Marks `name` as a tag: a behavior name with no class, which gets no object and no warning. */
    static tag(name: string): void;
    /** Returns the first behavior on `actor` named `name` (any name if omitted), or null. */
    static of<T extends Behavior = Behavior>(actor: wgf_actor_t | 0, name?: string | null): T | null;
    /** Returns every behavior on `actor`, in the order added. */
    static all(actor: wgf_actor_t | 0): Behavior[];
    /** Returns how many behaviors are live. */
    static count(): number;
}
