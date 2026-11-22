// The page tools/check_js_binding.py's "input" step drives in a browser: one text field of the
// Clay UI's, the binding left on the page (globalThis.wgf) for the check to read the field
// through, and the frames counted, so it can wait for one.
import createWgfHost from "./wgf-host.js";
import * as wgf from "./wgf.js";

const host = await createWgfHost({ canvas: document.getElementById("canvas") });
wgf.attach(host);
globalThis.wgf = wgf;
globalThis.frames = 0;
wgf.wgf_app_run(null, null, () => {
    if (wgf.wgf_ui_begin()) {
        wgf.wgf_ui_text_field("name", "Name", 0);
        wgf.wgf_ui_end();
    }
    globalThis.frames++;
}, null);
