import wgf.*;

/**
	The rear-view mirror: the stage seen from the car looking back, drawn into a render target
	on each frame it shows (the countdown and the race), and shown at the top of the screen,
	wide and short in a dark frame, left-right mirrored as a mirror is. The mirroring is the
	sprite's: a sprite on a 2D stage of its own, scaled -1 in x (Draw.texture can't flip, a
	camera's scale is ignored, and an effect redraws the screen, not a target).

	Its camera rides on the car, behind the cabin, and sees only the near track (FAR m), so
	the second view draws less than the first.

	Probes: racer.mirror (1 on a frame it is drawn, 0 else); once, half a second into the
	countdown, its pixels are read back (Texture.read): racer.mirror_read (2 DONE, 3 FAILED,
	as headless builds have no GPU to read), racer.mirror_seen (the share of its pixels
	unlike its clear color; -1 where it couldn't be read).
**/
class Mirror {
	public static inline var WIDTH = 480; // the target's pixels, 4:1
	public static inline var HEIGHT = 120;
	public static inline var SHOWN_WIDTH = 400.0; // on the screen, in the design's pixels
	public static inline var SHOWN_HEIGHT = 100.0;
	public static inline var BORDER = 5.0;
	public static inline var TOP = 14.0;
	static inline var FAR = 120.0; // m: what the mirror sees behind
	static inline var FOV = 0.36; // vertical, radians: about 69 degrees across at 4:1
	// what it is cleared to: the sky's color, under the sky (Main's clear color)
	static inline var CLEAR_R = 150;
	static inline var CLEAR_G = 196;
	static inline var CLEAR_B = 236;

	public static var shown(default, null) = false;
	/** Where it is on the screen this frame (the design's pixels): the effect leaves it be. **/
	public static final rect = new Vec4();

	static var target:Texture = 0;
	static var camera:Camera3d = 0;
	static var hud:Stage2d = 0;
	static var sprite:Sprite = 0;
	static var read:TextureRead = 0;
	static var asked = false;
	static var drawn = 0;

	/** The target, its camera on the car, and the sprite that shows it. **/
	public static function create(car:Actor) {
		target = Texture.createTarget(WIDTH, HEIGHT);
		Render.setTargetClearColor(target, Color.make(CLEAR_R, CLEAR_G, CLEAR_B, 255));
		camera = Camera3d.create();
		camera.setFov(FOV);
		camera.setClip(0.3, FAR);
		(camera : Actor).setParent(car);
		(camera : Actor).setPosition(0, 1.05, -2.4); // behind the cabin; unturned it looks down -z, back
		hud = Stage2d.create();
		sprite = Sprite.create(target);
		sprite.setSize(SHOWN_WIDTH, SHOWN_HEIGHT);
		sprite.setAlphaMode(AlphaMode.OPAQUE, 0.5);
		(sprite : Actor).setScale(-1, 1, 1); // a mirror's left is its right
		(sprite : Actor).setParent(hud);
		Probe.setValue("racer.mirror", 0);
	}

	/** Into the target, when it shows this frame: before the screen, whatever the order. **/
	public static function draw(stage:Stage3d, show:Bool) {
		shown = show && target != 0;
		Probe.setValue("racer.mirror", shown ? 1 : 0);
		if (!shown)
			return;
		if (Render.beginTarget(target)) {
			stage.drawWith(camera);
			Render.endTarget();
		}
		check();
	}

	/** On the screen, at the top of the visible area `v`, over the HUD's other parts. **/
	public static function show(v:Vec4) {
		if (!shown)
			return;
		final x = v.x + v.z / 2, y = v.y + TOP + BORDER + SHOWN_HEIGHT / 2;
		rect.set(x - SHOWN_WIDTH / 2 - BORDER, y - SHOWN_HEIGHT / 2 - BORDER, SHOWN_WIDTH + 2 * BORDER, SHOWN_HEIGHT + 2 * BORDER);
		Draw.rectangle(rect.x, rect.y, rect.z, rect.w, Color.make(20, 20, 24, 255));
		(sprite : Actor).setPosition(x, y, 0);
		hud.draw();
	}

	/** Its pixels, read once in the countdown: drawn, and not its clear color alone. **/
	static function check() {
		// in the countdown, half a second in: the read stalls the next frame's update, which
		// the grid can afford and the race (the stream check's budget, 33 ms) can't
		if (!asked && Main.state == Main.State.COUNTDOWN && ++drawn >= 30) {
			read = target.read();
			asked = true;
		}
		if (read == 0)
			return;
		final status = read.getStatus();
		if (status == TextureReadStatus.PENDING)
			return;
		Probe.setValue("racer.mirror_read", (status : Int));
		var seen = -1.0;
		if (status == TextureReadStatus.DONE) {
			// its size asked for by name: the web export's host keeps only the calls a game makes,
			// and getData's own use of get_size isn't counted (FRICTION part 8)
			final n = Std.int(read.getSize() / 4);
			final data = read.getData();
			var unlike = 0;
			for (i in 0...n) {
				final r = data.get(i * 4), g = data.get(i * 4 + 1), b = data.get(i * 4 + 2);
				if (Math.abs(r - CLEAR_R) + Math.abs(g - CLEAR_G) + Math.abs(b - CLEAR_B) > 24)
					unlike++;
			}
			seen = n > 0 ? unlike / n : 0;
		}
		Probe.setValue("racer.mirror_seen", seen);
		read.destroy();
		read = 0;
	}
}
