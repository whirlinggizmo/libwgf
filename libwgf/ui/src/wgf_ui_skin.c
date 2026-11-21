#include "wgf_ui.h"

#include <math.h>
#include <string.h>

#include "wgf_core_handle_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_draw.h"
#include "wgf_resource.h"
#include "wgf_ui_skin_priv.h"

/* The Clay UI's skins (wgf_ui.h): each widget's parts in each state drawn from a game's
 * art, helix-raylib's gui skins (its RGuiSkins). A file of its own, reached through the
 * hooks its first call sets (wgf_ui_skin_priv.h): a UI drawn in its colors links none of
 * it. */

#define WIDGETS 7 /* wgf_ui_widget_t's */
#define PARTS 4   /* wgf_ui_part_t's */
#define STATES 4  /* wgf_ui_state_t's */

typedef struct skin_t {
    wgf_texture_t texture; /* 0: none */
    float source[4];       /* x, y, width, height: 0 wide or high, the whole texture */
    float borders[4];      /* left, top, right, bottom, in its texels */
    wgf_color_t tint;      /* 0: white, not set */
} skin_t;

static skin_t skins[WIDGETS][PARTS][STATES];
static float offsets[WIDGETS][STATES][2];

static bool valid(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state)
{
    return (int)widget >= 0 && (int)widget < WIDGETS && (int)part >= 0 && (int)part < PARTS && (int)state >= 0 &&
           (int)state < STATES;
}

/* The skin a part draws in a state: its own, else the normal state's (dim: whether it
 * stands in for a disabled one); NULL for none. */
static const skin_t *skin_of(int widget, int part, int state, bool *dim)
{
    const skin_t *skin = &skins[widget][part][state];
    *dim = false;
    if (skin->texture != 0) return skin;
    skin = &skins[widget][part][WGF_UI_STATE_NORMAL];
    *dim = state == WGF_UI_STATE_DISABLED;
    return skin->texture != 0 ? skin : NULL;
}

static wgf_color_t tint_of(const skin_t *skin, bool dim)
{
    const wgf_color_t tint = skin->tint != 0 ? skin->tint : WGF_COLOR_WHITE;
    return dim ? wgf_color_with_alpha(tint, wgf_color_get_alpha(tint) / 2) : tint;
}

static bool draw(int widget, int part, int state, float x, float y, float width, float height)
{
    bool dim;
    const skin_t *skin = skin_of(widget, part, state, &dim);
    if (skin == NULL) return false;
    wgf_draw_texture_nine_slice(skin->texture, skin->source[0], skin->source[1], skin->source[2], skin->source[3],
                                skin->borders[0], skin->borders[1], skin->borders[2], skin->borders[3], x, y, width,
                                height, tint_of(skin, dim));
    return true;
}

static bool draw_at(int widget, int part, int state, float x, float y)
{
    bool dim;
    const skin_t *skin = skin_of(widget, part, state, &dim);
    float width, height;
    if (skin == NULL) return false;
    width = skin->source[2] > 0.0f && skin->source[3] > 0.0f ? skin->source[2] : (float)wgf_texture_get_width(skin->texture);
    height = skin->source[2] > 0.0f && skin->source[3] > 0.0f ? skin->source[3] : (float)wgf_texture_get_height(skin->texture);
    return draw(widget, part, state, x - width * 0.5f, y - height * 0.5f, width, height);
}

static bool has(int widget, int part)
{
    int s;
    for (s = 0; s < STATES; s++) {
        if (skins[widget][part][s].texture != 0) return true;
    }
    return false;
}

static void text_offset(int widget, int state, float *x, float *y)
{
    *x = offsets[widget][state][0];
    *y = offsets[widget][state][1];
}

static void reset(void)
{
    int w, p, s;
    for (w = 0; w < WIDGETS; w++) {
        for (p = 0; p < PARTS; p++) {
            for (s = 0; s < STATES; s++) {
                if (skins[w][p][s].texture != 0) wgf_resource_release(skins[w][p][s].texture);
            }
        }
    }
    memset(skins, 0, sizeof(skins));
    memset(offsets, 0, sizeof(offsets));
}

static const wgf_ui_priv_skins_t hooks = {draw, draw_at, has, text_offset, reset};

static void install(void)
{
    wgf_ui_priv_set_skins(&hooks);
}

static bool finite_all(const float *values, int count, bool at_least_zero)
{
    int i;
    for (i = 0; i < count; i++) {
        if (!isfinite(values[i]) || (at_least_zero && values[i] < 0.0f)) return false;
    }
    return true;
}

bool wgf_ui_set_skin(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_texture_t texture,
                     float source_x, float source_y, float source_width, float source_height)
{
    const float source[4] = {source_x, source_y, source_width, source_height};
    skin_t *skin;
    if (!valid(widget, part, state) || !finite_all(source, 4, false)) return false;
    if (texture != 0 && (WGF_CORE_PRIV_HANDLE_KIND(texture) != WGF_CORE_PRIV_HANDLE_KIND_TEXTURE ||
                         wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_NONE)) {
        return false;
    }
    install();
    skin = &skins[widget][part][state];
    if (texture != 0) wgf_core_priv_resource_retain(texture); /* before the release, in case it is the same */
    if (skin->texture != 0) wgf_resource_release(skin->texture);
    skin->texture = texture;
    memcpy(skin->source, source, sizeof(source));
    return true;
}

wgf_texture_t wgf_ui_get_skin(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state)
{
    return valid(widget, part, state) ? skins[widget][part][state].texture : 0;
}

wgf_vec4_t wgf_ui_get_skin_source(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state)
{
    const float *s = valid(widget, part, state) ? skins[widget][part][state].source : NULL;
    return s != NULL ? wgf_vec4_make(s[0], s[1], s[2], s[3]) : wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
}

bool wgf_ui_set_skin_borders(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, float left, float top,
                             float right, float bottom)
{
    const float borders[4] = {left, top, right, bottom};
    if (!valid(widget, part, state) || !finite_all(borders, 4, true)) return false;
    install();
    memcpy(skins[widget][part][state].borders, borders, sizeof(borders));
    return true;
}

wgf_vec4_t wgf_ui_get_skin_borders(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state)
{
    const float *b = valid(widget, part, state) ? skins[widget][part][state].borders : NULL;
    return b != NULL ? wgf_vec4_make(b[0], b[1], b[2], b[3]) : wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
}

bool wgf_ui_set_skin_tint(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_color_t tint)
{
    if (!valid(widget, part, state)) return false;
    install();
    skins[widget][part][state].tint = tint;
    return true;
}

wgf_color_t wgf_ui_get_skin_tint(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state)
{
    if (!valid(widget, part, state)) return 0;
    return skins[widget][part][state].tint != 0 ? skins[widget][part][state].tint : WGF_COLOR_WHITE;
}

bool wgf_ui_set_text_offset(wgf_ui_widget_t widget, wgf_ui_state_t state, float x, float y)
{
    if (!valid(widget, WGF_UI_PART_BASE, state) || !isfinite(x) || !isfinite(y)) return false;
    install();
    offsets[widget][state][0] = x;
    offsets[widget][state][1] = y;
    return true;
}

wgf_vec2_t wgf_ui_get_text_offset(wgf_ui_widget_t widget, wgf_ui_state_t state)
{
    if (!valid(widget, WGF_UI_PART_BASE, state)) return wgf_vec2_make(0.0f, 0.0f);
    return wgf_vec2_make(offsets[widget][state][0], offsets[widget][state][1]);
}

/* ---- before skins: a part by its color --------------------------------------------- */

/* The skin a style color's art is: its widget's part in a state; false for a color that
 * isn't one. */
static bool skin_by_color(wgf_ui_color_t which, wgf_ui_widget_t *widget, wgf_ui_part_t *part, wgf_ui_state_t *state)
{
    *part = WGF_UI_PART_BASE;
    *state = WGF_UI_STATE_NORMAL;
    switch (which) {
        case WGF_UI_COLOR_PANEL: *widget = WGF_UI_WIDGET_PANEL; return true;
        case WGF_UI_COLOR_BUTTON: *widget = WGF_UI_WIDGET_BUTTON; return true;
        case WGF_UI_COLOR_BUTTON_HOVERED:
            *widget = WGF_UI_WIDGET_BUTTON;
            *state = WGF_UI_STATE_HOVERED;
            return true;
        case WGF_UI_COLOR_BUTTON_PRESSED:
            *widget = WGF_UI_WIDGET_BUTTON;
            *state = WGF_UI_STATE_PRESSED;
            return true;
        case WGF_UI_COLOR_BAR: *widget = WGF_UI_WIDGET_PROGRESS; return true;
        case WGF_UI_COLOR_BAR_FILL:
            *widget = WGF_UI_WIDGET_PROGRESS;
            *part = WGF_UI_PART_FILL;
            return true;
        default: return false;
    }
}

bool wgf_ui_set_style_texture(wgf_ui_color_t which, wgf_texture_t texture, float left, float top, float right,
                              float bottom)
{
    const float borders[4] = {left, top, right, bottom};
    wgf_ui_widget_t widget;
    wgf_ui_part_t part;
    wgf_ui_state_t state;
    if (!skin_by_color(which, &widget, &part, &state) || !finite_all(borders, 4, true)) return false;
    return wgf_ui_set_skin(widget, part, state, texture, 0.0f, 0.0f, 0.0f, 0.0f) &&
           wgf_ui_set_skin_borders(widget, part, state, left, top, right, bottom) &&
           wgf_ui_set_skin_tint(widget, part, state, 0);
}

wgf_texture_t wgf_ui_get_style_texture(wgf_ui_color_t which)
{
    wgf_ui_widget_t widget;
    wgf_ui_part_t part;
    wgf_ui_state_t state;
    return skin_by_color(which, &widget, &part, &state) ? wgf_ui_get_skin(widget, part, state) : 0;
}
