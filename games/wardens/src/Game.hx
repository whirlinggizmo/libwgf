import wgf.*;

/**
	The play: the warden, her weapons and gear, the enemies, their spawning and pushing apart,
	shots, gems, experience and levels. Run at the fixed tick (Main.tick), so a seed plays the
	same enemies at any frame rate. Every actor of the play is under `world`, which Main
	pauses under a menu: the animations, the sparks and the rest hold still with it.
**/
class Game {
	public static inline var HALF = 20.0; // the arena is 40 by 40, centered on the origin
	static inline var BODY_RADIUS = 0.4;
	static inline var SPEED = 5.0;
	static inline var ENEMY_SPEED = 2.5;
	static inline var CONTACT = 0.75; // centers this close: touching
	static inline var CONTACT_DPS = 8.0;
	static inline var FADE = 0.2; // every change of animation blends over this
	static inline var SWORD_TIME = 0.8;
	static inline var SWORD_ARC_COS = 0.5; // cos 60: a 120 degree arc
	static inline var GUN_TIME = 0.5;
	static inline var GUN_RANGE = 12.0;
	static inline var GUN_ARC_COS = 0.70710678; // cos 45: a 90 degree arc
	static inline var SHOT_SPEED = 20.0;
	static inline var SHOT_DAMAGE = 6;
	static inline var GEM_SPEED = 11.0;
	static inline var WARNING_TIME = 1.0;
	static inline var MOST_ALIVE = 200;
	static inline var MOST_MADE = 240; // alive, coming, and dying: what the actor pool is sized for
	public static inline var SWORD = 0;
	public static inline var GUN = 1;
	public static inline var UPPER_BODY = "Abdomen"; // the mask's bone: the waist up; Hips and the legs run on

	public static var ENEMY_RED = 0;
	static var SPARK_RED = 0;

	// the clips, found by name once the warden's parts have them
	public static var idle = -1;
	public static var run = -1;
	public static var slash = -1;
	public static var shoot = -1;
	public static var hitClip = -1;
	public static var death = -1;
	static var deathTime = 1.0;

	public final world:Actor;
	public final warden:Character;
	final swordRoot:Model;
	final gunRoot:Model;
	var swordNode:Actor = 0;
	var gunNode:Actor = 0;
	var armed = false;

	// the warden
	public var x = 0.0;
	public var z = 0.0;
	public var heading = 0.0; // radians about y; 0 faces +z
	public var moving = false;
	public var hp = 100.0;
	public var maxHp = 100.0;
	public var armor = 0.0; // the share of damage kept off
	public var speed = SPEED;
	public var pull = 3.0; // gems fly to her from this far
	public var swordDamage = 10;
	public var swordReach = 2.0;
	public var quickness = 1.0; // every weapon's time between attacks, times this
	public var hasGun = false;
	public var gunShots = 1;
	public var holding = SWORD;
	var swordTimer = 0.0;
	var gunTimer = 0.0;
	var hurtTimer = 0.0;
	var baseClip = -1;
	public var dead = false;
	public var deadFor = 0.0;

	// the run
	public var time = 0.0;
	public var kills = 0;
	public var level = 1;
	public var xp = 0;
	public var levelUps = 0; // owed: a card choice each
	public var maxAlive = 5;
	public final fixedAlive:Int; // --enemies=N: N from the start, held there
	public final seed:Int;
	var spawner:Generator = 0;
	public var cards:Generator = 0;

	public final enemies:Array<Enemy> = [];
	final shots:Array<Shot> = [];
	final gems:Array<Gem> = [];
	final warnings:Array<Warning> = [];
	public var alive = 0;

	public function new(stage:Actor, fixedAlive:Int, seed:Int) {
		ENEMY_RED = Color.make(255, 70, 60, 255);
		SPARK_RED = Color.make(255, 50, 30, 255);
		this.fixedAlive = fixedAlive;
		this.seed = seed;
		world = Actor.create();
		world.setParent(stage);
		final adventurer = Assets.ADVENTURER;
		warden = new Character(world, [adventurer, adventurer, adventurer, adventurer], Color.get(ColorStock.WHITE));
		warden.onBodySwap = (old, now) -> socket(now);
		// the weapons: each file holds the skeleton with the weapon under Middle1.R, placed in the
		// bind pose's hand; the weapon's node is moved to the warden's own Middle1.R (a socket), so it
		// keeps that place in her hand and follows it through every clip
		swordRoot = Model.create(Assets.sword);
		gunRoot = Model.create(Assets.gun);
		Fx.create(world);
	}

	/** Once a frame, before the play's tick: trees made, clips found, weapons held. **/
	public function update() {
		warden.update();
		if (!armed && warden.isReady() && swordRoot.getChildCount() > 0 && gunRoot.getChildCount() > 0) {
			swordNode = swordRoot.findNode("Sword");
			gunNode = gunRoot.findNode("Pistol");
			socket(warden.parts[Character.BODY]);
			findClips(warden.parts[Character.BODY]);
			armed = true;
			holdWeapon();
			baseClip = idle;
			warden.play(idle, 0, true);
		}
	}

	public inline function isReady():Bool
		return armed;

	function socket(body:Model) {
		final hand = body.findNode("Middle1.R");
		if (hand.isNone())
			return;
		if (!swordNode.isNone())
			swordNode.setParent(hand);
		if (!gunNode.isNone())
			gunNode.setParent(hand);
	}

	static function findClips(part:Model) {
		inline function find(name:String)
			return part.findAnimation('CharacterArmature|$name');
		idle = find("Idle");
		run = find("Run");
		slash = find("Sword_Slash");
		shoot = find("Gun_Shoot");
		hitClip = find("HitRecieve");
		death = find("Death");
		deathTime = part.getAnimationDuration(death);
	}

	function holdWeapon() {
		if (!swordNode.isNone())
			(swordNode : Model).setVisible(holding == SWORD);
		if (!gunNode.isNone())
			(gunNode : Model).setVisible(holding == GUN);
	}

	/** A new run: the arena cleared, the warden back in the middle in her Adventurer gear with the sword. **/
	public function reset() {
		for (e in enemies)
			e.character.destroy();
		enemies.resize(0);
		for (s in shots)
			s.model.destroy(ActorDestroy.DESTROY_CHILDREN);
		shots.resize(0);
		for (g in gems)
			g.model.destroy(ActorDestroy.DESTROY_CHILDREN);
		gems.resize(0);
		for (w in warnings)
			w.model.destroy(ActorDestroy.DESTROY_CHILDREN);
		warnings.resize(0);
		Fx.clearBursts();
		alive = 0;
		for (s in 0...4)
			if (warden.outfits[s] != Assets.ADVENTURER)
				warden.swap(s, Assets.ADVENTURER);
		x = z = heading = 0;
		moving = false;
		hp = maxHp = 100;
		armor = 0;
		speed = SPEED;
		pull = 3;
		swordDamage = 10;
		swordReach = 2;
		quickness = 1;
		hasGun = false;
		gunShots = 1;
		holding = SWORD;
		swordTimer = gunTimer = hurtTimer = 0;
		dead = false;
		deadFor = 0;
		time = 0;
		kills = 0;
		level = 1;
		xp = 0;
		levelUps = 0;
		maxAlive = fixedAlive > 0 ? fixedAlive : 5;
		if (spawner != 0)
			Random.destroy(spawner);
		if (cards != 0)
			Random.destroy(cards);
		spawner = Random.create(seed);
		cards = Random.create(seed * 7 + 3);
		if (armed) {
			holdWeapon();
			warden.stopLayer(0, 0);
			warden.stopLayer(1, 0);
			baseClip = idle;
			warden.play(idle, FADE, true);
		}
		placeWarden();
	}

	/** The title's warden: in the middle, idling, turning slowly. **/
	public function showcase(dt:Float) {
		heading += 0.35 * dt;
		x = z = 0;
		if (armed && baseClip != idle) {
			baseClip = idle;
			warden.play(idle, FADE, true);
		}
		placeWarden();
		Fx.placeRing(0, 0, swordReach, false);
	}

	function placeWarden() {
		warden.root.setTransform(x, 0, z, 0, heading, 0, 1, 1, 1);
	}

	// ---- the tick ---------------------------------------------------------------------------

	public function tick(dt:Float, moveX:Float, moveZ:Float, switchWeapon:Bool) {
		if (dead) {
			deadFor += dt;
			tickEnemies(dt);
			tickShots(dt);
			return;
		}
		time += dt;
		if (fixedAlive <= 0)
			maxAlive = Std.int(Math.min(MOST_ALIVE, 5 + 10 * Math.floor(time / 30)));
		if (switchWeapon && hasGun) {
			holding = holding == SWORD ? GUN : SWORD;
			holdWeapon();
		}
		moveWarden(dt, moveX, moveZ);
		attack(dt);
		spawn(dt);
		tickEnemies(dt);
		tickShots(dt);
		tickGems(dt);
		hurtTimer -= dt;
	}

	function moveWarden(dt:Float, moveX:Float, moveZ:Float) {
		var len = Math.sqrt(moveX * moveX + moveZ * moveZ);
		if (len > 1) { // no faster on a diagonal
			moveX /= len;
			moveZ /= len;
			len = 1;
		}
		moving = len > 0.15;
		if (moving) {
			x += moveX * speed * dt;
			z += moveZ * speed * dt;
			heading = Trig.atan2(moveX, moveZ);
			final edge = HALF - BODY_RADIUS; // the walls stop her
			x = Math.max(-edge, Math.min(edge, x));
			z = Math.max(-edge, Math.min(edge, z));
		}
		placeWarden();
		// the base clip: Run moving, Idle standing; an attack played standing goes back to Idle when it ends
		if (moving && baseClip != run) {
			baseClip = run;
			warden.play(run, FADE, true);
		} else if (!moving && baseClip == run) {
			baseClip = idle;
			warden.play(idle, FADE, true);
		} else if (!moving && baseClip != idle && warden.animationState() == PlayState.COMPLETE) {
			baseClip = idle;
			warden.play(idle, FADE, true);
		}
		Fx.placeRing(x, z, swordReach, true);
	}

	/** The attack's clip: on her whole body standing, from the waist up moving (the legs running on). **/
	function attackAnimation(clip:Int) {
		if (moving) {
			warden.playLayer(0, clip, UPPER_BODY, FADE, false);
		} else {
			baseClip = clip;
			warden.play(clip, FADE, false);
		}
	}

	function attack(dt:Float) {
		final fx = Trig.sin(heading), fz = Trig.cos(heading);
		if (holding == SWORD) {
			swordTimer += dt;
			if (swordTimer >= SWORD_TIME * quickness) {
				swordTimer = 0;
				attackAnimation(slash);
				Main.play(Assets.slash);
				for (e in enemies) {
					if (e.state != Enemy.ALIVE)
						continue;
					final dx = e.x - x, dz = e.z - z;
					final d = Math.sqrt(dx * dx + dz * dz);
					if (d > swordReach + BODY_RADIUS)
						continue;
					if (d > 0.3 && (dx * fx + dz * fz) / d < SWORD_ARC_COS)
						continue;
					damage(e, swordDamage);
				}
			}
		} else {
			gunTimer += dt;
			if (gunTimer >= GUN_TIME * quickness) {
				final targets = nearestInFront(fx, fz, gunShots);
				if (targets.length > 0) {
					gunTimer = 0;
					attackAnimation(shoot);
					Main.play(Assets.shot);
					for (t in targets)
						fire(t);
				}
			}
		}
	}

	/** Up to `count` enemies within the gun's range and arc, nearest first. **/
	function nearestInFront(fx:Float, fz:Float, count:Int):Array<Enemy> {
		final found:Array<{e:Enemy, d:Float}> = [];
		for (e in enemies) {
			if (e.state != Enemy.ALIVE)
				continue;
			final dx = e.x - x, dz = e.z - z;
			final d = Math.sqrt(dx * dx + dz * dz);
			if (d > GUN_RANGE || d < 0.01 || (dx * fx + dz * fz) / d < GUN_ARC_COS)
				continue;
			found.push({e: e, d: d});
		}
		found.sort((a, b) -> a.d < b.d ? -1 : a.d > b.d ? 1 : 0);
		return [for (i in 0...Std.int(Math.min(count, found.length))) found[i].e];
	}

	function fire(target:Enemy) {
		final dx = target.x - x, dz = target.z - z;
		final d = Math.sqrt(dx * dx + dz * dz);
		final m = Model.create(Fx.shotMesh);
		m.setMaterial(-1, Fx.shotMaterial);
		m.setShadowCasting(false);
		m.setParent(world);
		final s = new Shot(m, x + dx / d * 0.6, z + dz / d * 0.6, dx / d, dz / d);
		s.place();
		shots.push(s);
	}

	function tickShots(dt:Float) {
		var i = shots.length;
		while (i-- > 0) {
			final s = shots[i];
			s.x += s.dx * SHOT_SPEED * dt;
			s.z += s.dz * SHOT_SPEED * dt;
			s.life -= dt;
			var spent = s.life <= 0 || Math.abs(s.x) > HALF || Math.abs(s.z) > HALF;
			if (!spent) {
				for (e in enemies) { // the first it meets
					if (e.state != Enemy.ALIVE)
						continue;
					final dx = e.x - s.x, dz = e.z - s.z;
					if (dx * dx + dz * dz < 0.5 * 0.5) {
						damage(e, SHOT_DAMAGE);
						spent = true;
						break;
					}
				}
			}
			if (spent) {
				s.model.destroy(ActorDestroy.DESTROY_CHILDREN);
				shots.splice(i, 1);
			} else {
				s.place();
			}
		}
	}

	function damage(e:Enemy, amount:Int) {
		e.hp -= amount;
		e.flash = 0.1;
		if (e.hp <= 0) {
			e.state = Enemy.DYING;
			e.timer = 0;
			e.character.setFlash(false);
			e.character.stopLayer(0, FADE);
			e.character.play(death, FADE, false);
			alive--;
			kills++;
			Fx.burst(e.x, 1.0, e.z, SPARK_RED);
			dropGem(e.x, e.z);
		} else {
			e.character.playLayer(0, hitClip, UPPER_BODY, FADE, false);
		}
	}

	// ---- enemies ----------------------------------------------------------------------------

	function spawn(dt:Float) {
		var i = warnings.length;
		while (i-- > 0) {
			final w = warnings[i];
			w.t += dt;
			Fx.setWarningFill(w.model, Math.min(1, w.t / WARNING_TIME));
			if (w.t >= WARNING_TIME) {
				w.model.destroy(ActorDestroy.DESTROY_CHILDREN);
				warnings.splice(i, 1);
				makeEnemy(w.x, w.z);
			}
		}
		var made = 0;
		while (alive + warnings.length < maxAlive && enemies.length + warnings.length < MOST_MADE && made < 4) {
			made++;
			// at the arena's edge, out of view where it can be
			var px = 0.0, pz = 0.0, seen = true;
			for (_ in 0...8) {
				final side = Random.nextInt(spawner, 0, 3);
				final along = Random.nextRange(spawner, -HALF + 1, HALF - 1);
				final edge = HALF - 1;
				px = side == 0 ? -edge : side == 1 ? edge : along;
				pz = side == 2 ? -edge : side == 3 ? edge : along;
				seen = Main.inView(px, pz);
				if (!seen)
					break;
			}
			if (seen)
				warnings.push(new Warning(Fx.makeWarning(world, px, pz, 0.7), px, pz));
			else
				makeEnemy(px, pz);
		}
	}

	function makeEnemy(px:Float, pz:Float) {
		// the other outfits' parts: one outfit but the warden's Adventurer, all four of its parts (an outfit's legs and feet are
		// made for each other; mixed across outfits, they leave gaps at the shin)
		final outfit = Random.nextInt(spawner, 1, Assets.OUTFITS.length - 1);
		final outfits = [for (_ in 0...4) outfit];
		final c = new Character(world, outfits, ENEMY_RED);
		final e = new Enemy(c, px, pz, 20 + 5 * Std.int(time / 60));
		e.heading = Trig.atan2(x - px, z - pz);
		c.play(run, 0, true);
		e.place();
		enemies.push(e);
		alive++;
	}

	function tickEnemies(dt:Float) {
		final n = enemies.length;
		// toward her, at 2.5 m/s, stopping where they touch
		for (e in enemies) {
			e.character.update();
			if (e.state != Enemy.ALIVE)
				continue;
			final dx = x - e.x, dz = z - e.z;
			final d = Math.sqrt(dx * dx + dz * dz);
			if (d > 0.001) {
				e.heading = Trig.atan2(dx, dz);
				final step = Math.min(ENEMY_SPEED * dt, Math.max(0, d - CONTACT * 0.8));
				e.x += dx / d * step;
				e.z += dz / d * step;
			}
			if (!dead && d < CONTACT) {
				hp -= CONTACT_DPS * dt * (1 - armor);
				if (hurtTimer <= 0) {
					hurtTimer = 0.6;
					Main.play(Assets.hit);
					warden.playLayer(1, hitClip, UPPER_BODY, FADE, false);
				}
			}
		}
		// pushed apart, so they don't overlap
		final gap = 2 * BODY_RADIUS;
		for (i in 0...n) {
			final a = enemies[i];
			if (a.state != Enemy.ALIVE)
				continue;
			for (j in i + 1...n) {
				final b = enemies[j];
				if (b.state != Enemy.ALIVE)
					continue;
				final dx = b.x - a.x, dz = b.z - a.z;
				if (Math.abs(dx) >= gap || Math.abs(dz) >= gap)
					continue;
				final d2 = dx * dx + dz * dz;
				if (d2 >= gap * gap)
					continue;
				final d = Math.sqrt(d2);
				final push = (gap - d) * 0.5;
				final ux = d > 0.0001 ? dx / d : 1.0, uz = d > 0.0001 ? dz / d : 0.0;
				a.x -= ux * push;
				a.z -= uz * push;
				b.x += ux * push;
				b.z += uz * push;
			}
		}
		var i = enemies.length;
		final edge = HALF - BODY_RADIUS;
		while (i-- > 0) {
			final e = enemies[i];
			if (e.state == Enemy.ALIVE) {
				e.x = Math.max(-edge, Math.min(edge, e.x));
				e.z = Math.max(-edge, Math.min(edge, e.z));
				if (e.flash > 0) {
					e.flash -= dt;
					e.character.setFlash(e.flash > 0);
				}
			} else {
				e.timer += dt;
				if (e.timer > deathTime) // then sinks into the ground and is gone
					e.y -= 1.0 * dt;
				if (e.y < -2.2) {
					e.character.destroy();
					enemies.splice(i, 1);
					continue;
				}
			}
			e.place();
		}
		if (!dead && hp <= 0) {
			hp = 0;
			dead = true;
			warden.stopLayer(0, FADE);
			warden.stopLayer(1, FADE);
			baseClip = death;
			warden.play(death, FADE, false);
		}
	}

	// ---- gems and levels --------------------------------------------------------------------

	function dropGem(px:Float, pz:Float) {
		final m = Model.create(Fx.gemMesh);
		m.setMaterial(-1, Fx.gemMaterial);
		m.setParent(world);
		gems.push(new Gem(m, px, pz));
	}

	function tickGems(dt:Float) {
		var i = gems.length;
		while (i-- > 0) {
			final g = gems[i];
			g.spin += dt * 3.0;
			final dx = x - g.x, dz = z - g.z;
			final d = Math.sqrt(dx * dx + dz * dz);
			if (d < pull)
				g.flying = true;
			if (g.flying) {
				final step = Math.min(d, GEM_SPEED * dt);
				if (d > 0.0001) {
					g.x += dx / d * step;
					g.z += dz / d * step;
				}
				if (d < 0.45) {
					g.model.destroy(ActorDestroy.DESTROY_CHILDREN);
					gems.splice(i, 1);
					Main.play(Assets.gem);
					gainXp();
					continue;
				}
			}
			g.model.setTransform(g.x, 0.55 + 0.1 * Trig.sin(g.spin * 1.3), g.z, 0, g.spin, 0, 1, 1.7, 1);
		}
	}

	public inline function xpToNext():Int
		return 5 * level; // level 2 at 5, each level after 5 more than the last

	function gainXp() {
		xp++;
		if (xp >= xpToNext()) {
			xp -= xpToNext();
			level++;
			levelUps++;
			Main.play(Assets.level);
		}
	}

	// ---- cards ------------------------------------------------------------------------------

	/** Three different cards drawn for a level up. **/
	public function drawCards():Array<Card> {
		final pool:Array<Card> = [Card.SHARPER, Card.QUICK, Card.REACH];
		if (!hasGun)
			pool.push(Card.GUN);
		else
			pool.push(Card.BULLETS);
		pool.push(gearCard());
		final out = [];
		while (out.length < 3 && pool.length > 0)
			out.push(pool.splice(Random.nextInt(cards, 0, pool.length - 1), 1)[0]);
		return out;
	}

	/** A random part from an outfit she isn't wearing. **/
	function gearCard():Card {
		final slot = Random.nextInt(cards, 0, 3);
		final choices = [for (o in 0...Assets.OUTFITS.length) if (warden.outfits.indexOf(o) < 0) o];
		if (choices.length == 0)
			for (o in 0...Assets.OUTFITS.length)
				if (o != warden.outfits[slot])
					choices.push(o);
		return Card.gear(slot, choices[Random.nextInt(cards, 0, choices.length - 1)]);
	}

	public function apply(card:Card) {
		switch card.kind {
			case Card.KIND_SHARPER:
				swordDamage += 5;
			case Card.KIND_QUICK:
				quickness *= 0.85;
			case Card.KIND_REACH:
				swordReach += 0.5;
			case Card.KIND_GUN:
				hasGun = true;
				holding = GUN; // the one picked last
				holdWeapon();
			case Card.KIND_BULLETS:
				gunShots++;
			case Card.KIND_GEAR:
				warden.swap(card.slot, card.outfit);
				switch card.slot {
					case Character.HEAD:
						maxHp += 10;
						hp += 10;
					case Character.BODY:
						armor = Math.min(0.9, armor + 0.1);
					case Character.LEGS:
						speed += 0.5;
					case Character.FEET:
						pull += 1;
					default:
				}
			default:
		}
		levelUps--;
	}
}

class Enemy {
	public static inline var ALIVE = 0;
	public static inline var DYING = 1;

	public final character:Character;
	public var x:Float;
	public var y = 0.0;
	public var z:Float;
	public var heading = 0.0;
	public var hp:Int;
	public var flash = 0.0;
	public var state = ALIVE;
	public var timer = 0.0;

	public function new(character:Character, x:Float, z:Float, hp:Int) {
		this.character = character;
		this.x = x;
		this.z = z;
		this.hp = hp;
	}

	public inline function place()
		character.root.setTransform(x, y, z, 0, heading, 0, 1, 1, 1);
}

class Shot {
	public final model:Model;
	public var x:Float;
	public var z:Float;
	public final dx:Float;
	public final dz:Float;
	public var life = 0.75;

	public function new(model:Model, x:Float, z:Float, dx:Float, dz:Float) {
		this.model = model;
		this.x = x;
		this.z = z;
		this.dx = dx;
		this.dz = dz;
	}

	public inline function place()
		model.setPosition(x, 1.1, z);
}

class Gem {
	public final model:Model;
	public var x:Float;
	public var z:Float;
	public var spin = 0.0;
	public var flying = false;

	public function new(model:Model, x:Float, z:Float) {
		this.model = model;
		this.x = x;
		this.z = z;
	}
}

class Warning {
	public final model:Model;
	public final x:Float;
	public final z:Float;
	public var t = 0.0;

	public function new(model:Model, x:Float, z:Float) {
		this.model = model;
		this.x = x;
		this.z = z;
	}
}
