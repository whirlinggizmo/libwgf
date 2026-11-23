#ifndef WGF_TEXT_BUFFER_H
#define WGF_TEXT_BUFFER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A text buffer: text in memory, UTF-8, which libwgf holds so it isn't passed in each time.
 *
 * - Load one from a file (credits, help, dialogue, data a game parses), or make one in code
 *   (a chat's history, a log).
 * - Read it, set it whole, or add to its end. Its version changes each time its text does,
 *   so whatever shows it knows when to lay it out again.
 *
 * A text buffer is a resource (wgf_resource.h), a handle of kind "asset.text_buffer".
 * - One loaded from a path is shared, reference counted, and loaded on create. It is PENDING
 *   at once, and READY or FAILED in a later update. Creating the same path again gives the
 *   same buffer, so a change to its text is seen through every handle of it.
 * - wgf_asset_reload loads it again in place, replacing its text, changes and all.
 * - One made in code is the program's own, and READY at once.
 * - Release it with wgf_resource_release.
 * - A buffer that isn't READY, and the handle 0, read as "". */
typedef wgf_handle_t wgf_text_buffer_t;

/* Creates a text buffer from a file, read as UTF-8 (a byte order mark at its start is
 * dropped).
 *
 * Returns 0 only if there is no room for another buffer. A path or file that can't be loaded
 * gives a buffer that FAILED (logged once, naming the file). A file over 16 MiB fails. */
WGF_API wgf_text_buffer_t wgf_text_buffer_create(const char *path);

/* Creates a text buffer holding `text` (NULL is ""), READY at once.
 *
 * Returns 0 if there is no room for another buffer, no memory, or `text` is over 16 MiB
 * (logged). */
WGF_API wgf_text_buffer_t wgf_text_buffer_create_text(const char *text);

/* Sets or gets the buffer's text.
 *
 * The text read is the buffer's own: valid until the buffer's text next changes or it is
 * released. Copy it to keep it.
 *
 * set_text returns false, changing nothing, for a buffer that isn't READY, or a text that
 * would take it over 16 MiB (logged). NULL is "". */
WGF_API bool wgf_text_buffer_set_text(wgf_text_buffer_t buffer, const char *text);
WGF_API const char *wgf_text_buffer_get_text(wgf_text_buffer_t buffer);

/* Adds `text` to the end of the buffer: a chat's new message, a log's new line.
 *
 * Returns false, changing nothing, as set_text does. Adding "" or NULL changes nothing and
 * returns true. */
WGF_API bool wgf_text_buffer_append(wgf_text_buffer_t buffer, const char *text);

/* Returns the text's length in bytes, without its ending NUL: 0 for a buffer that isn't
 * READY. */
WGF_API int wgf_text_buffer_get_length(wgf_text_buffer_t buffer);

/* Returns a number that changes each time the buffer's text does: set, added to, or loaded
 * again. Compare it with the one last seen to know the text changed. 0 for a buffer that
 * isn't READY. */
WGF_API int wgf_text_buffer_get_version(wgf_text_buffer_t buffer);

#ifdef __cplusplus
}
#endif

#endif
