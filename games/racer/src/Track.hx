import wgf.*;

/**
	The circuit: assets/track/track.glb, written by tools/gen_track.py from its centerline
	(TrackData, generated beside it). The scene file places it, with its surfaces' bodies,
	their shadows, and the checkpoints under its gates (racer.scene); this is its centerline
	for the game (the grid, the grass, the driver's telemetry), and the trees, made here.
**/
class Track {
	public static inline var WIDTH = TrackData.WIDTH;
	public static inline var STEP = TrackData.STEP;
	public static inline var CHECKPOINTS = TrackData.CHECKPOINTS;

	public static final xs = TrackData.xs;
	public static final zs = TrackData.zs;
	public static final headings = TrackData.headings;
	public static var models = 0; // the models it made itself (the file's nodes are counted apart)

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

	/** The trees, on `stage`: the scenery around the track's file. **/
	public static function create(stage:Stage3d) {
		trees(stage);
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
