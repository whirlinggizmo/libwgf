// libwgf from JavaScript: the C app-hello (examples/c/app-hello) through the JS binding,
// call for call. A window with 2D shapes, text, and input: a marker follows the mouse,
// typing shows what was typed (Backspace takes the last character back), and Escape
// quits where quitting means anything. The frame rate is drawn at the top.
//
// Its differences from the C: the colors are wgf_color_get's of the stock ones (C's
// WGF_COLOR_* are macros over it), fetched once; and the text typed is a JS string.
import createWgfHost from "./wgf-host.js";
import * as wgf from "./wgf.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
wgf.attach(host);

const color = (stock) => wgf.wgf_color_get(stock);
let colors = null;
let typed = "";

function init() {
    colors = {
        raywhite: color(wgf.WGF_COLOR_STOCK_RAYWHITE), skyblue: color(wgf.WGF_COLOR_STOCK_SKYBLUE),
        darkblue: color(wgf.WGF_COLOR_STOCK_DARKBLUE), red: color(wgf.WGF_COLOR_STOCK_RED),
        gold: color(wgf.WGF_COLOR_STOCK_GOLD), purple: color(wgf.WGF_COLOR_STOCK_PURPLE),
        black: color(wgf.WGF_COLOR_STOCK_BLACK), maroon: color(wgf.WGF_COLOR_STOCK_MAROON),
        darkgray: color(wgf.WGF_COLOR_STOCK_DARKGRAY), gray: color(wgf.WGF_COLOR_STOCK_GRAY),
        fps: wgf.wgf_color_make(0, 255, 0, 255),
    };
    wgf.wgf_render_set_clear_color(colors.raywhite);
}

const mouse = { x: 0, y: 0 };

function frame() {
    wgf.wgf_mouse_get_position(mouse);
    if (wgf.wgf_app_can_quit() && wgf.wgf_keyboard_is_pressed(wgf.WGF_KEY_ESCAPE)) wgf.wgf_app_quit();
    if (wgf.wgf_keyboard_is_pressed(wgf.WGF_KEY_BACKSPACE) && typed.length > 0) typed = typed.slice(0, -1);
    const chars = wgf.wgf_input_get_chars();
    if (typed.length + chars.length < 64) typed += chars;

    // filled and outlined rectangles
    wgf.wgf_draw_rectangle(40, 40, 200, 120, colors.skyblue);
    wgf.wgf_draw_rectangle_lines(40, 40, 200, 120, 1, colors.darkblue);
    // a line, a triangle, circles
    wgf.wgf_draw_line(40, 200, 240, 320, 1, colors.red);
    wgf.wgf_draw_triangle(320, 60, 280, 180, 360, 180, colors.gold);
    wgf.wgf_draw_circle(440, 120, 60, colors.purple);
    wgf.wgf_draw_circle_lines(440, 120, 60, 1, colors.black);
    // a marker that follows the mouse
    wgf.wgf_draw_circle(mouse.x, mouse.y, 8, colors.maroon);
    // text
    wgf.wgf_draw_text(0, "libwgf", 40, 360, 32, colors.darkgray);
    wgf.wgf_draw_text(0, typed || "type something", 40, 410, 16, typed ? colors.darkblue : colors.gray);
    if (wgf.wgf_app_can_quit()) wgf.wgf_draw_text(0, "press Esc to quit", 40, 440, 16, colors.gray);
    wgf.wgf_draw_text(0, `${Math.round(wgf.wgf_loop_get_fps())} FPS`, 40, 12, 16, colors.fps);
}

wgf.wgf_window_set_title("libwgf hello (JS)");
wgf.wgf_window_set_size(800, 600);
wgf.wgf_window_set_msaa(true);
wgf.wgf_app_run(init, null, frame, null);
