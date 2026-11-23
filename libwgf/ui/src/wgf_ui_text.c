#include <stdlib.h>
#include <string.h>

#include "wgf_input.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_ui.h"
#include "wgf_ui_text_priv.h"

/* The Clay UI's text fields' texts, kept by id, and their editing (wgf_ui_text_priv.h): the
 * caret and the selection moved, text typed and pasted over the selection, words, undo and
 * redo, and the on-screen keyboard. */

enum { EDIT_NONE, EDIT_TYPING, EDIT_DELETING, EDIT_OTHER };

static wgf_ui_priv_field_t **fields; /* each its own allocation, so one never moves */
static int field_count, field_capacity;

static struct {
    bool shown;
    float x, y, height;
} keyboard;

/* ---- the hooks ---------------------------------------------------------------------- */

static void end(bool wanted, float x, float y, float height)
{
    if (wanted && (!keyboard.shown || x != keyboard.x || y != keyboard.y || height != keyboard.height)) {
        wgf_platform_priv_show_keyboard(true, x, y, height);
    } else if (!wanted && keyboard.shown) {
        wgf_platform_priv_show_keyboard(false, 0.0f, 0.0f, 0.0f);
    }
    keyboard.shown = wanted;
    keyboard.x = x;
    keyboard.y = y;
    keyboard.height = height;
}

static void forget(wgf_ui_priv_snapshot_t *steps, int *count)
{
    int i;
    for (i = 0; i < *count; i++) free(steps[i].text);
    *count = 0;
}

static void stop(void)
{
    int i;
    if (keyboard.shown) wgf_platform_priv_show_keyboard(false, 0.0f, 0.0f, 0.0f);
    for (i = 0; i < field_count; i++) {
        forget(fields[i]->undo, &fields[i]->undos);
        forget(fields[i]->redo, &fields[i]->redos);
        free(fields[i]->text);
        free(fields[i]);
    }
    free(fields);
    fields = NULL;
    field_count = field_capacity = 0;
    memset(&keyboard, 0, sizeof(keyboard));
}

static const wgf_ui_priv_text_hooks_t hooks = {end, stop};

/* ---- the texts ---------------------------------------------------------------------- */

wgf_ui_priv_field_t *wgf_ui_priv_field(const char *id, bool make)
{
    wgf_ui_priv_field_t *field;
    int i;
    if (id == NULL || id[0] == '\0' || strlen(id) >= sizeof(field->id)) return NULL;
    for (i = 0; i < field_count; i++) {
        if (strcmp(fields[i]->id, id) == 0) return fields[i];
    }
    if (!make) return NULL;
    if (field_count == field_capacity) {
        const int capacity = field_capacity > 0 ? field_capacity * 2 : 8;
        wgf_ui_priv_field_t **grown = (wgf_ui_priv_field_t **)realloc(fields, sizeof(*fields) * (size_t)capacity);
        if (grown == NULL) return NULL;
        fields = grown;
        field_capacity = capacity;
    }
    field = (wgf_ui_priv_field_t *)calloc(1, sizeof(*field));
    if (field == NULL) return NULL;
    field->text = (char *)calloc(1, 16);
    if (field->text == NULL) {
        free(field);
        return NULL;
    }
    field->capacity = 16;
    memcpy(field->id, id, strlen(id) + 1);
    fields[field_count++] = field;
    wgf_ui_priv_set_text_hooks(&hooks);
    return field;
}

/* Room for `length` bytes and the NUL. */
static bool reserve(wgf_ui_priv_field_t *field, int length)
{
    char *grown;
    int capacity = field->capacity;
    if (length + 1 <= capacity) return true;
    while (capacity < length + 1) capacity *= 2;
    grown = (char *)realloc(field->text, (size_t)capacity);
    if (grown == NULL) return false;
    field->text = grown;
    field->capacity = capacity;
    return true;
}

static bool continuation(char byte)
{
    return ((unsigned char)byte & 0xC0u) == 0x80u;
}

int wgf_ui_priv_field_step(const char *text, int length, int at, int step)
{
    if (step > 0) {
        if (at >= length) return length;
        for (at++; at < length && continuation(text[at]); at++) {
        }
    } else {
        if (at <= 0) return 0;
        for (at--; at > 0 && continuation(text[at]); at--) {
        }
    }
    return at;
}

int wgf_ui_priv_field_count(const char *text, int length)
{
    int i, count = 0;
    for (i = 0; i < length; i++) {
        if (!continuation(text[i])) count++;
    }
    return count;
}

/* The byte `count` characters into `text` (its length, for fewer). */
static int bytes_of(const char *text, int length, int count)
{
    int at = 0;
    while (count-- > 0 && at < length) at = wgf_ui_priv_field_step(text, length, at, 1);
    return at;
}

void wgf_ui_priv_field_limit(wgf_ui_priv_field_t *field, int most)
{
    int at;
    if (most <= 0) return;
    at = bytes_of(field->text, field->length, most);
    if (at >= field->length) return;
    field->length = at;
    field->text[at] = '\0';
    if (field->caret > at) field->caret = at;
    if (field->anchor > at) field->anchor = at;
}

/* Bytes `from` to `to` taken out, `text`'s first `length` put in their place, the caret
 * after it. */
static bool splice(wgf_ui_priv_field_t *field, int from, int to, const char *text, int length)
{
    const int after = field->length - to;
    if (!reserve(field, field->length - (to - from) + length)) return false;
    memmove(field->text + from + length, field->text + to, (size_t)after + 1);
    if (length > 0) memcpy(field->text + from, text, (size_t)length);
    field->length += length - (to - from);
    field->caret = field->anchor = from + length;
    return true;
}

/* ---- undo --------------------------------------------------------------------------- */

static bool snapshot(const wgf_ui_priv_field_t *field, wgf_ui_priv_snapshot_t *out)
{
    out->text = (char *)malloc((size_t)field->length + 1);
    if (out->text == NULL) return false;
    memcpy(out->text, field->text, (size_t)field->length + 1);
    out->caret = field->caret;
    out->anchor = field->anchor;
    return true;
}

static void push(wgf_ui_priv_snapshot_t *steps, int *count, const wgf_ui_priv_field_t *field)
{
    if (*count == WGF_UI_PRIV_UNDO_MAX) { /* the oldest let go */
        free(steps[0].text);
        memmove(steps, steps + 1, sizeof(*steps) * (WGF_UI_PRIV_UNDO_MAX - 1));
        (*count)--;
    }
    if (snapshot(field, &steps[*count])) (*count)++;
}

/* Before an edit of `kind`: the text as it was kept as a step, but in a run of the same
 * typing or deleting, which is one step. */
static void record(wgf_ui_priv_field_t *field, int kind)
{
    if (kind != field->last || kind == EDIT_OTHER) {
        push(field->undo, &field->undos, field);
        forget(field->redo, &field->redos);
    }
    field->last = kind;
}

/* The text put back as the last step `from` holds, the text now kept on `to`. */
static bool step_back(wgf_ui_priv_field_t *field, wgf_ui_priv_snapshot_t *from, int *from_count,
                      wgf_ui_priv_snapshot_t *to, int *to_count)
{
    wgf_ui_priv_snapshot_t *step;
    int length;
    if (*from_count == 0) return false;
    step = &from[*from_count - 1];
    length = (int)strlen(step->text);
    if (!reserve(field, length)) return false;
    push(to, to_count, field);
    memcpy(field->text, step->text, (size_t)length + 1);
    field->length = length;
    field->caret = step->caret <= length ? step->caret : length;
    field->anchor = step->anchor <= length ? step->anchor : length;
    free(step->text);
    (*from_count)--;
    field->last = EDIT_NONE;
    return true;
}

/* ---- editing ------------------------------------------------------------------------ */

static bool is_word(char byte)
{
    const unsigned char c = (unsigned char)byte;
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80u;
}

/* The word's edge from `at` the way `step` goes: what isn't a word skipped, then the word. */
static int word_edge(const wgf_ui_priv_field_t *field, int at, int step, bool masked)
{
    const char *text = field->text;
    if (masked) return step > 0 ? field->length : 0;
    if (step > 0) {
        while (at < field->length && !is_word(text[at])) at = wgf_ui_priv_field_step(text, field->length, at, 1);
        while (at < field->length && is_word(text[at])) at = wgf_ui_priv_field_step(text, field->length, at, 1);
    } else {
        while (at > 0 && !is_word(text[wgf_ui_priv_field_step(text, field->length, at, -1)]))
            at = wgf_ui_priv_field_step(text, field->length, at, -1);
        while (at > 0 && is_word(text[wgf_ui_priv_field_step(text, field->length, at, -1)]))
            at = wgf_ui_priv_field_step(text, field->length, at, -1);
    }
    return at;
}

void wgf_ui_priv_field_word(const wgf_ui_priv_field_t *field, int at, int *start, int *end_at)
{
    const char *text = field->text;
    bool word;
    if (at > field->length) at = field->length;
    if (at == field->length && at > 0) at = wgf_ui_priv_field_step(text, field->length, at, -1);
    *start = *end_at = at;
    if (at >= field->length) return;
    word = is_word(text[at]);
    while (*start > 0 && is_word(text[wgf_ui_priv_field_step(text, field->length, *start, -1)]) == word)
        *start = wgf_ui_priv_field_step(text, field->length, *start, -1);
    while (*end_at < field->length && is_word(text[*end_at]) == word)
        *end_at = wgf_ui_priv_field_step(text, field->length, *end_at, 1);
}

/* `text` put in place of the selection, its control characters (newlines among them)
 * stripped and cut to what the field may still hold: whether anything changed. */
static bool insert(wgf_ui_priv_field_t *field, const char *text, int most, int kind)
{
    const int lo = field->caret < field->anchor ? field->caret : field->anchor;
    const int hi = field->caret < field->anchor ? field->anchor : field->caret;
    const int kept = field->length - (hi - lo);
    int room = most > 0 ? most - wgf_ui_priv_field_count(field->text, field->length) +
                              wgf_ui_priv_field_count(field->text + lo, hi - lo)
                        : WGF_UI_PRIV_TEXT_MAX;
    char *clean;
    int length = 0, i = 0, total = (int)strlen(text);
    bool changed;
    clean = (char *)malloc((size_t)total + 1);
    if (clean == NULL) return false;
    while (i < total && room > 0) { /* whole characters, as long as they fit */
        const int next = wgf_ui_priv_field_step(text, total, i, 1);
        if ((unsigned char)text[i] >= 32u && text[i] != 127) {
            if (kept + length + (next - i) > WGF_UI_PRIV_TEXT_MAX - 1) break;
            memcpy(clean + length, text + i, (size_t)(next - i));
            length += next - i;
            room--;
        }
        i = next;
    }
    changed = length > 0 || hi > lo;
    if (changed) {
        record(field, kind);
        changed = splice(field, lo, hi, clean, length);
    }
    free(clean);
    return changed;
}

/* The selection taken out, as `kind`. */
static bool erase(wgf_ui_priv_field_t *field, int from, int to, int kind)
{
    if (to <= from) return false;
    record(field, kind);
    return splice(field, from, to, NULL, 0);
}

bool wgf_ui_priv_field_edit(wgf_ui_priv_field_t *field, const wgf_ui_priv_edit_t *edit, float clock)
{
    const int was_caret = field->caret, was_anchor = field->anchor;
    bool changed = false;
    int lo, hi;
    if (field->caret > field->length) field->caret = field->length; /* set by the program since */
    if (field->anchor > field->length) field->anchor = field->length;
    if (edit->all) {
        field->anchor = 0;
        field->caret = field->length;
        field->last = EDIT_NONE;
    }
    if (edit->undo) changed |= step_back(field, field->undo, &field->undos, field->redo, &field->redos);
    else if (edit->redo) changed |= step_back(field, field->redo, &field->redos, field->undo, &field->undos);
    lo = field->caret < field->anchor ? field->caret : field->anchor;
    hi = field->caret < field->anchor ? field->anchor : field->caret;
    if ((edit->copy || edit->cut) && hi > lo && !edit->masked) {
        char *copy = (char *)malloc((size_t)(hi - lo) + 1);
        if (copy != NULL) {
            memcpy(copy, field->text + lo, (size_t)(hi - lo));
            copy[hi - lo] = '\0';
            wgf_input_set_clipboard(copy);
            free(copy);
        }
        if (edit->cut) changed |= erase(field, lo, hi, EDIT_OTHER);
    }
    if (edit->paste) {
        const char *clipboard = wgf_input_get_clipboard();
        if (clipboard != NULL && clipboard[0] != '\0') changed |= insert(field, clipboard, edit->most, EDIT_OTHER);
    }
    if (edit->typed != NULL && edit->typed[0] != '\0') changed |= insert(field, edit->typed, edit->most, EDIT_TYPING);
    if (edit->back || edit->forward) {
        lo = field->caret < field->anchor ? field->caret : field->anchor;
        hi = field->caret < field->anchor ? field->anchor : field->caret;
        if (hi > lo) {
            changed |= erase(field, lo, hi, EDIT_DELETING);
        } else if (edit->back) {
            const int from = edit->word ? word_edge(field, lo, -1, edit->masked)
                                        : wgf_ui_priv_field_step(field->text, field->length, lo, -1);
            changed |= erase(field, from, lo, EDIT_DELETING);
        } else {
            const int to = edit->word ? word_edge(field, lo, 1, edit->masked)
                                      : wgf_ui_priv_field_step(field->text, field->length, lo, 1);
            changed |= erase(field, lo, to, EDIT_DELETING);
        }
    }
    if (edit->left != edit->right || edit->home != edit->end) {
        lo = field->caret < field->anchor ? field->caret : field->anchor;
        hi = field->caret < field->anchor ? field->anchor : field->caret;
        if (edit->home != edit->end) {
            field->caret = edit->home ? 0 : field->length;
        } else if (hi > lo && !edit->shift && !edit->word) { /* a selection collapsed to its edge */
            field->caret = edit->left ? lo : hi;
        } else if (edit->word) {
            field->caret = word_edge(field, field->caret, edit->left ? -1 : 1, edit->masked);
        } else {
            field->caret = wgf_ui_priv_field_step(field->text, field->length, field->caret, edit->left ? -1 : 1);
        }
        if (!edit->shift) field->anchor = field->caret;
        field->last = EDIT_NONE;
    }
    if (changed || field->caret != was_caret || field->anchor != was_anchor) field->since = clock;
    return changed;
}

void wgf_ui_priv_field_place(wgf_ui_priv_field_t *field, int caret, int anchor, float clock)
{
    caret = caret < 0 ? 0 : caret > field->length ? field->length : caret;
    anchor = anchor < 0 ? 0 : anchor > field->length ? field->length : anchor;
    if (caret != field->caret || anchor != field->anchor) field->since = clock;
    field->caret = caret;
    field->anchor = anchor;
    field->last = EDIT_NONE;
}

/* ---- the calls ---------------------------------------------------------------------- */

bool wgf_ui_priv_field_take(wgf_ui_priv_field_t *field, const char *text)
{
    int length;
    for (length = 0; length < WGF_UI_PRIV_TEXT_MAX && text[length] != '\0'; length++) {
    }
    if (length > WGF_UI_PRIV_TEXT_MAX - 1) { /* cut to whole characters */
        length = WGF_UI_PRIV_TEXT_MAX - 1;
        while (length > 0 && continuation(text[length])) length--;
    }
    if (!reserve(field, length)) return false;
    memcpy(field->text, text, (size_t)length);
    field->text[length] = '\0';
    field->length = field->caret = field->anchor = length;
    field->last = EDIT_NONE;
    forget(field->undo, &field->undos);
    forget(field->redo, &field->redos);
    return true;
}
