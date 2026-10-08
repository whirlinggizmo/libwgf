import wgf.*;

/**
	The circuit: a closed centerline (a Catmull-Rom loop through a few points, resampled
	every STEP metres), and the world built from it out of generated meshes -- the grass,
	the asphalt as a box per sample, curbs on the corners, the start line, and trees. The
	checkpoints are spawned at even distances along it, the first on the start line.
	The asphalt, curbs, and start line are one static body (a mesh of their triangles,
	physics3d's), on a static box under the grass; the trees are scenery.
**/
class Track {
	public static inline var WIDTH = 12.0;
	public static inline var STEP = 2.0;
	public static inline var CHECKPOINTS = 8;
	static inline var ROAD_FRICTION = 1.0;
	static inline var GRASS_FRICTION = 0.7;

	// the loop, on the ground (x, z), driven in this order; SCALE metres a unit
	static inline var SCALE = 1.7;
	static final POINTS = [
		0.0, -50, 0, 30, 12, 62, 42, 72, 72, 58, 78, 26, 56, 2, 58, -28, 82, -52, 66, -82, 26, -86
	];

	public static final xs:Array<Float> = [];
	public static final zs:Array<Float> = [];
	public static final headings:Array<Float> = []; // yaw: the direction (sin, cos) along x and z
	public static var length = 0.0;
	public static var models = 0; // the static models it made: a draw each, unmerged

	/** The samples along the centerline, every STEP metres. **/
	public static function build() {
		if (xs.length > 0)
			return;
		final dense = densePoints(40);
		// resample by arc length
		var carried = 0.0;
		xs.push(dense[0]);
		zs.push(dense[1]);
		final n = Std.int(dense.length / 2);
		for (i in 0...n) {
			final ax = dense[i * 2], az = dense[i * 2 + 1];
			final bx = dense[((i + 1) % n) * 2], bz = dense[((i + 1) % n) * 2 + 1];
			final d = Math.sqrt((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
			var t = STEP - carried;
			while (t <= d) {
				xs.push(ax + (bx - ax) * t / d);
				zs.push(az + (bz - az) * t / d);
				t += STEP;
			}
			carried = d - (t - STEP);
			length += d;
		}
		if (carried < STEP * 0.5) { // the last sample too near the first
			xs.pop();
			zs.pop();
		}
		final count = xs.length;
		for (i in 0...count) {
			final j = (i + 1) % count;
			headings.push(Math.atan2(xs[j] - xs[i], zs[j] - zs[i]));
		}
	}

	static function densePoints(perSegment:Int):Array<Float> {
		final out:Array<Float> = [];
		final n = Std.int(POINTS.length / 2);
		inline function px(i:Int)
			return POINTS[((i + n) % n) * 2] * SCALE;
		inline function pz(i:Int)
			return POINTS[((i + n) % n) * 2 + 1] * SCALE;
		for (i in 0...n)
			for (s in 0...perSegment) {
				final t = s / perSegment, t2 = t * t, t3 = t2 * t;
				inline function cr(p0:Float, p1:Float, p2:Float, p3:Float)
					return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
				out.push(cr(px(i - 1), px(i), px(i + 1), px(i + 2)));
				out.push(cr(pz(i - 1), pz(i), pz(i + 1), pz(i + 2)));
			}
		return out;
	}

	/** The index of the sample nearest (x, z), searched near `hint` (the last answer). **/
	public static function nearest(x:Float, z:Float, hint:Int):Int {
		final count = xs.length;
		var best = hint, bestD = 1e18;
		for (k in -12...13) {
			final i = (hint + k + count) % count;
			final d = (xs[i] - x) * (xs[i] - x) + (zs[i] - z) * (zs[i] - z);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		return best;
	}

	/** How far (x, z) is across the track from sample `i`, right positive. **/
	public static function offset(x:Float, z:Float, i:Int):Float {
		// across the track: the distance along the sample's right-hand normal
		final h = headings[i];
		return (x - xs[i]) * Math.cos(h) - (z - zs[i]) * Math.sin(h);
	}

	/** The world: ground, asphalt, curbs, the start line, trees; on `stage`. **/
	public static function create(stage:Stage3d) {
		build();
		final unit = Mesh.createCube(1, 1, 1); // every box is this one, scaled
		final flat = Mesh.createPlane(1, 1, 0);
		final grass = Model.create(Mesh.createPlane(800, 800, 0));
		grass.setTint(Color.make(70, 128, 58, 255));
		grass.setParent(stage);
		models++;
		// the ground: a static box under the grass, its top at 0, slippery next to the asphalt
		final ground = Actor.create();
		ground.setPosition(0, -0.5, 0);
		ground.setParent(stage);
		ground.addComponent(Component.BODY);
		(ground : Body).setType(BodyType.STATIC);
		(ground : Body).setShape(BodyShape.BOX, 800, 1, 800);
		(ground : Body).setFriction(GRASS_FRICTION);
		// the road: every asphalt, curb, and line model under one actor, its body their triangles
		road = Actor.create();
		road.setParent(stage);

		final asphalt = Color.make(58, 60, 66, 255);
		final red = Color.make(200, 40, 36, 255), white = Color.make(235, 235, 235, 255);
		final count = xs.length;
		for (i in 0...count) {
			final j = (i + 1) % count;
			final h = headings[i];
			final mx = (xs[i] + xs[j]) / 2, mz = (zs[i] + zs[j]) / 2;
			// a plane per sample, overlapping the next: coplanar, one tint, one normal, so where they
			// overlap they fight to the same color (there is no mesh from vertices for one ribbon)
			box(stage, flat, mx, 0.03, mz, h, WIDTH, 1, STEP + 0.6, asphalt);

			// curbs where it turns: the heading's change over the next few samples
			var turn = headings[(i + 3) % count] - headings[(i + count - 3) % count];
			while (turn > Math.PI)
				turn -= 2 * Math.PI;
			while (turn < -Math.PI)
				turn += 2 * Math.PI;
			if (Math.abs(turn) > 0.25) {
				final side = WIDTH / 2 + 0.5;
				final nx = Math.cos(h), nz = -Math.sin(h); // right-hand normal
				// red and white at two heights, so where a corner's inside overlaps them their tops don't fight
				final c = i % 2 == 0 ? red : white, tall = i % 2 == 0 ? 0.1 : 0.12;
				box(stage, unit, mx + nx * side, tall / 2, mz + nz * side, h, 1.0, tall, STEP, c);
				box(stage, unit, mx - nx * side, tall / 2, mz - nz * side, h, 1.0, tall, STEP, c);
			}
		}
		// the start line, across checkpoint 0
		final s = checkpointSample(0);
		box(stage, unit, xs[s], 0.05, zs[s], headings[s], WIDTH, 0.02, 1.0, white);

		road.addComponent(Component.BODY);
		(road : Body).setType(BodyType.STATIC);
		(road : Body).setShape(BodyShape.MESH, 0, 0, 0);
		(road : Body).setFriction(ROAD_FRICTION);
		trees(stage);
	}

	static var road:Actor = 0;

	static function box(stage:Actor, mesh:Mesh, x:Float, y:Float, z:Float, yaw:Float, w:Float, h:Float, l:Float,
			tint:Int) {
		final m = Model.create(mesh);
		models++;
		m.setTint(tint);
		m.setTransform(x, y, z, 0, yaw, 0, w, h, l);
		m.setParent(road);
	}

	static function trees(stage:Stage3d) {
		final trunk = Mesh.createCylinder(0.35, 2.4, 8);
		final crown = Mesh.createCone(2.2, 5.0, 10);
		final bark = Color.make(96, 66, 40, 255), leaves = Color.make(34, 96, 44, 255);
		seed = 7; // the same woods every run
		var made = 0, tries = 0;
		while (made < 140 && tries < 4000) {
			tries++;
			final x = range(-90, 170) * SCALE, z = range(-150, 140) * SCALE;
			var near = 1e18;
			for (i in 0...xs.length)
				near = Math.min(near, (xs[i] - x) * (xs[i] - x) + (zs[i] - z) * (zs[i] - z));
			if (near < 15 * 15)
				continue;
			final scale = range(0.8, 1.5);
			final t = Model.create(trunk);
			t.setTint(bark);
			t.setTransform(x, 1.2 * scale, z, 0, 0, 0, scale, scale, scale);
			t.setParent(stage);
			final c = Model.create(crown);
			c.setTint(leaves);
			c.setTransform(x, (2.4 + 2.5) * scale, z, 0, 0, 0, scale, scale, scale);
			c.setParent(stage);
			made++;
			models += 2;
		}
	}

	// the trees' own generator (Park-Miller, exact in a double on every target), apart from wgf_random's
	static var seed = 7.0;

	static function range(lo:Float, hi:Float):Float {
		seed = (seed * 16807) % 2147483647;
		return lo + (hi - lo) * (seed / 2147483647);
	}

	/** The sample checkpoint `index` sits on. **/
	public static function checkpointSample(index:Int):Int {
		build();
		// checkpoint 0 a little way up the first straight, so the grid is behind it
		return (Std.int(index * xs.length / CHECKPOINTS) + 6) % xs.length;
	}
}
