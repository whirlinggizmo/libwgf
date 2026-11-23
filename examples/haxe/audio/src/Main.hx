import wgf.*;

/** A setting the arrow keys change: its name, its range, and how to read and write it. **/
typedef Setting = {
	final name:String;
	final min:Float;
	final max:Float;
	final get:() -> Float;
	final set:(Float) -> Void;
}

/**
	Audio, in Haxe: sounds, voices, and buses.

	It shows each way a sound plays, with its state drawn as it changes.
	- Music: a streamed sound on its own looping voice, heard through the music bus.
	- A click: a sound played once with `Sound.play`, on a voice the mixer owns.
	- A tone: a sound made in code from samples, on its own voice.
	- The music voice's volume, pitch, and pan, the music and effects buses' volumes, and the
	  master volume, each changed while it plays.

	The controls:
	- Space plays the music from the start. P pauses or resumes it. S stops it.
	- Enter, or a click, plays the click. A click is panned to where the mouse is.
	- T plays the tone.
	- Up and Down choose a setting. Left and Right, held, change it.
	- Escape quits, where quitting means anything (not in a browser).

	In a browser, sound stays off until the page's first key or click. Until then a voice told
	to play reads PLAYING, but is silent and its position doesn't move.

	Run it with `wgf serve` or `wgf run` from this folder.
**/
class Main {
	static var music:Voice;
	static var click:Sound;
	static var tone:Voice;
	static var settings:Array<Setting>;
	static var selected = 0;
	static var plays = 0;
	static var flash = 0.0; // the click's ring, fading after each play
	static final mouse = new Vec2(0, 0);

	static function main() {
		Window.setTitle("libwgf audio (Haxe)");
		// The design size, fitted to any window or page. A desktop window opens at this size.
		Presentation.set(PresentationMode.FIT, 800, 600);
		Window.setFlags(WindowFlag.MSAA);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(18, 20, 28, 255));

		// Music is streamed: decoded as it plays, rather than held whole.
		final song = Sound.createStreamed("music/a_hero_is_born.mp3");
		music = Voice.create(song);
		Resource.release(song); // the voice holds its own reference
		music.setLoop(true);
		music.setVolume(0.6);
		music.setBus(Bus.getMusic()); // a voice is on the effects bus until set

		click = Sound.create("sounds/click_004.ogg"); // its plays are on the effects bus

		tone = Voice.create(makeTone());

		settings = [
			{name: "music volume", min: 0, max: 1.5, get: () -> music.getVolume(), set: v -> music.setVolume(v)},
			{name: "music pitch", min: 0.25, max: 2, get: () -> music.getPitch(), set: v -> music.setPitch(v)},
			{name: "music pan", min: -1, max: 1, get: () -> music.getPan(), set: v -> music.setPan(v)},
			busSetting("music bus", Bus.getMusic()),
			busSetting("effects bus", Bus.getEffects()),
			{name: "master volume", min: 0, max: 1.5, get: () -> Audio.getVolume(), set: v -> Audio.setVolume(v)},
		];
	}

	static function busSetting(name:String, bus:Bus):Setting
		return {name: name, min: 0, max: 1.5, get: () -> bus.getVolume(), set: v -> bus.setVolume(v)};

	/** A falling chirp a third of a second long, made a sample at a time: 22050 a second, mono. **/
	static function makeTone():Sound {
		final rate = 22050;
		final samples = new Array<Float>();
		var phase = 0.0;
		for (i in 0...Std.int(rate * 0.35)) {
			final t = i / rate;
			phase += (880 - 1200 * t) / rate; // the pitch falls from 880 Hz
			samples.push(Math.sin(phase * Math.PI * 2) * Math.exp(-t * 9) * 0.5);
		}
		return Sound.createFromSamples(samples, 1, rate);
	}

	static function loaded():Bool
		return Resource.getStatus(music.getSound()) == ResourceStatus.READY
			&& Resource.getStatus(click) == ResourceStatus.READY;

	static function frame() {
		Mouse.getPosition(mouse);
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();

		if (Keyboard.isPressed(KeyboardKey.SPACE))
			music.play();
		if (Keyboard.isPressed(KeyboardKey.P) && !music.pause())
			music.resume();
		if (Keyboard.isPressed(KeyboardKey.S))
			music.stop();
		if (Keyboard.isPressed(KeyboardKey.T))
			tone.play();
		if (Keyboard.isPressed(KeyboardKey.ENTER))
			playClick(0);
		if (Mouse.isPressed(MouseButton.LEFT))
			playClick(mouse.x / 400 - 1);

		// Up and Down choose a setting. Left and Right move it, half its range a second.
		final count = settings.length;
		if (Keyboard.isRepeated(KeyboardKey.UP))
			selected = (selected + count - 1) % count;
		if (Keyboard.isRepeated(KeyboardKey.DOWN))
			selected = (selected + 1) % count;
		final dir = (Keyboard.isDown(KeyboardKey.RIGHT) ? 1 : 0) - (Keyboard.isDown(KeyboardKey.LEFT) ? 1 : 0);
		if (dir != 0) {
			final s = settings[selected];
			final v = s.get() + dir * (s.max - s.min) * 0.5 * Loop.getFrameDelta();
			s.set(Math.min(s.max, Math.max(s.min, v)));
		}
		flash = Math.max(0, flash - Loop.getFrameDelta() * 3);

		draw();

		// Probes publish numbers an autopilot run can check (autopilot/smoke.autopilot).
		Probe.setValue("audio.loaded", loaded() ? 1 : 0);
		Probe.setValue("audio.music_state", music.getState());
		Probe.setValue("audio.tone_state", tone.getState());
		Probe.setValue("audio.plays", plays);
		Probe.setValue("audio.music_volume", music.getVolume());
	}

	/** Plays the click once, at `pan` (-1 left to 1 right), and forgets it. **/
	static function playClick(pan:Float) {
		if (click.play(0.8, 1, pan)) {
			plays++;
			flash = 1;
		}
	}

	static function draw() {
		final white = Color.get(ColorStock.RAYWHITE);
		final gray = Color.get(ColorStock.GRAY);
		Draw.text(0, "libwgf audio", 40, 30, 32, white);
		Draw.text(0, loaded() ? "sounds loaded" : "loading sounds...", 40, 70, 16, gray);

		drawVoice("music", music, 120, Color.get(ColorStock.SKYBLUE));
		drawVoice("tone", tone, 180, Color.get(ColorStock.GOLD));
		Draw.text(0, 'click: played $plays times', 40, 240, 18, Color.get(ColorStock.LIME));
		Draw.circleLines(700, 160, 30 + (1 - flash) * 40, 3, Color.make(0, 228, 48, Std.int(flash * 255)));

		for (i in 0...settings.length) {
			final s = settings[i];
			final y = 280 + i * 30;
			final on = i == selected;
			Draw.text(0, (on ? "> " : "  ") + s.name, 40, y, 18, on ? white : gray);
			Draw.rectangle(240, y + 4, 300, 10, Color.make(50, 54, 66, 255));
			Draw.rectangle(240, y + 4, 300 * (s.get() - s.min) / (s.max - s.min), 10,
				Color.get(on ? ColorStock.ORANGE : ColorStock.DARKGRAY));
			Draw.text(0, '${Math.round(s.get() * 100) / 100}', 560, y, 18, on ? white : gray);
		}

		Draw.text(0, "[Space] play  [P] pause/resume  [S] stop  [Enter]/click: click  [T] tone", 40, 480, 16, gray);
		Draw.text(0, "[Up/Down] choose a setting  [Left/Right] change it" + (App.canQuit() ? "  [Esc] quit" : ""), 40,
			504, 16, gray);
		if (Audio.isBlocked())
			Draw.text(0, "Press a key or click to turn sound on.", 40, 528, 16, Color.get(ColorStock.YELLOW));
	}

	/** A voice's line: its name, its state, and its position along its sound. **/
	static function drawVoice(name:String, voice:Voice, y:Float, color:Int) {
		final state = switch (voice.getState()) {
			case PLAYING: "playing";
			case PAUSED: "paused";
			case COMPLETE: "complete";
			default: "stopped";
		}
		final length = voice.getSound().getDuration();
		final at = voice.getPosition();
		Draw.text(0, '$name: $state', 40, y, 18, color);
		Draw.rectangle(240, y + 4, 300, 10, Color.make(50, 54, 66, 255));
		if (length > 0)
			Draw.rectangle(240, y + 4, 300 * at / length, 10, color);
		Draw.text(0, '${Math.round(at * 10) / 10} / ${Math.round(length * 10) / 10} s', 560, y, 18, color);
	}
}
