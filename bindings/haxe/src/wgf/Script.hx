package wgf;

/**
	A script: the program's code for each entity whose behavior has a name (wgf.Behavior),
	a class extending this, registered by that name. The runtime makes one when the ecs
	says the entity's behavior was made, tells it of its triggers, runs its tick and frame
	before the program's, and ends it when the entity goes -- all from the ecs's events,
	taken each tick and frame (wgf.Ecs.takeEvents): nothing in C calls a script.

	```haxe
	class Rock extends Script {
		override function onTick(dt:Float) { ... (entity:Motion).setVelocity(...) ... }
	}
	Script.register("Rock", Rock.new);
	```

	Scripts set intent (SPEC): moving, wrapping, ageing, and overlapping are the ecs's
	systems, in C. A script's entity is `entity`; its parameters, wgf.Behavior's.
**/
class Script {
	/** The entity this script is for. **/
	public var entity(default, null):Entity;

	/** The behavior's name it was made for. **/
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

	static var factories = new Map<String, (entity:Entity) -> Script>();
	static var live = new Map<Int, Script>();
	static var order:Array<Script> = [];
	static var unknown = new Map<String, Bool>();
	static final events:Array<Int> = [for (_ in 0...3 * 64) 0];

	/**
		The class to make for each entity whose behavior is named `name` (`Rock.new`); a
		second registration of a name replaces the first, for the scripts made after it.
	**/
	public static function register(name:String, factory:(entity:Entity) -> Script):Void
		factories.set(name, factory);

	/** The script of `entity`, or null when it has none. **/
	public static function of(entity:Entity):Null<Script>
		return live.get(entity);

	/** How many scripts are live. **/
	public static function count():Int
		return order.length;

	/** The ecs's events since the last time: scripts made, told, and ended. **/
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
						final script = live.get(entity);
						if (script != null) {
							live.remove(entity);
							order.remove(script);
							script.onDestroy();
						}
					case EcsEvent.TRIGGER_ENTER:
						final script = live.get(entity);
						if (script != null)
							script.onTriggerEnter(other);
					case EcsEvent.TRIGGER_EXIT:
						final script = live.get(entity);
						if (script != null)
							script.onTriggerExit(other);
					default:
				}
			}
		}
	}

	static function make(entity:Entity):Void {
		if (live.exists(entity) || !entity.isAlive())
			return;
		final name = (entity : Behavior).getName();
		final factory = factories.get(name);
		if (factory == null) {
			if (name != "" && !unknown.exists(name)) {
				unknown.set(name, true);
				Log.message(LogLevel.WARN, 'wgf: no script registered for the behavior "$name" (Script.register)');
			}
			return;
		}
		final script = factory(entity);
		script.name = name;
		live.set(entity, script);
		order.push(script);
		script.onCreate();
	}

	public static function tickAll(dt:Float):Void {
		poll();
		for (script in order.copy())
			if (live.exists(script.entity))
				script.onTick(dt);
	}

	public static function frameAll(dt:Float):Void {
		poll();
		for (script in order.copy())
			if (live.exists(script.entity))
				script.onFrame(dt);
	}

	/** The run is ending: every script ended, newest first. **/
	public static function endAll():Void {
		var i = order.length;
		while (i-- > 0)
			order[i].onDestroy();
		order = [];
		live.clear();
	}
}
