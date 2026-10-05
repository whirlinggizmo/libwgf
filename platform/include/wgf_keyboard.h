#ifndef WGF_KEYBOARD_H
#define WGF_KEYBOARD_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The keyboard, read inside a tick or a frame: what changed since the previous
 * tick, or the previous frame. Keys are named by where they are on a US layout:
 * WGF_KEY_Z is the key left of X, whatever the layout prints on it. For the
 * characters the user typed, read wgf_input_get_chars. */

typedef enum wgf_keyboard_key_t {
    WGF_KEY_SPACE = 32,
    WGF_KEY_APOSTROPHE = 39,
    WGF_KEY_COMMA = 44,
    WGF_KEY_MINUS = 45,
    WGF_KEY_PERIOD = 46,
    WGF_KEY_SLASH = 47,
    WGF_KEY_0 = 48,
    WGF_KEY_1 = 49,
    WGF_KEY_2 = 50,
    WGF_KEY_3 = 51,
    WGF_KEY_4 = 52,
    WGF_KEY_5 = 53,
    WGF_KEY_6 = 54,
    WGF_KEY_7 = 55,
    WGF_KEY_8 = 56,
    WGF_KEY_9 = 57,
    WGF_KEY_SEMICOLON = 59,
    WGF_KEY_EQUAL = 61,
    WGF_KEY_A = 65,
    WGF_KEY_B = 66,
    WGF_KEY_C = 67,
    WGF_KEY_D = 68,
    WGF_KEY_E = 69,
    WGF_KEY_F = 70,
    WGF_KEY_G = 71,
    WGF_KEY_H = 72,
    WGF_KEY_I = 73,
    WGF_KEY_J = 74,
    WGF_KEY_K = 75,
    WGF_KEY_L = 76,
    WGF_KEY_M = 77,
    WGF_KEY_N = 78,
    WGF_KEY_O = 79,
    WGF_KEY_P = 80,
    WGF_KEY_Q = 81,
    WGF_KEY_R = 82,
    WGF_KEY_S = 83,
    WGF_KEY_T = 84,
    WGF_KEY_U = 85,
    WGF_KEY_V = 86,
    WGF_KEY_W = 87,
    WGF_KEY_X = 88,
    WGF_KEY_Y = 89,
    WGF_KEY_Z = 90,
    WGF_KEY_LEFT_BRACKET = 91,
    WGF_KEY_BACKSLASH = 92,
    WGF_KEY_RIGHT_BRACKET = 93,
    WGF_KEY_GRAVE_ACCENT = 96,
    WGF_KEY_WORLD_1 = 161,
    WGF_KEY_WORLD_2 = 162,
    WGF_KEY_ESCAPE = 256,
    WGF_KEY_ENTER = 257,
    WGF_KEY_TAB = 258,
    WGF_KEY_BACKSPACE = 259,
    WGF_KEY_INSERT = 260,
    WGF_KEY_DELETE = 261,
    WGF_KEY_RIGHT = 262,
    WGF_KEY_LEFT = 263,
    WGF_KEY_DOWN = 264,
    WGF_KEY_UP = 265,
    WGF_KEY_PAGE_UP = 266,
    WGF_KEY_PAGE_DOWN = 267,
    WGF_KEY_HOME = 268,
    WGF_KEY_END = 269,
    WGF_KEY_CAPS_LOCK = 280,
    WGF_KEY_SCROLL_LOCK = 281,
    WGF_KEY_NUM_LOCK = 282,
    WGF_KEY_PRINT_SCREEN = 283,
    WGF_KEY_PAUSE = 284,
    WGF_KEY_F1 = 290,
    WGF_KEY_F2 = 291,
    WGF_KEY_F3 = 292,
    WGF_KEY_F4 = 293,
    WGF_KEY_F5 = 294,
    WGF_KEY_F6 = 295,
    WGF_KEY_F7 = 296,
    WGF_KEY_F8 = 297,
    WGF_KEY_F9 = 298,
    WGF_KEY_F10 = 299,
    WGF_KEY_F11 = 300,
    WGF_KEY_F12 = 301,
    WGF_KEY_F13 = 302,
    WGF_KEY_F14 = 303,
    WGF_KEY_F15 = 304,
    WGF_KEY_F16 = 305,
    WGF_KEY_F17 = 306,
    WGF_KEY_F18 = 307,
    WGF_KEY_F19 = 308,
    WGF_KEY_F20 = 309,
    WGF_KEY_F21 = 310,
    WGF_KEY_F22 = 311,
    WGF_KEY_F23 = 312,
    WGF_KEY_F24 = 313,
    WGF_KEY_F25 = 314,
    WGF_KEY_KP_0 = 320,
    WGF_KEY_KP_1 = 321,
    WGF_KEY_KP_2 = 322,
    WGF_KEY_KP_3 = 323,
    WGF_KEY_KP_4 = 324,
    WGF_KEY_KP_5 = 325,
    WGF_KEY_KP_6 = 326,
    WGF_KEY_KP_7 = 327,
    WGF_KEY_KP_8 = 328,
    WGF_KEY_KP_9 = 329,
    WGF_KEY_KP_DECIMAL = 330,
    WGF_KEY_KP_DIVIDE = 331,
    WGF_KEY_KP_MULTIPLY = 332,
    WGF_KEY_KP_SUBTRACT = 333,
    WGF_KEY_KP_ADD = 334,
    WGF_KEY_KP_ENTER = 335,
    WGF_KEY_KP_EQUAL = 336,
    WGF_KEY_LEFT_SHIFT = 340,
    WGF_KEY_LEFT_CONTROL = 341,
    WGF_KEY_LEFT_ALT = 342,
    WGF_KEY_LEFT_SUPER = 343,
    WGF_KEY_RIGHT_SHIFT = 344,
    WGF_KEY_RIGHT_CONTROL = 345,
    WGF_KEY_RIGHT_ALT = 346,
    WGF_KEY_RIGHT_SUPER = 347,
    WGF_KEY_MENU = 348
} wgf_keyboard_key_t;

/* Where `key` is: UP, PRESSED (went down since the previous tick or frame), DOWN,
 * or RELEASED. A key the keyboard doesn't have is UP. A key pressed and released
 * between two reads is PRESSED for one, so no tap is missed. */
WGF_API wgf_input_state_t wgf_keyboard_get_state(wgf_keyboard_key_t key);

/* PRESSED or DOWN. */
WGF_API bool wgf_keyboard_is_down(wgf_keyboard_key_t key);
/* Went down since the previous tick or frame; held keys repeating don't count. */
WGF_API bool wgf_keyboard_is_pressed(wgf_keyboard_key_t key);
/* Went up since the previous tick or frame. */
WGF_API bool wgf_keyboard_is_released(wgf_keyboard_key_t key);

#ifdef __cplusplus
}
#endif

#endif
