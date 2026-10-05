#include "wgf_app_script_priv.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_app.h" /* the window's events, which the script's inputs are */
#include "wgf_app_script_source_priv.h"
#include "wgf_gamepad.h"
#include "wgf_keyboard.h"
#include "wgf_log.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_probe.h"
#include "wgf_random.h"

/* A scripted run (wgf_app_script_priv.h): the script parsed whole first, each line
 * checked, into commands sorted by frame, then played a frame at a time. */

#define LINE_MAX_BYTES 512
#define FRAME_MAX 100000000L /* about 19 days at 60 a second */

typedef enum {
    CMD_KEY_DOWN,
    CMD_KEY_UP,
    CMD_TEXT,
    CMD_MOUSE_MOVE,
    CMD_MOUSE_DOWN,
    CMD_MOUSE_UP,
    CMD_MOUSE_SCROLL,
    CMD_PAD_CONNECT,
    CMD_PAD_DISCONNECT,
    CMD_PAD_DOWN,
    CMD_PAD_UP,
    CMD_PAD_AXIS,
    CMD_EXPECT,
    CMD_LOG,
    CMD_SCREENSHOT,
    CMD_END
} kind_t;

typedef enum { OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE } op_t;

typedef struct command_t {
    long frame;
    int line;  /* in the script, for what is logged */
    int order; /* its place in the script, to keep the order within a frame */
    kind_t kind;
    int code;  /* a key, a mouse button, a pad's button or axis */
    int pad;
    op_t op;
    float x, y; /* a position, a scroll, an axis's value */
    double value;
    char *text; /* text, a probe's name, a log's or a screenshot's */
    bool done;  /* played */
} command_t;

static struct {
    bool running;
    bool ended;
    bool passed;
    command_t *commands;
    int count, capacity, next;
    int failures, expectations;
    long end_frame; /* the end's frame, -1 when the script has none */
    float mouse_x, mouse_y;
    bool pads_taken;
    wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS];
} script;

/* ---- names ------------------------------------------------------------------ */

typedef struct name_t {
    const char *name;
    int code;
} name_t;

static const name_t keys[] = {
    {"space", WGF_KEY_SPACE}, {"apostrophe", WGF_KEY_APOSTROPHE}, {"comma", WGF_KEY_COMMA},
    {"minus", WGF_KEY_MINUS}, {"period", WGF_KEY_PERIOD}, {"slash", WGF_KEY_SLASH}, {"0", WGF_KEY_0},
    {"1", WGF_KEY_1}, {"2", WGF_KEY_2}, {"3", WGF_KEY_3}, {"4", WGF_KEY_4}, {"5", WGF_KEY_5}, {"6", WGF_KEY_6},
    {"7", WGF_KEY_7}, {"8", WGF_KEY_8}, {"9", WGF_KEY_9}, {"semicolon", WGF_KEY_SEMICOLON},
    {"equal", WGF_KEY_EQUAL}, {"a", WGF_KEY_A}, {"b", WGF_KEY_B}, {"c", WGF_KEY_C}, {"d", WGF_KEY_D},
    {"e", WGF_KEY_E}, {"f", WGF_KEY_F}, {"g", WGF_KEY_G}, {"h", WGF_KEY_H}, {"i", WGF_KEY_I}, {"j", WGF_KEY_J},
    {"k", WGF_KEY_K}, {"l", WGF_KEY_L}, {"m", WGF_KEY_M}, {"n", WGF_KEY_N}, {"o", WGF_KEY_O}, {"p", WGF_KEY_P},
    {"q", WGF_KEY_Q}, {"r", WGF_KEY_R}, {"s", WGF_KEY_S}, {"t", WGF_KEY_T}, {"u", WGF_KEY_U}, {"v", WGF_KEY_V},
    {"w", WGF_KEY_W}, {"x", WGF_KEY_X}, {"y", WGF_KEY_Y}, {"z", WGF_KEY_Z}, {"left_bracket", WGF_KEY_LEFT_BRACKET},
    {"backslash", WGF_KEY_BACKSLASH}, {"right_bracket", WGF_KEY_RIGHT_BRACKET},
    {"grave_accent", WGF_KEY_GRAVE_ACCENT}, {"escape", WGF_KEY_ESCAPE}, {"enter", WGF_KEY_ENTER},
    {"tab", WGF_KEY_TAB}, {"backspace", WGF_KEY_BACKSPACE}, {"insert", WGF_KEY_INSERT}, {"delete", WGF_KEY_DELETE},
    {"right", WGF_KEY_RIGHT}, {"left", WGF_KEY_LEFT}, {"down", WGF_KEY_DOWN}, {"up", WGF_KEY_UP},
    {"page_up", WGF_KEY_PAGE_UP}, {"page_down", WGF_KEY_PAGE_DOWN}, {"home", WGF_KEY_HOME}, {"end", WGF_KEY_END},
    {"f1", WGF_KEY_F1}, {"f2", WGF_KEY_F2}, {"f3", WGF_KEY_F3}, {"f4", WGF_KEY_F4}, {"f5", WGF_KEY_F5},
    {"f6", WGF_KEY_F6}, {"f7", WGF_KEY_F7}, {"f8", WGF_KEY_F8}, {"f9", WGF_KEY_F9}, {"f10", WGF_KEY_F10},
    {"f11", WGF_KEY_F11}, {"f12", WGF_KEY_F12}, {"left_shift", WGF_KEY_LEFT_SHIFT},
    {"left_control", WGF_KEY_LEFT_CONTROL}, {"left_alt", WGF_KEY_LEFT_ALT}, {"right_shift", WGF_KEY_RIGHT_SHIFT},
    {"right_control", WGF_KEY_RIGHT_CONTROL}, {"right_alt", WGF_KEY_RIGHT_ALT},
};

static const name_t mouse_buttons[] = {
    {"left", SAPP_MOUSEBUTTON_LEFT}, {"right", SAPP_MOUSEBUTTON_RIGHT}, {"middle", SAPP_MOUSEBUTTON_MIDDLE}};

static const name_t pad_buttons[] = {
    {"south", WGF_GAMEPAD_BUTTON_SOUTH}, {"east", WGF_GAMEPAD_BUTTON_EAST}, {"west", WGF_GAMEPAD_BUTTON_WEST},
    {"north", WGF_GAMEPAD_BUTTON_NORTH}, {"left_bumper", WGF_GAMEPAD_BUTTON_LEFT_BUMPER},
    {"right_bumper", WGF_GAMEPAD_BUTTON_RIGHT_BUMPER}, {"left_trigger", WGF_GAMEPAD_BUTTON_LEFT_TRIGGER},
    {"right_trigger", WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER}, {"back", WGF_GAMEPAD_BUTTON_BACK},
    {"start", WGF_GAMEPAD_BUTTON_START}, {"guide", WGF_GAMEPAD_BUTTON_GUIDE},
    {"left_stick", WGF_GAMEPAD_BUTTON_LEFT_STICK}, {"right_stick", WGF_GAMEPAD_BUTTON_RIGHT_STICK},
    {"dpad_up", WGF_GAMEPAD_BUTTON_DPAD_UP}, {"dpad_down", WGF_GAMEPAD_BUTTON_DPAD_DOWN},
    {"dpad_left", WGF_GAMEPAD_BUTTON_DPAD_LEFT}, {"dpad_right", WGF_GAMEPAD_BUTTON_DPAD_RIGHT},
};

static const name_t pad_axes[] = {
    {"left_x", WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X},
    {"left_y", WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y},
    {"right_x", WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X},
    {"right_y", WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y},
    {"left_trigger", WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER},
    {"right_trigger", WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER},
};

static const char *const ops[] = {"==", "!=", "<", "<=", ">", ">="};

static bool find_name(const name_t *names, size_t count, const char *word, int *code)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (strcmp(names[i].name, word) == 0) {
            *code = names[i].code;
            return true;
        }
    }
    return false;
}

/* ---- parsing ---------------------------------------------------------------- */

typedef struct parser_t {
    int line;
    int order;
    bool failed;
} parser_t;

static void parse_error(parser_t *parser, const char *what, const char *word)
{
    wgf_log_error("wgf_script: line %d: %s%s%s%s", parser->line, what, word != NULL ? " (\"" : "",
                  word != NULL ? word : "", word != NULL ? "\")" : "");
    parser->failed = true;
}

/* The next word of `*at`, NUL-terminated in place; NULL at the line's end. */
static char *next_word(char **at)
{
    char *p = *at, *start;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') {
        *at = p;
        return NULL;
    }
    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') p++;
    if (*p != '\0') *p++ = '\0';
    *at = p;
    return start;
}

/* The rest of the line, its leading blanks dropped: a text's, a log's. */
static char *rest_of(char **at)
{
    char *p = *at;
    while (*p == ' ' || *p == '\t') p++;
    *at = p + strlen(p);
    return p;
}

static bool parse_long(const char *word, long min, long max, long *out)
{
    char *end;
    long value;
    if (word == NULL) return false;
    errno = 0;
    value = strtol(word, &end, 10);
    if (errno != 0 || end == word || *end != '\0' || value < min || value > max) return false;
    *out = value;
    return true;
}

static bool parse_number(const char *word, double *out)
{
    char *end;
    double value;
    if (word == NULL) return false;
    errno = 0;
    value = strtod(word, &end);
    if (errno != 0 || end == word || *end != '\0' || !isfinite(value)) return false;
    *out = value;
    return true;
}

static command_t *add(parser_t *parser, long frame, kind_t kind)
{
    command_t *command;
    if (script.count == script.capacity) {
        const int capacity = script.capacity > 0 ? script.capacity * 2 : 64;
        command_t *grown = (command_t *)realloc(script.commands, sizeof(command_t) * (size_t)capacity);
        if (grown == NULL) {
            parse_error(parser, "out of memory", NULL);
            return NULL;
        }
        script.commands = grown;
        script.capacity = capacity;
    }
    command = &script.commands[script.count++];
    memset(command, 0, sizeof(*command));
    command->frame = frame;
    command->line = parser->line;
    command->order = parser->order++;
    command->kind = kind;
    return command;
}

static char *copy_of(parser_t *parser, const char *text)
{
    const size_t n = strlen(text) + 1;
    char *copy = (char *)malloc(n);
    if (copy == NULL) {
        parse_error(parser, "out of memory", NULL);
        return NULL;
    }
    memcpy(copy, text, n);
    return copy;
}

/* `at <frame> <command>`, the frame already read. */
static void parse_command(parser_t *parser, long frame, char *at)
{
    char *word = next_word(&at);
    command_t *command;
    if (word == NULL) {
        parse_error(parser, "a frame with no command", NULL);
        return;
    }
    if (strcmp(word, "key") == 0) {
        char *how = next_word(&at), *name = next_word(&at);
        int code;
        if (how == NULL || name == NULL || (strcmp(how, "down") != 0 && strcmp(how, "up") != 0 &&
                                            strcmp(how, "tap") != 0)) {
            parse_error(parser, "key takes down, up, or tap, and a key", how);
            return;
        }
        if (!find_name(keys, sizeof(keys) / sizeof(keys[0]), name, &code)) {
            parse_error(parser, "no such key", name);
            return;
        }
        command = add(parser, frame, strcmp(how, "up") == 0 ? CMD_KEY_UP : CMD_KEY_DOWN);
        if (command != NULL) command->code = code;
        if (strcmp(how, "tap") == 0 && (command = add(parser, frame + 1, CMD_KEY_UP)) != NULL) command->code = code;
    } else if (strcmp(word, "text") == 0) {
        const char *text = rest_of(&at);
        if (text[0] == '\0') {
            parse_error(parser, "text with nothing to type", NULL);
            return;
        }
        if ((command = add(parser, frame, CMD_TEXT)) != NULL) command->text = copy_of(parser, text);
    } else if (strcmp(word, "mouse") == 0) {
        char *how = next_word(&at);
        if (how != NULL && (strcmp(how, "move") == 0 || strcmp(how, "scroll") == 0)) {
            double x, y;
            if (!parse_number(next_word(&at), &x) || !parse_number(next_word(&at), &y)) {
                parse_error(parser, "mouse move and scroll take two numbers", how);
                return;
            }
            command = add(parser, frame, strcmp(how, "move") == 0 ? CMD_MOUSE_MOVE : CMD_MOUSE_SCROLL);
            if (command != NULL) {
                command->x = (float)x;
                command->y = (float)y;
            }
        } else if (how != NULL && (strcmp(how, "down") == 0 || strcmp(how, "up") == 0 || strcmp(how, "click") == 0)) {
            char *name = next_word(&at);
            int code;
            if (name == NULL || !find_name(mouse_buttons, 3, name, &code)) {
                parse_error(parser, "no such mouse button", name);
                return;
            }
            command = add(parser, frame, strcmp(how, "up") == 0 ? CMD_MOUSE_UP : CMD_MOUSE_DOWN);
            if (command != NULL) command->code = code;
            if (strcmp(how, "click") == 0 && (command = add(parser, frame + 1, CMD_MOUSE_UP)) != NULL) {
                command->code = code;
            }
        } else {
            parse_error(parser, "mouse takes move, down, up, click, or scroll", how);
            return;
        }
    } else if (strcmp(word, "pad") == 0) {
        long pad;
        char *how;
        if (!parse_long(next_word(&at), 0, WGF_PLATFORM_PRIV_GAMEPADS - 1, &pad)) {
            parse_error(parser, "pad takes a pad from 0 to 3", NULL);
            return;
        }
        how = next_word(&at);
        if (how != NULL && (strcmp(how, "connect") == 0 || strcmp(how, "disconnect") == 0)) {
            command = add(parser, frame, strcmp(how, "connect") == 0 ? CMD_PAD_CONNECT : CMD_PAD_DISCONNECT);
            if (command != NULL) command->pad = (int)pad;
        } else if (how != NULL && (strcmp(how, "down") == 0 || strcmp(how, "up") == 0 || strcmp(how, "tap") == 0)) {
            char *name = next_word(&at);
            int code;
            if (name == NULL || !find_name(pad_buttons, sizeof(pad_buttons) / sizeof(pad_buttons[0]), name, &code)) {
                parse_error(parser, "no such pad button", name);
                return;
            }
            /* each command filled as it is added: the next add may move them */
            if ((command = add(parser, frame, strcmp(how, "up") == 0 ? CMD_PAD_UP : CMD_PAD_DOWN)) != NULL) {
                command->code = code;
                command->pad = (int)pad;
            }
            if (strcmp(how, "tap") == 0 && (command = add(parser, frame + 1, CMD_PAD_UP)) != NULL) {
                command->code = code;
                command->pad = (int)pad;
            }
        } else if (how != NULL && strcmp(how, "axis") == 0) {
            char *name = next_word(&at);
            int code;
            double value;
            if (name == NULL || !find_name(pad_axes, sizeof(pad_axes) / sizeof(pad_axes[0]), name, &code)) {
                parse_error(parser, "no such pad axis", name);
                return;
            }
            if (!parse_number(next_word(&at), &value) || value < -1.0 || value > 1.0) {
                parse_error(parser, "an axis takes a value from -1 to 1", name);
                return;
            }
            if ((command = add(parser, frame, CMD_PAD_AXIS)) != NULL) {
                command->code = code;
                command->pad = (int)pad;
                command->x = (float)value;
            }
        } else {
            parse_error(parser, "pad takes connect, disconnect, down, up, tap, or axis", how);
            return;
        }
    } else if (strcmp(word, "expect") == 0) {
        char *probe = next_word(&at), *op = next_word(&at);
        double value;
        int i;
        if (probe == NULL || op == NULL || !parse_number(next_word(&at), &value)) {
            parse_error(parser, "expect takes a probe, an operator, and a number", probe);
            return;
        }
        for (i = 0; i < 6 && strcmp(ops[i], op) != 0; i++) {
        }
        if (i == 6) {
            parse_error(parser, "no such operator (==, !=, <, <=, >, >=)", op);
            return;
        }
        command = add(parser, frame, CMD_EXPECT);
        if (command != NULL) {
            command->op = (op_t)i;
            command->value = value;
            command->text = copy_of(parser, probe);
        }
    } else if (strcmp(word, "log") == 0 || strcmp(word, "screenshot") == 0) {
        const char *text = rest_of(&at);
        if (text[0] == '\0') {
            parse_error(parser, "log and screenshot take a name or text", word);
            return;
        }
        command = add(parser, frame, word[0] == 'l' ? CMD_LOG : CMD_SCREENSHOT);
        if (command != NULL) command->text = copy_of(parser, text);
    } else if (strcmp(word, "end") == 0) {
        if (script.end_frame >= 0) {
            parse_error(parser, "a second end", NULL);
            return;
        }
        script.end_frame = frame;
        add(parser, frame, CMD_END);
    } else {
        parse_error(parser, "no such command", word);
        return;
    }
    if (!parser->failed && next_word(&at) != NULL) parse_error(parser, "more on the line than its command takes", NULL);
}

static int by_frame(const void *a, const void *b)
{
    const command_t *x = (const command_t *)a, *y = (const command_t *)b;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

static void clear(void)
{
    int i;
    for (i = 0; i < script.count; i++) free(script.commands[i].text);
    free(script.commands);
    memset(&script, 0, sizeof(script));
    script.end_frame = -1;
}

/* Parse `text` whole; false, with each bad line logged, when anything is wrong. */
static bool parse(const char *text)
{
    parser_t parser = {0, 0, false};
    const char *p = text;
    bool header = false, seeded = false;
    clear();
    while (*p != '\0') {
        char line[LINE_MAX_BYTES], *at, *word;
        size_t n = strcspn(p, "\n");
        char *hash;
        parser.line++;
        if (n >= sizeof(line)) {
            parse_error(&parser, "a line longer than 511 bytes", NULL);
            break;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p += n + (p[n] == '\n' ? 1 : 0);
        if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
        for (at = line; *at != '\0'; at++) {
            if ((unsigned char)*at < 0x20 && *at != '\t') {
                parse_error(&parser, "a control character", NULL);
                break;
            }
        }
        hash = strchr(line, '#');
        if (hash != NULL) *hash = '\0';
        at = line;
        word = next_word(&at);
        if (word == NULL) continue;
        if (!header) {
            const char *version = strcmp(word, "wgf-script") == 0 ? next_word(&at) : NULL;
            if (version == NULL || strcmp(version, "1") != 0 || next_word(&at) != NULL) {
                parse_error(&parser, "the first line is `wgf-script 1`", word);
                break;
            }
            header = true;
        } else if (strcmp(word, "seed") == 0) {
            long seed;
            if (seeded || script.count > 0 || !parse_long(next_word(&at), -2147483647L - 1, 2147483647L, &seed) ||
                next_word(&at) != NULL) {
                parse_error(&parser, "seed takes one whole number, once, before the commands", NULL);
                continue;
            }
            wgf_random_set_seed((int)seed);
            seeded = true;
        } else if (strcmp(word, "at") == 0) {
            long frame;
            if (!parse_long(next_word(&at), 0, FRAME_MAX, &frame)) {
                parse_error(&parser, "at takes a frame from 0", NULL);
                continue;
            }
            parse_command(&parser, frame, at);
        } else {
            parse_error(&parser, "a line is `seed`, `at`, or a comment", word);
        }
    }
    if (!header && !parser.failed) parse_error(&parser, "empty: the first line is `wgf-script 1`", NULL);
    if (parser.failed) {
        clear();
        return false;
    }
    if (script.count > 0) qsort(script.commands, (size_t)script.count, sizeof(command_t), by_frame);
    return true;
}

/* ---- playing ---------------------------------------------------------------- */

static void deliver(sapp_event *event)
{
    wgf_platform_priv_input_handle_event(event, wgf_platform_priv_get_dpi_scale());
}

static void pads_changed(int pad)
{
    int i;
    if (!script.pads_taken) { /* from the first, every pad is the script's */
        script.pads_taken = true;
        for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) wgf_platform_priv_gamepad_set_test_pad(i, &script.pads[i]);
    }
    wgf_platform_priv_gamepad_set_test_pad(pad, &script.pads[pad]);
}

/* The code point at `*p`, advanced past it; 0xFFFD for a byte that starts none. */
static uint32_t next_code_point(const unsigned char **p)
{
    const unsigned char *s = *p;
    uint32_t c = s[0];
    int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    int i;
    if (extra < 0) {
        *p = s + 1;
        return 0xFFFDu;
    }
    c &= extra == 0 ? 0x7Fu : (0x3Fu >> extra);
    for (i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *p = s + i;
            return 0xFFFDu;
        }
        c = (c << 6) | (s[i] & 0x3Fu);
    }
    *p = s + extra + 1;
    return c;
}

static void play_input(const command_t *command)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.mouse_x = script.mouse_x * wgf_platform_priv_get_dpi_scale();
    event.mouse_y = script.mouse_y * wgf_platform_priv_get_dpi_scale();
    switch (command->kind) {
        case CMD_KEY_DOWN:
        case CMD_KEY_UP:
            event.type = command->kind == CMD_KEY_DOWN ? SAPP_EVENTTYPE_KEY_DOWN : SAPP_EVENTTYPE_KEY_UP;
            event.key_code = (sapp_keycode)command->code;
            deliver(&event);
            break;
        case CMD_TEXT: {
            const unsigned char *p = (const unsigned char *)command->text;
            event.type = SAPP_EVENTTYPE_CHAR;
            while (*p != '\0') {
                event.char_code = next_code_point(&p);
                deliver(&event);
            }
            break;
        }
        case CMD_MOUSE_MOVE:
            event.type = SAPP_EVENTTYPE_MOUSE_MOVE;
            event.mouse_dx = (command->x - script.mouse_x) * wgf_platform_priv_get_dpi_scale();
            event.mouse_dy = (command->y - script.mouse_y) * wgf_platform_priv_get_dpi_scale();
            script.mouse_x = command->x;
            script.mouse_y = command->y;
            event.mouse_x = script.mouse_x * wgf_platform_priv_get_dpi_scale();
            event.mouse_y = script.mouse_y * wgf_platform_priv_get_dpi_scale();
            deliver(&event);
            break;
        case CMD_MOUSE_DOWN:
        case CMD_MOUSE_UP:
            event.type = command->kind == CMD_MOUSE_DOWN ? SAPP_EVENTTYPE_MOUSE_DOWN : SAPP_EVENTTYPE_MOUSE_UP;
            event.mouse_button = (sapp_mousebutton)command->code;
            deliver(&event);
            break;
        case CMD_MOUSE_SCROLL:
            event.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
            event.scroll_x = command->x;
            event.scroll_y = command->y;
            deliver(&event);
            break;
        case CMD_PAD_CONNECT:
        case CMD_PAD_DISCONNECT: {
            wgf_platform_priv_gamepad_t *pad = &script.pads[command->pad];
            memset(pad, 0, sizeof(*pad));
            pad->connected = command->kind == CMD_PAD_CONNECT;
            if (pad->connected) memcpy(pad->name, "wgf_script", sizeof("wgf_script"));
            pads_changed(command->pad);
            break;
        }
        case CMD_PAD_DOWN:
        case CMD_PAD_UP:
            script.pads[command->pad].buttons[command->code] = command->kind == CMD_PAD_DOWN;
            pads_changed(command->pad);
            break;
        case CMD_PAD_AXIS:
            script.pads[command->pad].axes[command->code] = command->x;
            pads_changed(command->pad);
            break;
        default:
            break;
    }
}

static bool holds(op_t op, double got, double want)
{
    switch (op) {
        case OP_EQ: return got == want;
        case OP_NE: return got != want;
        case OP_LT: return got < want;
        case OP_LE: return got <= want;
        case OP_GT: return got > want;
        default: return got >= want;
    }
}

static void check(const command_t *command)
{
    script.expectations++;
    if (!wgf_probe_has_value(command->text)) {
        wgf_log_error("wgf_script: FAIL at frame %ld (line %d): expect %s %s %g: %s was never set", command->frame,
                      command->line, command->text, ops[command->op], command->value, command->text);
        script.failures++;
    } else if (!holds(command->op, wgf_probe_get_value(command->text), command->value)) {
        wgf_log_error("wgf_script: FAIL at frame %ld (line %d): expect %s %s %g: %s is %g", command->frame,
                      command->line, command->text, ops[command->op], command->value, command->text,
                      wgf_probe_get_value(command->text));
        script.failures++;
    }
}

static void finish(long frames)
{
    script.ended = true;
    script.passed = script.failures == 0;
    if (script.passed) {
        wgf_log_info("wgf_script: PASS (%d expectation%s, %ld frames)", script.expectations,
                     script.expectations == 1 ? "" : "s", frames);
    } else {
        wgf_log_error("wgf_script: FAIL (%d of %d expectation%s failed, %ld frames)", script.failures,
                      script.expectations, script.expectations == 1 ? "" : "s", frames);
    }
}

bool wgf_app_priv_script_start_text(const char *text)
{
    if (text == NULL || !parse(text)) {
        wgf_log_error("wgf_script: the script can't be run (above); quitting");
        wgf_platform_priv_request_quit();
        return false;
    }
    script.running = true;
    wgf_log_info("wgf_script: %d command%s, %s", script.count, script.count == 1 ? "" : "s",
                 script.end_frame >= 0 ? "ending at its end" : "with no end: it runs until the program quits");
    return true;
}

bool wgf_app_priv_script_start(void)
{
    bool named = false;
    char *text = wgf_app_priv_script_read_source(&named);
    bool started = false;
    clear();
    if (text != NULL) {
        started = wgf_app_priv_script_start_text(text);
        free(text);
    } else if (named) {
        wgf_log_error("wgf_script: the script named couldn't be read; quitting");
        wgf_platform_priv_request_quit();
    }
    return started;
}

bool wgf_app_priv_script_is_running(void)
{
    return script.running;
}

void wgf_app_priv_script_begin_frame(long frame)
{
    int i;
    if (!script.running) return;
    for (i = script.next; i < script.count && script.commands[i].frame <= frame; i++) {
        command_t *command = &script.commands[i];
        if (!command->done && command->kind < CMD_EXPECT) {
            play_input(command);
            command->done = true;
        }
    }
}

void wgf_app_priv_script_end_frame(long frame)
{
    if (!script.running) return;
    for (; script.next < script.count && script.commands[script.next].frame <= frame; script.next++) {
        command_t *command = &script.commands[script.next];
        if (command->done) continue;
        command->done = true;
        switch (command->kind) {
            case CMD_EXPECT: check(command); break;
            case CMD_LOG: wgf_log_info("wgf_script: %s", command->text); break;
            case CMD_SCREENSHOT: wgf_log_info("wgf_script: SCREENSHOT %s", command->text); break;
            case CMD_END:
                finish(frame + 1);
                script.running = false;
                wgf_platform_priv_request_quit();
                return;
            default: break; /* inputs are played before the frame */
        }
    }
}

void wgf_app_priv_script_stop(long frames)
{
    if (script.running && script.end_frame >= 0) {
        wgf_log_error("wgf_script: FAIL: the program quit at frame %ld, before the script's end at frame %ld",
                      frames, script.end_frame);
        script.failures++;
        finish(frames);
    } else if (script.running) {
        finish(frames); /* no end: the program decided when */
    }
    script.running = false;
}

int wgf_app_priv_script_get_failures(void)
{
    return script.failures;
}

bool wgf_app_priv_script_has_passed(void)
{
    return script.ended && script.passed;
}
