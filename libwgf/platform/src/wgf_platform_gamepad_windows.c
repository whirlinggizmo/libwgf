#include "wgf_platform_gamepad_priv.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "wgf_gamepad.h"

/* Gamepads on Windows, through XInput: the four slots XInput itself numbers, so a
 * pad's number is XInput's. XInput is loaded at runtime, from whichever of its DLLs
 * the system has, so the program needs none of them to start. Its documented
 * XInputGetState leaves out the Guide button; XInputGetStateEx, which the DLLs export
 * by ordinal 100 only, reports it too, so that is used where there is one, as SDL and
 * others do. Cribbed from wgrender's wgr_gamepad. */

#define EMPTY_CHECK_MS 2000 /* asking XInput about an empty slot is slow */
#define GET_STATE_EX_ORDINAL 100
#define GUIDE_MASK 0x0400 /* XInputGetStateEx's only */

/* XInput's own types: xinput.h isn't in every toolchain */
typedef struct xinput_gamepad_t {
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
} xinput_gamepad_t;
typedef struct xinput_state_t {
    DWORD packet;
    xinput_gamepad_t gamepad;
} xinput_state_t;
typedef DWORD(WINAPI *xinput_get_state_fn)(DWORD, xinput_state_t *);

static HMODULE xinput;
static xinput_get_state_fn xinput_get_state;
static DWORD next_empty_check[WGF_PLATFORM_PRIV_GAMEPADS];

void wgf_platform_priv_gamepad_platform_open(void)
{
    static const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    int i;
    for (i = 0; i < 3 && xinput == NULL; i++) xinput = LoadLibraryA(dlls[i]);
    if (xinput != NULL) {
        FARPROC get_state = GetProcAddress(xinput, (LPCSTR)(ULONG_PTR)GET_STATE_EX_ORDINAL);
        if (get_state == NULL) get_state = GetProcAddress(xinput, "XInputGetState");
        memcpy(&xinput_get_state, &get_state, sizeof(xinput_get_state)); /* a function pointer of another type */
    }
    memset(next_empty_check, 0, sizeof(next_empty_check));
}

void wgf_platform_priv_gamepad_platform_close(void)
{
    if (xinput != NULL) FreeLibrary(xinput);
    xinput = NULL;
    xinput_get_state = NULL;
}

static float stick(SHORT value, bool flip)
{
    const float v = (float)value / 32767.0f;
    return flip ? -v : v;
}

void wgf_platform_priv_gamepad_platform_poll(wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS])
{
    static const struct {
        WORD mask;
        int button;
    } map[] = {
        {0x1000, WGF_GAMEPAD_BUTTON_SOUTH},       {0x2000, WGF_GAMEPAD_BUTTON_EAST},
        {0x4000, WGF_GAMEPAD_BUTTON_WEST},        {0x8000, WGF_GAMEPAD_BUTTON_NORTH},
        {0x0100, WGF_GAMEPAD_BUTTON_LEFT_BUMPER}, {0x0200, WGF_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {0x0020, WGF_GAMEPAD_BUTTON_BACK},        {0x0010, WGF_GAMEPAD_BUTTON_START},
        {0x0040, WGF_GAMEPAD_BUTTON_LEFT_STICK},  {0x0080, WGF_GAMEPAD_BUTTON_RIGHT_STICK},
        {0x0001, WGF_GAMEPAD_BUTTON_DPAD_UP},     {0x0002, WGF_GAMEPAD_BUTTON_DPAD_DOWN},
        {0x0004, WGF_GAMEPAD_BUTTON_DPAD_LEFT},   {0x0008, WGF_GAMEPAD_BUTTON_DPAD_RIGHT},
        {GUIDE_MASK, WGF_GAMEPAD_BUTTON_GUIDE},
    };
    const DWORD now = GetTickCount();
    int i;
    size_t b;
    if (xinput_get_state == NULL) return;
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) {
        xinput_state_t state;
        if (!pads[i].connected && (LONG)(now - next_empty_check[i]) < 0) continue;
        if (xinput_get_state((DWORD)i, &state) != ERROR_SUCCESS) {
            memset(&pads[i], 0, sizeof(pads[i]));
            next_empty_check[i] = now + EMPTY_CHECK_MS;
            continue;
        }
        pads[i].connected = true;
        snprintf(pads[i].name, sizeof(pads[i].name), "XInput controller %d", i + 1);
        for (b = 0; b < sizeof(map) / sizeof(map[0]); b++) {
            pads[i].buttons[map[b].button] = (state.gamepad.buttons & map[b].mask) != 0;
        }
        pads[i].axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = stick(state.gamepad.lx, false);
        pads[i].axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y] = stick(state.gamepad.ly, true); /* XInput: up is positive */
        pads[i].axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X] = stick(state.gamepad.rx, false);
        pads[i].axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y] = stick(state.gamepad.ry, true);
        pads[i].axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER] = (float)state.gamepad.left_trigger / 255.0f;
        pads[i].axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER] = (float)state.gamepad.right_trigger / 255.0f;
    }
}
