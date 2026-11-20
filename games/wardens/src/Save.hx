import wgf.*;

/**
	The best time survived and the volume, kept between runs in `user:wardens.save` (natively
	under the user's data directory, by the program's identity; on the web in the browser's
	store), as two lines of text: `best=<seconds>` and `volume=<0 to 1>`.
**/
class Save {
	static inline var PATH = "user:wardens.save";

	public static var best = 0.0;
	public static var volume = 0.8;
	/** Read, or found missing: what was saved is known. **/
	public static var loaded = false;

	static var reading:FsTask = 0;
	static var writing:Array<FsTask> = [];

	public static function load() {
		reading = Fs.read(PATH);
	}

	/** Once a frame: a read that has finished is taken in, writes that have finished are let go. **/
	public static function update() {
		if (reading != 0) {
			final status = reading.getStatus();
			if (status != FsTaskStatus.PENDING) {
				if (status == FsTaskStatus.DONE)
					parse(reading.getText());
				reading.destroy();
				reading = 0;
				loaded = true;
			}
		}
		var i = writing.length;
		while (i-- > 0) {
			if (writing[i].getStatus() != FsTaskStatus.PENDING) {
				writing[i].destroy();
				writing.splice(i, 1);
			}
		}
	}

	static function parse(text:String) {
		for (line in text.split("\n")) {
			final eq = line.indexOf("=");
			if (eq < 0)
				continue;
			final value = Std.parseFloat(StringTools.trim(line.substr(eq + 1)));
			if (Math.isNaN(value))
				continue;
			switch StringTools.trim(line.substr(0, eq)) {
				case "best":
					best = Math.max(0, value);
				case "volume":
					volume = Math.min(1, Math.max(0, value));
				default:
			}
		}
	}

	public static function write() {
		final text = 'best=$best\nvolume=$volume\n';
		final task = Fs.write(PATH, haxe.io.Bytes.ofString(text));
		if (task != 0)
			writing.push(task);
	}
}
