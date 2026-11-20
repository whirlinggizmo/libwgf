import wgf.*;

/**
	Every file the game uses, asked for once at the start and held for the run, so a part put
	on, an enemy made, or a sound played later finds its file already loaded. The loading
	screen waits for all of them (`progress`).
**/
class Assets {
	/** The ten outfits, in icons.txt's order (its outfit icons are 5 to 14). **/
	public static final OUTFITS = [
		"Adventurer", "Casual", "Formal", "Medieval", "Punk", "SciFi", "Soldier", "Suit", "Witch", "Worker"
	];
	public static final SLOTS = ["Head", "Body", "Legs", "Feet"];
	public static inline var ADVENTURER = 0;

	/** parts[outfit][slot]: each a glTF file of one part on the shared skeleton. **/
	public static var parts:Array<Array<Mesh>> = [];
	/** Animations.glb: the 24 clips every part plays, no meshes. **/
	public static var clips:Mesh = 0;
	public static var sword:Mesh = 0;
	public static var gun:Mesh = 0;

	public static var wall:Mesh = 0;
	public static var pillar:Mesh = 0;
	public static var props:Array<Mesh> = [];

	public static var panel:Texture = 0;
	public static var button:Texture = 0;
	public static var icons:Texture = 0;
	public static var font:Font = 0;

	public static var ringShader:Shader = 0;
	public static var warningShader:Shader = 0;
	public static var flashShader:Shader = 0;

	public static var slash:Sound = 0;
	public static var shot:Sound = 0;
	public static var hit:Sound = 0;
	public static var gem:Sound = 0;
	public static var level:Sound = 0;
	public static var music:Sound = 0;

	static var all:Array<Handle> = [];

	public static function load() {
		for (o in 0...OUTFITS.length)
			parts.push([for (s in 0...SLOTS.length) keep(Mesh.create('characters/${OUTFITS[o]}_${SLOTS[s]}.glb'))]);
		clips = keep(Mesh.create("characters/Animations.glb"));
		sword = keep(Mesh.create("characters/Sword.glb"));
		gun = keep(Mesh.create("characters/Gun.glb"));
		wall = keep(Mesh.create("models/wall.glb"));
		pillar = keep(Mesh.create("models/pillar.glb"));
		props = [for (name in ["barrel", "crates", "keg", "box", "chest"]) keep(Mesh.create('models/$name.glb'))];
		panel = keep(Texture.create("ui/panel.png"));
		button = keep(Texture.create("ui/button.png"));
		icons = keep(Texture.create("ui/icons.png"));
		font = keep(Font.create("ui/font.ttf"));
		ringShader = keep(Shader.create("shaders/ring.wgfshader"));
		warningShader = keep(Shader.create("shaders/warning.wgfshader"));
		flashShader = keep(Shader.create("shaders/flash.wgfshader"));
		slash = keep(Sound.create("sound/slash.wav"));
		shot = keep(Sound.create("sound/shot.wav"));
		hit = keep(Sound.create("sound/hit.wav"));
		gem = keep(Sound.create("sound/gem.wav"));
		level = keep(Sound.create("sound/level.wav"));
		music = keep(Sound.createStreamed("sound/music.ogg"));
		gem.setPolyphony(16);
		slash.setPolyphony(4);
		shot.setPolyphony(8);
	}

	static function keep<T>(handle:T):T {
		all.push(cast handle); // every handle kind is an Int underneath

		return handle;
	}

	/** How much has loaded, 0 to 1: a file that failed counts as done (libwgf has logged it, and draws a placeholder). **/
	public static function progress():Float {
		var done = 0;
		for (h in all)
			if (Resource.getStatus(h) != ResourceStatus.PENDING)
				done++;
		return all.length == 0 ? 0 : done / all.length;
	}

	public static function partName(outfit:Int, slot:Int):String
		return '${OUTFITS[outfit]} ${SLOTS[slot]}';
}
