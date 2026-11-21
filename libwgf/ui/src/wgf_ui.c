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
#include "wgf_core_resource_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_draw.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_log.h"
#include "wgf_probe.h"
#include "wgf_mouse.h"
#include "wgf_platform_priv.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_ui_priv.h"
#include "wgf_ui_scroll_priv.h"
#include "wgf_ui_skin_priv.h"

/* The ui module: Clay lays out and hit-tests, this file describes each frame's UI to it,
 * answers widgets from the input, moves the focus, and draws Clay's commands through
 * gfx's immediate mode (wgf_draw.h). Clay's state is one context in one arena, made by
 * the first wgf_ui_begin and freed at gfx's stop.
 *
 * A widget's look is a piece: a record of the frame's that its Clay element carries (as
 * its custom data), drawn by the function the widget gave it, so drawing calls only what
 * the program's widgets need. Its parts are drawn from their skins when the program set
 * any (wgf_ui_skin.c, reached through its hooks), else in the style's colors. */

#define ID_MAX 64       /* an id's bytes, with its NUL */
#define DEPTH_MAX 64    /* boxes inside boxes */
#define ELEMENTS_MAX 2048
#define WORDS_MAX 8192  /* Clay's cache of measured words */
#define TEXT_BLOCK 16384
#define STYLE_COLORS 13 /* wgf_ui_color_t's */
#define STYLE_VALUES 16 /* wgf_ui_value_t's */
#define WIDGETS 15      /* wgf_ui_widget_t's */
#define CHOICES_MAX 32  /* a tab row's, a choice's, a combo's labels */

enum { KIND_ROOT, KIND_BOX, KIND_PANEL, KIND_GROUP, KIND_GRID, KIND_SCROLL, KIND_MODAL };

/* The arrows a focused widget keeps for itself, not the focus's. */
enum { ARROWS_NONE, ARROWS_ACROSS, ARROWS_DOWN, ARROWS_ALL };

/* A widget's look this frame (see the top). widget is -1 for an image's, which no skin or
 * text offset reaches. */
typedef struct piece_t piece_t;
typedef void (*piece_draw_t)(const piece_t *piece, Clay_BoundingBox box);
struct piece_t {
    piece_draw_t draw;
    int widget, state;
    bool on;     /* a checkbox checked, a list's item chosen, a scroll bar down */
    float value; /* a progress bar's or slider's, 0 to 1; how far a scroll bar's thumb is along */
    float size;  /* a scroll bar's share in view, 0 to 1 */
    wgf_texture_t texture, pressed;
};

typedef struct focusable_t {
    char id[ID_MAX];
    uint32_t element; /* its Clay id: where it was laid out, for the arrows */
    int arrows;
} focusable_t;

typedef struct level_t level_t;
struct level_t {
    int kind;
    bool configured; /* Clay told its declaration: no more changes to it */
    bool solid;      /* drawn: the pointer over it is the UI's */
    Clay_ElementDeclaration declaration;
    void (*before_child)(level_t *level); /* a grid's: the row its next child goes in */
    int columns, count;                   /* a grid's columns, and its children so far */
    bool row_open;                        /* a grid's row is open in Clay */
    void (*close)(level_t *level);        /* a scroll area's or a modal's own close */
    Clay_ElementId window, bars[2];       /* a scroll area's window, and its bars across and down */
    int first_focusable;                  /* a scroll area's: the frame's first widget in it */
    bool shown[2];                        /* its bars shown */
    int bar_states[2];
    float thumbs[2][2]; /* each bar's share in view and how far its thumb is along, 0 to 1 */
    float offset[2];    /* how far it is scrolled, as it is laid out this frame */
};

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
    float origin_x, origin_y; /* the visible area's top left: the UI lays out from it (wgf_presentation.h) */
    float width, height;      /* the visible area's size: the layout's */
    float clock;              /* seconds of the frames the UI was begun in: a tooltip's wait */
    bool pointer_down, pointer_pressed, pointer_released;
    bool eaten; /* a press off an open dropdown, which closed it: no widget's, until let go */
    struct {
        bool activate, cancel, tab_next, tab_previous;
        bool up, down, left, right;                 /* pressed: the focus's */
        bool more_up, more_down, more_left, more_right; /* pressed or repeating: a slider's */
        bool page_up, page_down;                        /* pressed or repeating */
    } keys;
    bool disabled; /* the widgets given now are */
    bool hovering; /* the pointer is over a widget that answers it (an image button's art) */
    wgf_ui_priv_scroll_t scroll;
    float fade; /* 1 less the whole UI's alpha (wgf_ui_set_alpha): 0, drawn as given, until set */

    struct { /* the widget given last: a tooltip's */
        char id[ID_MAX];
        uint32_t element;
        bool over;
    } last;
    struct { /* the open dropdown's list */
        char owner[ID_MAX];   /* the dropdown's id; "" none open */
        uint32_t list, face;  /* its list's Clay id and its face's, as last laid out */
        int hot, item;        /* the item stepped to, and the one a press began on (-1 none) */
        float pointer[2];     /* where the pointer was: one that moves over an item steps to it */
        bool seen;            /* given this frame */
    } popup;
    struct {
        bool seen, was;        /* given this frame, and the frame before */
        int first, last;       /* this frame's widgets in it */
        char before[ID_MAX];   /* the focus as it opened, given back as it closes */
    } modal;
    struct {
        char id[ID_MAX];   /* the widget whose tooltip waits or shows */
        char next[ID_MAX]; /* the first other one pointed at this frame */
        float since;       /* the clock as it was first pointed at */
        bool seen;         /* pointed at this frame */
    } tip;

    char focus[ID_MAX];
    bool focus_visible; /* moved by keys or pads: drawn */
    bool focus_seen;    /* the focused button was given this frame */
    bool focus_fresh;   /* given by wgf_ui_set_focus since the last end */
    char pressed[ID_MAX]; /* the button a press of the pointer started on */

    focusable_t *focusables; /* this frame's, in order: the focus moves through them */
    int focusable_count, focusable_capacity;
    piece_t *pieces; /* this frame's looks */
    int piece_count, piece_capacity;
    uint32_t *solids; /* this frame's drawn boxes, panels, and buttons, by Clay id */
    int solid_count, solid_capacity;
    char *scratch; /* text NUL-terminated for measuring and drawing */
    size_t scratch_size;

    wgf_color_t colors[STYLE_COLORS];
    float values[STYLE_VALUES];
    wgf_font_t font;
    unsigned char fill[WIDGETS], from[WIDGETS]; /* a progress bar's and slider's wgf_ui_fill_t, wgf_ui_from_t */
} ui;

/* The skins' hooks, set by their first call (wgf_ui_skin_priv.h); NULL draws in colors. */
static const wgf_ui_priv_skins_t *skins;

void wgf_ui_priv_set_skins(const wgf_ui_priv_skins_t *hooks)
{
    skins = hooks;
}

static const wgf_color_t default_colors[STYLE_COLORS] = {
    0xE6E9F0FFu, /* text */
    0x1A1E28E6u, /* panel */
    0x2E3546FFu, /* button */
    0x3F4860FFu, /* hovered */
    0x232836FFu, /* pressed */
    0xF2F4F8FFu, /* button text */
    0xFFD040FFu, /* focus */
    0x2E3546FFu, /* bar: a button's color */
    0x5AA0E6FFu, /* bar fill */
    0x3F4860FFu, /* separator */
    0xF2F4F8FFu, /* handle */
    0x2A3040FAu, /* popup */
    0x00000099u, /* dim */
};
static const float default_values[STYLE_VALUES] = {20.0f, 24.0f, 12.0f, 8.0f, 24.0f, 200.0f, 3.0f,  300.0f,
                                                   12.0f, 24.0f, 200.0f, 8.0f, 20.0f, 2.0f, 96.0f,  10.0f};

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
    if ((int)which < 0 || (int)which >= STYLE_COLORS) return false;
    ui.colors[which] = color;
    return true;
}

wgf_color_t wgf_ui_get_style_color(wgf_ui_color_t which)
{
    style_ready();
    return (int)which >= 0 && (int)which < STYLE_COLORS ? ui.colors[which] : 0;
}

bool wgf_ui_set_style_value(wgf_ui_value_t which, float value)
{
    style_ready();
    if ((int)which < 0 || (int)which >= STYLE_VALUES || !(value >= 0.0f) || !isfinite(value)) return false;
    ui.values[which] = value;
    return true;
}

float wgf_ui_get_style_value(wgf_ui_value_t which)
{
    style_ready();
    return (int)which >= 0 && (int)which < STYLE_VALUES ? ui.values[which] : 0.0f;
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
    memset(ui.fill, 0, sizeof(ui.fill));
    memset(ui.from, 0, sizeof(ui.from));
    if (skins != NULL) skins->reset();
}

static bool fills(wgf_ui_widget_t widget)
{
    return widget == WGF_UI_WIDGET_PROGRESS || widget == WGF_UI_WIDGET_SLIDER;
}

bool wgf_ui_set_fill(wgf_ui_widget_t widget, wgf_ui_fill_t fill, wgf_ui_from_t from)
{
    if (!fills(widget) || (int)fill < 0 || (int)fill > 1 || (int)from < 0 || (int)from > 3) return false;
    ui.fill[widget] = (unsigned char)fill;
    ui.from[widget] = (unsigned char)from;
    return true;
}

wgf_ui_fill_t wgf_ui_get_fill(wgf_ui_widget_t widget)
{
    return fills(widget) ? (wgf_ui_fill_t)ui.fill[widget] : WGF_UI_FILL_STRETCHED;
}

wgf_ui_from_t wgf_ui_get_fill_from(wgf_ui_widget_t widget)
{
    return fills(widget) ? (wgf_ui_from_t)ui.from[widget] : WGF_UI_FROM_LEFT;
}

/* Whether the widget's track runs across (its fill from the left or right), else up and down. */
static bool across(int widget)
{
    return ui.from[widget] == WGF_UI_FROM_LEFT || ui.from[widget] == WGF_UI_FROM_RIGHT;
}

/* ---- the focus ---------------------------------------------------------------------- */

bool wgf_ui_set_focus(const char *id)
{
    if (id == NULL) id = "";
    if (strlen(id) >= ID_MAX) return false;
    snprintf(ui.focus, sizeof(ui.focus), "%s", id);
    ui.focus_visible = id[0] != '\0';
    ui.focus_fresh = true;
    ui.scroll.reveal_next = true;
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

static bool add_focusable(const char *id, uint32_t element, int arrows)
{
    if (ui.focusable_count == ui.focusable_capacity) {
        const int capacity = ui.focusable_capacity > 0 ? ui.focusable_capacity * 2 : 16;
        focusable_t *grown = (focusable_t *)realloc(ui.focusables, sizeof(focusable_t) * (size_t)capacity);
        if (grown == NULL) return false;
        ui.focusables = grown;
        ui.focusable_capacity = capacity;
    }
    snprintf(ui.focusables[ui.focusable_count].id, ID_MAX, "%s", id);
    ui.focusables[ui.focusable_count].element = element;
    ui.focusables[ui.focusable_count++].arrows = arrows;
    return true;
}

/* A piece for the frame, as the custom data its element carries (its index plus one: NULL,
 * drawing nothing, when out of memory). */
static void *add_piece(piece_draw_t draw, int widget, int state)
{
    piece_t *piece;
    if (ui.piece_count == ui.piece_capacity) {
        const int capacity = ui.piece_capacity > 0 ? ui.piece_capacity * 2 : 32;
        piece_t *grown = (piece_t *)realloc(ui.pieces, sizeof(piece_t) * (size_t)capacity);
        if (grown == NULL) return NULL;
        ui.pieces = grown;
        ui.piece_capacity = capacity;
    }
    piece = &ui.pieces[ui.piece_count++];
    memset(piece, 0, sizeof(*piece));
    piece->draw = draw;
    piece->widget = widget;
    piece->state = state;
    return (void *)(intptr_t)ui.piece_count;
}

static piece_t *piece_at(void *data)
{
    const intptr_t index = (intptr_t)data - 1;
    return index >= 0 && index < ui.piece_count ? &ui.pieces[index] : NULL;
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
    if (skins != NULL) skins->reset();
    free(ui.memory);
    free(ui.focusables);
    free(ui.pieces);
    free(ui.solids);
    free(ui.scroll.areas);
    free(ui.scroll.before);
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

/* A color drawn in `state`: a disabled widget's at half its alpha. */
static wgf_color_t dim(wgf_color_t color, int state)
{
    return state == WGF_UI_STATE_DISABLED ? wgf_color_with_alpha(color, wgf_color_get_alpha(color) / 2) : color;
}

/* A color drawn at the whole UI's alpha (wgf_ui_set_alpha). */
static wgf_color_t faded(wgf_color_t color)
{
    if (ui.fade <= 0.0f) return color;
    return wgf_color_with_alpha(color, (int)((float)wgf_color_get_alpha(color) * (1.0f - ui.fade) + 0.5f));
}

bool wgf_ui_set_alpha(float alpha)
{
    if (!(alpha >= 0.0f) || !(alpha <= 1.0f)) return false;
    ui.fade = 1.0f - alpha;
    return true;
}

float wgf_ui_get_alpha(void)
{
    return 1.0f - ui.fade;
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

/* A child may go in the open level: false (logged) with no UI begun. A grid puts it in
 * its row. */
static bool child(const char *what)
{
    level_t *level;
    if (!ui.begun) {
        wgf_log_error("wgf_ui: %s outside wgf_ui_begin and wgf_ui_end", what);
        return false;
    }
    configure();
    level = &ui.levels[ui.depth - 1];
    if (level->before_child != NULL) level->before_child(level);
    return true;
}

/* A level's close in Clay: a grid's open row first; a scroll area's its own. */
static void close_element(level_t *level)
{
    if (level->close != NULL) {
        level->close(level);
        return;
    }
    if (level->row_open) Clay__CloseElement();
    level->row_open = false;
    Clay__CloseElement();
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

static void fixed(Clay_ElementDeclaration *declaration, float width, float height)
{
    sizing(&declaration->layout.sizing.width, WGF_UI_SIZING_FIXED, width > 0.0f ? width : 0.0f);
    sizing(&declaration->layout.sizing.height, WGF_UI_SIZING_FIXED, height > 0.0f ? height : 0.0f);
}

static void draw_panel(const piece_t *piece, Clay_BoundingBox box);

/* A panel's layout (a group's, a modal's): padded, gapped, and rounded by the style, drawn
 * by `look` as `widget`; solid but for a group, an outline whose inside is the game's. */
static void panel_layout(level_t *level, int widget, piece_draw_t look)
{
    const uint16_t padding = pixels(ui.values[WGF_UI_VALUE_PADDING]);
    level->declaration.layout.padding.left = level->declaration.layout.padding.right = padding;
    level->declaration.layout.padding.top = level->declaration.layout.padding.bottom = padding;
    level->declaration.layout.childGap = pixels(ui.values[WGF_UI_VALUE_GAP]);
    level->declaration.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    level->declaration.custom.customData = add_piece(look, widget, WGF_UI_STATE_NORMAL);
    level->solid = widget != WGF_UI_WIDGET_GROUP;
}

/* A box, panel, or group opened; `look` draws a panel's or group's (given by the call that
 * opens one, so only a program with groups links a group's drawing). */
static bool open_level(int kind, const char *id, wgf_ui_direction_t direction, piece_draw_t look)
{
    level_t *level;
    static const char *const names[] = {"", "wgf_ui_begin_box", "wgf_ui_begin_panel", "wgf_ui_begin_group",
                                        "wgf_ui_begin_grid"};
    if (!child(names[kind])) return false;
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
    if (kind == KIND_PANEL || kind == KIND_GROUP)
        panel_layout(level, kind == KIND_PANEL ? WGF_UI_WIDGET_PANEL : WGF_UI_WIDGET_GROUP, look);
    return true;
}

static bool close_level(int kind)
{
    if (!ui.begun || ui.depth <= 1 || ui.levels[ui.depth - 1].kind != kind) return false;
    configure();
    close_element(&ui.levels[ui.depth - 1]);
    ui.depth--;
    return true;
}

bool wgf_ui_begin_box(const char *id, wgf_ui_direction_t direction)
{
    return open_level(KIND_BOX, id, direction, NULL);
}

bool wgf_ui_end_box(void)
{
    return close_level(KIND_BOX);
}

/* A grid's next child: a row of `columns` begun when the last is full. */
static void grid_row(level_t *grid)
{
    Clay_ElementDeclaration row;
    if (grid->row_open && grid->count % grid->columns == 0) {
        Clay__CloseElement();
        grid->row_open = false;
    }
    if (!grid->row_open) {
        memset(&row, 0, sizeof(row));
        row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        row.layout.childGap = grid->declaration.layout.childGap;
        Clay__OpenElement();
        Clay__ConfigureOpenElement(row);
        grid->row_open = true;
    }
    grid->count++;
}

bool wgf_ui_begin_grid(const char *id, int columns)
{
    level_t *grid;
    if (columns < 1 || !open_level(KIND_GRID, id, WGF_UI_DIRECTION_COLUMN, NULL)) return false;
    grid = &ui.levels[ui.depth - 1];
    grid->declaration.layout.childGap = pixels(ui.values[WGF_UI_VALUE_GAP]);
    grid->columns = columns;
    grid->before_child = grid_row;
    return true;
}

bool wgf_ui_end_grid(void)
{
    return close_level(KIND_GRID);
}

bool wgf_ui_begin_panel(const char *id)
{
    return open_level(KIND_PANEL, id, WGF_UI_DIRECTION_COLUMN, draw_panel);
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

bool wgf_ui_set_width(wgf_ui_sizing_t how, float value)
{
    level_t *level = settable();
    Clay_SizingAxis axis;
    if (level == NULL || level->kind == KIND_SCROLL || !sizing(&axis, how, value)) return false;
    level->declaration.layout.sizing.width = axis;
    return true;
}

bool wgf_ui_set_height(wgf_ui_sizing_t how, float value)
{
    level_t *level = settable();
    Clay_SizingAxis axis;
    if (level == NULL || level->kind == KIND_SCROLL || !sizing(&axis, how, value)) return false;
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

/* What Clay's cache of measured words knows the style's font by: its handle, and whether
 * it is READY (before, it measures as the built-in font), so a word measured in one font
 * is measured again in another. */
static uint16_t font_key(void)
{
    const bool ready = ui.font != 0 && wgf_resource_get_status(ui.font) == WGF_RESOURCE_STATUS_READY;
    return (uint16_t)(((ui.font ^ (ui.font >> 15)) & 0x7FFFu) | (ready ? 0x8000u : 0u));
}

/* Text, the first `length` bytes of `text`, `piece` (NULL for none) the widget whose text
 * offset moves it. */
static bool text_slice(const char *text, size_t length, float size, wgf_color_t color,
                       Clay_TextElementConfigWrapMode wrap, void *piece)
{
    Clay_String string;
    Clay_TextElementConfig config;
    string.isStaticallyAllocated = false;
    string.length = (int32_t)length;
    string.chars = keep(text, length);
    if (string.chars == NULL) {
        wgf_log_error("wgf_ui: out of memory for the frame's text");
        return false;
    }
    memset(&config, 0, sizeof(config));
    config.textColor = clay_color(color);
    config.fontSize = pixels(size > 0.0f ? size : ui.values[WGF_UI_VALUE_TEXT_SIZE]);
    config.wrapMode = wrap;
    config.userData = piece;
    config.fontId = font_key();
    Clay__OpenTextElement(string, config);
    return true;
}

static bool text_element(const char *text, float size, wgf_color_t color, Clay_TextElementConfigWrapMode wrap,
                         void *piece)
{
    return text_slice(text, strlen(text), size, color, wrap, piece);
}

bool wgf_ui_label(const char *text, float size)
{
    if (text == NULL || text[0] == '\0' || !child("wgf_ui_label")) return false;
    return text_element(text, size, ui.colors[WGF_UI_COLOR_TEXT], CLAY_TEXT_WRAP_NEWLINES, NULL);
}

static void draw_group(const piece_t *piece, Clay_BoundingBox box);

bool wgf_ui_begin_group(const char *id, const char *title)
{
    if (!open_level(KIND_GROUP, id, WGF_UI_DIRECTION_COLUMN, draw_group)) return false;
    if (title != NULL && title[0] != '\0') {
        configure();
        text_element(title, 0.0f, ui.colors[WGF_UI_COLOR_TEXT], CLAY_TEXT_WRAP_NEWLINES, NULL);
    }
    return true;
}

bool wgf_ui_end_group(void)
{
    return close_level(KIND_GROUP);
}

/* Whether the open level lays its children out in a row. */
static bool in_row(void)
{
    return ui.depth > 0 && ui.levels[ui.depth - 1].declaration.layout.layoutDirection == CLAY_LEFT_TO_RIGHT;
}

/* Room in the open element, across (in a row) or down: `size`, or the room left (0). */
static void room(bool across_it, float size)
{
    Clay_ElementDeclaration declaration;
    Clay_SizingAxis along;
    sizing(&along, size > 0.0f ? WGF_UI_SIZING_FIXED : WGF_UI_SIZING_GROW, size);
    memset(&declaration, 0, sizeof(declaration));
    if (across_it)
        declaration.layout.sizing.width = along;
    else
        declaration.layout.sizing.height = along;
    Clay__OpenElement();
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
}

bool wgf_ui_spacer(float size)
{
    if (!(size >= 0.0f) || !isfinite(size) || !child("wgf_ui_spacer")) return false;
    room(in_row(), size);
    return true;
}

bool wgf_ui_separator(void)
{
    Clay_ElementDeclaration declaration;
    const float line = ui.values[WGF_UI_VALUE_LINE_WIDTH];
    if (!child("wgf_ui_separator")) return false;
    memset(&declaration, 0, sizeof(declaration));
    if (in_row()) {
        sizing(&declaration.layout.sizing.width, WGF_UI_SIZING_FIXED, line);
        sizing(&declaration.layout.sizing.height, WGF_UI_SIZING_GROW, 0.0f);
    } else {
        sizing(&declaration.layout.sizing.width, WGF_UI_SIZING_GROW, 0.0f);
        sizing(&declaration.layout.sizing.height, WGF_UI_SIZING_FIXED, line);
    }
    declaration.backgroundColor = clay_color(ui.colors[WGF_UI_COLOR_SEPARATOR]);
    Clay__OpenElement();
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
    return true;
}

static bool is_kind(wgf_handle_t handle, int kind)
{
    return WGF_CORE_PRIV_HANDLE_KIND(handle) == kind && wgf_resource_get_status(handle) != WGF_RESOURCE_STATUS_NONE;
}

static void draw_image(const piece_t *piece, Clay_BoundingBox box);

/* An image's element, `widget` the widget it is part of (-1: none), `width` by `height`. */
static void image_element(wgf_texture_t texture, int widget, int state, float width, float height)
{
    Clay_ElementDeclaration declaration;
    piece_t *piece;
    memset(&declaration, 0, sizeof(declaration));
    fixed(&declaration, width, height);
    declaration.custom.customData = add_piece(draw_image, widget, state);
    piece = piece_at(declaration.custom.customData);
    if (piece != NULL) piece->texture = texture;
    Clay__OpenElement();
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
}

bool wgf_ui_image(wgf_texture_t texture, float width, float height)
{
    if (!is_kind(texture, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE) || !child("wgf_ui_image")) return false;
    if (!(width > 0.0f) || !isfinite(width)) width = (float)wgf_texture_get_width(texture);
    if (!(height > 0.0f) || !isfinite(height)) height = (float)wgf_texture_get_height(texture);
    image_element(texture, -1, ui.disabled ? WGF_UI_STATE_DISABLED : WGF_UI_STATE_NORMAL, width, height);
    return true;
}

/* The element `id` names, for a widget: false (logged where it is the program's) for no
 * id, one of 64 bytes or more, or no UI begun. */
static bool widget_named(const char *id, const char *what, Clay_ElementId *element, Clay_String *named)
{
    Clay_String name;
    if (id == NULL || id[0] == '\0' || strlen(id) >= ID_MAX || !child(what)) return false;
    if (!clay_string(id, &name)) return false;
    *element = Clay_GetElementId(name);
    if (named != NULL) *named = name;
    return true;
}

static bool widget_id(const char *id, const char *what, Clay_ElementId *element)
{
    return widget_named(id, what, element, NULL);
}

bool wgf_ui_custom(const char *id, float width, float height)
{
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    if (!(width >= 0.0f) || !(height >= 0.0f) || !isfinite(width) || !isfinite(height)) return false;
    if (!widget_id(id, "wgf_ui_custom", &element)) return false;
    memset(&declaration, 0, sizeof(declaration));
    fixed(&declaration, width, height);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
    return true;
}

wgf_vec4_t wgf_ui_get_bounds(const char *id)
{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    wgf_ui_priv_get_bounds(id, &x, &y, &width, &height);
    return wgf_vec4_make(x, y, width, height);
}

bool wgf_ui_set_disabled(bool disabled)
{
    if (!ui.begun) return false;
    ui.disabled = disabled;
    return true;
}

bool wgf_ui_is_disabled(void)
{
    return ui.begun && ui.disabled;
}

/* ---- answering ---------------------------------------------------------------------- */

/* What this frame's input did to a widget. */
typedef struct answer_t {
    bool over, held, activated, focused;
    int state; /* wgf_ui_state_t */
} answer_t;

/* A widget's answer, `over` whether the pointer is over it, `arrows` those it keeps when
 * focused, `drags` whether a press on it holds it wherever the pointer goes (a slider's).
 * A disabled widget answers nothing and isn't focusable. */
static void answer(const char *id, Clay_ElementId element, bool over, int arrows, bool drags, answer_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(ui.last.id, sizeof(ui.last.id), "%s", id);
    ui.last.element = element.id;
    ui.last.over = over;
    if (ui.disabled) {
        out->state = WGF_UI_STATE_DISABLED;
        return;
    }
    out->over = over;
    if (over) ui.hovering = true;
    if (ui.pointer_pressed && over) {
        snprintf(ui.pressed, sizeof(ui.pressed), "%s", id);
        snprintf(ui.focus, sizeof(ui.focus), "%s", id);
        ui.focus_visible = false;
        ui.scroll.press_drags = drags;
    }
    if (ui.pointer_released && over && strcmp(ui.pressed, id) == 0) out->activated = true;
    out->focused = strcmp(ui.focus, id) == 0;
    if (out->focused) {
        ui.focus_seen = true;
        if (ui.keys.activate) {
            out->activated = true;
            ui.focus_visible = true;
        }
    }
    out->held = ui.pointer_down && (over || drags) && strcmp(ui.pressed, id) == 0;
    out->state = out->held ? WGF_UI_STATE_PRESSED : over ? WGF_UI_STATE_HOVERED : WGF_UI_STATE_NORMAL;
    if (!add_focusable(id, element.id, arrows)) wgf_log_error("wgf_ui: out of memory for the frame's widgets");
}

/* The line around a focused widget, when keys or a pad moved the focus. */
static void ring(Clay_ElementDeclaration *declaration, bool focused)
{
    if (focused && ui.focus_visible) {
        const uint16_t width = pixels(ui.values[WGF_UI_VALUE_FOCUS_WIDTH]);
        declaration->border.color = clay_color(ui.colors[WGF_UI_COLOR_FOCUS]);
        declaration->border.width.left = declaration->border.width.right = width;
        declaration->border.width.top = declaration->border.width.bottom = width;
    }
}

/* ---- widgets ------------------------------------------------------------------------ */

static void draw_face(const piece_t *piece, Clay_BoundingBox box);

/* An icon in a face, as tall as its text: the icon button's (given by it, so only a program
 * with icon buttons links images' drawing). */
static void icon_element(wgf_texture_t icon, int widget, int state)
{
    const float height = ui.values[WGF_UI_VALUE_TEXT_SIZE];
    const int w = wgf_texture_get_width(icon), h = wgf_texture_get_height(icon);
    image_element(icon, widget, state, w > 0 && h > 0 ? height * (float)w / (float)h : height, height);
}

typedef void (*icon_t)(wgf_texture_t icon, int widget, int state);

/* A button's face (a button, an icon button, a toggle): its icon (drawn by `with_icon`)
 * and text in it. False when refused; `activated` whether it was this frame. */
static bool face(const char *what, const char *id, int widget, wgf_texture_t icon, icon_t with_icon, const char *text,
                 bool on, bool *activated)
{
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    answer_t answered;
    void *piece;
    const float padding = ui.values[WGF_UI_VALUE_BUTTON_PADDING];
    *activated = false;
    if (icon != 0 && !is_kind(icon, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE)) return false;
    if (!widget_id(id, what, &element)) return false;
    answer(id, element, Clay_PointerOver(element), ARROWS_NONE, false, &answered); /* where it was in the frame before */
    if (on && answered.state != WGF_UI_STATE_DISABLED) answered.state = WGF_UI_STATE_PRESSED;
    piece = add_piece(draw_face, widget, answered.state);
    memset(&declaration, 0, sizeof(declaration));
    declaration.layout.sizing.width.type = CLAY__SIZING_TYPE_FIT;
    declaration.layout.sizing.width.size.minMax.min = ui.values[WGF_UI_VALUE_BUTTON_WIDTH];
    declaration.layout.padding.left = declaration.layout.padding.right = pixels(padding);
    declaration.layout.padding.top = declaration.layout.padding.bottom = pixels(padding * 0.5f);
    declaration.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    declaration.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    declaration.layout.childGap = pixels(padding * 0.5f);
    declaration.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    declaration.custom.customData = piece;
    ring(&declaration, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    add_solid(element.id);
    if (icon != 0 && with_icon != NULL) with_icon(icon, widget, answered.state);
    if (text != NULL && text[0] != '\0')
        text_element(text, 0.0f, dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], answered.state), CLAY_TEXT_WRAP_NONE, piece);
    Clay__CloseElement();
    *activated = answered.activated;
    return true;
}

bool wgf_ui_button(const char *id, const char *text)
{
    bool activated;
    return face("wgf_ui_button", id, WGF_UI_WIDGET_BUTTON, 0, NULL, text, false, &activated) && activated;
}

bool wgf_ui_icon_button(const char *id, wgf_texture_t icon, const char *text)
{
    bool activated;
    return face("wgf_ui_icon_button", id, WGF_UI_WIDGET_BUTTON, icon, icon_element, text, false, &activated) &&
           activated;
}

bool wgf_ui_toggle(const char *id, const char *text, bool on)
{
    bool activated;
    return face("wgf_ui_toggle", id, WGF_UI_WIDGET_TOGGLE, 0, NULL, text, on, &activated) && activated ? !on : on;
}

/* Whether the pointer is on `mask` stretched over the element's last box: its pixel there
 * not clear. */
static bool on_mask(Clay_ElementId element, wgf_image_t mask)
{
    const Clay_ElementData data = Clay_GetElementData(element);
    const int width = wgf_image_get_width(mask), height = wgf_image_get_height(mask);
    int x, y;
    if (!data.found || data.boundingBox.width <= 0.0f || data.boundingBox.height <= 0.0f || width <= 0 || height <= 0)
        return false;
    x = (int)((ui.pointer_x - data.boundingBox.x) / data.boundingBox.width * (float)width);
    y = (int)((ui.pointer_y - data.boundingBox.y) / data.boundingBox.height * (float)height);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return wgf_color_get_alpha(wgf_image_get_pixel(mask, x, y)) > 0;
}

static void draw_image_button(const piece_t *piece, Clay_BoundingBox box);

bool wgf_ui_image_button(const char *id, wgf_texture_t normal, wgf_texture_t pressed, wgf_image_t mask)
{
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    answer_t answered;
    piece_t *piece;
    bool over;
    if (!is_kind(normal, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE) ||
        (pressed != 0 && !is_kind(pressed, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE)) ||
        (mask != 0 && !is_kind(mask, WGF_CORE_PRIV_HANDLE_KIND_IMAGE))) {
        return false;
    }
    if (!widget_id(id, "wgf_ui_image_button", &element)) return false;
    over = Clay_PointerOver(element) && (mask == 0 || on_mask(element, mask));
    answer(id, element, over, ARROWS_NONE, false, &answered);
    memset(&declaration, 0, sizeof(declaration));
    fixed(&declaration, (float)wgf_texture_get_width(normal), (float)wgf_texture_get_height(normal));
    declaration.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    declaration.custom.customData = add_piece(draw_image_button, -1, answered.state);
    piece = piece_at(declaration.custom.customData);
    if (piece != NULL) {
        piece->texture = normal;
        piece->pressed = pressed;
    }
    ring(&declaration, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
    return answered.activated;
}

static void draw_check(const piece_t *piece, Clay_BoundingBox box);

bool wgf_ui_checkbox(const char *id, const char *text, bool on)
{
    Clay_ElementId element;
    Clay_ElementDeclaration row, box;
    answer_t answered;
    piece_t *piece;
    const float size = ui.values[WGF_UI_VALUE_CHECK_SIZE];
    if (!widget_id(id, "wgf_ui_checkbox", &element)) return on;
    answer(id, element, Clay_PointerOver(element), ARROWS_NONE, false, &answered);
    memset(&row, 0, sizeof(row));
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childGap = pixels(ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.5f);
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(row);
    add_solid(element.id);
    memset(&box, 0, sizeof(box));
    fixed(&box, size, size);
    box.cornerRadius = corners(fminf(ui.values[WGF_UI_VALUE_CORNER_RADIUS], size * 0.25f));
    box.custom.customData = add_piece(draw_check, WGF_UI_WIDGET_CHECKBOX, answered.state);
    piece = piece_at(box.custom.customData);
    if (piece != NULL) piece->on = on;
    ring(&box, answered.focused);
    Clay__OpenElement();
    Clay__ConfigureOpenElement(box);
    Clay__CloseElement();
    if (text != NULL && text[0] != '\0')
        text_element(text, 0.0f, dim(ui.colors[WGF_UI_COLOR_TEXT], answered.state), CLAY_TEXT_WRAP_NONE, NULL);
    Clay__CloseElement();
    return answered.activated ? !on : on;
}

/* Where along a slider's last box the pointer is, 0 at its fill's start to 1 at its end,
 * its handle's center kept inside it. */
static float slider_at(Clay_BoundingBox box)
{
    const int from = ui.from[WGF_UI_WIDGET_SLIDER];
    const bool row = across(WGF_UI_WIDGET_SLIDER);
    const float handle = ui.values[WGF_UI_VALUE_HANDLE_SIZE];
    const float length = row ? box.width : box.height, span = length - handle;
    float at = row ? ui.pointer_x - box.x : ui.pointer_y - box.y;
    if (from == WGF_UI_FROM_RIGHT || from == WGF_UI_FROM_BOTTOM) at = length - at;
    if (span <= 0.0f) return 0.0f;
    at = (at - handle * 0.5f) / span;
    return at > 0.0f ? (at < 1.0f ? at : 1.0f) : 0.0f;
}

static void draw_slider(const piece_t *piece, Clay_BoundingBox box);

float wgf_ui_slider(const char *id, float value, float min, float max)
{
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    answer_t answered;
    piece_t *piece;
    const int from = ui.from[WGF_UI_WIDGET_SLIDER];
    const bool row = across(WGF_UI_WIDGET_SLIDER);
    const float length = ui.values[WGF_UI_VALUE_SLIDER_WIDTH];
    const float thickness = fmaxf(ui.values[WGF_UI_VALUE_SLIDER_HEIGHT], ui.values[WGF_UI_VALUE_HANDLE_SIZE]);
    if (!isfinite(min) || !isfinite(max) || !(max > min)) return value;
    if (!widget_id(id, "wgf_ui_slider", &element)) return value;
    value = value > min ? (value < max ? value : max) : min; /* NaN too: min */
    answer(id, element, Clay_PointerOver(element), row ? ARROWS_ACROSS : ARROWS_DOWN, true, &answered);
    if (answered.held) {
        const Clay_ElementData data = Clay_GetElementData(element);
        if (data.found) value = min + slider_at(data.boundingBox) * (max - min);
    }
    if (answered.focused) {
        const bool more = from == WGF_UI_FROM_LEFT    ? ui.keys.more_right
                          : from == WGF_UI_FROM_RIGHT ? ui.keys.more_left
                          : from == WGF_UI_FROM_TOP   ? ui.keys.more_down
                                                      : ui.keys.more_up;
        const bool less = from == WGF_UI_FROM_LEFT    ? ui.keys.more_left
                          : from == WGF_UI_FROM_RIGHT ? ui.keys.more_right
                          : from == WGF_UI_FROM_TOP   ? ui.keys.more_up
                                                      : ui.keys.more_down;
        const float step = (max - min) / 20.0f;
        if (more != less) {
            value += more ? step : -step;
            value = value > min ? (value < max ? value : max) : min;
            ui.focus_visible = true;
        }
    }
    memset(&declaration, 0, sizeof(declaration));
    fixed(&declaration, row ? length : thickness, row ? thickness : length);
    declaration.cornerRadius = corners(fminf(ui.values[WGF_UI_VALUE_CORNER_RADIUS], thickness * 0.5f));
    declaration.custom.customData = add_piece(draw_slider, WGF_UI_WIDGET_SLIDER, answered.state);
    piece = piece_at(declaration.custom.customData);
    if (piece != NULL) piece->value = (value - min) / (max - min);
    ring(&declaration, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    add_solid(element.id);
    Clay__CloseElement();
    return value;
}

static void draw_progress(const piece_t *piece, Clay_BoundingBox box);

bool wgf_ui_progress(float value)
{
    Clay_ElementDeclaration declaration;
    piece_t *piece;
    const bool row = across(WGF_UI_WIDGET_PROGRESS);
    const float width = ui.values[WGF_UI_VALUE_BAR_WIDTH], height = ui.values[WGF_UI_VALUE_BAR_HEIGHT];
    if (!child("wgf_ui_progress")) return false;
    value = value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f; /* NaN too: 0 */
    memset(&declaration, 0, sizeof(declaration));
    fixed(&declaration, row ? width : height, row ? height : width);
    declaration.custom.customData =
        add_piece(draw_progress, WGF_UI_WIDGET_PROGRESS, ui.disabled ? WGF_UI_STATE_DISABLED : WGF_UI_STATE_NORMAL);
    piece = piece_at(declaration.custom.customData);
    if (piece != NULL) piece->value = value;
    Clay__OpenElement();
    Clay__ConfigureOpenElement(declaration);
    Clay__CloseElement();
    return true;
}

/* `labels`' lines, their starts and lengths, up to CHOICES_MAX; how many. */
static int lines_of(const char *labels, const char **starts, size_t *lengths)
{
    int count = 0;
    while (labels != NULL && labels[0] != '\0' && count < CHOICES_MAX) {
        const char *end = strchr(labels, '\n');
        const size_t length = end != NULL ? (size_t)(end - labels) : strlen(labels);
        starts[count] = labels;
        lengths[count++] = length;
        labels = end != NULL ? end + 1 : NULL;
    }
    return count;
}

static int clamp_index(int index, int count)
{
    return index < 0 ? 0 : index >= count ? count - 1 : index;
}

/* A face's declaration: a button's padding and least width, its content centered. */
static void face_layout(Clay_ElementDeclaration *declaration, void *piece)
{
    const float padding = ui.values[WGF_UI_VALUE_BUTTON_PADDING];
    memset(declaration, 0, sizeof(*declaration));
    declaration->layout.padding.left = declaration->layout.padding.right = pixels(padding);
    declaration->layout.padding.top = declaration->layout.padding.bottom = pixels(padding * 0.5f);
    declaration->layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    declaration->layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    declaration->layout.childGap = pixels(padding * 0.5f);
    declaration->cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    declaration->custom.customData = piece;
}

/* The state of a part `i` of a widget (a tab, a number's minus or plus): disabled with it,
 * pressed while chosen or held, hovered under the pointer. */
static int part_state(const answer_t *answered, bool chosen, bool over)
{
    if (answered->state == WGF_UI_STATE_DISABLED) return WGF_UI_STATE_DISABLED;
    if (chosen || (over && ui.pointer_down && answered->held)) return WGF_UI_STATE_PRESSED;
    return over ? WGF_UI_STATE_HOVERED : WGF_UI_STATE_NORMAL;
}

/* Tabs and a choice: a row of faces, one a label, the chosen one pressed; a press chooses
 * the one it is on, and the arrows step when the row is focused. */
static int faces_row(const char *what, const char *id, const char *labels, int selected, int widget, float gap)
{
    const char *starts[CHOICES_MAX];
    size_t lengths[CHOICES_MAX];
    const int count = lines_of(labels, starts, lengths);
    Clay_ElementId element;
    Clay_ElementDeclaration row;
    Clay_String name;
    answer_t answered;
    int i, hovered = -1;
    if (count == 0 || !widget_named(id, what, &element, &name)) return selected;
    selected = clamp_index(selected, count);
    for (i = 0; i < count && hovered < 0; i++) {
        if (Clay_PointerOver(Clay_GetElementIdWithIndex(name, (uint32_t)i))) hovered = i;
    }
    answer(id, element, Clay_PointerOver(element), ARROWS_ACROSS, false, &answered);
    if (answered.state != WGF_UI_STATE_DISABLED) {
        if (ui.pointer_pressed && hovered >= 0) selected = hovered;
        if (answered.focused && ui.keys.more_left != ui.keys.more_right) {
            selected = clamp_index(selected + (ui.keys.more_right ? 1 : -1), count);
            ui.focus_visible = true;
        }
    }
    memset(&row, 0, sizeof(row));
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childGap = pixels(gap);
    row.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    ring(&row, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(row);
    add_solid(element.id);
    for (i = 0; i < count; i++) {
        Clay_ElementDeclaration tab;
        const int state = part_state(&answered, i == selected, i == hovered);
        void *piece = add_piece(draw_face, widget, state);
        face_layout(&tab, piece);
        tab.layout.padding.left = tab.layout.padding.right = pixels(ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.5f);
        Clay__OpenElementWithId(Clay_GetElementIdWithIndex(name, (uint32_t)i));
        Clay__ConfigureOpenElement(tab);
        text_slice(starts[i], lengths[i], 0.0f, dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], state), CLAY_TEXT_WRAP_NONE,
                   piece);
        Clay__CloseElement();
    }
    Clay__CloseElement();
    return selected;
}

int wgf_ui_tabs(const char *id, const char *labels, int selected)
{
    return faces_row("wgf_ui_tabs", id, labels, selected, WGF_UI_WIDGET_TAB, 2.0f);
}

int wgf_ui_choice(const char *id, const char *labels, int selected)
{
    return faces_row("wgf_ui_choice", id, labels, selected, WGF_UI_WIDGET_TOGGLE, ui.values[WGF_UI_VALUE_GAP] * 0.5f);
}

int wgf_ui_combo(const char *id, const char *labels, int selected)
{
    const char *starts[CHOICES_MAX];
    size_t lengths[CHOICES_MAX];
    const int count = lines_of(labels, starts, lengths);
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    answer_t answered;
    void *piece;
    wgf_color_t color;
    if (count == 0 || !widget_id(id, "wgf_ui_combo", &element)) return selected;
    selected = clamp_index(selected, count);
    answer(id, element, Clay_PointerOver(element), ARROWS_ACROSS, false, &answered);
    if (answered.activated) selected = (selected + 1) % count;
    if (answered.focused && ui.keys.more_left != ui.keys.more_right) {
        selected = (selected + (ui.keys.more_right ? 1 : count - 1)) % count;
        ui.focus_visible = true;
    }
    piece = add_piece(draw_face, WGF_UI_WIDGET_COMBO, answered.state);
    face_layout(&declaration, piece);
    declaration.layout.sizing.width.type = CLAY__SIZING_TYPE_FIT;
    declaration.layout.sizing.width.size.minMax.min = ui.values[WGF_UI_VALUE_BUTTON_WIDTH];
    ring(&declaration, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    add_solid(element.id);
    color = dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], answered.state);
    text_element("<", 0.0f, color, CLAY_TEXT_WRAP_NONE, piece);
    room(true, 0.0f);
    text_slice(starts[selected], lengths[selected], 0.0f, color, CLAY_TEXT_WRAP_NONE, piece);
    room(true, 0.0f);
    text_element(">", 0.0f, color, CLAY_TEXT_WRAP_NONE, piece);
    Clay__CloseElement();
    return selected;
}

static void draw_number_box(const piece_t *piece, Clay_BoundingBox box);

/* `value` stepped by `steps` of `step`, on the step's grid from `min`, within the range. */
static float stepped(float value, float min, float max, float step, int steps)
{
    value = min + roundf((value - min) / step + (float)steps) * step;
    return value > min ? (value < max ? value : max) : min;
}

float wgf_ui_number(const char *id, float value, float min, float max, float step)
{
    Clay_ElementId element, minus, plus;
    Clay_ElementDeclaration row, box;
    Clay_String name;
    answer_t answered;
    char text[48];
    int decimals = 0, i;
    if (!isfinite(min) || !isfinite(max) || !(max > min) || !(step > 0.0f) || !isfinite(step)) return value;
    if (!widget_named(id, "wgf_ui_number", &element, &name)) return value;
    value = value > min ? (value < max ? value : max) : min; /* NaN too: min */
    minus = Clay_GetElementIdWithIndex(name, 0);
    plus = Clay_GetElementIdWithIndex(name, 1);
    answer(id, element, Clay_PointerOver(element), ARROWS_ACROSS, false, &answered);
    if (answered.state != WGF_UI_STATE_DISABLED) {
        if (ui.pointer_pressed && Clay_PointerOver(minus)) value = stepped(value, min, max, step, -1);
        if (ui.pointer_pressed && Clay_PointerOver(plus)) value = stepped(value, min, max, step, 1);
        if (answered.focused && ui.keys.more_left != ui.keys.more_right) {
            value = stepped(value, min, max, step, ui.keys.more_right ? 1 : -1);
            ui.focus_visible = true;
        }
    }
    while (decimals < 4 && fabsf(step * powf(10.0f, (float)decimals) - roundf(step * powf(10.0f, (float)decimals))) > 1e-3f)
        decimals++;
    snprintf(text, sizeof(text), "%.*f", decimals, (double)value);
    memset(&row, 0, sizeof(row));
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childGap = pixels(ui.values[WGF_UI_VALUE_GAP] * 0.5f);
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    ring(&row, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(row);
    add_solid(element.id);
    for (i = 0; i < 3; i++) {
        if (i == 1) { /* the value, in its box */
            void *piece = add_piece(draw_number_box, WGF_UI_WIDGET_NUMBER, answered.state);
            face_layout(&box, piece);
            box.layout.sizing.width.type = CLAY__SIZING_TYPE_FIT;
            box.layout.sizing.width.size.minMax.min = ui.values[WGF_UI_VALUE_BUTTON_WIDTH] * 0.5f;
            Clay__OpenElement();
            Clay__ConfigureOpenElement(box);
            text_element(text, 0.0f, dim(ui.colors[WGF_UI_COLOR_TEXT], answered.state), CLAY_TEXT_WRAP_NONE, NULL);
            Clay__CloseElement();
        } else { /* the minus and the plus, buttons' faces */
            const Clay_ElementId which = i == 0 ? minus : plus;
            const int state = part_state(&answered, false, Clay_PointerOver(which));
            void *piece = add_piece(draw_face, WGF_UI_WIDGET_BUTTON, state);
            face_layout(&box, piece);
            box.layout.padding.left = box.layout.padding.right = box.layout.padding.top;
            Clay__OpenElementWithId(which);
            Clay__ConfigureOpenElement(box);
            text_element(i == 0 ? "-" : "+", 0.0f, dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], state), CLAY_TEXT_WRAP_NONE,
                         piece);
            Clay__CloseElement();
        }
    }
    Clay__CloseElement();
    return value;
}

bool wgf_ui_tile(const char *id, wgf_texture_t picture, const char *text)
{
    Clay_ElementId element;
    Clay_ElementDeclaration declaration;
    answer_t answered;
    void *piece;
    const float size = ui.values[WGF_UI_VALUE_TILE_SIZE], padding = ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.25f;
    if (picture != 0 && !is_kind(picture, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE)) return false;
    if (!widget_id(id, "wgf_ui_tile", &element)) return false;
    answer(id, element, Clay_PointerOver(element), ARROWS_NONE, false, &answered);
    piece = add_piece(draw_face, WGF_UI_WIDGET_TILE, answered.state);
    face_layout(&declaration, piece);
    declaration.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    declaration.layout.padding.left = declaration.layout.padding.right = pixels(padding);
    declaration.layout.padding.top = declaration.layout.padding.bottom = pixels(padding);
    sizing(&declaration.layout.sizing.width, WGF_UI_SIZING_FIXED, size);
    ring(&declaration, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    add_solid(element.id);
    if (picture != 0) image_element(picture, WGF_UI_WIDGET_TILE, answered.state, size - padding * 2.0f, size - padding * 2.0f);
    if (text != NULL && text[0] != '\0')
        text_element(text, 0.0f, dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], answered.state), CLAY_TEXT_WRAP_NONE, piece);
    Clay__CloseElement();
    return answered.activated;
}

/* ---- scrolling ---------------------------------------------------------------------- */

#define DRAG_SLOP 6.0f /* logical pixels a press moves before it is a drag's */

/* A list's item's height, and how far a wheel's notch scrolls. */
static float row_height(void)
{
    return ui.values[WGF_UI_VALUE_TEXT_SIZE] + ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.5f;
}

static float clamped(float value, float most)
{
    return value > 0.0f ? (value < most ? value : most) : 0.0f; /* NaN too: 0 */
}

/* Each frame's start for scrolling, once a scroll area or list was given (wgf_ui_scroll.c,
 * through the hook it sets: set here, the optimizer would call it in every UI). */
static wgf_ui_priv_scroll_frame_t scroll_frame;

void wgf_ui_priv_set_scroll_frame(wgf_ui_priv_scroll_frame_t frame)
{
    scroll_frame = frame;
}

/* How far the area whose window is `window` was scrolled as last laid out (`now` if it
 * wasn't): what the frame before's bounds inside it are offset by. */
static void laid_out(uint32_t window, const float now[2], float out[2])
{
    int i;
    out[0] = now[0];
    out[1] = now[1];
    for (i = 0; i < ui.scroll.before_count; i++) {
        if (ui.scroll.before[i].window == window) {
            out[0] = ui.scroll.before[i].offset[0];
            out[1] = ui.scroll.before[i].offset[1];
        }
    }
}

/* A scroll bar's thumb along a track `track` long: its start and length, `share` of what
 * it scrolls in view and `at` (0 to 1) of the way scrolled. */
static void thumb_of(float track, float share, float at, float *start, float *length)
{
    *length = fminf(track, fmaxf(track * share, ui.values[WGF_UI_VALUE_SCROLL_BAR] * 2.0f));
    *start = (track - *length) * at;
}

/* What the input does to a scroll area before it is laid out: `offset`, how far it is
 * scrolled across and down, moved by the wheel, a drag, a fling, or its bars (`bars`), as it
 * was last laid out (`view` of `content` each way), the ways it scrolls `ways`; and held to
 * its content. A `live` one alone answers. */
static void scroll_input(uint32_t window, const Clay_ElementId bars[2], const bool ways[2], const float view[2],
                         const float content[2], bool live, float offset[2])
{
    const float pointer[2] = {ui.pointer_x, ui.pointer_y};
    const float dt = (float)wgf_platform_priv_get_frame_duration();
    float before[2], most[2];
    int a;
    for (a = 0; a < 2; a++) {
        most[a] = ways[a] ? fmaxf(content[a] - view[a], 0.0f) : 0.0f;
        before[a] = offset[a];
        if (live && ui.scroll.target == window) offset[a] -= ui.scroll.wheel[a] * row_height();
    }
    if (live && ui.scroll.held == window && ui.pointer_down) {
        if (!ui.scroll.started) { /* the press: on a thumb it holds, on a track it pages */
            ui.scroll.started = true;
            for (a = 0; a < 2; a++) {
                const Clay_ElementData bar = Clay_GetElementData(bars[a]);
                const float track = a == 0 ? bar.boundingBox.width : bar.boundingBox.height;
                const float at = pointer[a] - (a == 0 ? bar.boundingBox.x : bar.boundingBox.y);
                float start, length;
                ui.scroll.from[a] = pointer[a];
                ui.scroll.offset_from[a] = offset[a];
                if (most[a] <= 0.0f || !bar.found || !Clay_PointerOver(bars[a])) continue;
                thumb_of(track, view[a] / content[a], offset[a] / most[a], &start, &length);
                if (at >= start && at < start + length) {
                    ui.scroll.bar = a;
                    ui.scroll.grab = at - start;
                } else {
                    ui.scroll.bar = 2;
                    offset[a] += at < start ? -view[a] : view[a];
                }
            }
        } else if (ui.scroll.bar == 0 || ui.scroll.bar == 1) { /* the thumb dragged */
            const Clay_ElementData bar = Clay_GetElementData(bars[ui.scroll.bar]);
            a = ui.scroll.bar;
            if (bar.found && most[a] > 0.0f) {
                const float track = a == 0 ? bar.boundingBox.width : bar.boundingBox.height;
                const float at = pointer[a] - (a == 0 ? bar.boundingBox.x : bar.boundingBox.y);
                float start, length;
                thumb_of(track, view[a] / content[a], 0.0f, &start, &length);
                if (track > length) offset[a] = (at - ui.scroll.grab) / (track - length) * most[a];
            }
        } else if (ui.scroll.bar < 0 && !ui.scroll.dragging && !ui.scroll.press_drags) {
            for (a = 0; a < 2; a++) {
                if (most[a] > 0.0f && fabsf(pointer[a] - ui.scroll.from[a]) > DRAG_SLOP) ui.scroll.dragging = true;
            }
            if (ui.scroll.dragging) ui.pressed[0] = '\0'; /* the widget it began on is let go */
        }
        if (ui.scroll.dragging) {
            for (a = 0; a < 2; a++) {
                offset[a] = ui.scroll.offset_from[a] - (pointer[a] - ui.scroll.from[a]);
                ui.scroll.velocity[a] = dt > 0.0f ? (clamped(offset[a], most[a]) - before[a]) / dt : 0.0f;
            }
        }
    }
    if (live && ui.scroll.flung == window) {
        const float decay = powf(0.05f, dt); /* to a twentieth in a second */
        for (a = 0; a < 2; a++) {
            offset[a] += ui.scroll.velocity[a] * dt;
            ui.scroll.velocity[a] *= decay;
        }
        if (fabsf(ui.scroll.velocity[0]) < 20.0f && fabsf(ui.scroll.velocity[1]) < 20.0f) ui.scroll.flung = 0;
    }
    for (a = 0; a < 2; a++) offset[a] = clamped(offset[a], most[a]);
}

static void close_scroll(level_t *level);

/* A scroll area opened, a list's too: `element` its id's, `name` its id, `width` by `height`
 * (0: the room left, at least `least` wide), padded by `padding`, drawn by `look` (NULL: its
 * content alone), scrolling the ways `ways`; its window the open level, for its content. */
static bool open_scroll(Clay_ElementId element, Clay_String name, float width, float height, float least,
                        float padding, void *look, bool focused, const bool ways[2], bool live)
{
    Clay_ElementDeclaration outer, row;
    const Clay_ElementId window = Clay_GetElementIdWithIndex(name, 0);
    const Clay_ScrollContainerData data = Clay_GetScrollContainerData(window);
    float view[2] = {0.0f, 0.0f}, content[2] = {0.0f, 0.0f}, offset[2] = {0.0f, 0.0f};
    level_t *level;
    int a;
    if (ui.depth == DEPTH_MAX) {
        wgf_log_error("wgf_ui: more than %d boxes inside each other", DEPTH_MAX);
        return false;
    }
    wgf_ui_priv_install_scrolling();
    if (ui.scroll.area_count == ui.scroll.area_capacity) {
        const int capacity = ui.scroll.area_capacity > 0 ? ui.scroll.area_capacity * 2 : 8;
        wgf_ui_priv_area_t *grown =
            (wgf_ui_priv_area_t *)realloc(ui.scroll.areas, sizeof(wgf_ui_priv_area_t) * (size_t)capacity);
        if (grown != NULL) {
            ui.scroll.areas = grown;
            ui.scroll.area_capacity = capacity;
        }
    }
    level = &ui.levels[ui.depth];
    memset(level, 0, sizeof(*level));
    level->bars[0] = Clay_GetElementIdWithIndex(name, 2);
    level->bars[1] = Clay_GetElementIdWithIndex(name, 1);
    if (data.found && data.scrollPosition != NULL) {
        view[0] = data.scrollContainerDimensions.width;
        view[1] = data.scrollContainerDimensions.height;
        content[0] = data.contentDimensions.width;
        content[1] = data.contentDimensions.height;
        offset[0] = -data.scrollPosition->x;
        offset[1] = -data.scrollPosition->y;
    }
    scroll_input(window.id, level->bars, ways, view, content, live, offset);
    if (data.found && data.scrollPosition != NULL) {
        data.scrollPosition->x = -offset[0];
        data.scrollPosition->y = -offset[1];
    }
    if (ui.scroll.area_count < ui.scroll.area_capacity) { /* for the next frame: under the pointer, laid out */
        wgf_ui_priv_area_t *area = &ui.scroll.areas[ui.scroll.area_count++];
        area->element = element.id;
        area->window = window.id;
        area->offset[0] = offset[0];
        area->offset[1] = offset[1];
    }
    memset(&outer, 0, sizeof(outer));
    outer.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    if (width > 0.0f) {
        sizing(&outer.layout.sizing.width, WGF_UI_SIZING_FIXED, width);
    } else {
        outer.layout.sizing.width.type = CLAY__SIZING_TYPE_GROW;
        outer.layout.sizing.width.size.minMax.min = least;
    }
    sizing(&outer.layout.sizing.height, height > 0.0f ? WGF_UI_SIZING_FIXED : WGF_UI_SIZING_GROW, height);
    outer.layout.padding.left = outer.layout.padding.right = pixels(padding);
    outer.layout.padding.top = outer.layout.padding.bottom = pixels(padding);
    outer.cornerRadius = corners(look != NULL ? ui.values[WGF_UI_VALUE_CORNER_RADIUS] : 0.0f);
    outer.custom.customData = look;
    ring(&outer, focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(outer);
    add_solid(element.id); /* the wheel over it is the UI's */
    memset(&row, 0, sizeof(row));
    row.layout.sizing.width.type = row.layout.sizing.height.type = CLAY__SIZING_TYPE_GROW;
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    Clay__OpenElement();
    Clay__ConfigureOpenElement(row);
    Clay__OpenElementWithId(window);
    level->kind = KIND_SCROLL;
    level->declaration.layout.sizing.width.type = level->declaration.layout.sizing.height.type =
        CLAY__SIZING_TYPE_GROW;
    level->declaration.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    level->declaration.clip.horizontal = level->declaration.clip.vertical = true;
    level->declaration.clip.childOffset.x = -offset[0];
    level->declaration.clip.childOffset.y = -offset[1];
    level->close = close_scroll;
    level->window = window;
    level->first_focusable = ui.focusable_count;
    for (a = 0; a < 2; a++) {
        const float most = content[a] - view[a];
        level->offset[a] = offset[a];
        level->shown[a] = ways[a] && most > 0.5f;
        if (!level->shown[a]) continue;
        level->thumbs[a][0] = view[a] / content[a];
        level->thumbs[a][1] = offset[a] / most;
        level->bar_states[a] = !live                                                  ? WGF_UI_STATE_DISABLED
                               : ui.scroll.held == window.id && ui.scroll.bar == a   ? WGF_UI_STATE_PRESSED
                               : Clay_PointerOver(level->bars[a])                    ? WGF_UI_STATE_HOVERED
                                                                                     : WGF_UI_STATE_NORMAL;
    }
    ui.depth++;
    return true;
}

static void draw_scroll_bar(const piece_t *piece, Clay_BoundingBox box);

/* A scroll area's bar across (0) or down (1). */
static void scroll_bar(const level_t *level, int a)
{
    Clay_ElementDeclaration bar;
    piece_t *piece;
    const float thickness = ui.values[WGF_UI_VALUE_SCROLL_BAR];
    memset(&bar, 0, sizeof(bar));
    sizing(a == 0 ? &bar.layout.sizing.width : &bar.layout.sizing.height, WGF_UI_SIZING_GROW, 0.0f);
    sizing(a == 0 ? &bar.layout.sizing.height : &bar.layout.sizing.width, WGF_UI_SIZING_FIXED, thickness);
    bar.custom.customData = add_piece(draw_scroll_bar, WGF_UI_WIDGET_SCROLL, level->bar_states[a]);
    piece = piece_at(bar.custom.customData);
    if (piece != NULL) {
        piece->on = a == 1;
        piece->size = level->thumbs[a][0];
        piece->value = level->thumbs[a][1];
    }
    Clay__OpenElementWithId(level->bars[a]);
    Clay__ConfigureOpenElement(bar);
    Clay__CloseElement();
}

/* A scroll area's close: the widget in it the focus moved to scrolled into view, and Page Up
 * or Down paging while the focus is in it, but on a list (shown from the next frame); its
 * window closed, and its bars beside and below it. */
static void close_scroll(level_t *level)
{
    const Clay_ScrollContainerData data = Clay_GetScrollContainerData(level->window);
    int i, at = -1;
    for (i = level->first_focusable; i < ui.focusable_count && at < 0; i++) {
        if (strcmp(ui.focusables[i].id, ui.focus) == 0) at = i;
    }
    if (at >= 0 && data.found && data.scrollPosition != NULL) {
        const float view[2] = {data.scrollContainerDimensions.width, data.scrollContainerDimensions.height};
        const float content[2] = {data.contentDimensions.width, data.contentDimensions.height};
        float offset[2];
        int a;
        offset[0] = level->offset[0];
        offset[1] = level->offset[1];
        if (ui.scroll.reveal) {
            Clay_ElementId focused;
            Clay_ElementData widget, window;
            memset(&focused, 0, sizeof(focused));
            focused.id = ui.focusables[at].element;
            widget = Clay_GetElementData(focused);
            window = Clay_GetElementData(level->window);
            if (widget.found && window.found) {
                const float margin = ui.values[WGF_UI_VALUE_FOCUS_WIDTH] * 3.0f;
                float laid[2], place[2];
                laid_out(level->window.id, offset, laid);
                place[0] = widget.boundingBox.x - window.boundingBox.x + laid[0];
                place[1] = widget.boundingBox.y - window.boundingBox.y + laid[1];
                for (a = 0; a < 2; a++) {
                    const float size = a == 0 ? widget.boundingBox.width : widget.boundingBox.height;
                    offset[a] = fminf(offset[a], place[a] - margin);
                    offset[a] = fmaxf(offset[a], place[a] + size + margin - view[a]);
                }
            }
        }
        if (ui.keys.page_down != ui.keys.page_up && ui.focusables[at].arrows != ARROWS_DOWN &&
            ui.focusables[at].arrows != ARROWS_ALL)
            offset[1] += ui.keys.page_down ? view[1] : -view[1];
        for (a = 0; a < 2; a++) offset[a] = level->shown[a] ? clamped(offset[a], content[a] - view[a]) : level->offset[a];
        data.scrollPosition->x = -offset[0];
        data.scrollPosition->y = -offset[1];
    }
    Clay__CloseElement(); /* the window */
    if (level->shown[1]) scroll_bar(level, 1);
    Clay__CloseElement(); /* the row */
    if (level->shown[0]) {
        Clay_ElementDeclaration row;
        memset(&row, 0, sizeof(row));
        row.layout.sizing.width.type = CLAY__SIZING_TYPE_GROW;
        row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        Clay__OpenElement();
        Clay__ConfigureOpenElement(row);
        scroll_bar(level, 0);
        if (level->shown[1]) {
            Clay_ElementDeclaration corner;
            memset(&corner, 0, sizeof(corner));
            fixed(&corner, ui.values[WGF_UI_VALUE_SCROLL_BAR], 0.0f);
            Clay__OpenElement();
            Clay__ConfigureOpenElement(corner);
            Clay__CloseElement();
        }
        Clay__CloseElement();
    }
    Clay__CloseElement(); /* the area */
}

bool wgf_ui_begin_scroll(const char *id, float width, float height)
{
    static const bool both[2] = {true, true};
    Clay_ElementId element;
    Clay_String name;
    if (!(width >= 0.0f) || !(height >= 0.0f) || !isfinite(width) || !isfinite(height)) return false;
    if (!widget_named(id, "wgf_ui_begin_scroll", &element, &name)) return false;
    return open_scroll(element, name, width, height, 0.0f, 0.0f, NULL, false, both, true);
}

bool wgf_ui_end_scroll(void)
{
    return close_level(KIND_SCROLL);
}

/* The Clay data of the scroll area or list `id`: false for one not laid out. */
static bool scroll_named(const char *id, Clay_ScrollContainerData *data)
{
    Clay_String name;
    if (!ui.started || id == NULL || id[0] == '\0') return false;
    name.isStaticallyAllocated = false;
    name.length = (int32_t)strlen(id);
    name.chars = id;
    *data = Clay_GetScrollContainerData(Clay_GetElementIdWithIndex(name, 0));
    return data->found && data->scrollPosition != NULL;
}

bool wgf_ui_set_scroll(const char *id, float x, float y)
{
    Clay_ScrollContainerData data;
    if (!isfinite(x) || !isfinite(y) || !scroll_named(id, &data)) return false;
    data.scrollPosition->x = -clamped(x, data.contentDimensions.width - data.scrollContainerDimensions.width);
    data.scrollPosition->y = -clamped(y, data.contentDimensions.height - data.scrollContainerDimensions.height);
    ui.scroll.flung = 0;
    return true;
}

wgf_vec2_t wgf_ui_get_scroll(const char *id)
{
    Clay_ScrollContainerData data;
    if (!scroll_named(id, &data)) return wgf_vec2_make(0.0f, 0.0f);
    return wgf_vec2_make(-data.scrollPosition->x, -data.scrollPosition->y);
}

static void draw_list_box(const piece_t *piece, Clay_BoundingBox box);
static void draw_list_item(const piece_t *piece, Clay_BoundingBox box);

/* A list's item, a dropdown's (`widget`), the first `length` bytes of `text`; `id` its Clay
 * id (NULL: none). */
static void list_item(const char *text, size_t length, int widget, int state, bool chosen, const Clay_ElementId *id)
{
    Clay_ElementDeclaration item;
    void *look = add_piece(draw_list_item, widget, state);
    piece_t *piece = piece_at(look);
    const float padding = ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.5f;
    if (piece != NULL) piece->on = chosen;
    memset(&item, 0, sizeof(item));
    sizing(&item.layout.sizing.width, WGF_UI_SIZING_GROW, 0.0f);
    sizing(&item.layout.sizing.height, WGF_UI_SIZING_FIXED, row_height());
    item.layout.padding.left = item.layout.padding.right = pixels(padding);
    item.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    item.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS] * 0.5f);
    item.custom.customData = look;
    if (id != NULL)
        Clay__OpenElementWithId(*id);
    else
        Clay__OpenElement();
    Clay__ConfigureOpenElement(item);
    if (length > 0)
        text_slice(text, length, 0.0f, dim(ui.colors[WGF_UI_COLOR_TEXT], state), CLAY_TEXT_WRAP_NONE, look);
    Clay__CloseElement();
}

int wgf_ui_list(const char *id, const char *items, int selected, int rows)
{
    static const bool down[2] = {false, true};
    Clay_ElementId element, window;
    Clay_String name;
    Clay_ScrollContainerData data;
    answer_t answered;
    const float row = row_height(), padding = floorf(ui.values[WGF_UI_VALUE_CORNER_RADIUS] * 0.5f);
    const char *at;
    float view, offset = 0.0f;
    int count = 0, over = -1, first, last, i;
    if (rows < 1 || !widget_named(id, "wgf_ui_list", &element, &name)) return selected;
    for (at = items; at != NULL && at[0] != '\0'; count++) {
        at = strchr(at, '\n');
        if (at != NULL) at++;
    }
    selected = selected < -1 ? -1 : selected >= count ? count - 1 : selected;
    view = row * (float)rows;
    window = Clay_GetElementIdWithIndex(name, 0);
    data = Clay_GetScrollContainerData(window);
    if (data.found && data.scrollPosition != NULL) offset = -data.scrollPosition->y;
    answer(id, element, Clay_PointerOver(element), ARROWS_DOWN, false, &answered);
    if (answered.state != WGF_UI_STATE_DISABLED) {
        const Clay_ElementData box = Clay_GetElementData(window);
        int step = 0;
        if (box.found && Clay_PointerOver(window) && row > 0.0f) {
            const float now[2] = {0.0f, offset};
            float laid[2];
            laid_out(window.id, now, laid);
            over = (int)floorf((ui.pointer_y - box.boundingBox.y + laid[1]) / row);
            if (over >= count) over = -1;
        }
        if (ui.pointer_pressed && answered.over) ui.scroll.item = over;
        if (ui.pointer_released && answered.activated && over >= 0 && over == ui.scroll.item) selected = over;
        if (answered.focused && ui.keys.more_down != ui.keys.more_up) step = ui.keys.more_down ? 1 : -1;
        if (answered.focused && ui.keys.page_down != ui.keys.page_up) step = ui.keys.page_down ? rows : -rows;
        if (step != 0 && count > 0) {
            selected = selected < 0 ? 0 : clamp_index(selected + step, count);
            ui.focus_visible = true;
            if (data.found && data.scrollPosition != NULL) { /* the chosen one into view */
                offset = fmaxf(fminf(offset, row * (float)selected), row * (float)(selected + 1) - view);
                data.scrollPosition->y = -offset;
            }
        }
    }
    if (!open_scroll(element, name, 0.0f, view + padding * 2.0f, ui.values[WGF_UI_VALUE_BUTTON_WIDTH], padding,
                     add_piece(draw_list_box, WGF_UI_WIDGET_LIST, answered.state), answered.focused, down,
                     answered.state != WGF_UI_STATE_DISABLED))
        return selected;
    offset = ui.levels[ui.depth - 1].offset[1];
    configure();
    first = row > 0.0f ? (int)floorf(offset / row) : 0; /* the items in view alone, the rest room */
    last = row > 0.0f ? (int)ceilf((offset + view) / row) : count;
    first = first < 0 ? 0 : first > count ? count : first;
    last = last < first ? first : last > count ? count : last;
    if (first > 0) wgf_ui_spacer(row * (float)first);
    for (i = 0, at = items; i < last && at != NULL; i++) {
        const char *end = strchr(at, '\n');
        if (i >= first) {
            const int state = answered.state == WGF_UI_STATE_DISABLED ? WGF_UI_STATE_DISABLED
                              : i == selected                          ? WGF_UI_STATE_PRESSED
                              : i == over                              ? WGF_UI_STATE_HOVERED
                                                                       : WGF_UI_STATE_NORMAL;
            list_item(at, end != NULL ? (size_t)(end - at) : strlen(at), WGF_UI_WIDGET_LIST, state, i == selected, NULL);
        }
        at = end != NULL ? end + 1 : NULL;
    }
    if (last < count) wgf_ui_spacer(row * (float)(count - last));
    close_level(KIND_SCROLL);
    return selected;
}

/* ---- the popup layer ---------------------------------------------------------------- */

#define MODAL_Z 1000
#define POPUP_Z 2000      /* a dropdown's list: over a modal */
#define TOOLTIP_Z 3000
#define TOOLTIP_WAIT 0.5f /* seconds the pointer or the focus rests on a widget before its tooltip shows */

/* A popup `width` by `height` hung from `anchor`, as it was last laid out: below it, or above
 * when the screen has no room below and more above; its left at the anchor's (`centered`: its
 * center at the anchor's), moved across to stay on the screen. */
static void hang(Clay_ElementId anchor, float width, float height, bool centered, Clay_FloatingElementConfig *floating)
{
    const Clay_ElementData data = Clay_GetElementData(anchor);
    const float gap = ui.values[WGF_UI_VALUE_FOCUS_WIDTH];
    bool above = false;
    float shift = 0.0f;
    memset(floating, 0, sizeof(*floating));
    floating->attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID;
    floating->parentId = anchor.id;
    if (data.found) {
        const Clay_BoundingBox box = data.boundingBox;
        const float below = ui.height - (box.y + box.height);
        const float x = centered ? box.x + (box.width - width) * 0.5f : box.x;
        above = below < height + gap && box.y > below;
        if (x + width > ui.width) shift = ui.width - width - x;
        if (x + shift < 0.0f) shift = -x;
    }
    floating->attachPoints.parent = above ? (centered ? CLAY_ATTACH_POINT_CENTER_TOP : CLAY_ATTACH_POINT_LEFT_TOP)
                                          : (centered ? CLAY_ATTACH_POINT_CENTER_BOTTOM : CLAY_ATTACH_POINT_LEFT_BOTTOM);
    floating->attachPoints.element = above ? (centered ? CLAY_ATTACH_POINT_CENTER_BOTTOM : CLAY_ATTACH_POINT_LEFT_BOTTOM)
                                           : (centered ? CLAY_ATTACH_POINT_CENTER_TOP : CLAY_ATTACH_POINT_LEFT_TOP);
    floating->offset.x = shift;
    floating->offset.y = above ? -gap : gap;
}

static void draw_chevron(const piece_t *piece, Clay_BoundingBox box);
static void draw_popup(const piece_t *piece, Clay_BoundingBox box);

/* A dropdown's open list: its items, `selected` chosen and `hot` stepped to, hung from its face. */
static void dropdown_list(Clay_ElementId face, Clay_String name, const char *const *starts, const size_t *lengths,
                          int count, int selected)
{
    const Clay_ElementData laid = Clay_GetElementData(face);
    const Clay_ElementId list = Clay_GetElementIdWithIndex(name, CHOICES_MAX);
    const float inset = floorf(ui.values[WGF_UI_VALUE_CORNER_RADIUS] * 0.5f);
    const float padding = ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.5f;
    Clay_ElementDeclaration box;
    float width = 0.0f, height;
    int i;
    for (i = 0; i < count; i++) {
        const char *line = terminated(starts[i], (int32_t)lengths[i]);
        if (line != NULL) width = fmaxf(width, wgf_font_measure(ui.font, line, ui.values[WGF_UI_VALUE_TEXT_SIZE]).x);
    }
    width = fmaxf(width + padding * 2.0f + inset * 2.0f, laid.found ? laid.boundingBox.width : 0.0f);
    height = row_height() * (float)count + inset * 2.0f;
    memset(&box, 0, sizeof(box));
    box.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    box.layout.padding.left = box.layout.padding.right = box.layout.padding.top = box.layout.padding.bottom = pixels(inset);
    fixed(&box, width, height);
    hang(face, width, height, false, &box.floating);
    box.floating.zIndex = POPUP_Z;
    box.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
    box.custom.customData = add_piece(draw_popup, WGF_UI_WIDGET_POPUP, WGF_UI_STATE_NORMAL);
    Clay__OpenElementWithId(list);
    Clay__ConfigureOpenElement(box);
    add_solid(list.id);
    for (i = 0; i < count; i++) {
        const Clay_ElementId item = Clay_GetElementIdWithIndex(name, (uint32_t)i);
        const int state = i == selected ? WGF_UI_STATE_PRESSED : i == ui.popup.hot ? WGF_UI_STATE_HOVERED : WGF_UI_STATE_NORMAL;
        list_item(starts[i], lengths[i], WGF_UI_WIDGET_DROPDOWN, state, i == selected, &item);
    }
    Clay__CloseElement();
    ui.popup.list = list.id;
    ui.popup.face = face.id;
}

int wgf_ui_dropdown(const char *id, const char *labels, int selected)
{
    const char *starts[CHOICES_MAX];
    size_t lengths[CHOICES_MAX];
    const int count = lines_of(labels, starts, lengths);
    const float size = ui.values[WGF_UI_VALUE_TEXT_SIZE] * 0.75f; /* its chevron's */
    Clay_ElementId element;
    Clay_ElementDeclaration declaration, chevron;
    Clay_String name;
    answer_t answered;
    piece_t *piece;
    void *look;
    bool was_open, open;
    int i, over = -1;
    if (count == 0 || !widget_named(id, "wgf_ui_dropdown", &element, &name)) return selected;
    selected = clamp_index(selected, count);
    was_open = open = strcmp(ui.popup.owner, id) == 0;
    for (i = 0; open && i < count && over < 0; i++) {
        if (Clay_PointerOver(Clay_GetElementIdWithIndex(name, (uint32_t)i))) over = i;
    }
    answer(id, element, Clay_PointerOver(element), open ? ARROWS_ALL : ARROWS_NONE, false, &answered);
    if (answered.state == WGF_UI_STATE_DISABLED || !answered.focused) open = false;
    if (open) {
        const bool moved = ui.pointer_x != ui.popup.pointer[0] || ui.pointer_y != ui.popup.pointer[1];
        if (over >= 0 && (moved || ui.pointer_pressed)) ui.popup.hot = over;
        if (ui.keys.more_down != ui.keys.more_up) {
            ui.popup.hot = clamp_index(ui.popup.hot + (ui.keys.more_down ? 1 : -1), count);
            ui.focus_visible = true;
        }
        if (ui.pointer_pressed) ui.popup.item = over;
        if (ui.keys.cancel) {
            open = false;
        } else if (ui.keys.activate) { /* the item stepped to */
            selected = ui.popup.hot;
            open = false;
        } else if (ui.pointer_released && over >= 0 && over == ui.popup.item) {
            selected = over;
            open = false;
        } else if (answered.activated) { /* its face pressed and released again */
            open = false;
        }
    } else if (answered.activated && !was_open) {
        open = true;
        ui.popup.hot = selected;
        ui.popup.item = -1;
    }
    ui.popup.pointer[0] = ui.pointer_x;
    ui.popup.pointer[1] = ui.pointer_y;
    if (open) {
        snprintf(ui.popup.owner, sizeof(ui.popup.owner), "%s", id);
        ui.popup.seen = true;
    } else if (was_open) {
        ui.popup.owner[0] = '\0';
    }
    look = add_piece(draw_face, WGF_UI_WIDGET_DROPDOWN, answered.state);
    face_layout(&declaration, look);
    declaration.layout.sizing.width.type = CLAY__SIZING_TYPE_FIT;
    declaration.layout.sizing.width.size.minMax.min = ui.values[WGF_UI_VALUE_BUTTON_WIDTH];
    ring(&declaration, answered.focused);
    Clay__OpenElementWithId(element);
    Clay__ConfigureOpenElement(declaration);
    add_solid(element.id);
    text_slice(starts[selected], lengths[selected], 0.0f, dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], answered.state),
               CLAY_TEXT_WRAP_NONE, look);
    room(true, 0.0f);
    memset(&chevron, 0, sizeof(chevron));
    fixed(&chevron, size, size);
    chevron.custom.customData = add_piece(draw_chevron, WGF_UI_WIDGET_DROPDOWN, answered.state);
    piece = piece_at(chevron.custom.customData);
    if (piece != NULL) piece->on = open;
    Clay__OpenElement();
    Clay__ConfigureOpenElement(chevron);
    Clay__CloseElement();
    Clay__CloseElement();
    if (open) dropdown_list(element, name, starts, lengths, count, selected);
    return selected;
}

/* A modal's close: its panel, then the dimmed screen it is on. */
static void close_modal(level_t *level)
{
    (void)level;
    Clay__CloseElement();
    Clay__CloseElement();
    ui.modal.last = ui.focusable_count;
}

bool wgf_ui_begin_modal(const char *id)
{
    Clay_ElementId element, screen;
    Clay_ElementDeclaration dim_screen;
    Clay_String name;
    level_t *level;
    if (ui.modal.seen || !widget_named(id, "wgf_ui_begin_modal", &element, &name)) return false;
    if (ui.depth == DEPTH_MAX) {
        wgf_log_error("wgf_ui: more than %d boxes inside each other", DEPTH_MAX);
        return false;
    }
    screen = Clay_GetElementIdWithIndex(name, 0);
    memset(&dim_screen, 0, sizeof(dim_screen));
    dim_screen.layout.sizing.width.type = dim_screen.layout.sizing.height.type = CLAY__SIZING_TYPE_GROW;
    dim_screen.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    dim_screen.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    dim_screen.backgroundColor = clay_color(ui.colors[WGF_UI_COLOR_DIM]);
    dim_screen.floating.attachTo = CLAY_ATTACH_TO_ROOT; /* capturing the pointer: nothing under it answers */
    dim_screen.floating.zIndex = MODAL_Z;
    Clay__OpenElementWithId(screen);
    Clay__ConfigureOpenElement(dim_screen);
    add_solid(screen.id);
    Clay__OpenElementWithId(element);
    level = &ui.levels[ui.depth++];
    memset(level, 0, sizeof(*level));
    level->kind = KIND_MODAL;
    level->declaration.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    panel_layout(level, WGF_UI_WIDGET_PANEL, draw_panel);
    level->close = close_modal;
    ui.modal.seen = true;
    ui.modal.first = ui.modal.last = ui.focusable_count;
    return true;
}

bool wgf_ui_end_modal(void)
{
    return close_level(KIND_MODAL);
}

bool wgf_ui_tooltip(const char *text)
{
    Clay_ElementDeclaration box;
    Clay_ElementId anchor;
    wgf_vec2_t size;
    float across_pad, down_pad;
    bool pointed;
    if (text == NULL || text[0] == '\0') return false;
    if (!ui.begun) {
        wgf_log_error("wgf_ui: wgf_ui_tooltip outside wgf_ui_begin and wgf_ui_end");
        return false;
    }
    if (ui.last.id[0] == '\0') return false;
    pointed = (ui.last.over && !ui.pointer_down) || (ui.focus_visible && strcmp(ui.focus, ui.last.id) == 0);
    if (!pointed) return true;
    if (strcmp(ui.tip.id, ui.last.id) != 0) { /* waits from the next frame, unless another is already */
        if (ui.tip.next[0] == '\0') snprintf(ui.tip.next, sizeof(ui.tip.next), "%s", ui.last.id);
        return true;
    }
    ui.tip.seen = true;
    if (ui.clock - ui.tip.since < TOOLTIP_WAIT) return true;
    size = wgf_font_measure(ui.font, text, ui.values[WGF_UI_VALUE_TEXT_SIZE]);
    across_pad = ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.5f;
    down_pad = ui.values[WGF_UI_VALUE_BUTTON_PADDING] * 0.25f;
    memset(&box, 0, sizeof(box));
    memset(&anchor, 0, sizeof(anchor));
    anchor.id = ui.last.element;
    box.layout.padding.left = box.layout.padding.right = pixels(across_pad);
    box.layout.padding.top = box.layout.padding.bottom = pixels(down_pad);
    hang(anchor, size.x + across_pad * 2.0f, size.y + down_pad * 2.0f, true, &box.floating);
    box.floating.zIndex = TOOLTIP_Z;
    box.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
    box.cornerRadius = corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS] * 0.5f);
    box.custom.customData = add_piece(draw_popup, WGF_UI_WIDGET_POPUP, WGF_UI_STATE_NORMAL);
    configure();
    Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("wgf-ui-tooltip"))); /* one at a time: a test finds it */
    Clay__ConfigureOpenElement(box);
    text_element(text, 0.0f, ui.colors[WGF_UI_COLOR_TEXT], CLAY_TEXT_WRAP_NEWLINES, NULL);
    Clay__CloseElement();
    return true;
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
    const bool pad_up = any_pad(WGF_GAMEPAD_BUTTON_DPAD_UP), pad_down = any_pad(WGF_GAMEPAD_BUTTON_DPAD_DOWN);
    const bool pad_left = any_pad(WGF_GAMEPAD_BUTTON_DPAD_LEFT), pad_right = any_pad(WGF_GAMEPAD_BUTTON_DPAD_RIGHT);
    ui.pointer_x = pointer.x - ui.origin_x; /* in the layout's coordinates, which start at the visible area */
    ui.pointer_y = pointer.y - ui.origin_y;
    ui.pointer_down = wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT);
    ui.pointer_pressed = wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT);
    ui.pointer_released = wgf_mouse_is_released(WGF_MOUSE_BUTTON_LEFT);
    ui.keys.activate = wgf_keyboard_is_pressed(WGF_KEY_ENTER) || wgf_keyboard_is_pressed(WGF_KEY_KP_ENTER) ||
                       wgf_keyboard_is_pressed(WGF_KEY_SPACE) || any_pad(WGF_GAMEPAD_BUTTON_SOUTH);
    ui.keys.cancel = wgf_keyboard_is_pressed(WGF_KEY_ESCAPE) || any_pad(WGF_GAMEPAD_BUTTON_EAST);
    ui.keys.tab_next = tab && !shift;
    ui.keys.tab_previous = tab && shift;
    ui.keys.up = wgf_keyboard_is_pressed(WGF_KEY_UP) || pad_up;
    ui.keys.down = wgf_keyboard_is_pressed(WGF_KEY_DOWN) || pad_down;
    ui.keys.left = wgf_keyboard_is_pressed(WGF_KEY_LEFT) || pad_left;
    ui.keys.right = wgf_keyboard_is_pressed(WGF_KEY_RIGHT) || pad_right;
    ui.keys.more_up = wgf_keyboard_is_repeated(WGF_KEY_UP) || pad_up;
    ui.keys.more_down = wgf_keyboard_is_repeated(WGF_KEY_DOWN) || pad_down;
    ui.keys.more_left = wgf_keyboard_is_repeated(WGF_KEY_LEFT) || pad_left;
    ui.keys.more_right = wgf_keyboard_is_repeated(WGF_KEY_RIGHT) || pad_right;
    ui.keys.page_up = wgf_keyboard_is_repeated(WGF_KEY_PAGE_UP);
    ui.keys.page_down = wgf_keyboard_is_repeated(WGF_KEY_PAGE_DOWN);
}

bool wgf_ui_begin(void)
{
    level_t *root;
    Clay_Dimensions dimensions;
    Clay_Vector2 pointer;
    if (ui.begun) {
        wgf_log_error("wgf_ui_begin: a UI is already begun");
        return false;
    }
    if (!wgf_gfx_priv_is_in_frame() && !wgf_gfx_priv_render_is_lost()) { /* lost: laid out, drawing nothing */
        wgf_log_error("wgf_ui_begin: outside a frame (call it in the frame callback)");
        return false;
    }
    if (!start()) return false;
    for (ui.block = ui.blocks; ui.block != NULL; ui.block = ui.block->next) ui.block->used = 0;
    ui.block = ui.blocks;
    ui.focusable_count = ui.solid_count = ui.piece_count = 0;
    ui.focus_seen = false;
    ui.disabled = false;
    ui.hovering = false;
    ui.last.id[0] = '\0';
    ui.popup.seen = ui.modal.seen = false;
    ui.clock += (float)wgf_platform_priv_get_frame_duration();
    wgf_gfx_priv_render_get_visible(&ui.origin_x, &ui.origin_y, &dimensions.width, &dimensions.height);
    ui.width = dimensions.width;
    ui.height = dimensions.height;
    read_input();
    Clay_SetLayoutDimensions(dimensions);
    pointer.x = ui.pointer_x;
    pointer.y = ui.pointer_y;
    Clay_SetPointerState(pointer, ui.pointer_down);
    if (ui.popup.owner[0] != '\0' && ui.pointer_pressed) { /* a press off an open dropdown closes it, and is eaten */
        Clay_ElementId list, face;
        memset(&list, 0, sizeof(list));
        memset(&face, 0, sizeof(face));
        list.id = ui.popup.list;
        face.id = ui.popup.face;
        if (!Clay_PointerOver(list) && !Clay_PointerOver(face)) {
            ui.popup.owner[0] = '\0';
            ui.eaten = true;
        }
    }
    if (ui.eaten) {
        if (ui.pointer_released) ui.eaten = false;
        ui.pointer_pressed = ui.pointer_down = ui.pointer_released = false;
    }
    ui.scroll.reveal = ui.scroll.reveal_next;
    ui.scroll.reveal_next = false;
    if (scroll_frame != NULL) scroll_frame(&ui.scroll, ui.pointer_pressed, ui.pointer_down);
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

/* The focusable nearest the one at `from` the way (dx, dy) points (a unit along an axis), as
 * this frame laid them out: of those whose centers are that way, the least distance along
 * it plus twice the distance across (helix-legacy's); -1 for none. */
static int nearest(int from, float dx, float dy, int lo, int hi)
{
    Clay_ElementId id;
    Clay_ElementData a, b;
    float best = 0.0f;
    int i, found = -1;
    memset(&id, 0, sizeof(id));
    id.id = ui.focusables[from].element;
    a = Clay_GetElementData(id);
    if (!a.found) return -1;
    for (i = lo; i < hi; i++) {
        float x, y, along, across, score;
        if (i == from) continue;
        id.id = ui.focusables[i].element;
        b = Clay_GetElementData(id);
        if (!b.found) continue;
        x = (b.boundingBox.x + b.boundingBox.width * 0.5f) - (a.boundingBox.x + a.boundingBox.width * 0.5f);
        y = (b.boundingBox.y + b.boundingBox.height * 0.5f) - (a.boundingBox.y + a.boundingBox.height * 0.5f);
        along = x * dx + y * dy;
        across = fabsf(x * dy - y * dx);
        if (along < 1.0f) continue;
        score = along + 2.0f * across;
        if (found < 0 || score < best) {
            best = score;
            found = i;
        }
    }
    return found;
}

/* The frame's widget named `id`; -1 for none. */
static int focusable_named(const char *id)
{
    int i;
    for (i = 0; i < ui.focusable_count; i++) {
        if (strcmp(ui.focusables[i].id, id) == 0) return i;
    }
    return -1;
}

/* The focus moved: by an arrow the focused widget doesn't keep, to the nearest widget that
 * way, else through the frame's widgets in order (Down and Right the next, Up and Left the
 * one before), wrapping; by Tab and Shift+Tab, in order. The first press puts it on the
 * first or the last. While a modal is given, its widgets alone: the focus put on its first
 * as it opens, and given back as it closes. */
static void navigate(void)
{
    int at, arrows, to, lo = 0, hi = ui.focusable_count;
    float dx = 0.0f, dy = 0.0f;
    bool next, previous;
    if (!ui.focus_seen && !ui.focus_fresh) ui.focus[0] = '\0';
    ui.focus_fresh = false;
    at = ui.focus[0] != '\0' ? focusable_named(ui.focus) : -1;
    if (ui.modal.seen) {
        lo = ui.modal.first;
        hi = ui.modal.last;
        if (!ui.modal.was) snprintf(ui.modal.before, sizeof(ui.modal.before), "%s", ui.focus);
        if (at < lo || at >= hi) {
            at = lo < hi ? lo : -1;
            snprintf(ui.focus, sizeof(ui.focus), "%s", at >= 0 ? ui.focusables[at].id : "");
            ui.scroll.reveal_next = true;
        }
    } else if (ui.modal.was && ui.modal.before[0] != '\0') {
        to = focusable_named(ui.modal.before);
        if (to >= 0) {
            snprintf(ui.focus, sizeof(ui.focus), "%s", ui.modal.before);
            at = to;
        }
    }
    ui.modal.was = ui.modal.seen;
    arrows = at >= 0 ? ui.focusables[at].arrows : ARROWS_NONE;
    if (arrows != ARROWS_DOWN && arrows != ARROWS_ALL && ui.keys.down != ui.keys.up) dy = ui.keys.down ? 1.0f : -1.0f;
    else if (arrows != ARROWS_ACROSS && arrows != ARROWS_ALL && ui.keys.right != ui.keys.left)
        dx = ui.keys.right ? 1.0f : -1.0f;
    next = ui.keys.tab_next || dx > 0.0f || dy > 0.0f;
    previous = ui.keys.tab_previous || dx < 0.0f || dy < 0.0f;
    if (next == previous || hi <= lo) return;
    if (at < 0) {
        to = next ? lo : hi - 1;
    } else {
        to = ui.keys.tab_next || ui.keys.tab_previous ? -1 : nearest(at, dx, dy, lo, hi);
        if (to < 0) to = lo + (at - lo + (next ? 1 : hi - lo - 1)) % (hi - lo);
    }
    snprintf(ui.focus, sizeof(ui.focus), "%s", ui.focusables[to].id);
    ui.focus_visible = true;
    ui.scroll.reveal_next = true;
}

static void capture(void)
{
    bool pointer = ((ui.pressed[0] != '\0' || ui.scroll.held != 0) && ui.pointer_down) || ui.hovering;
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

/* ---- drawing ------------------------------------------------------------------------ */

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

/* A rectangle in a color, its corners rounded by `radius`. */
static void flat(Clay_BoundingBox box, float radius, wgf_color_t color)
{
    float points[8 * (SEGMENTS + 1)];
    if (box.width <= 0.0f || box.height <= 0.0f) return;
    wgf_draw_polygon(points, rounded(box, corners(radius), 0.0f, points), faded(color));
}

/* A widget's part drawn from its skin; false, drawing nothing, for one without. */
static bool skinned(int widget, int which, int state, Clay_BoundingBox box)
{
    return skins != NULL && widget >= 0 &&
           skins->draw(widget, which, state, box.x, box.y, box.width, box.height, 1.0f - ui.fade);
}

/* A widget's part painted from its skin, else in `color`, rounded by `radius`: but for a
 * widget whose base is art, whose other parts are drawn from art alone (a skinned slider
 * with no fill skin has none, as helix-raylib's). */
static void paint(int widget, int which, int state, Clay_BoundingBox box, wgf_color_t color, float radius)
{
    if (skinned(widget, which, state, box)) return;
    if (which != WGF_UI_PART_BASE && skins != NULL && widget >= 0 && skins->has(widget, WGF_UI_PART_BASE)) return;
    flat(box, radius, dim(color, state));
}

/* A face's color in its state. */
static wgf_color_t face_color(int state)
{
    return ui.colors[state == WGF_UI_STATE_PRESSED   ? WGF_UI_COLOR_BUTTON_PRESSED
                     : state == WGF_UI_STATE_HOVERED ? WGF_UI_COLOR_BUTTON_HOVERED
                                                     : WGF_UI_COLOR_BUTTON];
}

static void draw_panel(const piece_t *piece, Clay_BoundingBox box)
{
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, ui.colors[WGF_UI_COLOR_PANEL],
         ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
}

static void draw_group(const piece_t *piece, Clay_BoundingBox box)
{
    float points[8 * (SEGMENTS + 1)];
    const float line = ui.values[WGF_UI_VALUE_LINE_WIDTH];
    if (skinned(piece->widget, WGF_UI_PART_BASE, piece->state, box) || line <= 0.0f) return;
    wgf_draw_polyline(points, rounded(box, corners(ui.values[WGF_UI_VALUE_CORNER_RADIUS]), line * 0.5f, points), true,
                      line, faded(ui.colors[WGF_UI_COLOR_SEPARATOR]));
}

static void draw_face(const piece_t *piece, Clay_BoundingBox box)
{
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, face_color(piece->state),
         ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
}

static void draw_number_box(const piece_t *piece, Clay_BoundingBox box)
{
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, ui.colors[WGF_UI_COLOR_BAR],
          ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
}

/* A texture stretched over the box, dimmed when disabled. */
static void stretched(wgf_texture_t texture, Clay_BoundingBox box, int state)
{
    wgf_draw_texture_nine_slice(texture, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, box.x, box.y, box.width,
                                box.height, faded(dim(WGF_COLOR_WHITE, state)));
}

static void draw_image(const piece_t *piece, Clay_BoundingBox box)
{
    if (skins != NULL && piece->widget >= 0) { /* a face's icon moves with its text */
        float x = 0.0f, y = 0.0f;
        skins->text_offset(piece->widget, piece->state, &x, &y);
        box.x += x;
        box.y += y;
    }
    stretched(piece->texture, box, piece->state);
}

static void draw_image_button(const piece_t *piece, Clay_BoundingBox box)
{
    stretched(piece->state == WGF_UI_STATE_PRESSED && piece->pressed != 0 ? piece->pressed : piece->texture, box,
              piece->state);
}

static void draw_check(const piece_t *piece, Clay_BoundingBox box)
{
    const float inset = box.width * 0.25f;
    Clay_BoundingBox check = box;
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, face_color(piece->state),
         fminf(ui.values[WGF_UI_VALUE_CORNER_RADIUS], box.width * 0.25f));
    if (!piece->on || skinned(piece->widget, WGF_UI_PART_FILL, piece->state, box)) return;
    if (skins != NULL && skins->has(piece->widget, WGF_UI_PART_BASE)) return; /* art alone, as paint */
    check.x += inset;
    check.y += inset;
    check.width -= inset * 2.0f;
    check.height -= inset * 2.0f;
    flat(check, check.width * 0.25f, dim(ui.colors[WGF_UI_COLOR_BAR_FILL], piece->state));
}

/* The part of `track` that `amount` (0 to 1) of it covers, from its widget's fill end. */
static Clay_BoundingBox covered(Clay_BoundingBox track, float amount, int from)
{
    Clay_BoundingBox covering = track;
    switch (from) {
    case WGF_UI_FROM_RIGHT:
        covering.width = track.width * amount;
        covering.x = track.x + track.width - covering.width;
        break;
    case WGF_UI_FROM_TOP: covering.height = track.height * amount; break;
    case WGF_UI_FROM_BOTTOM:
        covering.height = track.height * amount;
        covering.y = track.y + track.height - covering.height;
        break;
    default: covering.width = track.width * amount; break;
    }
    return covering;
}

/* A track's fill, `amount` of it: stretched over the part covered, or drawn over the whole
 * track and clipped to that part; its edge, a skin's alone, at its front. */
static void fill(int widget, int state, Clay_BoundingBox track, float amount, float radius)
{
    const int from = ui.from[widget];
    const Clay_BoundingBox covering = covered(track, amount, from);
    if (amount <= 0.0f) return;
    if (ui.fill[widget] == WGF_UI_FILL_CLIPPED) {
        wgf_render_push_clip(covering.x, covering.y, covering.width, covering.height);
        paint(widget, WGF_UI_PART_FILL, state, track, ui.colors[WGF_UI_COLOR_BAR_FILL], radius);
        wgf_render_pop_clip();
    } else {
        paint(widget, WGF_UI_PART_FILL, state, covering, ui.colors[WGF_UI_COLOR_BAR_FILL], radius);
    }
    if (skins != NULL) {
        const float x = from == WGF_UI_FROM_LEFT    ? covering.x + covering.width
                        : from == WGF_UI_FROM_RIGHT ? covering.x
                                                    : covering.x + covering.width * 0.5f;
        const float y = from == WGF_UI_FROM_TOP      ? covering.y + covering.height
                        : from == WGF_UI_FROM_BOTTOM ? covering.y
                                                     : covering.y + covering.height * 0.5f;
        skins->draw_at(widget, WGF_UI_PART_EDGE, state, x, y, 1.0f - ui.fade);
    }
}

static void draw_progress(const piece_t *piece, Clay_BoundingBox box)
{
    const float thickness = across(piece->widget) ? box.height : box.width;
    const float radius = fminf(ui.values[WGF_UI_VALUE_CORNER_RADIUS], thickness * 0.5f);
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, ui.colors[WGF_UI_COLOR_BAR], radius);
    fill(piece->widget, piece->state, box, piece->value, radius);
}

static void draw_slider(const piece_t *piece, Clay_BoundingBox box)
{
    const int from = ui.from[piece->widget];
    const bool row = across(piece->widget);
    const float handle = ui.values[WGF_UI_VALUE_HANDLE_SIZE];
    const float length = row ? box.width : box.height;
    const float at = handle * 0.5f + piece->value * fmaxf(0.0f, length - handle); /* the handle's center, from the start */
    Clay_BoundingBox track = box, knob = box;
    float radius;
    if (row) {
        track.height = fminf(ui.values[WGF_UI_VALUE_SLIDER_HEIGHT], box.height);
        track.y = box.y + (box.height - track.height) * 0.5f;
        knob.width = handle;
        knob.x = from == WGF_UI_FROM_LEFT ? box.x + at - handle * 0.5f : box.x + box.width - at - handle * 0.5f;
    } else {
        track.width = fminf(ui.values[WGF_UI_VALUE_SLIDER_HEIGHT], box.width);
        track.x = box.x + (box.width - track.width) * 0.5f;
        knob.height = handle;
        knob.y = from == WGF_UI_FROM_TOP ? box.y + at - handle * 0.5f : box.y + box.height - at - handle * 0.5f;
    }
    radius = fminf(ui.values[WGF_UI_VALUE_CORNER_RADIUS], (row ? track.height : track.width) * 0.5f);
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, track, ui.colors[WGF_UI_COLOR_BAR], radius);
    fill(piece->widget, piece->state, track, length > 0.0f ? at / length : 0.0f, radius);
    paint(piece->widget, WGF_UI_PART_HANDLE, piece->state, knob, ui.colors[WGF_UI_COLOR_HANDLE],
         fminf(knob.width, knob.height) * 0.5f);
}

static void draw_list_box(const piece_t *piece, Clay_BoundingBox box)
{
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, ui.colors[WGF_UI_COLOR_BAR],
          ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
}

/* A list's item: the chosen one in the fill's color, one under the pointer as a hovered
 * button, the rest the box's (a skin draws each). */
static void draw_list_item(const piece_t *piece, Clay_BoundingBox box)
{
    if (skinned(piece->widget, WGF_UI_PART_ITEM, piece->state, box)) return;
    if (!piece->on && piece->state != WGF_UI_STATE_HOVERED) return;
    paint(piece->widget, WGF_UI_PART_ITEM, piece->state, box,
          ui.colors[piece->on ? WGF_UI_COLOR_BAR_FILL : WGF_UI_COLOR_BUTTON_HOVERED],
          ui.values[WGF_UI_VALUE_CORNER_RADIUS] * 0.5f);
}

/* A scroll bar: its track, and its thumb as long as the share in view and as far along as
 * the scroll, in the handle's color (half its alpha but under the pointer or held). */
static void draw_scroll_bar(const piece_t *piece, Clay_BoundingBox box)
{
    const float track = piece->on ? box.height : box.width, radius = (piece->on ? box.width : box.height) * 0.5f;
    const wgf_color_t handle = ui.colors[WGF_UI_COLOR_HANDLE];
    Clay_BoundingBox thumb = box;
    float start, length;
    thumb_of(track, piece->size, piece->value, &start, &length);
    if (piece->on) {
        thumb.y += start;
        thumb.height = length;
    } else {
        thumb.x += start;
        thumb.width = length;
    }
    paint(piece->widget, WGF_UI_PART_BASE, piece->state == WGF_UI_STATE_DISABLED ? piece->state : WGF_UI_STATE_NORMAL,
          box, ui.colors[WGF_UI_COLOR_BAR], radius);
    paint(piece->widget, WGF_UI_PART_HANDLE, piece->state, thumb,
          piece->state == WGF_UI_STATE_NORMAL ? wgf_color_with_alpha(handle, wgf_color_get_alpha(handle) / 2) : handle,
          radius);
}

static void draw_popup(const piece_t *piece, Clay_BoundingBox box)
{
    paint(piece->widget, WGF_UI_PART_BASE, piece->state, box, ui.colors[WGF_UI_COLOR_POPUP],
          ui.values[WGF_UI_VALUE_CORNER_RADIUS]);
}

/* A dropdown's chevron: a triangle in its text's color, pointing down, up while open. */
static void draw_chevron(const piece_t *piece, Clay_BoundingBox box)
{
    const float top = box.y + box.height * 0.25f, bottom = box.y + box.height * 0.75f;
    const float tip = piece->on ? top : bottom, base = piece->on ? bottom : top;
    const float points[6] = {box.x, base, box.x + box.width * 0.5f, tip, box.x + box.width, base};
    wgf_draw_polygon(points, 6, faded(dim(ui.colors[WGF_UI_COLOR_BUTTON_TEXT], piece->state)));
}

static void draw(Clay_RenderCommandArray commands)
{
    float points[8 * (SEGMENTS + 1)];
    int i;
    for (i = 0; i < commands.length; i++) {
        const Clay_RenderCommand *command = Clay_RenderCommandArray_Get(&commands, i);
        Clay_BoundingBox box = command->boundingBox;
        box.x += ui.origin_x; /* drawn where the visible area is */
        box.y += ui.origin_y;
        switch (command->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const Clay_RectangleRenderData *rect = &command->renderData.rectangle;
            wgf_draw_polygon(points, rounded(box, rect->cornerRadius, 0.0f, points), faded(from_clay(rect->backgroundColor)));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
            const piece_t *piece = piece_at(command->renderData.custom.customData);
            if (piece != NULL) piece->draw(piece, box);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const Clay_BorderRenderData *border = &command->renderData.border;
            const float width = (float)border->width.left;
            if (width > 0.0f) {
                wgf_draw_polyline(points, rounded(box, border->cornerRadius, width * 0.5f, points), true, width,
                                  faded(from_clay(border->color)));
            }
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const Clay_TextRenderData *text = &command->renderData.text;
            const piece_t *piece = piece_at(command->userData); /* a face's: moved by its text offset */
            const char *line = terminated(text->stringContents.chars, text->stringContents.length);
            if (piece != NULL && skins != NULL) {
                float x = 0.0f, y = 0.0f;
                skins->text_offset(piece->widget, piece->state, &x, &y);
                box.x += x;
                box.y += y;
            }
            if (line != NULL)
                wgf_draw_text(ui.font, line, box.x, box.y, (float)text->fontSize, faded(from_clay(text->textColor)));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: wgf_render_push_clip(box.x, box.y, box.width, box.height); break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: wgf_render_pop_clip(); break;
        default: break; /* images and overlays: libwgf's UI makes none */
        }
    }
}

bool wgf_ui_end(void)
{
    Clay_RenderCommandArray commands;
    bool balanced;
    if (!ui.begun) {
        wgf_log_error("wgf_ui_end: no UI begun");
        return false;
    }
    balanced = ui.depth == 1;
    if (!balanced) wgf_log_error("wgf_ui_end: %d box(es) or panel(s) left open, closed", ui.depth - 1);
    while (ui.depth > 0) {
        configure();
        close_element(&ui.levels[ui.depth - 1]);
        ui.depth--;
    }
    ui.begun = false;
    ui.ended = true;
    commands = Clay_EndLayout((float)wgf_platform_priv_get_frame_duration());
    if (ui.fade < 1.0f) draw(commands);
    if (!ui.popup.seen) ui.popup.owner[0] = '\0'; /* its dropdown not given */
    if (!ui.tip.seen) { /* the tooltip pointed at is let go of: the next one waits */
        snprintf(ui.tip.id, sizeof(ui.tip.id), "%s", ui.tip.next);
        ui.tip.since = ui.clock;
    }
    ui.tip.seen = false;
    ui.tip.next[0] = '\0';
    navigate();
    capture();
    wgf_probe_set_text("ui.focus", ui.focus); /* what an autopilot can expect on: the focused widget's id */
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
    *x = data.boundingBox.x + ui.origin_x;
    *y = data.boundingBox.y + ui.origin_y;
    *width = data.boundingBox.width;
    *height = data.boundingBox.height;
    return true;
}
