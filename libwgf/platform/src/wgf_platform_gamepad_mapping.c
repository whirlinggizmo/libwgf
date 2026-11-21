#include "wgf_platform_gamepad_priv.h"

#include <stdlib.h>
#include <string.h>

#include "wgf_gamepad.h"
#include "wgf_log.h"

/* Pads' mappings (wgf_gamepad_add_mappings): SDL's game controller database's lines,
 * raylib's SetGamepadMappings, read into a table a platform that reads its pads raw (Linux's
 * evdev) asks by a pad's GUID, and applied to the raw buttons, axes, and hats it reports, in
 * SDL's order, so a line from SDL's community database (gamecontrollerdb.txt) maps a pad
 * here as SDL maps it. Read by hand: no regular expressions, one field at a time. */

#define BINDINGS 40 /* a line's bindings kept: SDL's 26 targets, some split by halves */

typedef enum source_kind_t { SOURCE_NONE = 0, SOURCE_BUTTON, SOURCE_AXIS, SOURCE_HAT } source_kind_t;

/* A target: one of libwgf's buttons, or (TARGET_AXIS + n) its axis n. */
#define TARGET_AXIS 100

typedef struct binding_t {
    int target;
    int output_half; /* an axis target's: 0 whole, +1 or -1 only that half */
    source_kind_t kind;
    int index;      /* the raw button's, axis's, or hat's */
    int hat_mask;   /* a hat's: 1 up, 2 right, 4 down, 8 left */
    int input_half; /* an axis source's: 0 whole, +1 or -1 only that half */
    bool invert;    /* an axis source's `~` */
} binding_t;

typedef struct mapping_t {
    unsigned char guid[16];
    binding_t bindings[BINDINGS];
    int count;
} mapping_t;

static mapping_t *mappings;
static int mapping_count, mapping_capacity;
static unsigned generation; /* changed whenever the table is: a platform looks its pads up again */

static const struct {
    const char *name;
    int target;
} targets[] = {
    {"a", WGF_GAMEPAD_BUTTON_SOUTH},
    {"b", WGF_GAMEPAD_BUTTON_EAST},
    {"x", WGF_GAMEPAD_BUTTON_WEST},
    {"y", WGF_GAMEPAD_BUTTON_NORTH},
    {"leftshoulder", WGF_GAMEPAD_BUTTON_LEFT_BUMPER},
    {"rightshoulder", WGF_GAMEPAD_BUTTON_RIGHT_BUMPER},
    {"back", WGF_GAMEPAD_BUTTON_BACK},
    {"start", WGF_GAMEPAD_BUTTON_START},
    {"guide", WGF_GAMEPAD_BUTTON_GUIDE},
    {"leftstick", WGF_GAMEPAD_BUTTON_LEFT_STICK},
    {"rightstick", WGF_GAMEPAD_BUTTON_RIGHT_STICK},
    {"dpup", WGF_GAMEPAD_BUTTON_DPAD_UP},
    {"dpdown", WGF_GAMEPAD_BUTTON_DPAD_DOWN},
    {"dpleft", WGF_GAMEPAD_BUTTON_DPAD_LEFT},
    {"dpright", WGF_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"leftx", TARGET_AXIS + WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X},
    {"lefty", TARGET_AXIS + WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y},
    {"rightx", TARGET_AXIS + WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X},
    {"righty", TARGET_AXIS + WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y},
    {"lefttrigger", TARGET_AXIS + WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER},
    {"righttrigger", TARGET_AXIS + WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER},
};

/* ---------------------------------------------------------------- reading ---- */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 32 hex digits, `length` of them, into 16 bytes. */
static bool read_guid(const char *text, size_t length, unsigned char guid[16])
{
    int i;
    if (length != 32) return false;
    for (i = 0; i < 16; i++) {
        const int high = hex_digit(text[i * 2]), low = hex_digit(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        guid[i] = (unsigned char)(high * 16 + low);
    }
    return true;
}

/* A whole number of digits, from `*at` before `end`; false for none. */
static bool read_number(const char **at, const char *end, int *out)
{
    int value = 0, digits = 0;
    while (*at < end && **at >= '0' && **at <= '9' && digits < 4) {
        value = value * 10 + (**at - '0');
        (*at)++;
        digits++;
    }
    *out = value;
    return digits > 0;
}

static bool equals(const char *text, size_t length, const char *word)
{
    return strlen(word) == length && memcmp(text, word, length) == 0;
}

/* One `target:source` field into `binding`; false for a field that isn't one (`platform:`, a
 * target libwgf has no place for, as misc1 and the paddles, or a source it can't read). */
static bool read_binding(const char *field, size_t length, binding_t *binding)
{
    const char *colon = memchr(field, ':', length), *at, *end = field + length;
    size_t name_length;
    int output_half = 0, i, target = -1;
    if (colon == NULL) return false;
    at = field;
    if (*at == '+' || *at == '-') output_half = *at++ == '+' ? 1 : -1;
    name_length = (size_t)(colon - at);
    for (i = 0; i < (int)(sizeof(targets) / sizeof(targets[0])); i++) {
        if (equals(at, name_length, targets[i].name)) target = targets[i].target;
    }
    if (target < 0) return false;
    memset(binding, 0, sizeof(*binding));
    binding->target = target;
    binding->output_half = target >= TARGET_AXIS ? output_half : 0;
    at = colon + 1;
    if (at < end && (*at == '+' || *at == '-')) binding->input_half = *at++ == '+' ? 1 : -1;
    if (at >= end) return false;
    if (*at == 'b') {
        at++;
        binding->kind = SOURCE_BUTTON;
        if (!read_number(&at, end, &binding->index)) return false;
    } else if (*at == 'a') {
        at++;
        binding->kind = SOURCE_AXIS;
        if (!read_number(&at, end, &binding->index)) return false;
        if (at < end && *at == '~') {
            binding->invert = true;
            at++;
        }
    } else if (*at == 'h') {
        at++;
        binding->kind = SOURCE_HAT;
        if (!read_number(&at, end, &binding->index) || at >= end || *at++ != '.' ||
            !read_number(&at, end, &binding->hat_mask)) {
            return false;
        }
    } else {
        return false;
    }
    return at == end;
}

/* The platform's name in SDL's lines. */
static const char *platform_name(void)
{
#if defined(__EMSCRIPTEN__)
    return "Web";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Mac OS X";
#else
    return "Linux";
#endif
}

static mapping_t *find_exact(const unsigned char guid[16])
{
    int i;
    for (i = 0; i < mapping_count; i++) {
        if (memcmp(mappings[i].guid, guid, 16) == 0) return &mappings[i];
    }
    return NULL;
}

/* One line, `length` long: kept and true when it is a mapping for this platform. */
static bool read_line(const char *line, size_t length, int number)
{
    const char *end = line + length, *at = line, *comma;
    mapping_t mapping, *kept;
    bool platform_seen = false, platform_ours = false;
    memset(&mapping, 0, sizeof(mapping));
    comma = memchr(at, ',', length);
    if (comma == NULL || !read_guid(at, (size_t)(comma - at), mapping.guid)) {
        wgf_log_warn("wgf_gamepad_add_mappings: line %d: no GUID first, 32 hex digits: skipped", number);
        return false;
    }
    at = comma + 1;
    comma = memchr(at, ',', (size_t)(end - at)); /* the name: not kept, a pad's own is shown */
    if (comma == NULL) {
        wgf_log_warn("wgf_gamepad_add_mappings: line %d: no name after its GUID: skipped", number);
        return false;
    }
    at = comma + 1;
    while (at < end) {
        const char *next = memchr(at, ',', (size_t)(end - at));
        const size_t field = (size_t)((next != NULL ? next : end) - at);
        if (field > 9 && memcmp(at, "platform:", 9) == 0) {
            platform_seen = true;
            platform_ours = equals(at + 9, field - 9, platform_name());
        } else if (field > 0 && mapping.count < BINDINGS) {
            if (read_binding(at, field, &mapping.bindings[mapping.count])) mapping.count++;
        }
        at = next != NULL ? next + 1 : end;
    }
    if (platform_seen && !platform_ours) return false;
    mapping.guid[2] = mapping.guid[3] = 0; /* SDL's name check, which pads here don't have */
    kept = find_exact(mapping.guid);
    if (kept == NULL) {
        if (mapping_count == mapping_capacity) {
            const int capacity = mapping_capacity > 0 ? mapping_capacity * 2 : 32;
            mapping_t *grown = realloc(mappings, sizeof(mapping_t) * (size_t)capacity);
            if (grown == NULL) {
                wgf_log_error("wgf_gamepad_add_mappings: no memory for another mapping");
                return false;
            }
            mappings = grown;
            mapping_capacity = capacity;
        }
        kept = &mappings[mapping_count++];
    }
    *kept = mapping; /* the same GUID again: replaced */
    return true;
}

int wgf_gamepad_add_mappings(const char *text)
{
    const char *at = text;
    int added = 0, number = 0;
    if (text == NULL) return 0;
    while (*at != '\0') {
        const char *end = at;
        size_t length;
        while (*end != '\0' && *end != '\n') end++;
        length = (size_t)(end - at);
        if (length > 0 && at[length - 1] == '\r') length--;
        number++;
        while (length > 0 && (*at == ' ' || *at == '\t')) {
            at++;
            length--;
        }
        if (length > 0 && *at != '#' && read_line(at, length, number)) added++;
        at = *end == '\n' ? end + 1 : end;
    }
    if (added > 0) generation++;
    return added;
}

/* ------------------------------------------------------- the platforms' side ---- */

unsigned wgf_platform_priv_gamepad_mapping_generation(void)
{
    return generation;
}

const wgf_platform_priv_gamepad_mapping_t *wgf_platform_priv_gamepad_find_mapping(const unsigned char guid[16])
{
    unsigned char key[16];
    const mapping_t *found;
    int i;
    memcpy(key, guid, 16);
    key[2] = key[3] = 0;
    found = find_exact(key);
    if (found != NULL) return (const wgf_platform_priv_gamepad_mapping_t *)found;
    key[12] = key[13] = 0; /* SDL's fall back: a line written without the pad's version */
    for (i = 0; i < mapping_count; i++) {
        unsigned char theirs[16];
        memcpy(theirs, mappings[i].guid, 16);
        theirs[12] = theirs[13] = 0;
        if (memcmp(theirs, key, 16) == 0 && mappings[i].guid[12] == 0 && mappings[i].guid[13] == 0) {
            return (const wgf_platform_priv_gamepad_mapping_t *)&mappings[i];
        }
    }
    return NULL;
}

/* A source's value, 0 to 1 for a button or hat, -1 to 1 for an axis (its half, 0 to 1). */
static float source_value(const binding_t *b, const wgf_platform_priv_gamepad_raw_t *raw)
{
    float v;
    switch (b->kind) {
        case SOURCE_BUTTON: return b->index < raw->button_count && raw->buttons[b->index] ? 1.0f : 0.0f;
        case SOURCE_HAT: return b->index < raw->hat_count && (raw->hats[b->index] & b->hat_mask) != 0 ? 1.0f : 0.0f;
        case SOURCE_AXIS:
            if (b->index >= raw->axis_count) return 0.0f;
            v = raw->axes[b->index];
            if (b->invert) v = -v;
            if (b->input_half > 0) return v > 0.0f ? v : 0.0f;
            if (b->input_half < 0) return v < 0.0f ? -v : 0.0f;
            return v;
        default: return 0.0f;
    }
}

void wgf_platform_priv_gamepad_apply_mapping(const wgf_platform_priv_gamepad_mapping_t *map,
                                             const wgf_platform_priv_gamepad_raw_t *raw, wgf_platform_priv_gamepad_t *pad)
{
    const mapping_t *mapping = (const mapping_t *)map;
    int i;
    memset(pad->buttons, 0, sizeof(pad->buttons));
    memset(pad->axes, 0, sizeof(pad->axes));
    for (i = 0; i < mapping->count; i++) {
        const binding_t *b = &mapping->bindings[i];
        const float v = source_value(b, raw);
        const bool whole = b->kind == SOURCE_AXIS && b->input_half == 0; /* -1 to 1; the rest 0 to 1 */
        const float amount = whole ? (v + 1.0f) * 0.5f : v;              /* how far, 0 to 1 */
        int axis;
        if (b->target < TARGET_AXIS) { /* a button: held past halfway */
            pad->buttons[b->target] |= (whole ? v : amount) > 0.5f;
            continue;
        }
        axis = b->target - TARGET_AXIS;
        if (b->output_half != 0) {
            pad->axes[axis] += b->output_half > 0 ? amount : -amount; /* one half of a stick's axis */
        } else if (axis >= WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER) {
            pad->axes[axis] += amount; /* a trigger: released at an axis's bottom, pulled at its top */
        } else {
            pad->axes[axis] += whole ? v : amount * 2.0f - 1.0f; /* a half axis stretched over the whole */
        }
    }
}
