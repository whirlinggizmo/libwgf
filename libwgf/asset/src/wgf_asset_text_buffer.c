#include "wgf_text_buffer.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_asset_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* Text buffers: a resource of a pool of its own ("asset.text_buffer"), text in memory. One
 * from a file is read on a worker and moved into its record on the main thread; one made in
 * code is READY as it is added. A part of its own, installed by the first buffer, so a
 * program that makes none links none of it. */

enum { TEXT_MAX = 16 * 1024 * 1024 }; /* bytes, without the NUL */

typedef struct buffer_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    char *text;                        /* NUL-terminated, malloc'd; NULL until it has some */
    int length;                        /* bytes, without the NUL */
    int capacity;                      /* bytes text holds, the NUL's included */
    int version;                       /* changes with the text; 1 at its first */
} buffer_t;

typedef struct read_t {
    char *text; /* malloc'd, NUL-terminated */
    int length;
} read_t;

static struct {
    bool ready;
    bool installed;
    wgf_core_priv_handle_pool_t pool;
    buffer_t *buffers;
} tb;

static buffer_t *buffer_of_at(wgf_text_buffer_t buffer, const char *caller)
{
    uint16_t index;
    if (!tb.ready || !wgf_core_priv_handle_pool_resolve_at(&tb.pool, buffer, &index, caller)) return NULL;
    return &tb.buffers[index];
}
#define buffer_of(...) buffer_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* A READY buffer's record, or NULL. */
static buffer_t *ready_of_at(wgf_text_buffer_t buffer, const char *caller)
{
    buffer_t *buffer_ptr = buffer_of_at(buffer, caller);
    return buffer_ptr != NULL && buffer_ptr->resource.status == WGF_RESOURCE_STATUS_READY ? buffer_ptr : NULL;
}
#define ready_of(...) ready_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

static void changed(buffer_t *buffer_ptr)
{
    buffer_ptr->version = buffer_ptr->version == INT_MAX ? 1 : buffer_ptr->version + 1;
}

/* Room in the buffer for `length` bytes and the NUL. False (logged, naming `caller`) past
 * the most, or with no memory. */
static bool reserve(buffer_t *buffer_ptr, size_t length, const char *caller)
{
    char *grown;
    size_t capacity;
    if (length > TEXT_MAX) {
        wgf_log_warn("%s: %lu bytes, over the most a text buffer holds (%d)", caller, (unsigned long)length, TEXT_MAX);
        return false;
    }
    if (buffer_ptr->text != NULL && length < (size_t)buffer_ptr->capacity) return true;
    capacity = buffer_ptr->capacity > 0 ? (size_t)buffer_ptr->capacity : 64;
    while (capacity <= length) capacity *= 2;
    if (capacity > (size_t)TEXT_MAX + 1) capacity = (size_t)TEXT_MAX + 1;
    grown = (char *)realloc(buffer_ptr->text, capacity);
    if (grown == NULL) {
        wgf_log_error("%s: no memory for %lu bytes of text", caller, (unsigned long)length);
        return false;
    }
    if (buffer_ptr->text == NULL) grown[0] = '\0';
    buffer_ptr->text = grown;
    buffer_ptr->capacity = (int)capacity;
    return true;
}

/* --- the loader ------------------------------------------------------------ */

static void discard(void *prepared)
{
    read_t *read = (read_t *)prepared;
    if (read == NULL) return;
    free(read->text);
    free(read);
}

/* The file's text, its byte order mark dropped: on a worker. NULL (logged) when it can't be
 * read, or is over the most. */
static void *prepare(const char *path)
{
    unsigned char *bytes;
    int size, start = 0;
    read_t *read;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) {
        wgf_log_warn("wgf_text_buffer: %s: couldn't be read", path);
        return NULL;
    }
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) start = 3;
    read = NULL;
    if (size - start > TEXT_MAX) {
        wgf_log_warn("wgf_text_buffer: %s: %d bytes, over the most a text buffer holds (%d)", path, size - start,
                     TEXT_MAX);
    } else if ((read = (read_t *)calloc(1, sizeof(read_t))) != NULL &&
               (read->text = (char *)malloc((size_t)(size - start) + 1)) != NULL) {
        memcpy(read->text, bytes + start, (size_t)(size - start));
        read->text[size - start] = '\0';
        read->length = size - start;
    } else {
        wgf_log_error("wgf_text_buffer: %s: no memory for its %d bytes", path, size - start);
        discard(read);
        read = NULL;
    }
    wgf_core_priv_fs_read_free(bytes);
    return read;
}

/* The text moved into the buffer, READY: in place of what it held, when it loads again. */
static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    read_t *read = (read_t *)prepared;
    buffer_t *buffer_ptr = buffer_of(resource);
    if (buffer_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    free(buffer_ptr->text);
    buffer_ptr->text = read->text;
    buffer_ptr->length = read->length;
    buffer_ptr->capacity = read->length + 1;
    read->text = NULL; /* the buffer's now: discard frees the rest */
    changed(buffer_ptr);
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"text buffer", prepare, finish, discard, fail, NULL, true, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_buffer(wgf_handle_t buffer, void *record)
{
    buffer_t *buffer_ptr = (buffer_t *)record;
    (void)buffer;
    free(buffer_ptr->text);
    buffer_ptr->text = NULL;
}

/* Its loader is set by the first buffer from a path (wgf_text_buffer_create), so a program that
 * makes them only in code links no loading. */
static wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_text_buffer_create", .free = free_buffer};

/* --- the part --------------------------------------------------------------- */

static void stop(void)
{
    if (tb.ready) {
        wgf_core_priv_resource_unregister(&tb.pool); /* each buffer freed */
        wgf_core_priv_handle_pool_destroy(&tb.pool);
    }
    memset(&tb, 0, sizeof(tb));
}

static wgf_core_priv_part_t part = {.name = "text buffers",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_ASSET,
                                    .order = WGF_CORE_PRIV_PART_TEXT_BUFFERS,
                                    .stop = stop};

static bool ensure_started(void)
{
    if (tb.ready) return true;
    tb.ready = wgf_core_priv_handle_pool_init(&tb.pool, WGF_CORE_PRIV_HANDLE_KIND_TEXT_BUFFER, (void **)&tb.buffers,
                                              sizeof(buffer_t), 8, 65535);
    if (!tb.ready) return false;
    wgf_core_priv_resource_register(&tb.pool, &resource_kind);
    if (!tb.installed) {
        wgf_core_priv_part_install(&part);
        tb.installed = true;
    }
    return true;
}

/* --- the public API --------------------------------------------------------- */

wgf_text_buffer_t wgf_text_buffer_create(const char *path)
{
    resource_kind.loader = loader_of;
    return ensure_started() ? wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEXT_BUFFER, path) : 0;
}

wgf_text_buffer_t wgf_text_buffer_create_text(const char *text)
{
    wgf_text_buffer_t buffer;
    buffer_t *buffer_ptr;
    const size_t length = text != NULL ? strlen(text) : 0;
    if (!ensure_started()) return 0;
    if (length > TEXT_MAX) {
        wgf_log_warn("wgf_text_buffer_create_text: %lu bytes, over the most a text buffer holds (%d)",
                     (unsigned long)length, TEXT_MAX);
        return 0;
    }
    buffer = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_TEXT_BUFFER);
    buffer_ptr = buffer_of(buffer);
    if (buffer_ptr == NULL) {
        wgf_log_error("wgf_text_buffer_create_text: no room for another text buffer");
        return 0;
    }
    if (!reserve(buffer_ptr, length, "wgf_text_buffer_create_text")) { /* READY as it was added: let go of */
        wgf_resource_release(buffer);
        return 0;
    }
    if (length > 0) memcpy(buffer_ptr->text, text, length);
    buffer_ptr->text[length] = '\0';
    buffer_ptr->length = (int)length;
    changed(buffer_ptr);
    return buffer;
}

bool wgf_text_buffer_set_text(wgf_text_buffer_t buffer, const char *text)
{
    buffer_t *buffer_ptr = ready_of(buffer);
    const size_t length = text != NULL ? strlen(text) : 0;
    if (buffer_ptr == NULL || !reserve(buffer_ptr, length, "wgf_text_buffer_set_text")) return false;
    memmove(buffer_ptr->text, text != NULL ? text : "", length); /* the text may be its own */
    buffer_ptr->text[length] = '\0';
    buffer_ptr->length = (int)length;
    changed(buffer_ptr);
    return true;
}

const char *wgf_text_buffer_get_text(wgf_text_buffer_t buffer)
{
    const buffer_t *buffer_ptr = ready_of(buffer);
    return buffer_ptr != NULL && buffer_ptr->text != NULL ? buffer_ptr->text : "";
}

bool wgf_text_buffer_append(wgf_text_buffer_t buffer, const char *text)
{
    buffer_t *buffer_ptr = ready_of(buffer);
    size_t added, offset;
    if (buffer_ptr == NULL) return false;
    added = text != NULL ? strlen(text) : 0;
    if (added == 0) return true;
    /* the text may be in this buffer, which reserving can move */
    offset = buffer_ptr->text != NULL && text >= buffer_ptr->text && text < buffer_ptr->text + buffer_ptr->capacity
                 ? (size_t)(text - buffer_ptr->text)
                 : SIZE_MAX;
    if (!reserve(buffer_ptr, (size_t)buffer_ptr->length + added, "wgf_text_buffer_append")) return false;
    if (offset != SIZE_MAX) text = buffer_ptr->text + offset;
    memmove(buffer_ptr->text + buffer_ptr->length, text, added);
    buffer_ptr->length += (int)added;
    buffer_ptr->text[buffer_ptr->length] = '\0';
    changed(buffer_ptr);
    return true;
}

int wgf_text_buffer_get_length(wgf_text_buffer_t buffer)
{
    const buffer_t *buffer_ptr = ready_of(buffer);
    return buffer_ptr != NULL ? buffer_ptr->length : 0;
}

int wgf_text_buffer_get_version(wgf_text_buffer_t buffer)
{
    const buffer_t *buffer_ptr = ready_of(buffer);
    return buffer_ptr != NULL ? buffer_ptr->version : 0;
}
