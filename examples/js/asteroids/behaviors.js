// An object for each entity whose behavior has a name: what the Haxe binding's wgf.Script
// does for a Haxe program, here in the program, on the JS binding alone. The objects are
// made, told of their triggers, and ended from the ecs's events (wgf_ecs_take_events),
// taken each tick and frame before the program's own: nothing in C calls them.
import * as wgf from "./wgf.js";

/** A behavior's object: the program's code for one entity. Override what it needs. */
export class Behavior {
    constructor(entity) {
        this.entity = entity;
    }
    /** Its entity was made (its components set, its scene's lines applied). */
    onCreate() {}
    /** Each tick, before the program's, with the tick's length in seconds. */
    onTick(dt) {}
    /** Each frame, before the program's. */
    onFrame(dt) {}
    /** Its entity started to overlap `other`'s (both have colliders). */
    onTriggerEnter(other) {}
    /** Its entity stopped overlapping `other`'s. */
    onTriggerExit(other) {}
    /** Its entity is gone: `entity` is stale from here. */
    onDestroy() {}
}

const factories = new Map();
const live = new Map();
let order = [];
const unknown = new Set();
const events = new Int32Array(3 * 64);

/** The class to make for each entity whose behavior is named `name`. */
export function register(name, factory) {
    factories.set(name, factory);
}

/** The object of `entity`, or undefined when it has none. */
export function of(entity) {
    return live.get(entity);
}

function make(entity) {
    if (live.has(entity) || !wgf.wgf_entity_is_alive(entity)) return;
    const name = wgf.wgf_behavior_get_name(entity);
    const factory = factories.get(name);
    if (factory === undefined) {
        if (name !== "" && !unknown.has(name)) {
            unknown.add(name);
            wgf.wgf_log_message(wgf.WGF_LOG_LEVEL_WARN, `no behavior registered for "${name}"`);
        }
        return;
    }
    const object = factory(entity);
    live.set(entity, object);
    order.push(object);
    object.onCreate();
}

function poll() {
    for (;;) {
        const n = wgf.wgf_ecs_take_events(events);
        if (n === 0) break;
        for (let i = 0; i < n; i += 3) {
            const kind = events[i];
            const entity = events[i + 1] >>> 0, other = events[i + 2] >>> 0; // handles, unsigned
            const object = live.get(entity);
            if (kind === wgf.WGF_ECS_EVENT_CREATED) {
                make(entity);
            } else if (kind === wgf.WGF_ECS_EVENT_DESTROYED && object) {
                live.delete(entity);
                order.splice(order.indexOf(object), 1);
                object.onDestroy();
            } else if (kind === wgf.WGF_ECS_EVENT_TRIGGER_ENTER && object) {
                object.onTriggerEnter(other);
            } else if (kind === wgf.WGF_ECS_EVENT_TRIGGER_EXIT && object) {
                object.onTriggerExit(other);
            }
        }
    }
}

/** Before the program's tick: the events taken, then every object's tick. */
export function tickAll(dt) {
    poll();
    for (const object of order.slice()) if (live.has(object.entity)) object.onTick(dt);
}

/** Before the program's frame: the same, with frames. */
export function frameAll(dt) {
    poll();
    for (const object of order.slice()) if (live.has(object.entity)) object.onFrame(dt);
}

/** The run is ending: every object ended, newest first. */
export function endAll() {
    for (let i = order.length - 1; i >= 0; i--) order[i].onDestroy();
    order = [];
    live.clear();
}
