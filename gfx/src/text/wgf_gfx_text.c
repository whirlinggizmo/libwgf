#include "wgf_text.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "node/wgf_gfx_node_priv.h"
#include "text/wgf_gfx_font_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_font.h"
#include "wgf_resource.h"

/* Text nodes: a string in a font, laid out and drawn by its canvas each frame, as
 * libwgt's in a canvas. */

static wgf_gfx_priv_node_t *text_of(wgf_node_t text)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(text);
    return node_ptr != NULL && node_ptr->type == WGF_NODE_TYPE_TEXT ? node_ptr : NULL;
}

static bool is_font(wgf_font_t font)
{
    return font == 0 || (WGF_CORE_PRIV_HANDLE_KIND(font) == WGF_CORE_PRIV_HANDLE_KIND_FONT &&
                         wgf_resource_get_status(font) != WGF_RESOURCE_STATUS_NONE);
}

static void text_free(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr);
static void text_draw(wgf_node_t node, const wgf_gfx_priv_node_t *node_ptr, const wgf_mat4_t *placed,
                      const wgf_mat4_t *view);
static const wgf_gfx_priv_node_kind_t kind = {text_free, text_draw};

wgf_node_t wgf_text_create(wgf_font_t font)
{
    wgf_node_t text;
    wgf_gfx_priv_node_t *node_ptr;
    if (!is_font(font)) return 0;
    text = wgf_gfx_priv_node_create(WGF_NODE_TYPE_TEXT);
    node_ptr = wgf_gfx_priv_node_of(text);
    if (node_ptr == NULL) return 0;
    wgf_gfx_priv_node_set_kind(WGF_NODE_TYPE_TEXT, &kind);
    node_ptr->as.text.color = 0xFFFFFFFFu;
    if (font != 0) wgf_gfx_priv_font_retain(font);
    node_ptr->as.text.font = font;
    return text;
}

/* A text node let go of: its font reference and its string. */
static void text_free(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr)
{
    (void)node;
    if (node_ptr->as.text.font != 0) wgf_resource_release(node_ptr->as.text.font);
    node_ptr->as.text.font = 0;
    free(node_ptr->as.text.string);
    node_ptr->as.text.string = NULL;
}

bool wgf_text_set_font(wgf_node_t text, wgf_font_t font)
{
    wgf_gfx_priv_node_t *node_ptr = text_of(text);
    if (node_ptr == NULL || !is_font(font)) return false;
    if (font != 0) wgf_gfx_priv_font_retain(font); /* before the release, in case it is the same */
    if (node_ptr->as.text.font != 0) wgf_resource_release(node_ptr->as.text.font);
    node_ptr->as.text.font = font;
    return true;
}

wgf_font_t wgf_text_get_font(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    return node_ptr != NULL ? node_ptr->as.text.font : 0;
}

bool wgf_text_set_string(wgf_node_t text, const char *string)
{
    wgf_gfx_priv_node_t *node_ptr = text_of(text);
    char *copy = NULL;
    if (node_ptr == NULL) return false;
    if (string != NULL && string[0] != '\0') {
        const size_t n = strlen(string);
        copy = (char *)malloc(n + 1);
        if (copy == NULL) return false;
        memcpy(copy, string, n + 1);
    }
    free(node_ptr->as.text.string);
    node_ptr->as.text.string = copy;
    return true;
}

const char *wgf_text_get_string(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    return node_ptr != NULL && node_ptr->as.text.string != NULL ? node_ptr->as.text.string : "";
}

bool wgf_text_set_font_size(wgf_node_t text, float size)
{
    wgf_gfx_priv_node_t *node_ptr = text_of(text);
    if (node_ptr == NULL) return false;
    node_ptr->as.text.size = size > 0.0f ? size : 0.0f; /* 0: unset */
    return true;
}

float wgf_text_get_font_size(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    if (node_ptr == NULL) return 0.0f;
    return node_ptr->as.text.size > 0.0f ? node_ptr->as.text.size : WGF_GFX_PRIV_FONT_DEFAULT_SIZE;
}

bool wgf_text_set_color(wgf_node_t text, wgf_color_t color)
{
    wgf_gfx_priv_node_t *node_ptr = text_of(text);
    if (node_ptr == NULL) return false;
    node_ptr->as.text.color = color;
    return true;
}

wgf_color_t wgf_text_get_color(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    return node_ptr != NULL ? node_ptr->as.text.color : 0u;
}

bool wgf_text_set_wrap_width(wgf_node_t text, float width)
{
    wgf_gfx_priv_node_t *node_ptr = text_of(text);
    if (node_ptr == NULL || width < 0.0f) return false;
    node_ptr->as.text.wrap_width = width;
    return true;
}

float wgf_text_get_wrap_width(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    return node_ptr != NULL ? node_ptr->as.text.wrap_width : 0.0f;
}

bool wgf_text_set_align(wgf_node_t text, wgf_text_halign_t horizontal, wgf_text_valign_t vertical)
{
    wgf_gfx_priv_node_t *node_ptr = text_of(text);
    if (node_ptr == NULL || (int)horizontal < 0 || horizontal > WGF_TEXT_HALIGN_RIGHT || (int)vertical < 0 ||
        vertical > WGF_TEXT_VALIGN_BOTTOM) {
        return false;
    }
    node_ptr->as.text.halign = horizontal;
    node_ptr->as.text.valign = vertical;
    return true;
}

wgf_text_halign_t wgf_text_get_halign(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    return node_ptr != NULL ? node_ptr->as.text.halign : WGF_TEXT_HALIGN_LEFT;
}

wgf_text_valign_t wgf_text_get_valign(wgf_node_t text)
{
    const wgf_gfx_priv_node_t *node_ptr = text_of(text);
    return node_ptr != NULL ? node_ptr->as.text.valign : WGF_TEXT_VALIGN_TOP;
}

static void text_draw(wgf_node_t node, const wgf_gfx_priv_node_t *node_ptr, const wgf_mat4_t *placed,
                      const wgf_mat4_t *view)
{
    const wgf_gfx_priv_text_t *text = &node_ptr->as.text;
    (void)node;
    (void)view;
    if (text->string == NULL) return;
    wgf_gfx_priv_font_draw_block(text->font, text->string, text->size, text->color, text->wrap_width, text->halign,
                                 text->valign, placed->m);
    wgf_gfx_priv_render_set_2d(); /* fontstash left its own state */
}
