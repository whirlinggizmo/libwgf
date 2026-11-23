import wgf.*;

/**
	A sensor's behavior: it counts the bodies that enter it.

	The runtime takes the world's events each tick and tells this of its actor's. A sensor
	body (Body3dType.SENSOR) meets no body, so the pile falls straight through it.
**/
class Sensor extends Behavior {
	/** How many bodies have entered, since the last reset. **/
	public var count = 0;

	/** Seconds since the last body entered, to flash the sensor's outline. **/
	public var sinceLast = 99.0;

	override function onTick(dt:Float) {
		sinceLast += dt;
	}

	override function onTriggerEnter(other:Actor, layer:Int) {
		count++;
		sinceLast = 0;
	}
}
