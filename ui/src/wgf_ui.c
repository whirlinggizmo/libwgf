#include "wgf_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "render/wgf_gfx_render_priv.h"
#include "text/wgf_gfx_font_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_draw.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_log.h"
#include "wgf_mouse.h"
#include "wgf_platform_priv.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_ui_priv.h"

/* The ui module: Clay lays out and hit-tests, this file describes each frame's UI to it,
 * answers buttons from the input, moves the focus, and draws Clay's commands through
 * gfx's immediate mode (wgf_draw.h). Clay's state is one context in one arena, made by
 * the first wgf_ui_begin and freed at gfx's stop. */

#define ID_MAX 64       /* an id's bytes, with its NUL */
#define DEPTH_MAX 64    /* boxes inside boxes */
#define ELEMENTS_MAX 2048
#define WORDS_MAX 8192  /* Clay's cache of measured words */
#define TEXT_BLOCK 16384

enum { KIND_ROOT, KIND_BOX, KIND_PANEL };

typedef struct level_t {
    int kind;
    bool configured; /* Clay told its declaration: no more changes to it */
    bool solid;      /* drawn: the pointer over it is the UI's */
    Clay_ElementDeclaration declaration;
} level_t;

/* The frame's copies of the text and ids given, which Clay points at until it draws:
 * blocks that never move, kept for the next frame and reused. */
typedef struct text_block_t {
    struct text_block_t *next;
    size_t size, used;
    char bytes[];
} text_block_t;

static struct {
    void *memory;
    bool started; /* the part installed, Clay's context made */
    bool begun;   /* between wgf_ui_begin and wgf_ui_end */
    bool ended;   /* wgf_ui_end ran this frame */
    bool captured; /* the UI set the captures, to let go of */
    level_t levels[DEPTH_MAX];
    int depth;
    text_block_t *blocks, *block;

    /* this frame's answers, from the input as the UI began */
    float pointer_x, pointer_y;
    bool pointer_down, pointer_pressed, pointer_released;
    bool activate, next, previous;

    char focus[ID_MAX];
    bool focus_visible; /* moved by keys or pads: drawn */
    bool focus_seen;    /* the focused button was given this frame */
    bool focus_fresh;   /* given by wgf_ui_set_focus since the last end */
    char pressed[ID_MAX]; /* the button a press of the pointer started on */

    char (*buttons)[ID_MAX]; /* this frame's, in order: the focus moves through them */
    int button_count, button_capacity;
    uint32_t *solids; /* this frame's drawn boxes, panels, and buttons, by Clay id */
    int solid_count, solid_capacity;
    char *scratch; /* text NUL-terminated for measuring and drawing */
    size_t scratch_size;

    wgf_color_t colors[7];
    float values[7];
    wgf_font_t font;
} ui;

static const wgf_color_t default_colors[7] = {
    0xE6E9F0FFu, /* text */
    0x1A1E28E6u, /* panel */
    0x2E3546FFu, /* button */
    0x3F4860FFu, /* hovered */
    0x232836FFu, /* pressed */
    0xF2F4F8FFu, /* button text */
    0xFFD040FFu, /* focus */
};
static const float default_values[7] = {20.0f, 24.0f, 12.0f, 8.0f, 24.0f, 200.0f, 3.0f};

/* ---- the style ---------------------------------------------------------------------- */

static void style_defaults(void)
{
    memcpy(ui.colors, default_colors, sizeof(ui.colors));
    memcpy(ui.values, default_values, sizeof(ui.values));
}

static bool style_set;

static void style_ready(void)
{
    if (!style_set) {
        style_defaults();
        style_set = true;
    }
}

bool wgf_ui_set_style_color(wgf_ui_color_t which, wgf_color_t color)
{
    style_ready();
    if ((int)which < 0 || (int)which >= 7) return false;
    ui.colors[which] = color;
    return true;
}

wgf_color_t wgf_ui_get_style_color(wgf_ui_color_t which)
{
    style_ready();
    return (int)which >= 0 && (int)which < 7 ? ui.colors[which] : 0;
}

bool wgf_ui_set_style_value(wgf_ui_value_t which, float value)
{
    style_ready();
    if ((int)which < 0 || (int)which >= 7 || !(value >= 0.0f) || !isfinite(value)) return false;
    ui.values[which] = value;
    return true;
}

float wgf_ui_get_style_value(wgf_ui_value_t which)
{
    style_ready();
    return (int)which >= 0 && (int)which < 7 ? ui.values[which] : 0.0f;
}

bool wgf_ui_set_style_font(wgf_font_t font)
{
    if (font != 0 && (WGF_CORE_PRIV_HANDLE_KIND(font) != WGF_CORE_PRIV_HANDLE_KIND_FONT ||
                      wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_NONE))
        return false;
    if (font != 0) wgf_gfx_priv_font_retain(font); /* before the release, in case it is the same */
    if (ui.font != 0) wgf_resource_release(ui.font);
    ui.font = font;
    return true;
}

wgf_font_t wgf_ui_get_style_font(void)
{
    return ui.font;
}

void wgf_ui_reset_style(void)
{
    style_defaults();
    style_set = true;
    wgf_ui_set_style_font(0);
}

/* ---- the focus ---------------------------------------------------------------------- */

bool wgf_ui_set_focus(const char *id)
{
    if (id == NULL) id = "";
    if (strlen(id) >= ID_MAX) return false;
    snprintf(ui.focus, sizeof(ui.focus), "%s", id);
    ui.focus_visible = id[0] != '\0';
    ui.focus_fresh = true;
    return true;
}

const char *wgf_ui_get_focus(void)
{
    return ui.focus;
}

/* ---- copies for Clay ---------------------------------------------------------------- */

/* `text`'s first `length` bytes kept until the next frame's begin, NUL after them. */
static const char *keep(const char *text, size_t length)
{
    text_block_t *block = ui.block;
    char *copy;
    while (block != NULL && block->size - block->used < length + 1) block = block->next;
    if (block == NULL) {
        const size_t size = length + 1 > TEXT_BLOCK ? length + 1 : TEXT_BLOCK;
        block = (text_block_t *)malloc(sizeof(text_block_t) + size);
        if (block == NULL) return NULL;
        block->size = size;
        block->used = 0;
        block->next = ui.blocks;
        ui.blocks = block;
    }
    ui.block = block;
    copy = block->bytes + block->used;
    memcpy(copy, text, length);
    copy[length] = '\0';
    block->used += length + 1;
    return copy;
}

static bool clay_string(const char *text, Clay_String *out)
{
    const size_t length = strlen(text);
    const char *copy = keep(text, length);
    if (copy == NULL) {
        wgf_log_error("wgf_ui: out of memory for the frame's text");
        return false;
    }
    out->isStaticallyAllocated = false;
    out->length = (int32_t)length;
    out->chars = copy;
    return true;
}

/* `slice` NUL-terminated, in the scratch buffer; NULL when out of memory. */
static const char *terminated(const char *chars, int32_t length)
{
    if (length < 0) length = 0;
    if ((size_t)length + 1 > ui.scratch_size) {
        const size_t size = (size_t)length + 1 > 256 ? (size_t)length + 1 : 256;
        char *grown = (char *)realloc(ui.scratch, size);
        if (grown == NULL) return NULL;
        ui.scratch = grown;
        ui.scratch_size = size;
    }
    memcpy(ui.scratch, chars, (size_t)length);
    ui.scratch[length] = '\0';
    return ui.scratch;
}

static bool add_button(const char *id)
{
    if (ui.button_count == ui.button_capacity) {
        const int capacity = ui.button_capacity > 0 ? ui.button_capacity * 2 : 16;
        char(*grown)[ID_MAX] = realloc(ui.buttons, sizeof(*grown) * (size_t)capacity);
        if (grown == NULL) return false;
        ui.buttons = grown;
        ui.button_capacity = capacity;
    }
    snprintf(ui.buttons[ui.button_count++], ID_MAX, "%s", id);
    return true;
}

static void add_solid(uint32_t id)
{
    if (ui.solid_count == ui.solid_capacity) {
        const int capacity = ui.solid_capacity > 0 ? ui.solid_capacity * 2 : 32;
        uint32_t *grown = (uint32_t *)realloc(ui.solids, sizeof(uint32_t) * (size_t)capacity);
        if (grown == NULL) return;
        ui.solids = grown;
        ui.solid_capacity = capacity;
    }
    ui.solids[ui.solid_count++] = id;
}

/* ---- Clay's hooks ------------------------------------------------------------------- */

static Clay_Dimensions measure(Clay_StringSlice text, Clay_TextElementConfig *config, void *user)
{
    const char *line = terminated(text.chars, text.length);
    wgf_vec2_t size;
    Clay_Dimensions out = {0.0f, 0.0f};
    (void)user;
    if (line == NULL) return out;
    size = wgf_font_measure(ui.font, line, (float)config->fontSize);
    out.width = size.x;
    out.height = size.y;
    return out;
}

static void on_error(Clay_ErrorData error)
{
    wgf_log_error("wgf_ui: %.*s", (int)error.errorText.length, error.errorText.chars);
}

/* ---- the part ----------------------------------------------------------------------- */

static void let_go(void)
{
    if (ui.captured) {
        wgf_input_set_pointer_captured(false);
        wgf_input_set_keyboard_captured(false);
        ui.captured = false;
    }
}

/* After each frame: a frame that described no UI lets the pointer and keyboard go, and
 * one that began without ending is told so. */
static void end_frame(void)
{
    if (ui.begun) {
        wgf_log_error("wgf_ui: wgf_ui_begin without wgf_ui_end in a frame: the UI wasn't drawn");
        ui.begun = false;
        ui.depth = 0;
    }
    if (!ui.ended) let_go();
    ui.ended = false;
}

static void stop(void)
{
    text_block_t *block = ui.blocks;
    let_go();
    while (block != NULL) {
        text_block_t *next = block->next;
        free(block);
        block = next;
    }
    if (ui.font != 0) wgf_resource_release(ui.font);
    free(ui.memory);
    free(ui.buttons);
    free(ui.solids);
    free(ui.scratch);
    memset(&ui, 0, sizeof(ui));
    style_set = false;
}

static wgf_core_priv_part_t part = {.name = "ui",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_UI,
                                    .end_frame = end_frame,
                                    .stop = stop};

static bool start(void)
{
    uint32_t size;
    Clay_ErrorHandler handler;
    Clay_Dimensions dimensions = {1.0f, 1.0f};
    if (ui.started) return true;
    style_ready();
    Clay_SetMaxElementCount(ELEMENTS_MAX);
    Clay_SetMaxMeasureTextCacheWordCount(WORDS_MAX);
    size = Clay_MinMemorySize();
    ui.memory = malloc(size);
    if (ui.memory == NULL) {
        wgf_log_error("wgf_ui: out of memory starting (%u bytes for layout)", (unsigned)size);
        return false;
    }
    handler.errorHandlerFunction = on_error;
    handler.userData = NULL;
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(size, ui.memory), dimensions, handler);
    Clay_SetMeasureTextFunction(measure, NULL);
    ui.started = true;
    wgf_core_priv_part_install(&part);
    return true;
}

/* ---- describing ------------------------------------------------------------------- */

static Clay_Color clay_color(wgf_color_t color)
{
    Clay_Color out;
    out.r = (float)wgf_color_get_red(color);
    out.g = (float)wgf_color_get_green(color);
    out.b = (float)wgf_color_get_blue(color);
    out.a = (float)wgf_color_get_alpha(color);
    return out;
}

static wgf_color_t from_clay(Clay_Color color)
{
    return wgf_color_make((int)color.r, (int)color.g, (int)color.b, (int)color.a);
}

static uint16_t pixels(float value)
{
    return value <= 0.0f ? 0 : value >= 65535.0f ? 65535 : (uint16_t)(value + 0.5f);
}

static Clay_CornerRadius corners(float radius)
{
    Clay_CornerRadius out;
    out.topLeft = out.topRight = out.bottomLeft = out.bottomRight = radius;
    return out;
}

/* The open level's declaration told to Clay, before a child or its close. */
static void configure(void)
{
    level_t *level = &ui.levels[ui.depth - 1];
    if (level->configured) return;
    level->configured = true;
    Clay__ConfigureOpenElement(level->declaration);
    if (level->solid) add_solid(Clay_GetOpenElementId());
}

/* A child may go in the open level: false (logged) with no UI begun. */
static bool child(const char *what)
{
    if (!ui.begun) {
        wgf_log_error("wgf_ui: %s outside wgf_ui_begin and wgf_ui_end", what);
        return false;
    }
    configure();
    return true;
}

static bool open_level(int kind, const char *id, wgf_ui_direction_t direction)
{
    level_t *level;
    if (!child(kind == KIND_PANEL ? "wgf_ui_begin_panel" : "wgf_ui_begin_box")) return false;
    if ((int)direction < 0 || (int)direction > 1) return false;
    if (ui.depth == DEPTH_MAX) {
        wgf_log_error("wgf_ui: more than %d boxes inside each other", DEPTH_MAX);
        return false;
    }
    if (id != NULL && id[0] != '\0') {
        Clay_String name;
        if (!clay_string(id, &name)) return false;
        Clay__OpenElementWithId(Clay_GetElementId(name));
    } else {
        Clay__OpenElement();
    }
    level = &ui.levels[ui.depth++];
    memset(level, 0, sizeof(*level));
    level->kind = kind;
    level->declaration.layout.layoutDirection = direction == WGF_UI_DIRECTION_ROW ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM;
    if (kind == KIND_PANEL) {
        const uint16_t padding = pixels(ui.values[WGF_UI_VALUE_PADDING]);
        level->declaration.layout.padding.left = level->declaration.layout.padding.right = padding;
        level->declaration.layout.padding.top = level->declaration.layout.padding.bottom = padding;
        level->declaration.layout.childGap = pixels(ui.values[WGF_UI_VALUE_GAP]);
        level->declaration.backgroundColor = clay_color(ui.colors[WGF_UI_COLOR_PANEL]);
        level->declaration.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
        level->solid = true;
    }
    return true;
}

static bool close_level(int kind)
{
    if (!ui.begun || ui.depth <= 1 || ui.levels[ui.depth - 1].kind != kind) return false;
    configure();
    Clay__CloseElement();
    ui.depth--;
    return true;
}

bool wgf_ui_begin_box(const char *id, wgf_ui_direction_t direction)
{
    return open_level(KIND_BOX, id, direction);
}

bool wgf_ui_end_box(void)
{
    return close_level(KIND_BOX);
}

bool wgf_ui_begin_panel(const char *id)
{
    return open_level(KIND_PANEL, id, WGF_UI_DIRECTION_COLUMN);
}

bool wgf_ui_end_panel(void)
{
    return close_level(KIND_PANEL);
}

/* The open level, still to be told to Clay: one of the program's, not the screen. */
static level_t *settable(void)
{
    level_t *level;
    if (!ui.begun || ui.depth <= 1) return NULL;
    level = &ui.levels[ui.depth - 1];
    return level->configured ? NULL : level;
}

static bool sizing(Clay_SizingAxis *axis, wgf_ui_sizing_t how, float value)
{
    if (!(value >= 0.0f) || !isfinite(value)) return false;
    memset(axis, 0, sizeof(*axis));
    switch (how) {
    case WGF_UI_SIZING_FIT: axis->type = CLAY__SIZING_TYPE_FIT; break;
    case WGF_UI_SIZING_GROW: axis->type = CLAY__SIZING_TYPE_GROW; break;
    case WGF_UI_SIZING_FIXED:
        axis->type = CLAY__SIZING_TYPE_FIXED;
        axis->size.minMax.min = axis->size.minMax.max = value;
        break;
    case WGF_UI_SIZING_PERCENT:
        if (value > 1.0f) return false;
        axis->type = CLAY__SIZING_TYPE_PERCENT;
        axis->size.percent = value;
        break;
    default: return false;
    }
    return true;
}

bool wgf_ui_set_width(wgf_ui_sizing_t how, float value)
{
    level_t *level = settable();
    Clay_SizingAxis axis;
    if (level == NULL || !sizing(&axis, how, value)) return false;
    level->declaration.layout.sizing.width = axis;
    return true;
}

bool wgf_ui_set_height(wgf_ui_sizing_t how, float value)
{
    level_t *level = settable();
    Clay_SizingAxis axis;
    if (level == NULL || !sizing(&axis, how, value)) return false;
    level->declaration.layout.sizing.height = axis;
    return true;
}

bool wgf_ui_set_padding(float x, float y)
{
    level_t *level = settable();
    if (level == NULL || !(x >= 0.0f) || !(y >= 0.0f) || !isfinite(x) || !isfinite(y)) return false;
    level->declaration.layout.padding.left = level->declaration.layout.padding.right = pixels(x);
    level->declaration.layout.padding.top = level->declaration.layout.padding.bottom = pixels(y);
    return true;
}

bool wgf_ui_set_gap(float gap)
{
    level_t *level = settable();
    if (level == NULL || !(gap >= 0.0f) || !isfinite(gap)) return false;
    level->declaration.layout.childGap = pixels(gap);
    return true;
}

bool wgf_ui_set_align(wgf_ui_align_t x, wgf_ui_align_t y)
{
    static const Clay_LayoutAlignmentX xs[3] = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_X_CENTER, CLAY_ALIGN_X_RIGHT};
    static const Clay_LayoutAlignmentY ys[3] = {CLAY_ALIGN_Y_TOP, CLAY_ALIGN_Y_CENTER, CLAY_ALIGN_Y_BOTTOM};
    level_t *level = settable();
    if (level == NULL || (int)x < 0 || (int)x > 2 || (int)y < 0 || (int)y > 2) return false;
    level->declaration.layout.childAlignment.x = xs[x];
    level->declaration.layout.childAlignment.y = ys[y];
    return true;
}

bool wgf_ui_set_color(wgf_color_t color)
{
    level_t *level = settable();
    if (level == NULL) return false;
    level->declaration.backgroundColor = clay_color(color);
    level->solid = level->kind == KIND_PANEL || wgf_color_get_alpha(color) > 0;
    return true;
}

static bool text_element(const char *text, float size, wgf_color_t color, Clay_TextElementConfigWrapMode wrap)
{
    Clay_String string;
    Clay_TextElementConfig config;
    if (!clay_string(text, &string)) return false;
    memset(&config, 0, sizeof(config));
    config.textColor = clay_color(color);
    config.fontSize = pixels(size > 0.0f ? size : ui.values[WGF_UI_VALUE_TEXT_SIZE]);
    config.wrapMode = wrap;
    Clay__OpenTextElement(string, config);
    return true;
}

bool wgf_ui_label(const char *text, float size)
{
    if (text == NULL || text[0] == '\0' || !child("wgf_ui_label")) return false;
    return text_element(text, size, ui.colors[WGF_UI_COLOR_TEXT], CLAY_TEXT_WRAP_NEWLINES);
}

bool wgf_ui_spacer(float size)
{
    Clay_ElementDeclaration declaration;
    const bool row = ui.depth > 0 && ui.levels[ui.depth - 1].declaration.layout.layoutDirection == CLAY_LEFT_TO_RIGHT;
    Clay_SizingAxis along;
    if (!(size >= 0.0f) || !isfinite(size) || !child("wgf_ui_spacer")) return false;
    sizing(&along, size > 0.0f ? WGF_UI_SIZING_FIXED : WGF_UI_SIZING_GROW, size);
    memset(&declaration, 0, sizeof(declaration));
    if (row)
        declaration.layout.sizing.width = along;
    else
        declaration.layout.sizing.height = along;
    Clay__OpenElement();
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
    return true;
}

bool wgf_ui_button(const char *id, const char *text)
{
    Clay_String name;
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    bool over, activated = false, held, focused;
    const float padding = ui.values[WGF_UI_VALUE_BUTTON_PADDING];
    if (id == NULL || id[0] == '\0' || strlen(id) >= ID_MAX || !child("wgf_ui_button")) return false;
    if (!clay_string(id, &name)) return false;
    element = Clay_GetElementId(name);
    over = Clay_PointerOver(element); /* where it was in the frame before */
    if (ui.pointer_pressed && over) {
        snprintf(ui.pressed, sizeof(ui.pressed), "%s", id);
        snprintf(ui.focus, sizeof(ui.focus), "%s", id);
        ui.focus_visible = false;
    }
    if (ui.pointer_released && over && strcmp(ui.pressed, id) == 0) activated = true;
    focused = strcmp(ui.focus, id) == 0;
    if (focused) {
        ui.focus_seen = true;
        if (ui.activate) {
            activated = true;
            ui.focus_visible = true;
        }
    }
    held = ui.pointer_down && over && strcmp(ui.pressed, id) == 0;
    memset(&declaration, 0, sizeof(declaration));
    declaration.layout.sizing.width.type = CLAY__SIZING_TYPE_FIT;
    declaration.layout.sizing.width.size.minMax.min = ui.values[WGF_UI_VALUE_BUTTON_WIDTH];
    declaration.layout.padding.left = declaration.layout.padding.right = pixels(padding);
    declaration.layout.padding.top = declaration.layout.padding.bottom = pixels(padding * 0.5f);
    declaration.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    declaration.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    declaration.backgroundColor = clay_color(ui.colors[held   ? WGF_UI_COLOR_BUTTON_PRESSED
                                                       : over ? WGF_UI_COLOR_BUTTON_HOVERED
                                                              : WGF_UI_COLOR_BUTTON]);
    declaration.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    if (focused && ui.focus_visible) {
        const uint16_t width = pixels(ui.values[WGF_UI_VALUE_FOCUS_WIDTH]);
        declaration.border.color = clay_color(ui.colors[WGF_UI_COLOR_FOCUS]);
        declaration.border.width.left = declaration.border.width.right = width;
        declaration.border.width.top = declaration.border.width.bottom = width;
    }
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    add_solid(element.id);
    if (text != NULL && text[0] != '\0')
        text_element(text, 0.0f, ui.colors[WGF_UI_COLOR_BUTTON_TEXT], CLAY_TEXT_WRAP_NONE);
    Clay__CloseElement();
    if (!add_button(id)) wgf_log_error("wgf_ui: out of memory for the frame's buttons");
    return activated;
}

/* ---- the frame ---------------------------------------------------------------------- */

static bool any_pad(wgf_gamepad_button_t button)
{
    int pad;
    for (pad = 0; pad < 4; pad++) {
        if (wgf_gamepad_is_pressed(pad, button)) return true;
    }
    return false;
}

static void read_input(void)
{
    const wgf_vec2_t pointer = wgf_mouse_get_position();
    const bool shift = wgf_keyboard_is_down(WGF_KEY_LEFT_SHIFT) || wgf_keyboard_is_down(WGF_KEY_RIGHT_SHIFT);
    const bool tab = wgf_keyboard_is_pressed(WGF_KEY_TAB);
    ui.pointer_x = pointer.x;
    ui.pointer_y = pointer.y;
    ui.pointer_down = wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT);
    ui.pointer_pressed = wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT);
    ui.pointer_released = wgf_mouse_is_released(WGF_MOUSE_BUTTON_LEFT);
    ui.activate = wgf_keyboard_is_pressed(WGF_KEY_ENTER) || wgf_keyboard_is_pressed(WGF_KEY_KP_ENTER) ||
                  wgf_keyboard_is_pressed(WGF_KEY_SPACE) || any_pad(WGF_GAMEPAD_BUTTON_SOUTH);
    ui.next = wgf_keyboard_is_pressed(WGF_KEY_DOWN) || wgf_keyboard_is_pressed(WGF_KEY_RIGHT) || (tab && !shift) ||
              any_pad(WGF_GAMEPAD_BUTTON_DPAD_DOWN) || any_pad(WGF_GAMEPAD_BUTTON_DPAD_RIGHT);
    ui.previous = wgf_keyboard_is_pressed(WGF_KEY_UP) || wgf_keyboard_is_pressed(WGF_KEY_LEFT) || (tab && shift) ||
                  any_pad(WGF_GAMEPAD_BUTTON_DPAD_UP) || any_pad(WGF_GAMEPAD_BUTTON_DPAD_LEFT);
}

bool wgf_ui_begin(void)
{
    level_t *root;
    Clay_Dimensions dimensions;
    Clay_Vector2 pointer;
    const float scale = wgf_render_get_dpi_scale();
    if (ui.begun) {
        wgf_log_error("wgf_ui_begin: a UI is already begun");
        return false;
    }
    if (!wgf_gfx_priv_is_in_frame()) {
        wgf_log_error("wgf_ui_begin: outside a frame (call it in the frame callback)");
        return false;
    }
    if (!start()) return false;
    for (ui.block = ui.blocks; ui.block != NULL; ui.block = ui.block->next) ui.block->used = 0;
    ui.block = ui.blocks;
    ui.button_count = ui.solid_count = 0;
    ui.focus_seen = false;
    read_input();
    dimensions.width = (float)wgf_render_get_width() / (scale > 0.0f ? scale : 1.0f);
    dimensions.height = (float)wgf_render_get_height() / (scale > 0.0f ? scale : 1.0f);
    Clay_SetLayoutDimensions(dimensions);
    pointer.x = ui.pointer_x;
    pointer.y = ui.pointer_y;
    Clay_SetPointerState(pointer, ui.pointer_down);
    Clay_BeginLayout();
    Clay__OpenElement();
    root = &ui.levels[0];
    memset(root, 0, sizeof(*root));
    root->kind = KIND_ROOT;
    root->declaration.layout.sizing.width.type = CLAY__SIZING_TYPE_GROW;
    root->declaration.layout.sizing.height.type = CLAY__SIZING_TYPE_GROW;
    root->declaration.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    root->declaration.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    root->declaration.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    root->declaration.layout.childGap = pixels(ui.values[WGF_UI_VALUE_GAP]);
    ui.depth = 1;
    ui.begun = true;
    return true;
}

/* The focus moved through this frame's buttons, wrapping; the first press puts it on
 * the first or the last. */
static void navigate(void)
{
    int i, at = -1;
    if (!ui.focus_seen && !ui.focus_fresh) ui.focus[0] = '\0';
    ui.focus_fresh = false;
    if (ui.next == ui.previous || ui.button_count == 0) return;
    for (i = 0; i < ui.button_count && at < 0; i++) {
        if (strcmp(ui.buttons[i], ui.focus) == 0) at = i;
    }
    if (at < 0)
        at = ui.next ? 0 : ui.button_count - 1;
    else
        at = (at + (ui.next ? 1 : ui.button_count - 1)) % ui.button_count;
    snprintf(ui.focus, sizeof(ui.focus), "%s", ui.buttons[at]);
    ui.focus_visible = true;
}

static void capture(void)
{
    bool pointer = ui.pressed[0] != '\0' && ui.pointer_down;
    int i;
    for (i = 0; i < ui.solid_count && !pointer; i++) {
        Clay_ElementId id;
        memset(&id, 0, sizeof(id));
        id.id = ui.solids[i];
        pointer = Clay_PointerOver(id);
    }
    if (!ui.pointer_down) ui.pressed[0] = '\0';
    wgf_input_set_pointer_captured(pointer);
    wgf_input_set_keyboard_captured(ui.focus[0] != '\0');
    ui.captured = true;
}

/* A rectangle with rounded corners as an outline's points, inset by `inset`, into `out`
 * (room for 4 * (SEGMENTS + 1) points); how many floats it filled. */
#define SEGMENTS 6
static int rounded(Clay_BoundingBox box, Clay_CornerRadius radius, float inset, float *out)
{
    const float x0 = box.x + inset, y0 = box.y + inset, x1 = box.x + box.width - inset, y1 = box.y + box.height - inset;
    const float most = fminf(x1 - x0, y1 - y0) * 0.5f;
    const float r[4] = {radius.topLeft, radius.topRight, radius.bottomRight, radius.bottomLeft};
    const float cx[4] = {x0, x1, x1, x0}, cy[4] = {y0, y0, y1, y1};
    const float start[4] = {3.14159265f, 4.71238898f, 0.0f, 1.57079633f};
    int corner, s, n = 0;
    for (corner = 0; corner < 4; corner++) {
        const float rr = fmaxf(0.0f, fminf(r[corner] - inset, most));
        const float ox = corner == 0 || corner == 3 ? rr : -rr, oy = corner < 2 ? rr : -rr;
        for (s = 0; s <= SEGMENTS; s++) {
            const float a = start[corner] + 1.57079633f * (float)s / (float)SEGMENTS;
            out[n++] = cx[corner] + ox + cosf(a) * rr;
            out[n++] = cy[corner] + oy + sinf(a) * rr;
            if (rr == 0.0f) break;
        }
    }
    return n;
}

static void draw(Clay_RenderCommandArray commands)
{
    float points[8 * (SEGMENTS + 1)];
    int i;
    for (i = 0; i < commands.length; i++) {
        const Clay_RenderCommand *command = Clay_RenderCommandArray_Get(&commands, i);
        const Clay_BoundingBox box = command->boundingBox;
        switch (command->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const Clay_RectangleRenderData *rect = &command->renderData.rectangle;
            wgf_draw_polygon(points, rounded(box, rect->cornerRadius, 0.0f, points), from_clay(rect->backgroundColor));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const Clay_BorderRenderData *border = &command->renderData.border;
            const float width = (float)border->width.left;
            if (width > 0.0f) {
                wgf_draw_polyline(points, rounded(box, border->cornerRadius, width * 0.5f, points), true, width,
                                  from_clay(border->color));
            }
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const Clay_TextRenderData *text = &command->renderData.text;
            const char *line = terminated(text->stringContents.chars, text->stringContents.length);
            if (line != NULL)
                wgf_draw_text(ui.font, line, box.x, box.y, (float)text->fontSize, from_clay(text->textColor));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: wgf_render_push_clip(box.x, box.y, box.width, box.height); break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: wgf_render_pop_clip(); break;
        default: break; /* images, overlays, custom: libwgf's UI makes none */
        }
    }
}

bool wgf_ui_end(void)
{
    bool balanced;
    if (!ui.begun) {
        wgf_log_error("wgf_ui_end: no UI begun");
        return false;
    }
    balanced = ui.depth == 1;
    if (!balanced) wgf_log_error("wgf_ui_end: %d box(es) or panel(s) left open, closed", ui.depth - 1);
    while (ui.depth > 0) {
        configure();
        Clay__CloseElement();
        ui.depth--;
    }
    ui.begun = false;
    ui.ended = true;
    draw(Clay_EndLayout((float)wgf_platform_priv_get_frame_duration()));
    navigate();
    capture();
    return balanced;
}

bool wgf_ui_priv_get_bounds(const char *id, float *x, float *y, float *width, float *height)
{
    Clay_String name;
    Clay_ElementData data;
    if (!ui.started || id == NULL || id[0] == '\0') return false;
    name.isStaticallyAllocated = false;
    name.length = (int32_t)strlen(id);
    name.chars = id;
    data = Clay_GetElementData(Clay_GetElementId(name));
    if (!data.found) return false;
    *x = data.boundingBox.x;
    *y = data.boundingBox.y;
    *width = data.boundingBox.width;
    *height = data.boundingBox.height;
    return true;
}
