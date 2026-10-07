// libwgf from JavaScript: the C app-hello (examples/c/app-hello) through the JS binding's
// typed layer (wgf-typed.js: Draw.circle, Keyboard.isPressed, ...), call for call. A window with 2D shapes, text, and input: a marker follows the mouse,
// typing shows what was typed (Backspace takes the last character back), and Escape
// quits where quitting means anything. The frame rate is drawn at the top.
//
// Its differences from the C: the colors are wgf_color_get's of the stock ones (C's
// WGF_COLOR_* are macros over it), fetched once; and the text typed is a JS string. The raw
// binding's names (wgf.wgf_draw_circle) are the same calls, as examples/js/asteroids uses.
import createWgfHost from "./wgf-host.js";
import { App, Color, ColorStock, Draw, Input, Keyboard, KeyboardKey, Loop, Mouse, Render, Window, attach }
    from "./wgf-typed.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

const color = (stock) => Color.get(stock);
let colors = null;
let typed = "";

function init() {
    colors = {
        raywhite: color(ColorStock.RAYWHITE), skyblue: color(ColorStock.SKYBLUE),
        darkblue: color(ColorStock.DARKBLUE), red: color(ColorStock.RED),
        gold: color(ColorStock.GOLD), purple: color(ColorStock.PURPLE),
        black: color(ColorStock.BLACK), maroon: color(ColorStock.MAROON),
        darkgray: color(ColorStock.DARKGRAY), gray: color(ColorStock.GRAY),
        fps: Color.make(0, 255, 0, 255),
    };
    Render.setClearColor(colors.raywhite);
}

const mouse = { x: 0, y: 0 };

function frame() {
    Mouse.getPosition(mouse);
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();
    if (Keyboard.isPressed(KeyboardKey.BACKSPACE) && typed.length > 0) typed = typed.slice(0, -1);
    const chars = Input.getChars();
    if (typed.length + chars.length < 64) typed += chars;

    // filled and outlined rectangles
    Draw.rectangle(40, 40, 200, 120, colors.skyblue);
    Draw.rectangleLines(40, 40, 200, 120, 1, colors.darkblue);
    // a line, a triangle, circles
    Draw.line(40, 200, 240, 320, 1, colors.red);
    Draw.triangle(320, 60, 280, 180, 360, 180, colors.gold);
    Draw.circle(440, 120, 60, colors.purple);
    Draw.circleLines(440, 120, 60, 1, colors.black);
    // a marker that follows the mouse
    Draw.circle(mouse.x, mouse.y, 8, colors.maroon);
    // text
    Draw.text(0, "libwgf", 40, 360, 32, colors.darkgray);
    Draw.text(0, typed || "type something", 40, 410, 16, typed ? colors.darkblue : colors.gray);
    if (App.canQuit()) Draw.text(0, "press Esc to quit", 40, 440, 16, colors.gray);
    Draw.text(0, `${Math.round(Loop.getFps())} FPS`, 40, 12, 16, colors.fps);
}

Window.setTitle("libwgf hello (JS)");
Window.setSize(800, 600);
Window.setMsaa(true);
App.run(init, null, frame, null);
