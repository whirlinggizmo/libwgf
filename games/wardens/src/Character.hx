import wgf.*;

/**
	A modular character: four part models (head, body, legs, feet), each a glTF file on the
	one shared skeleton, given Animations.glb's clips (Model.setAnimations) and played alike,
	so they move as one. The warden is one, and so is each enemy.

	A part's tree (its joints and its skinned mesh) is made once its file is there; the
	models with meshes in it are collected then (`nodes`), for the tint and the hit flash,
	which are each model's own. A part swapped for another outfit's is made off the stage,
	and put on once its tree is there, playing as the others do (Model.syncAnimation), so the
	animation doesn't jump.
**/
class Character {
	public static inline var HEAD = 0;
	public static inline var BODY = 1;
	public static inline var LEGS = 2;
	public static inline var FEET = 3;

	public final root:Actor;
	public final parts:Array<Model> = [];
	public final outfits:Array<Int>;
	public var tint(default, null):Int;
	/** Called as a new body goes on, before the old one goes: what is held at its bones moves over. **/
	public var onBodySwap:Null<(old:Model, now:Model) -> Void> = null;

	final nodes:Array<Array<Model>> = [[], [], [], []];
	final dressed = [false, false, false, false];
	final pending:Array<Model> = [0, 0, 0, 0];
	var flashing = false;

	public function new(parent:Actor, outfits:Array<Int>, tint:Int) {
		root = Actor.create();
		root.setParent(parent);
		this.outfits = outfits.copy();
		this.tint = tint;
		for (s in 0...4) {
			final part = makePart(outfits[s], s);
			part.setParent(root);
			parts.push(part);
		}
	}

	static function makePart(outfit:Int, slot:Int):Model {
		final part = Model.create(Assets.parts[outfit][slot]);
		part.setAnimations(Assets.clips); // the clips file's channels move this file's nodes of the same names
		return part;
	}

	/** Every part's tree made, and its clips there. **/
	public function isReady():Bool {
		for (s in 0...4)
			if (!dressed[s])
				return false;
		return parts[BODY].getAnimationCount() > 0;
	}

	/** Once a frame or a tick: parts whose trees have just been made are tinted; parts put on are swapped in. **/
	public function update() {
		for (s in 0...4) {
			if (!dressed[s] && parts[s].getChildCount() > 0) {
				collect(parts[s], nodes[s]);
				for (m in nodes[s])
					m.setTint(tint);
				dressed[s] = true;
			}
			final next = pending[s];
			if (next != 0 && next.getChildCount() > 0 && next.getAnimationCount() > 0)
				putOn(s, next);
		}
	}

	/** The models with meshes at and under `actor`: a file's skinned parts, and anything held at a bone. **/
	static function collect(actor:Actor, into:Array<Model>) {
		for (i in 0...actor.getChildCount()) {
			final child = actor.getChild(i);
			if (child.getKind() == ActorKind.MODEL && !(child : Model).getMesh().isNone())
				into.push(child);
			collect(child, into);
		}
	}

	/** `outfit`'s part in `slot`, put on as soon as its tree is made. **/
	public function swap(slot:Int, outfit:Int) {
		if (pending[slot] != 0)
			pending[slot].destroy(ActorDestroy.DESTROY_CHILDREN);
		pending[slot] = makePart(outfit, slot);
		outfits[slot] = outfit;
	}

	function putOn(slot:Int, now:Model) {
		pending[slot] = 0;
		final old = parts[slot];
		final from = parts[slot == BODY ? LEGS : BODY]; // a part that stays, playing on
		now.syncAnimation(from); // its clip, time, speed, loop, state and layer: no jump
		now.setParent(root);
		parts[slot] = now;
		nodes[slot] = [];
		collect(now, nodes[slot]);
		for (m in nodes[slot])
			m.setTint(tint);
		dressed[slot] = true;
		if (slot == BODY && onBodySwap != null)
			onBodySwap(old, now);
		old.destroy(ActorDestroy.DESTROY_CHILDREN);
	}

	public function play(clip:Int, fade:Float, loop:Bool) {
		for (p in parts) {
			p.setAnimationLoop(loop);
			p.playAnimation(clip, fade);
		}
	}

	public function playLayer(index:Int, clip:Int, bone:String, fade:Float, loop:Bool) {
		for (p in parts)
			p.playLayerAnimation(index, clip, bone, fade, loop);
	}

	public function stopLayer(index:Int, fade:Float) {
		for (p in parts)
			p.stopLayerAnimation(index, fade);
	}

	public inline function animation():Int
		return parts[BODY].getAnimation();

	public inline function animationState():PlayState
		return parts[BODY].getAnimationState();

	public function setTint(color:Int) {
		tint = color;
		if (flashing)
			return;
		for (s in 0...4)
			for (m in nodes[s])
				m.setTint(color);
	}

	/** The hit flash: every slot of every part drawn by the game's flash shader (Fx.flashMaterial), white-tinted; or back to its own. **/
	public function setFlash(on:Bool) {
		if (on == flashing)
			return;
		flashing = on;
		for (s in 0...4)
			for (m in nodes[s]) {
				if (on) {
					final mesh = m.getMesh();
					for (slot in 0...mesh.getMaterialCount())
						m.setMaterial(slot, Fx.flashMaterial(mesh, slot));
					m.setTint(Color.get(ColorStock.WHITE));
				} else {
					m.setMaterial(-1, 0);
					m.setTint(tint);
				}
			}
	}

	public function destroy() {
		for (s in 0...4)
			if (pending[s] != 0)
				pending[s].destroy(ActorDestroy.DESTROY_CHILDREN);
		root.destroy(ActorDestroy.DESTROY_CHILDREN);
	}
}
