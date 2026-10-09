import wgf.*;

/**
	The circuit: assets/track/track.glb, written by tools/gen_track.py from its centerline
	(TrackData, generated beside it). The file's named nodes are the ground, the road (with
	its curbs and start line), the barriers, and the gates; once it has loaded, each of
	those gets a static body of its triangles (physics3d's mesh shape), and each gate a
	checkpoint's sensor (Main.spawn). The trees are scenery, made here.
**/
class Track {
	public static inline var WIDTH = TrackData.WIDTH;
	public static inline var STEP = TrackData.STEP;
	public static inline var CHECKPOINTS = TrackData.CHECKPOINTS;
	static inline var ROAD_FRICTION = 1.0;
	static inline var GRASS_FRICTION = 0.7;
	static inline var BARRIER_FRICTION = 0.3; // a wall the car slides along, not one it sticks to

	public static final xs = TrackData.xs;
	public static final zs = TrackData.zs;
	public static final headings = TrackData.headings;
	public static var models = 0; // the models it made itself (the file's nodes are counted apart)

	/** The track's file, a model on the stage: its nodes are made under it once it loads. **/
	public static var root:Actor = 0; // a Model (a section type takes no 0: an Actor does)

	static var mesh:Mesh = 0;
	static var bodied = false;

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

	/** The world: the track's file, loading, and the trees; on `stage`. **/
	public static function create(stage:Stage3d) {
		mesh = Mesh.create("track/track.glb");
		root = Model.create(mesh);
		root.setName("track");
		root.setParent(stage);
		trees(stage);
	}

	/**
		Whether the track has loaded and its bodies are made: each frame until then, the
		file's nodes looked for, and once they are there each surface given its body.
	**/
	public static function ready():Bool {
		if (bodied)
			return true;
		if (Resource.getStatus(mesh) != ResourceStatus.READY || root.find("road").isNone())
			return false;
		body("ground", GRASS_FRICTION);
		body("road", ROAD_FRICTION); // its curbs and line, under it, are in its triangles
		body("barriers", BARRIER_FRICTION);
		body("gates", BARRIER_FRICTION); // every gate's posts and banner
		bodied = true;
		return true;
	}

	/** The gate node checkpoint `index` stands at. **/
	public static function gate(index:Int):Actor
		return root.find('gates/gate_$index');

	static function body(node:String, friction:Float) {
		final actor = root.find(node);
		actor.addComponent(Component.BODY);
		(actor : Body).setType(BodyType.STATIC);
		(actor : Body).setShape(BodyShape.MESH, 0, 0, 0);
		(actor : Body).setFriction(friction);
	}

	static function trees(stage:Stage3d) {
		final trunk = Mesh.createCylinder(0.35, 2.4, 8);
		final crown = Mesh.createCone(2.2, 5.0, 10);
		final bark = Color.make(96, 66, 40, 255), leaves = Color.make(34, 96, 44, 255);
		seed = 7; // the same woods every run
		var made = 0, tries = 0;
		while (made < 140 && tries < 4000) {
			tries++;
			final x = range(-150, 290), z = range(-255, 240);
			var near = 1e18;
			for (i in 0...xs.length)
				near = Math.min(near, (xs[i] - x) * (xs[i] - x) + (zs[i] - z) * (zs[i] - z));
			if (near < (TrackData.BARRIER_OFFSET + 3) * (TrackData.BARRIER_OFFSET + 3))
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
	public static function checkpointSample(index:Int):Int
		return TrackData.checkpoints[index];
}
