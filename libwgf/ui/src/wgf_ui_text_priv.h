#ifndef WGF_UI_TEXT_PRIV_H
#define WGF_UI_TEXT_PRIV_H

#include <stdbool.h>

/* The Clay UI's text fields (wgf_ui.h): their texts, kept by id, and their editing are a
 * file of its own (wgf_ui_text.c). The frame's end, which shows the on-screen keyboard while
 * a field wants it and hides it after, and the UI's stop, which frees the texts, reach it
 * through hooks its first field sets from there, so a UI without one links none of it. */

#define WGF_UI_PRIV_TEXT_MAX 4096 /* a field's bytes, with its NUL */
#define WGF_UI_PRIV_UNDO_MAX 32   /* the steps kept each way */

/* A text as it was: an undo or redo step. */
typedef struct wgf_ui_priv_snapshot_t {
    char *text; /* malloc'd */
    int caret, anchor;
} wgf_ui_priv_snapshot_t;

/* A field, by its id: its text, and where its caret and selection are, in bytes. */
typedef struct wgf_ui_priv_field_t {
    char id[64];
    char *text; /* NUL-terminated, malloc'd: never NULL */
    int length, capacity;
    int caret, anchor; /* the selection is between them: none when they are one */
    float scroll;      /* how far its text is scrolled left, in logical pixels */
    float since;       /* the UI's clock as the caret last moved: its blink starts there */
    bool was_focused;  /* focused in the frame before */
    int clicks;        /* the presses in a run: 2 a double click, 3 a triple */
    float click_at, click_x, click_y;
    int last;          /* the last edit's kind: a run of typing is one undo step */
    wgf_ui_priv_snapshot_t undo[WGF_UI_PRIV_UNDO_MAX], redo[WGF_UI_PRIV_UNDO_MAX];
    int undos, redos;
} wgf_ui_priv_field_t;

/* What the keys did to the focused field in a frame. */
typedef struct wgf_ui_priv_edit_t {
    const char *typed;                      /* the characters typed */
    bool left, right, home, end;            /* moves: by a word with `word` */
    bool back, forward;                     /* Backspace and Delete: a word with `word` */
    bool word, shift;                       /* moves extend the selection with `shift` */
    bool all, copy, cut, paste, undo, redo; /* the shortcuts */
    int most;                               /* the characters it may hold: 0 or less, any */
    bool masked;                            /* a password's: words are the whole, nothing is copied */
} wgf_ui_priv_edit_t;

/* The hooks the UI's frame calls. `end`: at a frame's end, whether a field wants the
 * on-screen keyboard, its caret at x, y (logical, the screen's) on a line `height` tall. */
typedef struct wgf_ui_priv_text_hooks_t {
    void (*end)(bool wanted, float x, float y, float height);
    void (*stop)(void);
} wgf_ui_priv_text_hooks_t;

void wgf_ui_priv_set_text_hooks(const wgf_ui_priv_text_hooks_t *hooks); /* wgf_ui.c's */

/* wgf_ui_text.c's. The field `id`, made (empty) with `make` if there is none, the hooks set
 * with the first: NULL for no id, one of 64 bytes or more, or out of memory. A field stays
 * where it is until the UI stops. */
wgf_ui_priv_field_t *wgf_ui_priv_field(const char *id, bool make);
/* The edit applied: whether the text changed. `clock` is the UI's, for the caret's blink. */
bool wgf_ui_priv_field_edit(wgf_ui_priv_field_t *field, const wgf_ui_priv_edit_t *edit, float clock);
/* Its caret and the selection's other end put at bytes `caret` and `anchor` (a press's), as a
 * move: a run of typing after it is a step of its own. */
void wgf_ui_priv_field_place(wgf_ui_priv_field_t *field, int caret, int anchor, float clock);
/* Its text cut to `most` characters (0 or less: no cut). */
void wgf_ui_priv_field_limit(wgf_ui_priv_field_t *field, int most);
/* The word, or the run of what isn't one, at byte `at`: its start and end. */
void wgf_ui_priv_field_word(const wgf_ui_priv_field_t *field, int at, int *start, int *end);
/* The byte after (`step` 1) or before (-1) the character at `at`. */
int wgf_ui_priv_field_step(const char *text, int length, int at, int step);
/* The characters in `text`'s first `length` bytes. */
int wgf_ui_priv_field_count(const char *text, int length);

#endif
