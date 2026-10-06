package wgf;

/**
	A behavior: the program's code for each entity whose behavior component names it
	(wgf.BehaviorComponent, C's wgf_behavior), a class extending this, registered by that
	name. The runtime makes one when the ecs says the entity's behavior was made, tells it
	of its triggers, runs its tick and frame before the program's, and ends it when the
	entity goes -- all from the ecs's events, taken each tick and frame
	(wgf.Ecs.takeEvents): nothing in C calls a behavior.

	```haxe
	class Rock extends Behavior {
		override function onTick(dt:Float) { ... (entity:Motion).setVelocity(...) ... }
	}
	Behavior.register("Rock", Rock.new);
	```

	Behaviors set intent (SPEC): moving, wrapping, ageing, and overlapping are the ecs's
	systems, in C. A behavior's entity is `entity`; its parameters, from the scene file or
	set in code, are its component's (getParam, getParamNumber, and hasParam here, or
	wgf.BehaviorComponent's on any entity).
**/
class Behavior {
	/** The entity this behavior is for. **/
	public var entity(default, null):Entity;

	/** The name it was registered and made for. **/
	public var name(default, null):String;

	public function new(entity:Entity) {
		this.entity = entity;
	}

	/** Its entity was made (its components set, its scene's lines applied). **/
	public function onCreate():Void {}

	/** Each tick, before the program's, with the tick's length in seconds. **/
	public function onTick(dt:Float):Void {}

	/** Each frame, before the program's, with the frame's length in seconds. **/
	public function onFrame(dt:Float):Void {}

	/** Its entity started to overlap `other`'s (both have colliders). **/
	public function onTriggerEnter(other:Entity):Void {}

	/** Its entity stopped overlapping `other`'s. **/
	public function onTriggerExit(other:Entity):Void {}

	/** Its entity is gone: `entity` is stale from here. **/
	public function onDestroy():Void {}

	/** This behavior's parameter `key`, as text ("" when it has none). **/
	public inline function getParam(key:String):String
		return (entity : BehaviorComponent).getParam(key);

	/** This behavior's parameter `key`, as a number (0 when it has none or isn't one). **/
	public inline function getParamNumber(key:String):Float
		return (entity : BehaviorComponent).getParamNumber(key);

	/** Whether this behavior has the parameter `key`. **/
	public inline function hasParam(key:String):Bool
		return (entity : BehaviorComponent).hasParam(key);

	static var factories = new Map<String, (entity:Entity) -> Behavior>();
	static var live = new Map<Int, Behavior>();
	static var order:Array<Behavior> = [];
	static var unknown = new Map<String, Bool>();
	static final events:Array<Int> = [for (_ in 0...3 * 64) 0];

	/**
		The class to make for each entity whose behavior is named `name` (`Rock.new`); a
		second registration of a name replaces the first, for the behaviors made after it.
	**/
	public static function register(name:String, factory:(entity:Entity) -> Behavior):Void
		factories.set(name, factory);

	/** The behavior of `entity`, or null when it has none. **/
	public static function of(entity:Entity):Null<Behavior>
		return live.get(entity);

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
				final kind:EcsEvent = events[i], entity:Entity = events[i + 1], other:Entity = events[i + 2];
				i += 3;
				switch kind {
					case EcsEvent.CREATED:
						make(entity);
					case EcsEvent.DESTROYED:
						final behavior = live.get(entity);
						if (behavior != null) {
							live.remove(entity);
							order.remove(behavior);
							behavior.onDestroy();
						}
					case EcsEvent.TRIGGER_ENTER:
						final behavior = live.get(entity);
						if (behavior != null)
							behavior.onTriggerEnter(other);
					case EcsEvent.TRIGGER_EXIT:
						final behavior = live.get(entity);
						if (behavior != null)
							behavior.onTriggerExit(other);
					default:
				}
			}
		}
	}

	static function make(entity:Entity):Void {
		if (live.exists(entity) || !entity.isAlive())
			return;
		final name = (entity : BehaviorComponent).getName();
		final factory = factories.get(name);
		if (factory == null) {
			if (name != "" && !unknown.exists(name)) {
				unknown.set(name, true);
				Log.message(LogLevel.WARN, 'wgf: no behavior registered as "$name" (Behavior.register)');
			}
			return;
		}
		final behavior = factory(entity);
		behavior.name = name;
		live.set(entity, behavior);
		order.push(behavior);
		behavior.onCreate();
	}

	public static function tickAll(dt:Float):Void {
		poll();
		for (behavior in order.copy())
			if (live.exists(behavior.entity))
				behavior.onTick(dt);
	}

	public static function frameAll(dt:Float):Void {
		poll();
		for (behavior in order.copy())
			if (live.exists(behavior.entity))
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
