// The page tools/check_js_binding.py's "input" step drives in a browser: one text field of the
// Clay UI's, the binding and the field's text buffer left on the page (globalThis.wgf,
// globalThis.name) for the check to read the field through, and the frames counted, so it can
// wait for one.
import createWgfHost from "./wgf-host.js";
import * as wgf from "./wgf.js";

const host = await createWgfHost({ canvas: document.getElementById("canvas") });
wgf.attach(host);
globalThis.wgf = wgf;
globalThis.frames = 0;
globalThis.name = 0;
wgf.wgf_app_run(() => { globalThis.name = wgf.wgf_text_buffer_create_text(""); }, null, () => {
    if (wgf.wgf_ui_begin()) {
        wgf.wgf_ui_text_field("name", globalThis.name, "Name", 0);
        wgf.wgf_ui_end();
    }
    globalThis.frames++;
}, null);
