package wgf;

/**
	A behavior: the program's code on an actor, a class extending this, registered by its
	name. An actor has any number of behaviors (C's wgf_actor_add_behavior, or a scene's
	`behavior name=...` lines), several of one name too; the runtime makes one of these for
	each as the ecs says it was added, tells it of its actor's triggers, runs its tick and
	frame before the program's, and ends it when it is removed or its actor goes -- all from
	the ecs's events, taken each tick and frame (wgf.Ecs.takeEvents): nothing in C calls a
	behavior.
	```haxe
	class Rock extends Behavior {
		override function onTick(dt:Float) { ... (actor:Motion).setVelocity(...) ... }
	}
	Behavior.register("Rock", Rock.new);
	```
	Behaviors set intent (SPEC): moving, wrapping, ageing, and overlapping are the ecs's
	systems, in C. A behavior's actor is `actor`, and `id` names it among the actor's; its
	parameters, from the scene file or set in code, are its own (getParam, getParamNumber,
	getParamActor, and hasParam here, or wgf.BehaviorComponent's with the id).
**/
class Behavior {
	/** The actor this behavior is on. **/
	public var actor(default, null):Actor;

	/** Its id among the actor's behaviors (C's: from 1, never given again on the actor). **/
	public var id(default, null):Int = 0;

	/** The name it was registered and made for. **/
	public var name(default, null):String;

	public function new(actor:Actor) {
		this.actor = actor;
	}

	/**
		It was added (its actor's lines, a scene's or the code's, applied). It runs at the
		runtime's next poll of the ecs's events, before the next tick or frame, never inside
		the call that added it: so the code that spawned its actor, a parameter it set right
		after `spawn` included, has run by then.
	**/
	public function onCreate():Void {}

	/** Each tick, before the program's, with the tick's length in seconds. **/
	public function onTick(dt:Float):Void {}

	/** Each frame, before the program's, with the frame's length in seconds. **/
	public function onFrame(dt:Float):Void {}

	/** Its actor started to overlap `other` (both have colliders). **/
	public function onTriggerEnter(other:Actor):Void {}

	/** Its actor stopped overlapping `other`. **/
	public function onTriggerExit(other:Actor):Void {}

	/** It was removed, or its actor is gone: `actor` may be stale from here. **/
	public function onDestroy():Void {}

	/** This behavior's parameter `key`, as text ("" when it has none). **/
	public inline function getParam(key:String):String
		return (actor : BehaviorComponent).getParam(id, key);

	/** This behavior's parameter `key`, as a number (0 when it has none or isn't one). **/
	public inline function getParamNumber(key:String):Float
		return (actor : BehaviorComponent).getParamNumber(id, key);

	/**
		The actor this behavior's parameter `key` refers to (a value "@name" or "@path",
		found once, as the scene made its actors); none when it isn't one or found none.
	**/
	public inline function getParamActor(key:String):Actor
		return (actor : BehaviorComponent).getParamActor(id, key);

	/** Whether this behavior has the parameter `key`. **/
	public inline function hasParam(key:String):Bool
		return (actor : BehaviorComponent).hasParam(id, key);

	static var factories = new Map<String, (actor:Actor) -> Behavior>();
	static var live = new Map<Int, Array<Behavior>>(); // by actor, in the order added
	static var order:Array<Behavior> = [];
	static var unknown = new Map<String, Bool>();
	static final events:Array<Int> = [for (_ in 0...3 * 64) 0];

	/**
		The class to make for each behavior named `name` (`Rock.new`); a second registration
		of a name replaces the first, for the behaviors made after it.
	**/
	public static function register(name:String, factory:(actor:Actor) -> Behavior):Void
		factories.set(name, factory);

	/** The first behavior on `actor` named `name` (any, for null), or null for none. **/
	public static function of(actor:Actor, ?name:String):Null<Behavior> {
		final list = live.get(actor);
		if (list == null)
			return null;
		for (behavior in list)
			if (name == null || behavior.name == name)
				return behavior;
		return null;
	}

	/** Every behavior on `actor`, in the order added (empty for none). **/
	public static function all(actor:Actor):Array<Behavior> {
		final list = live.get(actor);
		return list != null ? list.copy() : [];
	}

	/** How many behaviors are live. **/
	public static function count():Int
		return order.length;

	/** The ecs's events since the last time: behaviors made, told, and ended. **/
	public static function poll():Void {
		while (true) {
			final n = Ecs.takeEvents(events);
			if (n == 0)
				break;
			var i = 0;
			while (i < n) {
				final kind:EcsEvent = events[i], actor:Actor = events[i + 1], second = events[i + 2];
				i += 3;
				switch kind {
					case EcsEvent.CREATED:
						make(actor, second);
					case EcsEvent.DESTROYED:
						end(actor, second);
					case EcsEvent.TRIGGER_ENTER:
						final list = live.get(actor);
						if (list != null)
							for (behavior in list.copy())
								behavior.onTriggerEnter(second);
					case EcsEvent.TRIGGER_EXIT:
						final list = live.get(actor);
						if (list != null)
							for (behavior in list.copy())
								behavior.onTriggerExit(second);
					default:
				}
			}
		}
	}

	static function make(actor:Actor, id:Int):Void {
		final name = (actor : BehaviorComponent).getName(id);
		if (name == "")
			return; // gone again before this poll: its DESTROYED follows
		final factory = factories.get(name);
		if (factory == null) {
			if (!unknown.exists(name)) {
				unknown.set(name, true);
				Log.message(LogLevel.WARN, 'wgf: no behavior registered as "$name" (Behavior.register)');
			}
			return;
		}
		final behavior = factory(actor);
		behavior.name = name;
		behavior.id = id;
		var list = live.get(actor);
		if (list == null) {
			list = [];
			live.set(actor, list);
		}
		list.push(behavior);
		order.push(behavior);
		behavior.onCreate();
	}

	static function end(actor:Actor, id:Int):Void {
		final list = live.get(actor);
		if (list == null)
			return;
		for (behavior in list)
			if (behavior.id == id) {
				list.remove(behavior);
				if (list.length == 0)
					live.remove(actor);
				order.remove(behavior);
				behavior.onDestroy();
				return;
			}
	}

	static function liveNow(behavior:Behavior):Bool {
		final list = live.get(behavior.actor);
		return list != null && list.indexOf(behavior) >= 0;
	}

	public static function tickAll(dt:Float):Void {
		poll();
		for (behavior in order.copy())
			if (liveNow(behavior))
				behavior.onTick(dt);
	}

	public static function frameAll(dt:Float):Void {
		poll();
		for (behavior in order.copy())
			if (liveNow(behavior))
				behavior.onFrame(dt);
	}

	/** The run is ending: every behavior ended, newest first. **/
	public static function endAll():Void {
		var i = order.length;
		while (i-- > 0)
			order[i].onDestroy();
		order = [];
		live.clear();
	}
}
