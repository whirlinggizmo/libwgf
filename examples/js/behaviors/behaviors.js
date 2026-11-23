// Behaviors for a JS program: an object for each behavior on an actor, made, told of its
// actor's triggers, and ended from the ecs's events (World.takeEvents). The JS binding has
// no behavior class of its own, so the program keeps this module (examples/js/asteroids has
// the same on the raw calls). The Haxe binding's wgf.Behavior does the same for Haxe.
import { BehaviorComponent, Handle, Log, LogLevel, World, WorldEvent } from "./wgf-typed.js";

/** A behavior's object: the program's code for one behavior on an actor. Override what it needs. */
export class Behavior {
    constructor(actor) {
        this.actor = actor;
        this.id = 0; // its id among the actor's behaviors, set as it is made
    }
    /** It was added. It runs at the next tick or frame, never inside the call that added it. */
    onCreate() {}
    /** Each tick, before the program's, with the tick's length in seconds. */
    onTick(dt) {}
    /** Each frame, before the program's, with the frame's length in seconds. */
    onFrame(dt) {}
    /** Its actor started to overlap `other`, whose collider's layer is `layer`. Both are alive. */
    onTriggerEnter(other, layer) {}
    /** Its actor stopped overlapping `other`, of `layer`. */
    onTriggerExit(other, layer) {}
    /** It was removed, or its actor is gone: `actor` may be stale from here. */
    onDestroy() {}
}

const factories = new Map(); // name -> the function that makes its object
const live = new Map(); // actor -> its objects, in the order added
let order = [];
const events = new Int32Array(4 * 64);

/** Makes `factory(actor)` for each behavior named `name`. */
export function register(name, factory) {
    factories.set(name, factory);
}

/** The first object on `actor` named `name`, or undefined for none. */
export function of(actor, name) {
    return (live.get(actor) || []).find((object) => object.name === name);
}

const isLive = (object) => (live.get(object.actor) || []).includes(object);

function make(actor, id) {
    const name = BehaviorComponent.getName(actor, id);
    const factory = factories.get(name);
    if (factory === undefined) { // gone again before this poll (""), or never registered
        if (name !== "") Log.message(LogLevel.WARN, `no behavior registered as "${name}"`);
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
    const list = live.get(actor) || [];
    const object = list.find((o) => o.id === id);
    if (object === undefined) return;
    list.splice(list.indexOf(object), 1);
    if (list.length === 0) live.delete(actor);
    order.splice(order.indexOf(object), 1);
    object.onDestroy();
}

function poll() {
    let n;
    while ((n = World.takeEvents(events)) > 0) {
        for (let i = 0; i < n; i += 4) {
            const kind = events[i], layer = events[i + 3];
            const actor = events[i + 1] >>> 0, second = events[i + 2] >>> 0; // handles are unsigned
            if (kind === WorldEvent.CREATED) {
                if (Handle.isAlive(actor)) make(actor, second);
            } else if (kind === WorldEvent.DESTROYED) {
                end(actor, second);
            } else if (kind === WorldEvent.TRIGGER_ENTER || kind === WorldEvent.TRIGGER_EXIT) {
                for (const object of (live.get(actor) || []).slice()) {
                    // one destroyed by an object told earlier in the batch: dropped
                    if (!isLive(object) || !Handle.isAlive(actor) || !Handle.isAlive(second)) break;
                    if (kind === WorldEvent.TRIGGER_ENTER) object.onTriggerEnter(second, layer);
                    else object.onTriggerExit(second, layer);
                }
            }
        }
    }
}

/** Call first in the program's tick: the events taken, then every object's tick. */
export function tickAll(dt) {
    poll();
    for (const object of order.slice()) if (isLive(object)) object.onTick(dt);
}

/** Call first in the program's frame: the same, with frames. */
export function frameAll(dt) {
    poll();
    for (const object of order.slice()) if (isLive(object)) object.onFrame(dt);
}

/** The program's shutdown: every object ended, newest first. */
export function endAll() {
    for (let i = order.length - 1; i >= 0; i--) order[i].onDestroy();
    order = [];
    live.clear();
}
