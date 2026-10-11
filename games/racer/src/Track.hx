import wgf.*;

/**
	The circuit, written by tools/gen_track.py from its layout of Kenney's Racing Kit pieces:
	its collision and checkpoints (assets/scenes/track.scene, placing track/collision.glb),
	its drawn pieces in sections (assets/track/section_<n>.scene, Stream's to load), and its
	centerline (TrackData), what the game drives by: the grid, the grass, recovery, and the
	driver's telemetry.
**/
class Track {
	public static inline var WIDTH = TrackData.WIDTH;
	public static inline var STEP = TrackData.STEP;
	public static inline var CHECKPOINTS = TrackData.CHECKPOINTS;
	public static inline var BARRIER_OFFSET = TrackData.BARRIER_OFFSET;

	public static final xs = TrackData.xs;
	public static final zs = TrackData.zs;
	public static final headings = TrackData.headings;

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

	/** The index of the sample nearest (x, z), searched all the way round: after a jump. **/
	public static function nearestAnywhere(x:Float, z:Float):Int {
		var best = 0, bestD = 1e18;
		for (i in 0...xs.length) {
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
		// the right of heading h is (-cos h, sin h) on x and z
		final h = headings[i];
		return -(x - xs[i]) * Math.cos(h) + (z - zs[i]) * Math.sin(h);
	}

	/** The sample checkpoint `index` sits on. **/
	public static function checkpointSample(index:Int):Int
		return TrackData.checkpoints[index];

	/** The section sample `i` is in. **/
	public static function sectionOf(i:Int):Int {
		var n = 0;
		for (k in 0...TrackData.SECTIONS)
			if (i >= TrackData.sectionStarts[k])
				n = k;
		return n;
	}

	/**
		Whether the collision file's nodes are made, and with them the track's bodies (its
		scene hides the road and walls: visible=false).
	**/
	public static function collisionPlaced(stage:Stage3d):Bool {
		return (stage : Actor).find("track/road") != 0 && (stage : Actor).find("track/walls") != 0;
	}
}
