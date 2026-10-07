// An object for each behavior on an actor: what the Haxe binding's wgf.Behavior does for a
// Haxe program, here in the program, on the JS binding alone. An actor may have several
// behaviors; each object is made, told of its actor's triggers, and ended from the ecs's
// events (wgf_ecs_take_events), taken each tick and frame before the program's own:
// nothing in C calls them.
import * as wgf from "./wgf.js";

/** A behavior's object: the program's code for one behavior on an actor. Override what it needs. */
export class Behavior {
    constructor(actor) {
        this.actor = actor;
        this.id = 0; // its id among the actor's behaviors, set as it is made
    }
    /** It was added (its actor's lines applied). */
    onCreate() {}
    /** Each tick, before the program's, with the tick's length in seconds. */
    onTick(dt) {}
    /** Each frame, before the program's. */
    onFrame(dt) {}
    /** Its actor started to overlap `other` (both have colliders). */
    onTriggerEnter(other) {}
    /** Its actor stopped overlapping `other`. */
    onTriggerExit(other) {}
    /** It was removed, or its actor is gone: `actor` may be stale from here. */
    onDestroy() {}
}

const factories = new Map();
const live = new Map(); // actor -> its objects, in the order added
let order = [];
const unknown = new Set();
const events = new Int32Array(3 * 64);

/** The class to make for each behavior named `name`. */
export function register(name, factory) {
    factories.set(name, factory);
}

/** The first object on `actor` (named `name`, when given), or undefined when it has none. */
export function of(actor, name) {
    const list = live.get(actor);
    return list ? list.find((object) => name === undefined || object.name === name) : undefined;
}

function make(actor, id) {
    const name = wgf.wgf_behavior_get_name(actor, id);
    if (name === "") return; // gone again before this poll: its DESTROYED follows
    const factory = factories.get(name);
    if (factory === undefined) {
        if (!unknown.has(name)) {
            unknown.add(name);
            wgf.wgf_log_message(wgf.WGF_LOG_LEVEL_WARN, `no behavior registered for "${name}"`);
        }
        return;
    }
    const object = factory(actor);
    object.name = name;
    object.id = id;
    if (!live.has(actor)) live.set(actor, []);
    live.get(actor).push(object);
    order.push(object);
    object.onCreate();
}

function end(actor, id) {
    const list = live.get(actor);
    const object = list ? list.find((o) => o.id === id) : undefined;
    if (object === undefined) return;
    list.splice(list.indexOf(object), 1);
    if (list.length === 0) live.delete(actor);
    order.splice(order.indexOf(object), 1);
    object.onDestroy();
}

function poll() {
    for (;;) {
        const n = wgf.wgf_ecs_take_events(events);
        if (n === 0) break;
        for (let i = 0; i < n; i += 3) {
            const kind = events[i];
            const actor = events[i + 1] >>> 0, second = events[i + 2] >>> 0; // handles, unsigned
            if (kind === wgf.WGF_ECS_EVENT_CREATED) {
                make(actor, second);
            } else if (kind === wgf.WGF_ECS_EVENT_DESTROYED) {
                end(actor, second);
            } else if (kind === wgf.WGF_ECS_EVENT_TRIGGER_ENTER || kind === wgf.WGF_ECS_EVENT_TRIGGER_EXIT) {
                for (const object of (live.get(actor) || []).slice()) {
                    if (kind === wgf.WGF_ECS_EVENT_TRIGGER_ENTER) object.onTriggerEnter(second);
                    else object.onTriggerExit(second);
                }
            }
        }
    }
}

function liveNow(object) {
    const list = live.get(object.actor);
    return list !== undefined && list.includes(object);
}

/** Before the program's tick: the events taken, then every object's tick. */
export function tickAll(dt) {
    poll();
    for (const object of order.slice()) if (liveNow(object)) object.onTick(dt);
}

/** Before the program's frame: the same, with frames. */
export function frameAll(dt) {
    poll();
    for (const object of order.slice()) if (liveNow(object)) object.onFrame(dt);
}

/** The run is ending: every object ended, newest first. */
export function endAll() {
    for (let i = order.length - 1; i >= 0; i--) order[i].onDestroy();
    order = [];
    live.clear();
}
