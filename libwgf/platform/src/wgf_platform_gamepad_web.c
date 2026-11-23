#include "wgf_platform_gamepad_priv.h"

#include <emscripten.h>
#include <string.h>

/* Gamepads on the web, through the browser's Gamepad API, in its "standard" mapping:
 * buttons south, east, west, north, the bumpers, the triggers, back, start, the
 * sticks, the d-pad up, down, left, right, then guide; axes left x, y, right x, y,
 * y down. A browser lists a pad only once one of its buttons is pressed on the page.
 * Rumble is the pad's vibration actuator's "dual-rumble" effect (Chrome, Edge, Safari),
 * else its first haptic actuator's pulse, one strength for both motors (Firefox, behind a
 * setting); a browser with neither has a pad without motors. Cribbed from wgrender's
 * wgr_gamepad. */

/* Per pad: bit i of the result says pad i is connected, bit 4 + i that it can rumble; its
 * 6 axes, 17 buttons, and name go to the given addresses. */
EM_JS(int, web_poll_gamepads, (float *axes, int *buttons, char *names, int pads, int name_size), {
    const list = navigator.getGamepads ? navigator.getGamepads() : [];
    const order = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 10, 11, 12, 13, 14, 15 ]; /* ours, from theirs */
    let connected = 0;
    for (let i = 0; i < pads && i < list.length; i++) {
        const pad = list[i];
        if (!pad || !pad.connected) continue;
        connected |= 1 << i;
        const act = pad.vibrationActuator, haptic = pad.hapticActuators && pad.hapticActuators[0];
        if ((act && typeof act.playEffect === "function") || (haptic && typeof haptic.pulse === "function")) {
            connected |= 1 << (4 + i);
        }
        for (let b = 0; b < 17; b++) {
            const button = pad.buttons[order[b]];
            HEAP32[(buttons >> 2) + i * 17 + b] = button && button.pressed ? 1 : 0;
        }
        for (let a = 0; a < 4; a++) HEAPF32[(axes >> 2) + i * 6 + a] = a < pad.axes.length ? pad.axes[a] : 0;
        HEAPF32[(axes >> 2) + i * 6 + 4] = pad.buttons[6] ? pad.buttons[6].value : 0;
        HEAPF32[(axes >> 2) + i * 6 + 5] = pad.buttons[7] ? pad.buttons[7].value : 0;
        stringToUTF8(pad.id, names + i * name_size, name_size);
    }
    return connected;
})

/* Pad `index`'s motors for `seconds`, or stopped (0); the effect's promise, which a newer
 * effect rejects, is answered so it isn't an unhandled rejection. */
EM_JS(void, web_rumble_gamepad, (int index, float low, float high, float seconds), {
    const list = navigator.getGamepads ? navigator.getGamepads() : [];
    const pad = list[index];
    if (!pad) return;
    const ms = Math.round(seconds * 1000);
    const quiet = (promise) => { if (promise && typeof promise.catch === "function") promise.catch(() => {}); };
    const act = pad.vibrationActuator, haptic = pad.hapticActuators && pad.hapticActuators[0];
    if (act && typeof act.playEffect === "function") {
        if (ms > 0) {
            quiet(act.playEffect("dual-rumble",
                                 {startDelay : 0, duration : ms, strongMagnitude : low, weakMagnitude : high}));
        } else if (typeof act.reset === "function") {
            quiet(act.reset());
        } else {
            quiet(act.playEffect("dual-rumble",
                                 {startDelay : 0, duration : 0, strongMagnitude : 0, weakMagnitude : 0}));
        }
    } else if (haptic && typeof haptic.pulse === "function") {
        quiet(ms > 0 ? haptic.pulse(Math.max(low, high), ms) : haptic.pulse(0, 0));
    }
})

void wgf_platform_priv_gamepad_platform_open(void)
{
}

void wgf_platform_priv_gamepad_platform_close(void)
{
}

void wgf_platform_priv_gamepad_platform_poll(wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    static float axes[WGF_PLATFORM_PRIV_GAMEPADS * WGF_PLATFORM_PRIV_GAMEPAD_AXES];
    static int buttons[WGF_PLATFORM_PRIV_GAMEPADS * WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS];
    static char names[WGF_PLATFORM_PRIV_GAMEPADS * WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE];
    const int connected =
        web_poll_gamepads(axes, buttons, names, WGF_PLATFORM_PRIV_GAMEPADS, WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE);
    int i, b, a;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) {
        pads[i].connected = (connected >> i) & 1;
        if (!pads[i].connected) continue;
        pads[i].rumble = (connected >> (4 + i)) & 1;
        for (b = 0; b < WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS; b++) {
            pads[i].buttons[b] = buttons[i * WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS + b] != 0;
        }
        for (a = 0; a < WGF_PLATFORM_PRIV_GAMEPAD_AXES; a++)
            pads[i].axes[a] = axes[i * WGF_PLATFORM_PRIV_GAMEPAD_AXES + a];
        memcpy(pads[i].name, &names[i * WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE], WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE);
    }
}

void wgf_platform_priv_gamepad_platform_rumble(int pad, float low, float high, float seconds)
{
    web_rumble_gamepad(pad, low, high, seconds);
}
