#include "wgf_platform_tilt_priv.h"

#include <emscripten.h>

/* The page's device orientation (helix's DomTiltAdapter): where the browser asks permission
 * (iOS, and Chrome on Android), asked inside the next tap's own event, the only place it may
 * be; otherwise listened to at once. STARTING until the first reading with numbers; a second
 * and a half without one, counted from when the page has its focus (Chrome reads none
 * without it), UNAVAILABLE, though it goes on listening, as a phone's sensor may start late.
 * The status and newest reading wait in Module.wgf_tilt for the frame's take. */

EM_JS(int, wgf_platform_priv_tilt_js_start, (void), {
    const t = Module.wgf_tilt = {status : 0, fresh : false, beta : 0, gamma : 0, angle : 0, off : []};
    if (window.isSecureContext === false) return t.status = 6;
    if (typeof DeviceOrientationEvent === 'undefined') return t.status = 5;
    const listen = () => {
        let read = false;
        t.status = 2;
        const on = (e) => {
            if (e.beta == null || e.gamma == null) return; /* a sensor starting, or none */
            read = true;
            t.status = 3;
            t.beta = e.beta;
            t.gamma = e.gamma;
            t.angle = (screen.orientation && screen.orientation.angle) || window.orientation || 0;
            t.fresh = true;
        };
        window.addEventListener('deviceorientation', on);
        t.off.push(() => window.removeEventListener('deviceorientation', on));
        const count = () => {
            const id = setTimeout(() => { if (!read && t.status == 2) t.status = 5; }, 1500);
            t.off.push(() => clearTimeout(id));
        };
        if (document.hasFocus()) {
            count();
        } else {
            const focused = () => { window.removeEventListener('focus', focused); count(); };
            window.addEventListener('focus', focused);
            t.off.push(() => window.removeEventListener('focus', focused));
        }
    };
    if (typeof DeviceOrientationEvent.requestPermission !== 'function') {
        listen();
        return t.status;
    }
    const ask = () => {
        t.status = 1;
        const remove = () => {
            window.removeEventListener('touchend', tap, true);
            window.removeEventListener('click', tap, true);
        };
        /* a tap whose press is already spent (going fullscreen on it spends it) is passed over:
           asked there, Chrome answers "denied" without asking */
        const tap = () => {
            if (navigator.userActivation && !navigator.userActivation.isActive) return;
            remove();
            DeviceOrientationEvent.requestPermission().then(
                (answer) => { if (answer === 'granted') listen(); else t.status = 4; }, () => ask());
        };
        window.addEventListener('touchend', tap, true);
        window.addEventListener('click', tap, true);
        t.off.push(remove);
    };
    ask();
    return t.status;
})

EM_JS(void, wgf_platform_priv_tilt_js_stop, (void), {
    const t = Module.wgf_tilt;
    if (!t) return;
    for (const off of t.off) off();
    Module.wgf_tilt = null;
})

EM_JS(int, wgf_platform_priv_tilt_js_take, (double *reading), {
    const t = Module.wgf_tilt;
    if (!t) return -1;
    if (!t.fresh) return t.status;
    t.fresh = false;
    HEAPF64[(reading >> 3) + 0] = t.beta;
    HEAPF64[(reading >> 3) + 1] = t.gamma;
    HEAPF64[(reading >> 3) + 2] = t.angle;
    return t.status + 16;
})

wgf_tilt_status_t wgf_platform_priv_tilt_start(void)
{
    return (wgf_tilt_status_t)wgf_platform_priv_tilt_js_start();
}

void wgf_platform_priv_tilt_stop(void)
{
    wgf_platform_priv_tilt_js_stop();
}

wgf_tilt_status_t wgf_platform_priv_tilt_take(bool *fresh, double reading[3])
{
    const int taken = wgf_platform_priv_tilt_js_take(reading);
    *fresh = taken >= 16;
    return taken < 0 ? WGF_TILT_STATUS_OFF : (wgf_tilt_status_t)(taken & 15);
}
