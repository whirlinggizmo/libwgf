/**
	A level-up card: what it does, its name, its icon (an index into assets/ui/icons.png, as
	icons.txt names them) and its line of text.
**/
class Card {
	public static inline var KIND_SHARPER = 0;
	public static inline var KIND_QUICK = 1;
	public static inline var KIND_REACH = 2;
	public static inline var KIND_GUN = 3;
	public static inline var KIND_BULLETS = 4;
	public static inline var KIND_GEAR = 5;

	public static final SHARPER = new Card(KIND_SHARPER, "Sharper blade", 0, "Sword damage +5");
	public static final QUICK = new Card(KIND_QUICK, "Quick hands", 1, "Every weapon's time between attacks -15%");
	public static final REACH = new Card(KIND_REACH, "Long reach", 2, "The sword's reach +0.5 meters");
	public static final GUN = new Card(KIND_GUN, "Gun", 3, "Adds the gun: it fires at the nearest enemy in front");
	public static final BULLETS = new Card(KIND_BULLETS, "More bullets", 4, "The gun fires one more shot at the next nearest enemy");

	static final GEAR_TEXT = [
		"+10 max health", "+10% armor: less damage taken", "+0.5 meters a second", "+1 meter to the gem pull"
	];

	public final kind:Int;
	public final name:String;
	public final icon:Int;
	public final text:String;
	public var slot = -1;
	public var outfit = -1;

	function new(kind:Int, name:String, icon:Int, text:String) {
		this.kind = kind;
		this.name = name;
		this.icon = icon;
		this.text = text;
	}

	/** A gear card: `outfit`'s part in `slot`, "Witch Head", its icon the outfit's portrait. **/
	public static function gear(slot:Int, outfit:Int):Card {
		final card = new Card(KIND_GEAR, Assets.partName(outfit, slot), 5 + outfit, GEAR_TEXT[slot]);
		card.slot = slot;
		card.outfit = outfit;
		return card;
	}
}
