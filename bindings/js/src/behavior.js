// wgf: behaviors
// The JS binding's behaviors: the program's code on an actor, as the Haxe binding's wgf.Behavior
// is. Written by hand. tools/gen_binding.py puts it in wgf.js after the run, and a release copy
// keeps it only for a program that names Behavior, and the host each parameter call only for
// one that names its method (tools/jsbinding.py's behavior_calls).
//
// Nothing in C calls a behavior. The run takes the ecs's events before each of the program's
// ticks and frames (wgf_world_take_events) and makes, tells, and ends the objects from them.
// It starts doing so at the first Behavior.register or Behavior.tag, so a program with no
// behaviors can still take the events itself.

const behaviorTypes = new Map(); // name -> the class made for it
const behaviorTags = new Set();
const behaviorUnknown = new Set();
const behaviorLive = new Map(); // actor -> its objects, in the order added
let behaviorOrder = [];
const behaviorEvents = new Int32Array(4 * 64);

/**
 * A behavior: the program's code on an actor. Extend it, and register the class by name:
 *
 *     class Rock extends Behavior {
 *         onTick(dt) { Motion.setVelocity(this.actor, ...); }
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
 * `actor` is the behavior's actor, `id` its id among that actor's behaviors, and `name` the
 * name it was made for. A behavior of a name never registered is logged once and gets no object.
 * A class with its own constructor passes its arguments on: `constructor(...args) { super(...args); }`.
 */
export class Behavior {
    constructor(actor, id, name) {
        this.actor = actor;
        this.id = id;
        this.name = name;
    }

    /** The behavior was added. */
    onCreate() {}
    /** Each tick, with its length in seconds. */
    onTick(dt) {}
    /** Each frame, with its length in seconds. */
    onFrame(dt) {}
    /** The actor started to overlap `other`, whose collider's layer was `layer`. Both are alive. */
    onTriggerEnter(other, layer) {}
    /** The actor stopped overlapping `other`. */
    onTriggerExit(other, layer) {}
    /** The actor's body began to touch `other`'s, whose body layer was `layer`. Both are alive. */
    onContactBegin(other, layer) {}
    /** The actor's body stopped touching `other`'s. */
    onContactEnd(other, layer) {}
    /** The actor, a screen, came to the top of the stack. `from` is the screen it covers or replaced, or 0. */
    onScreenEnter(from) {}
    /** The actor, a screen, left the top of the stack. `to` is the screen now there, or 0. */
    onScreenExit(to) {}
    /** The behavior was removed or its actor is gone. `actor` may be dead from here. */
    onDestroy() {}

    /** Returns this behavior's parameter `key` as text, or "" if it has none. */
    getParam(key) { return wgf_behavior_get_param(this.actor, this.id, key); }
    /** Returns this behavior's parameter `key` as a number, or 0 if it has none or it isn't a number. */
    getParamNumber(key) { return wgf_behavior_get_param_number(this.actor, this.id, key); }
    /** Returns the actor this behavior's parameter `key` names ("@name"), or 0 if it names none. */
    getParamActor(key) { return wgf_behavior_get_param_actor(this.actor, this.id, key); }
    /** Returns whether this behavior has the parameter `key`. */
    hasParam(key) { return wgf_behavior_has_param(this.actor, this.id, key); }

    /** Makes a `type` for each behavior named `name`. A second call for a name replaces the first. */
    static register(name, type) {
        behaviorTypes.set(name, type);
        behaviors = behaviorRun;
    }

    /** Marks `name` as a tag: a behavior name with no class, which gets no object and no warning. */
    static tag(name) {
        behaviorTags.add(name);
        behaviors = behaviorRun;
    }

    /** Returns the first behavior on `actor` named `name` (any name if omitted), or null. */
    static of(actor, name) {
        const list = behaviorLive.get(actor >>> 0);
        if (!list) return null;
        for (const behavior of list) if (name === undefined || name === null || behavior.name === name) return behavior;
        return null;
    }

    /** Returns every behavior on `actor`, in the order added. */
    static all(actor) {
        return (behaviorLive.get(actor >>> 0) || []).slice();
    }

    /** Returns how many behaviors are live. */
    static count() {
        return behaviorOrder.length;
    }
}

function behaviorIsLive(behavior) {
    const list = behaviorLive.get(behavior.actor);
    return list !== undefined && list.includes(behavior);
}

function behaviorMake(actor, id) {
    if (!wgf_handle_is_alive(actor)) return; // gone again before this poll: its DESTROYED follows
    const name = wgf_behavior_get_name(actor, id);
    if (name === "") return;
    const type = behaviorTypes.get(name);
    if (!type) {
        if (!behaviorUnknown.has(name) && !behaviorTags.has(name)) {
            behaviorUnknown.add(name);
            console.warn(`wgf: no behavior registered as "${name}" (Behavior.register, or Behavior.tag for a tag)`);
        }
        return;
    }
    const behavior = new type(actor, id, name);
    let list = behaviorLive.get(actor);
    if (!list) behaviorLive.set(actor, list = []);
    list.push(behavior);
    behaviorOrder.push(behavior);
    behavior.onCreate();
}

function behaviorEnd(actor, id) {
    const list = behaviorLive.get(actor);
    if (!list) return;
    const at = list.findIndex((behavior) => behavior.id === id);
    if (at < 0) return;
    const behavior = list[at];
    list.splice(at, 1);
    if (list.length === 0) behaviorLive.delete(actor);
    behaviorOrder.splice(behaviorOrder.indexOf(behavior), 1);
    behavior.onDestroy();
}

// Tells an actor's behaviors of one event. One destroyed by a behavior told earlier is dropped.
function behaviorTell(actor, other, otherMustLive, tell) {
    const list = behaviorLive.get(actor);
    if (!list) return;
    for (const behavior of list.slice()) {
        if (!behaviorIsLive(behavior) || !wgf_handle_is_alive(actor)) break;
        if (otherMustLive && !wgf_handle_is_alive(other)) break;
        tell(behavior);
    }
}

// The ecs's events since the last poll: behaviors made, told, and ended. The numbers are
// wgf_world_event_t's.
function behaviorPoll() {
    for (;;) {
        const count = wgf_world_take_events(behaviorEvents);
        if (count === 0) break;
        for (let i = 0; i < count; i += 4) {
            const kind = behaviorEvents[i], actor = behaviorEvents[i + 1] >>> 0;
            const second = behaviorEvents[i + 2], layer = behaviorEvents[i + 3];
            const other = second >>> 0;
            if (kind === 1) behaviorMake(actor, second);
            else if (kind === 2) behaviorEnd(actor, second);
            else if (kind === 3) behaviorTell(actor, other, true, (b) => b.onTriggerEnter(other, layer));
            else if (kind === 4) behaviorTell(actor, other, true, (b) => b.onTriggerExit(other, layer));
            else if (kind === 5) behaviorTell(actor, other, true, (b) => b.onContactBegin(other, layer));
            else if (kind === 6) behaviorTell(actor, other, true, (b) => b.onContactEnd(other, layer));
            else if (kind === 7) behaviorTell(actor, other, false, (b) => b.onScreenEnter(other));
            else if (kind === 8) behaviorTell(actor, other, false, (b) => b.onScreenExit(other));
        }
    }
}

// What the run calls (app.js's `behaviors`), once a behavior is registered.
const behaviorRun = {
    tick() {
        behaviorPoll();
        const dt = wgf_loop_get_tick_delta();
        for (const behavior of behaviorOrder.slice()) {
            if (behaviorIsLive(behavior) && !wgf_actor_is_frozen(behavior.actor)) behavior.onTick(dt);
        }
    },
    frame() {
        behaviorPoll();
        const dt = wgf_loop_get_frame_delta();
        for (const behavior of behaviorOrder.slice()) {
            if (behaviorIsLive(behavior) && !wgf_actor_is_frozen(behavior.actor)) behavior.onFrame(dt);
        }
    },
    end() {
        for (let i = behaviorOrder.length - 1; i >= 0; i--) behaviorOrder[i].onDestroy();
        behaviorOrder = [];
        behaviorLive.clear();
    },
};
