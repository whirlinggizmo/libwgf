import wgf.*;

/**
	A screen's behavior: it makes the screen's 2D stage, and puts shapes and text on it.

	Every actor of a screen hangs under one of its stages, so the screen switches, pauses,
	and draws them together.
**/
class ScreenBehavior extends Behavior {
	final stage:Stage2d;

	public function new(screen:Actor) {
		super(screen);
		stage = Stage2d.create();
		Screen.addStage(screen, stage);
	}

	function rect(x:Float, y:Float, width:Float, height:Float, color:Int):Shape2d {
		final shape = Shape2d.create();
		shape.setRectangle(width, height);
		return place(shape, x, y, color);
	}

	function circle(x:Float, y:Float, radius:Float, color:Int):Shape2d {
		final shape = Shape2d.create();
		shape.setCircle(radius);
		return place(shape, x, y, color);
	}

	function place(shape:Shape2d, x:Float, y:Float, color:Int):Shape2d {
		shape.setColor(color);
		shape.setPosition(x, y, 0);
		shape.setParent(stage);
		return shape;
	}

	/** A line of text in the built-in font (font 0), centered on `x`, `y`. **/
	function label(string:String, x:Float, y:Float, size:Float, color:Int):Text {
		final text = Text.create(0);
		text.setString(string);
		text.setFontSize(size);
		text.setColor(color);
		text.setAlign(TextHalign.CENTER, TextValign.MIDDLE);
		text.setPosition(x, y, 0);
		text.setParent(stage);
		return text;
	}
}
