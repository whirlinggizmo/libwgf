import wgf.*;

/**
	The track's files, streamed. The start loads what the grid needs behind the loading
	screen: the scenes (the car's, and the track's collision and checkpoints), the collision
	file, the sky, and the first FIRST sections of kit pieces (TrackData: the grid's and the
	next). The rest come once the race is started, a section at a time, nearest first, each
	once its start is within LOOKAHEAD m ahead of the car: its files (its scene and the kit
	pieces it places first) fetched as one asset group, then its scene loaded and placed,
	and it is ready once every piece it shows has loaded. The manifest (Main.init) makes a
	second visit read all of it from the cache.

	Its probes: racer.stream_ready and racer.stream_pending (sections), racer.stream_late
	(sections the car drove into before they were ready: 0 is the goal), and
	racer.stream_pending_at_start (what was still to come when the race first started).
**/
class Stream {
	static inline var FIRST = 2;
	static inline var LOOKAHEAD = 280.0;

	static final START_FILES = ["scenes/racer.scene", "scenes/track.scene", "track/collision.glb", "sky/sky.hdr", "fx/puff.png"];

	public static var loading:AssetTask = 0; // the start's files, as one group
	public static var started(default, null) = false; // the race begun: the rest may come
	public static var ready(default, null) = 0;
	public static var late(default, null) = 0;

	static final count = TrackData.SECTIONS;
	static final status = [for (_ in 0...TrackData.SECTIONS) Status.WAITING];
	static final files:Array<AssetTask> = [for (_ in 0...TrackData.SECTIONS) 0];
	static final scenes:Array<Scene> = [for (_ in 0...TrackData.SECTIONS) 0];
	static final reached = [for (_ in 0...TrackData.SECTIONS) false];
	static final held:Map<String, Handle> = []; // each section's kit pieces and textures, by path
	static var stage:Stage3d = 0;

	/** Ask for the start's files: the loading screen's bar is their progress. **/
	public static function begin(on:Stage3d) {
		stage = on;
		loading = Asset.groupCreate();
		for (path in START_FILES)
			Asset.groupAdd(loading, Asset.ensure(path, null, 0));
		for (n in 0...FIRST) {
			for (path in TrackData.sectionFiles[n])
				Asset.groupAdd(loading, Asset.ensure(path, null, 0));
			status[n] = Status.FETCHING;
		}
		publish();
	}

	/** The start's files local: the sections among them loaded (Main loads the rest). **/
	public static function filesLocal():Bool {
		if (loading == 0 || loading.getStatus() == AssetTaskStatus.PENDING)
			return false;
		for (n in 0...FIRST)
			if (status[n] == Status.FETCHING)
				load(n);
		return true;
	}

	/** How far the start has come, 0 to 1: its files, then the sections' pieces loading. **/
	public static function progress():Float {
		if (loading == 0)
			return 0;
		final files = loading.getProgress();
		if (loading.getStatus() == AssetTaskStatus.PENDING)
			return 0.7 * files;
		var done = 0, all = 0;
		for (path => resource in held) {
			all++;
			if (Resource.getStatus(resource) != ResourceStatus.PENDING)
				done++;
		}
		return 0.7 + 0.3 * (all == 0 ? 0 : done / all);
	}

	/** Whether the grid's sections are placed and drawn. **/
	public static function gridReady():Bool {
		for (n in 0...FIRST)
			if (status[n] != Status.READY)
				return false;
		return true;
	}

	/** The start is pressed: what's still to come may be fetched from now on. **/
	public static function start() {
		started = true;
	}

	/** The race is under way (the countdown over): what is still to come then, published once. **/
	public static function raced() {
		if (!Probe.hasValue("racer.stream_pending_at_start"))
			Probe.setValue("racer.stream_pending_at_start", count - ready);
	}

	/** Each tick: move each section on, and start the nearest one wanted, from `sample`, the car's. **/
	public static function update(sample:Int) {
		var busy = false;
		for (n in 0...count) {
			switch status[n] {
				case Status.FETCHING:
					busy = true;
					if (n >= FIRST && files[n].getStatus() != AssetTaskStatus.PENDING) {
						if (files[n].getStatus() == AssetTaskStatus.FAILED)
							Log.message(LogLevel.WARN, 'racer: section $n\'s files failed; its scene tried anyway');
						files[n].destroy();
						files[n] = 0;
						load(n);
					}
				case Status.LOADING:
					busy = true;
					final s = Resource.getStatus(scenes[n]);
					if (s == ResourceStatus.READY) {
						scenes[n].instantiate(stage);
						status[n] = Status.PLACED;
					} else if (s == ResourceStatus.FAILED) {
						status[n] = Status.READY; // nothing to wait for: the collision is whole without it
						ready++;
					}
				case Status.PLACED:
					if (piecesLoaded(n)) {
						status[n] = Status.READY;
						ready++;
					}
				default:
			}
		}
		if (started && !busy) {
			final next = nearestWanted(sample);
			if (next >= 0) {
				final group = Asset.groupCreate();
				for (path in TrackData.sectionFiles[next])
					Asset.groupAdd(group, Asset.ensure(path, null, 0));
				files[next] = group;
				status[next] = Status.FETCHING;
			}
		}
		// the car in a section not ready yet: late (once a section)
		final here = Track.sectionOf(sample);
		if (started && !reached[here]) {
			reached[here] = true;
			if (status[here] != Status.READY)
				late++;
		}
		publish();
	}

	/** Whether section `n` is ready: placed, and every piece it shows loaded. **/
	public static function isReady(n:Int):Bool
		return status[n] == Status.READY;

	static function load(n:Int) {
		final sectionFiles = TrackData.sectionFiles[n];
		scenes[n] = Scene.create(sectionFiles[0]);
		// hold each kit piece's mesh and each texture it brings (shared by path with the
		// models and sprites its scene makes), so that they are seen loaded
		for (k in 1...sectionFiles.length) {
			final path = sectionFiles[k];
			held[path] = StringTools.endsWith(path, ".png") ? (Texture.create(path) : Handle) : (Mesh.create(path) : Handle);
		}
		status[n] = Status.LOADING;
	}

	static function piecesLoaded(n:Int):Bool {
		// a section shows the pieces it brings and those of the sections before it in the loop
		for (k in 0...n + 1)
			for (i in 1...TrackData.sectionFiles[k].length)
				if (Resource.getStatus(held[TrackData.sectionFiles[k][i]]) == ResourceStatus.PENDING)
					return false;
		return true;
	}

	/** The waiting section nearest ahead of `sample` within LOOKAHEAD m (its own first), or -1. **/
	static function nearestWanted(sample:Int):Int {
		final samples = Track.xs.length;
		final here = Track.sectionOf(sample);
		var best = -1, bestAhead = LOOKAHEAD;
		for (n in 0...count) {
			if (status[n] != Status.WAITING)
				continue;
			final ahead = n == here ? 0 : ((TrackData.sectionStarts[n] - sample + samples) % samples) * Track.STEP;
			if (ahead <= bestAhead) {
				best = n;
				bestAhead = ahead;
			}
		}
		return best;
	}

	static function publish() {
		Probe.setValue("racer.stream_ready", ready);
		Probe.setValue("racer.stream_pending", count - ready);
		Probe.setValue("racer.stream_late", late);
	}
}

enum abstract Status(Int) {
	var WAITING = 0; // not asked for yet
	var FETCHING = 1; // its files on their way
	var LOADING = 2; // its scene loading
	var PLACED = 3; // its scene's actors made, their pieces loading
	var READY = 4;
}
