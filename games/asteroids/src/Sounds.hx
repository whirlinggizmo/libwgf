import wgf.*;

/**
	The game's sounds (tools/gen_sounds.py made them, Ogg Vorbis; assets/sounds/), and voices to play
	them: a few per sound, taken in turn, so a quick second shot doesn't cut off the
	first; the thrust's own, looping, played while the ship thrusts.
**/
class Sounds {
	public final fire:Array<Voice>;
	public final bangLarge:Array<Voice>;
	public final bangMedium:Array<Voice>;
	public final bangSmall:Array<Voice>;
	public final extraLife:Array<Voice>;
	public final start:Array<Voice>;
	public final gameOver:Array<Voice>;

	final thrustVoice:Voice;
	final next = new Map<Array<Voice>, Int>();

	public function new() {
		fire = voices("fire", 4, 0.5);
		bangLarge = voices("bang_large", 3, 0.8);
		bangMedium = voices("bang_medium", 3, 0.7);
		bangSmall = voices("bang_small", 3, 0.6);
		extraLife = voices("extra_life", 1, 0.7);
		start = voices("start", 1, 0.6);
		gameOver = voices("game_over", 1, 0.7);
		final thrust = Sound.create("sounds/thrust.ogg");
		thrustVoice = Voice.create(thrust);
		thrustVoice.setLoop(true);
		thrustVoice.setVolume(0.45);
		Resource.release(thrust); // the voice holds its own
	}

	static function voices(name:String, count:Int, volume:Float):Array<Voice> {
		final sound = Sound.create('sounds/$name.ogg');
		final out = [for (_ in 0...count) Voice.create(sound)];
		for (v in out)
			v.setVolume(volume);
		Resource.release(sound);
		return out;
	}

	public function play(pool:Array<Voice>) {
		final i = next.exists(pool) ? next.get(pool) : 0;
		next.set(pool, (i + 1) % pool.length);
		pool[i].stop();
		pool[i].play();
	}

	public function thrust(on:Bool) {
		if (on)
			thrustVoice.play();
		else
			thrustVoice.stop();
	}
}
