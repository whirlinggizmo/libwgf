#include "wgf_document.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "text/wgf_gfx_font_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_mouse.h"
#include "wgf_render.h"
#include "wgf_texture.h"
#include "wgf_time.h"
#include "wgf_ui_priv.h"
#include "wgf_ui_rmlui_priv.h"

/* Documents (wgf_document.h): RmlUi's documents as libwgf's resources, their elements as
 * handles, and what RmlUi asks of libwgf (wgf_ui_rmlui_priv.h's C side): files read through
 * fs, textures made by gfx, glyphs laid out by fontstash, and drawing into the frame's
 * immediate mode. A part, installed by the first document created, so a program that makes
 * none links none of RmlUi (whose C++ is wgf_ui_rmlui.cpp alone). */

#define DOCUMENTS_MAX 4096
#define ELEMENTS_MAX 65535
#define FAMILY_MAX 64
#define FAMILIES_MAX 32

typedef struct document_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    int number;                        /* RmlUi's (wgf_ui_priv_rml_load); 0 until READY */
    bool want_visible;                 /* shown once READY */
} document_t;

typedef struct element_t {
    int slot; /* the C++ side's */
    wgf_document_t document;
} element_t;

typedef struct loaded_t {
    unsigned char *bytes;
    int size;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
} loaded_t;

typedef struct family_t {
    char name[FAMILY_MAX]; /* lowercase */
    wgf_font_t font;
} family_t;

typedef struct face_t {
    char family[FAMILY_MAX]; /* as RmlUi asked, lowercase */
    int size;
} face_t;

/* A texture RmlUi holds: one of libwgf's (a reference), or an image of its own (RmlUi's
 * generated ones). */
/* A change to an element held until the images it names are local (on the web, a file a
 * document's files didn't name isn't, and RmlUi reads images synchronously): RML, an
 * attribute, or a property, with a texture loading each image (its load makes the file
 * local, and RmlUi then takes the texture READY at the path). */
enum { HELD_RML, HELD_ATTRIBUTE, HELD_PROPERTY };
#define HELD_IMAGES 8
typedef struct held_t {
    wgf_element_t element;
    int what;
    char *name, *value; /* name NULL for RML */
    wgf_texture_t images[HELD_IMAGES];
    int image_count;
} held_t;

typedef struct texture_t {
    bool used;
    wgf_texture_t texture;
    sg_image image;
    sg_view view;
    int width, height;
} texture_t;

static struct {
    bool started; /* the part installed, the pools made */
    bool rml;     /* RmlUi started */
    wgf_core_priv_handle_pool_t document_pool, element_pool;
    document_t *documents;
    element_t *elements;

    family_t families[FAMILIES_MAX];
    int family_count;
    face_t *faces;
    int face_count, face_capacity;
    int font_version;
    unsigned last_atlas;
    float last_dpi;
    int last_ready;

    texture_t *textures; /* by handle less 2 (1 is the atlas) */
    int texture_count;

    held_t *held; /* changes waiting on their images, in the order made */
    int held_count, held_capacity;

    /* the frame's drawing */
    bool drawing;
    float origin_x, origin_y;
    bool clipped;
    bool captured;
    bool drawn; /* wgf_ui_draw_documents ran this frame */
    unsigned input_frame;
    int cursor;
    char *clipboard;
} doc;

static void ensure_started(void);

/* ---- handles ------------------------------------------------------------------------ */

static document_t *document_of_at(wgf_document_t document, const char *caller)
{
    uint16_t index;
    if (!doc.started || !wgf_core_priv_handle_pool_resolve_at(&doc.document_pool, document, &index, caller)) return NULL;
    return &doc.documents[index];
}
#define document_of(document) document_of_at((document), WGF_CORE_PRIV_CALLER)

/* An element's slot while it and its document live; 0 otherwise. */
static int slot_of_at(wgf_element_t element, const char *caller)
{
    uint16_t index;
    const element_t *element_ptr;
    if (!doc.started || !wgf_core_priv_handle_pool_resolve_at(&doc.element_pool, element, &index, caller)) return 0;
    element_ptr = &doc.elements[index];
    if (!wgf_core_priv_handle_pool_resolve(&doc.document_pool, element_ptr->document, NULL) ||
        !wgf_ui_priv_rml_element_alive(element_ptr->slot))
        return 0;
    return element_ptr->slot;
}
#define slot_of(element) slot_of_at((element), WGF_CORE_PRIV_CALLER)

/* The same, for an element that may have gone without its caller knowing: nothing logged. */
static int slot_of_silently(wgf_element_t element)
{
    uint16_t index;
    const element_t *element_ptr;
    if (!doc.started || !wgf_core_priv_handle_pool_resolve(&doc.element_pool, element, &index)) return 0;
    element_ptr = &doc.elements[index];
    if (!wgf_core_priv_handle_pool_resolve(&doc.document_pool, element_ptr->document, NULL) ||
        !wgf_ui_priv_rml_element_alive(element_ptr->slot))
        return 0;
    return element_ptr->slot;
}

uint32_t wgf_ui_priv_doc_element_made(int slot, uint32_t owner)
{
    const wgf_handle_t handle = wgf_core_priv_handle_pool_alloc(&doc.element_pool);
    uint16_t index;
    if (handle == 0 || !wgf_core_priv_handle_pool_resolve(&doc.element_pool, handle, &index)) {
        wgf_log_warn("wgf_document: no room for another element's handle (%d)", ELEMENTS_MAX);
        return 0;
    }
    doc.elements[index].slot = slot;
    doc.elements[index].document = owner;
    return handle;
}

void wgf_ui_priv_doc_element_gone(uint32_t element)
{
    wgf_core_priv_handle_pool_free(&doc.element_pool, element);
}

/* ---- the loader: a document's file, parsed once its fonts are loaded ------------------ */

static void *prepare(const char *path)
{
    loaded_t *loaded = (loaded_t *)calloc(1, sizeof(loaded_t));
    if (loaded == NULL) return NULL;
    if (!wgf_core_priv_fs_read(path, &loaded->bytes, &loaded->size)) {
        free(loaded);
        return NULL;
    }
    snprintf(loaded->path, sizeof(loaded->path), "%s", path);
    return loaded;
}

static bool fonts_loading(void)
{
    int i;
    if (wgf_resource_get_status(wgf_font_get_default()) == WGF_RESOURCE_STATUS_PENDING) return true;
    for (i = 0; i < doc.family_count; i++) {
        if (wgf_resource_get_status(doc.families[i].font) == WGF_RESOURCE_STATUS_PENDING) return true;
    }
    return false;
}

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    const loaded_t *loaded = (const loaded_t *)prepared;
    document_t *document_ptr = document_of(resource);
    int number;
    if (document_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (!wgf_gfx_priv_render_is_running() || fonts_loading()) return WGF_CORE_PRIV_LOAD_WAIT;
    if (!doc.rml) {
        doc.rml = wgf_ui_priv_rml_start();
        if (!doc.rml) return WGF_CORE_PRIV_LOAD_FAILED;
    }
    number = wgf_ui_priv_rml_load((const char *)loaded->bytes, loaded->size, loaded->path, resource);
    document_ptr = document_of(resource); /* loading may have made element handles: nothing moves documents, but be sure */
    if (number == 0 || document_ptr == NULL) {
        wgf_log_warn("wgf_document: %s: not a document RmlUi can read", loaded->path);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    document_ptr->number = number;
    if (document_ptr->want_visible) wgf_ui_priv_rml_set_visible(number, true);
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *prepared)
{
    loaded_t *loaded = (loaded_t *)prepared;
    wgf_core_priv_fs_read_free(loaded->bytes);
    free(loaded);
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"document", prepare, finish, discard, fail, NULL, false, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_document(wgf_handle_t document, void *record)
{
    document_t *document_ptr = (document_t *)record;
    (void)document;
    if (document_ptr->number != 0) wgf_ui_priv_rml_unload(document_ptr->number);
    document_ptr->number = 0;
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_document_create",
                                                            .loader = loader_of,
                                                            .free = free_document};

/* ---- the files a document names, for the asset part to make local with it ----------- */

static bool has_extension(const char *start, size_t length)
{
    static const char *const extensions[] = {".rcss", ".rml", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif"};
    size_t e, k;
    for (e = 0; e < sizeof(extensions) / sizeof(extensions[0]); e++) {
        const size_t n = strlen(extensions[e]);
        bool same = length > n;
        for (k = 0; same && k < n; k++) same = tolower((unsigned char)start[length - n + k]) == extensions[e][k];
        if (same) return true;
    }
    return false;
}

static bool is_path_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/';
}

/* Every run of a path's characters ending in a file's extension the documents read --
 * a style sheet, a template, an image -- whether in an attribute (`href="menu.rcss"`,
 * `src="logo.png"`) or RCSS (`src: skin.png;`, `image(icon.png)`), each named once, none
 * required: RmlUi says what it can't find. Read by its bytes, with no parser to trust. */
static void list_document_files(const unsigned char *data, int size, wgf_core_priv_load_add_fn add, void *context)
{
    int i = 0;
    while (i < size) {
        int start, length;
        char uri[WGF_CORE_PRIV_FS_PATH_MAX];
        if (!is_path_char((char)data[i])) {
            i++;
            continue;
        }
        start = i;
        while (i < size && is_path_char((char)data[i])) i++;
        length = i - start;
        if (start > 0 && data[start - 1] == ':' && i + 1 < size) continue; /* a URL's scheme part ("http:") */
        if (length >= (int)sizeof(uri) || !has_extension((const char *)data + start, (size_t)length)) continue;
        if (i + 2 < size && data[i] == ':' && data[i + 1] == '/' && data[i + 2] == '/') continue;
        memcpy(uri, data + start, (size_t)length);
        uri[length] = '\0';
        add(uri, NULL, false, context);
    }
}

WGF_CORE_PRIV_ON_LINK(register_document_listers)
{
    wgf_core_priv_load_set_lister(".rml", list_document_files);
    wgf_core_priv_load_set_lister(".rcss", list_document_files);
}

/* ---- the part ----------------------------------------------------------------------- */

static void let_go(void)
{
    if (doc.captured) {
        wgf_input_set_pointer_captured(false);
        wgf_input_set_keyboard_captured(false);
        doc.captured = false;
    }
}

static void end_frame(void)
{
    if (!doc.drawn) let_go();
    doc.drawn = false;
}

/* ---- changes held until their images are local --------------------------------------- */

static void free_held(held_t *h)
{
    int i;
    for (i = 0; i < h->image_count; i++) {
        if (wgf_resource_get_status(h->images[i]) != WGF_RESOURCE_STATUS_NONE) wgf_resource_release(h->images[i]);
    }
    free(h->name);
    free(h->value);
    memset(h, 0, sizeof(*h));
}

static char *copy_of(const char *text)
{
    const size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

/* `h` made now; false when RmlUi refused it. */
static bool apply_held(const held_t *h, int slot)
{
    if (h->what == HELD_RML) return wgf_ui_priv_rml_set_text(slot, WGF_UI_PRIV_RML_RML, h->value);
    if (h->what == HELD_ATTRIBUTE) return wgf_ui_priv_rml_set_attribute(slot, h->name, h->value);
    return wgf_ui_priv_rml_set_property(slot, h->name, h->value);
}

typedef struct images_t {
    const char *base; /* the document's path, what a name is read against */
    held_t *held;
} images_t;

/* One name in a change's text (the documents' lister's): an image not local, a texture
 * started on it. Style sheets and RML are left to RmlUi. */
static void hold_image(const char *uri, const char *fallback_uri, bool required, void *context)
{
    images_t *images = (images_t *)context;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    const char *dot = strrchr(uri, '.');
    wgf_texture_t texture;
    (void)fallback_uri;
    (void)required;
    if (dot == NULL || strcmp(dot, ".rcss") == 0 || strcmp(dot, ".rml") == 0) return;
    if (uri[0] == '/' ? !wgf_core_priv_fs_normalize_path(uri + 1, path, sizeof(path))
                      : !wgf_asset_priv_is_relative_uri(uri) ||
                            !wgf_asset_priv_join_relative(images->base, uri, path, sizeof(path))) {
        return;
    }
    if (wgf_core_priv_fs_exists(path)) return;
    texture = wgf_core_priv_resource_find(WGF_CORE_PRIV_HANDLE_KIND_TEXTURE, path);
    if (texture != 0 && wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY) {
        wgf_resource_release(texture);
        return;
    }
    if (texture != 0) wgf_resource_release(texture);
    if (images->held->image_count == HELD_IMAGES) return; /* more than that: the rest as RmlUi finds them */
    texture = wgf_texture_create(path);
    if (texture != 0) images->held->images[images->held->image_count++] = texture;
}

/* A change to `element` (slot `slot`): made now when every image its text names is local,
 * else held until they are, in a later update; a change held for the same element and
 * name (or its RML) is dropped for this one. False when RmlUi refused it, or out of
 * memory. */
static bool change(wgf_element_t element, int slot, int what, const char *name, const char *value)
{
    held_t h;
    images_t images;
    wgf_document_t document = wgf_element_get_document(element);
    const char *base = wgf_resource_get_path(document);
    int i, kept = 0;
    for (i = 0; i < doc.held_count; i++) { /* superseded */
        held_t *old = &doc.held[i];
        if (old->element == element && old->what == what &&
            (what == HELD_RML || (old->name != NULL && name != NULL && strcmp(old->name, name) == 0))) {
            free_held(old);
        } else {
            doc.held[kept++] = *old;
        }
    }
    doc.held_count = kept;
    memset(&h, 0, sizeof(h));
    h.element = element;
    h.what = what;
    h.value = (char *)value; /* borrowed until it is held */
    h.name = (char *)name;
    if (value != NULL && base != NULL && base[0] != '\0') {
        images.base = base;
        images.held = &h;
        list_document_files((const unsigned char *)value, (int)strlen(value), hold_image, &images);
    }
    if (h.image_count == 0) return apply_held(&h, slot);
    if (doc.held_count == doc.held_capacity) {
        const int capacity = doc.held_capacity > 0 ? doc.held_capacity * 2 : 8;
        held_t *grown = (held_t *)realloc(doc.held, sizeof(held_t) * (size_t)capacity);
        if (grown == NULL) {
            h.name = h.value = NULL;
            free_held(&h);
            wgf_log_error("wgf_document: out of memory holding a change for its images");
            return false;
        }
        doc.held = grown;
        doc.held_capacity = capacity;
    }
    h.name = name != NULL ? copy_of(name) : NULL;
    h.value = copy_of(value);
    if (h.value == NULL || (name != NULL && h.name == NULL)) {
        free_held(&h);
        wgf_log_error("wgf_document: out of memory holding a change for its images");
        return false;
    }
    doc.held[doc.held_count++] = h;
    return true;
}

/* Each update: a change whose images have all settled (READY, or FAILED: RmlUi says what
 * it can't find) made, in the order made; one whose element went is dropped. */
static void update(float dt)
{
    int i, k, kept = 0;
    (void)dt;
    for (i = 0; i < doc.held_count; i++) {
        held_t *h = &doc.held[i];
        const int slot = slot_of_silently(h->element);
        bool settled = true;
        for (k = 0; k < h->image_count && settled; k++) {
            settled = wgf_resource_get_status(h->images[k]) != WGF_RESOURCE_STATUS_PENDING;
        }
        if (slot != 0 && !settled) {
            doc.held[kept++] = *h;
            continue;
        }
        if (slot != 0) apply_held(h, slot);
        free_held(h);
    }
    doc.held_count = kept;
}

static void free_textures(void)
{
    int i;
    for (i = 0; i < doc.texture_count; i++) {
        texture_t *t = &doc.textures[i];
        if (!t->used) continue;
        if (wgf_resource_get_status(t->texture) != WGF_RESOURCE_STATUS_NONE) wgf_resource_release(t->texture);
        if (t->view.id != SG_INVALID_ID) sg_destroy_view(t->view);
        if (t->image.id != SG_INVALID_ID) sg_destroy_image(t->image);
    }
    free(doc.textures);
    doc.textures = NULL;
    doc.texture_count = 0;
}

static void stop(void)
{
    int i;
    let_go();
    for (i = 0; i < doc.held_count; i++) free_held(&doc.held[i]);
    free(doc.held);
    if (doc.started) {
        wgf_core_priv_resource_unregister(&doc.document_pool); /* each document unloaded */
        wgf_core_priv_handle_pool_destroy(&doc.document_pool);
    }
    if (doc.rml) wgf_ui_priv_rml_stop(); /* its textures and the elements' handles let go of */
    if (doc.started) wgf_core_priv_handle_pool_destroy(&doc.element_pool);
    free_textures();
    for (i = 0; i < doc.family_count; i++) { /* the fonts may have gone with text, which stops first */
        if (wgf_resource_get_status(doc.families[i].font) != WGF_RESOURCE_STATUS_NONE)
            wgf_resource_release(doc.families[i].font);
    }
    free(doc.faces);
    free(doc.clipboard);
    memset(&doc, 0, sizeof(doc));
}

static wgf_core_priv_part_t part = {.name = "documents",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_UI,
                                    .update = update,
                                    .end_frame = end_frame,
                                    .stop = stop};

static void ensure_started(void)
{
    if (doc.started) return;
    if (!wgf_core_priv_handle_pool_init(&doc.document_pool, WGF_CORE_PRIV_HANDLE_KIND_DOCUMENT,
                                        (void **)&doc.documents, sizeof(document_t), 8, DOCUMENTS_MAX))
        return;
    if (!wgf_core_priv_handle_pool_init(&doc.element_pool, WGF_CORE_PRIV_HANDLE_KIND_ELEMENT, (void **)&doc.elements,
                                        sizeof(element_t), 64, ELEMENTS_MAX)) {
        wgf_core_priv_handle_pool_destroy(&doc.document_pool);
        return;
    }
    wgf_core_priv_resource_register(&doc.document_pool, &resource_kind);
    doc.font_version = 1;
    doc.cursor = -1;
    doc.started = true;
    wgf_core_priv_part_install(&part);
}

/* ---- the public API: documents ------------------------------------------------------- */

wgf_document_t wgf_document_create(const char *path)
{
    ensure_started();
    if (!doc.started) return 0;
    return wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_DOCUMENT, path);
}

bool wgf_document_set_visible(wgf_document_t document, bool visible)
{
    document_t *document_ptr = document_of(document);
    if (document_ptr == NULL || document_ptr->resource.status == WGF_RESOURCE_STATUS_FAILED) return false;
    document_ptr->want_visible = visible;
    if (document_ptr->number != 0) wgf_ui_priv_rml_set_visible(document_ptr->number, visible);
    return true;
}

bool wgf_document_is_visible(wgf_document_t document)
{
    const document_t *document_ptr = document_of(document);
    return document_ptr != NULL && document_ptr->want_visible;
}

wgf_element_t wgf_document_find(wgf_document_t document, const char *id)
{
    const document_t *document_ptr = document_of(document);
    if (document_ptr == NULL || document_ptr->number == 0 || id == NULL) return 0;
    return wgf_ui_priv_rml_find(document_ptr->number, id);
}

bool wgf_document_set_focus(wgf_document_t document, wgf_element_t element)
{
    const document_t *document_ptr = document_of(document);
    int slot = 0;
    if (document_ptr == NULL || document_ptr->number == 0) return false;
    if (element != 0) {
        slot = slot_of(element);
        if (slot == 0) return false;
    }
    return wgf_ui_priv_rml_set_focus(document_ptr->number, slot);
}

wgf_element_t wgf_document_get_focus(wgf_document_t document)
{
    const document_t *document_ptr = document_of(document);
    if (document_ptr == NULL || document_ptr->number == 0) return 0;
    return wgf_ui_priv_rml_get_focus(document_ptr->number);
}

int wgf_document_get_event_count(wgf_document_t document)
{
    const document_t *document_ptr = document_of(document);
    if (document_ptr == NULL || document_ptr->number == 0) return 0;
    return wgf_ui_priv_rml_event_count(document_ptr->number);
}

int wgf_document_take_events(wgf_document_t document, int *out, int count)
{
    const document_t *document_ptr = document_of(document);
    if (document_ptr == NULL || document_ptr->number == 0 || out == NULL || count < 2) return 0;
    return wgf_ui_priv_rml_take_events(document_ptr->number, out, count);
}

/* ---- the public API: elements ------------------------------------------------------- */

static const char *text_or_empty(const char *text)
{
    return text != NULL ? text : "";
}

wgf_document_t wgf_element_get_document(wgf_element_t element)
{
    uint16_t index;
    if (slot_of(element) == 0 || !wgf_core_priv_handle_pool_resolve(&doc.element_pool, element, &index)) return 0;
    return doc.elements[index].document;
}

const char *wgf_element_get_id(wgf_element_t element)
{
    const int slot = slot_of(element);
    return slot != 0 ? text_or_empty(wgf_ui_priv_rml_get_text(slot, WGF_UI_PRIV_RML_ID)) : "";
}

bool wgf_element_set_text(wgf_element_t element, const char *text)
{
    const int slot = slot_of(element);
    return slot != 0 && text != NULL && wgf_ui_priv_rml_set_text(slot, WGF_UI_PRIV_RML_TEXT, text);
}

const char *wgf_element_get_text(wgf_element_t element)
{
    const int slot = slot_of(element);
    return slot != 0 ? text_or_empty(wgf_ui_priv_rml_get_text(slot, WGF_UI_PRIV_RML_TEXT)) : "";
}

bool wgf_element_set_rml(wgf_element_t element, const char *rml)
{
    const int slot = slot_of(element);
    return slot != 0 && rml != NULL && change(element, slot, HELD_RML, NULL, rml);
}

const char *wgf_element_get_rml(wgf_element_t element)
{
    const int slot = slot_of(element);
    return slot != 0 ? text_or_empty(wgf_ui_priv_rml_get_text(slot, WGF_UI_PRIV_RML_RML)) : "";
}

bool wgf_element_set_attribute(wgf_element_t element, const char *name, const char *value)
{
    const int slot = slot_of(element);
    return slot != 0 && name != NULL && change(element, slot, HELD_ATTRIBUTE, name, value != NULL ? value : "");
}

const char *wgf_element_get_attribute(wgf_element_t element, const char *name)
{
    const int slot = slot_of(element);
    bool has = false;
    return slot != 0 ? text_or_empty(wgf_ui_priv_rml_get_attribute(slot, name, &has)) : "";
}

bool wgf_element_has_attribute(wgf_element_t element, const char *name)
{
    const int slot = slot_of(element);
    bool has = false;
    if (slot != 0) wgf_ui_priv_rml_get_attribute(slot, name, &has);
    return has;
}

bool wgf_element_remove_attribute(wgf_element_t element, const char *name)
{
    const int slot = slot_of(element);
    return slot != 0 && name != NULL && change(element, slot, HELD_ATTRIBUTE, name, NULL); /* a change held for it dropped */
}

bool wgf_element_set_class(wgf_element_t element, const char *name, bool on)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_set_class(slot, name, on);
}

bool wgf_element_has_class(wgf_element_t element, const char *name)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_has_class(slot, name);
}

bool wgf_element_set_property(wgf_element_t element, const char *name, const char *value)
{
    const int slot = slot_of(element);
    return slot != 0 && name != NULL && change(element, slot, HELD_PROPERTY, name, value != NULL ? value : "");
}

const char *wgf_element_get_property(wgf_element_t element, const char *name)
{
    const int slot = slot_of(element);
    return slot != 0 ? text_or_empty(wgf_ui_priv_rml_get_property(slot, name)) : "";
}

bool wgf_element_set_value(wgf_element_t element, const char *value)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_set_text(slot, WGF_UI_PRIV_RML_VALUE, value != NULL ? value : "");
}

const char *wgf_element_get_value(wgf_element_t element)
{
    const int slot = slot_of(element);
    return slot != 0 ? text_or_empty(wgf_ui_priv_rml_get_text(slot, WGF_UI_PRIV_RML_VALUE)) : "";
}

bool wgf_element_set_number(wgf_element_t element, float value)
{
    const int slot = slot_of(element);
    return slot != 0 && isfinite(value) && wgf_ui_priv_rml_set_number(slot, value);
}

float wgf_element_get_number(wgf_element_t element)
{
    const int slot = slot_of(element);
    float value = 0.0f;
    if (slot != 0) wgf_ui_priv_rml_get_number(slot, &value);
    return value;
}

bool wgf_element_set_checked(wgf_element_t element, bool checked)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_set_checked(slot, checked);
}

bool wgf_element_is_checked(wgf_element_t element)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_is_checked(slot);
}

bool wgf_element_set_disabled(wgf_element_t element, bool disabled)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_set_disabled(slot, disabled);
}

bool wgf_element_is_disabled(wgf_element_t element)
{
    const int slot = slot_of(element);
    return slot != 0 && wgf_ui_priv_rml_is_disabled(slot);
}

/* ---- fonts -------------------------------------------------------------------------- */

/* `text` lowercased into `out`, its quotes and the spaces around it dropped; false when it
 * is empty or too long. */
static bool family_name(const char *text, size_t length, char *out)
{
    size_t start = 0, end = length, i;
    while (start < end && (isspace((unsigned char)text[start]) || text[start] == '"' || text[start] == '\''))
        start++;
    while (end > start && (isspace((unsigned char)text[end - 1]) || text[end - 1] == '"' || text[end - 1] == '\''))
        end--;
    if (end == start || end - start >= FAMILY_MAX) return false;
    for (i = start; i < end; i++) out[i - start] = (char)tolower((unsigned char)text[i]);
    out[end - start] = '\0';
    return true;
}

static int family_index(const char *name)
{
    int i;
    for (i = 0; i < doc.family_count; i++) {
        if (strcmp(doc.families[i].name, name) == 0) return i;
    }
    return -1;
}

bool wgf_ui_set_document_font(const char *family, wgf_font_t font)
{
    char name[FAMILY_MAX];
    int at;
    if (family == NULL || !family_name(family, strlen(family), name)) return false;
    if (font != 0 && (WGF_CORE_PRIV_HANDLE_KIND(font) != WGF_CORE_PRIV_HANDLE_KIND_FONT ||
                      wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_NONE))
        return false;
    at = family_index(name);
    if (at < 0) {
        if (font == 0) return true;
        if (doc.family_count == FAMILIES_MAX) {
            wgf_log_warn("wgf_ui_set_document_font: more than %d families", FAMILIES_MAX);
            return false;
        }
        at = doc.family_count++;
        snprintf(doc.families[at].name, sizeof(doc.families[at].name), "%s", name);
        doc.families[at].font = 0;
    }
    if (font != 0) wgf_gfx_priv_font_retain(font); /* before the release, in case it is the same */
    if (doc.families[at].font != 0) wgf_resource_release(doc.families[at].font);
    doc.families[at].font = font;
    if (font == 0) doc.families[at] = doc.families[--doc.family_count];
    doc.font_version++;
    return true;
}

wgf_font_t wgf_ui_get_document_font(const char *family)
{
    char name[FAMILY_MAX];
    int at;
    if (family == NULL || !family_name(family, strlen(family), name)) return 0;
    at = family_index(name);
    return at >= 0 ? doc.families[at].font : 0;
}

/* The font a face draws in: its family's, the first of a list set, else the default. */
static wgf_font_t face_font(int face)
{
    const char *family, *comma;
    if (face <= 0 || face > doc.face_count) return 0;
    family = doc.faces[face - 1].family;
    for (;;) {
        char name[FAMILY_MAX];
        int at;
        comma = strchr(family, ',');
        if (family_name(family, comma != NULL ? (size_t)(comma - family) : strlen(family), name)) {
            at = family_index(name);
            if (at >= 0) return doc.families[at].font;
        }
        if (comma == NULL) return 0;
        family = comma + 1;
    }
}

int wgf_ui_priv_doc_font_face(const char *family, int size)
{
    char name[FAMILY_MAX];
    int i;
    if (size <= 0) return 0;
    if (family == NULL || strlen(family) >= FAMILY_MAX) family = "";
    for (i = 0; family[i] != '\0'; i++) name[i] = (char)tolower((unsigned char)family[i]);
    name[i] = '\0';
    for (i = 0; i < doc.face_count; i++) {
        if (doc.faces[i].size == size && strcmp(doc.faces[i].family, name) == 0) return i + 1;
    }
    if (doc.face_count == doc.face_capacity) {
        const int capacity = doc.face_capacity > 0 ? doc.face_capacity * 2 : 16;
        face_t *grown = (face_t *)realloc(doc.faces, sizeof(face_t) * (size_t)capacity);
        if (grown == NULL) return 0;
        doc.faces = grown;
        doc.face_capacity = capacity;
    }
    snprintf(doc.faces[doc.face_count].family, FAMILY_MAX, "%s", name);
    doc.faces[doc.face_count].size = size;
    return ++doc.face_count;
}

bool wgf_ui_priv_doc_font_metrics(int face, float *ascent, float *descent, float *line_height, float *x_height)
{
    const wgf_font_t font = face_font(face);
    const float size = face > 0 && face <= doc.face_count ? (float)doc.faces[face - 1].size : 16.0f;
    static const char x[] = "x"; /* its height: the x-height */
    float quad[8];
    if (!wgf_gfx_priv_font_vertical_metrics(font, size, ascent, descent, line_height)) return false;
    *x_height = wgf_gfx_priv_font_quads(font, x, x + 1, size, 0.0f, 0.0f, quad, 1) == 1 ? -quad[1] : *ascent * 0.5f;
    return true;
}

float wgf_ui_priv_doc_font_width(int face, const char *text, int length)
{
    if (face <= 0 || face > doc.face_count || text == NULL || length <= 0) return 0.0f;
    return wgf_gfx_priv_font_span_width(face_font(face), text, text + length, (float)doc.faces[face - 1].size);
}

int wgf_ui_priv_doc_font_quads(int face, const char *text, int length, float x, float y, float *quads, int max)
{
    if (face <= 0 || face > doc.face_count || text == NULL || length <= 0) return 0;
    return wgf_gfx_priv_font_quads(face_font(face), text, text + length, (float)doc.faces[face - 1].size, x, y, quads,
                                   max);
}

/* Changed when text laid out before would be drawn wrong: the atlas's glyphs moved, the
 * pixel density changed, or a family's font was set or finished loading. */
int wgf_ui_priv_doc_font_version(void)
{
    const unsigned atlas = wgf_gfx_priv_font_atlas_version();
    const float dpi = wgf_render_get_dpi_scale();
    int ready = wgf_resource_get_status(wgf_font_get_default()) == WGF_RESOURCE_STATUS_READY ? 1 : 0, i;
    for (i = 0; i < doc.family_count; i++) {
        if (wgf_resource_get_status(doc.families[i].font) == WGF_RESOURCE_STATUS_READY) ready++;
    }
    if (atlas != doc.last_atlas || dpi != doc.last_dpi || ready != doc.last_ready) {
        doc.last_atlas = atlas;
        doc.last_dpi = dpi;
        doc.last_ready = ready;
        doc.font_version++;
    }
    return doc.font_version;
}

/* ---- the system --------------------------------------------------------------------- */

void wgf_ui_priv_doc_log(int level, const char *message)
{
    switch (level) {
    case 3: wgf_log_error("wgf_document: %s", message); break;
    case 2: wgf_log_warn("wgf_document: %s", message); break;
    case 1: wgf_log_info("wgf_document: %s", message); break;
    default: wgf_log_debug("wgf_document: %s", message); break;
    }
}

double wgf_ui_priv_doc_seconds(void)
{
    return wgf_time_get_seconds();
}

void wgf_ui_priv_doc_set_clipboard(const char *text)
{
    wgf_input_set_clipboard(text);
}

const char *wgf_ui_priv_doc_get_clipboard(void)
{
    return wgf_input_get_clipboard();
}

void wgf_ui_priv_doc_set_cursor(const char *name)
{
    static const struct {
        const char *name;
        wgf_mouse_cursor_t cursor;
    } names[] = {{"pointer", WGF_MOUSE_CURSOR_HAND},         {"text", WGF_MOUSE_CURSOR_IBEAM},
                 {"crosshair", WGF_MOUSE_CURSOR_CROSSHAIR},  {"move", WGF_MOUSE_CURSOR_RESIZE_ALL},
                 {"ew-resize", WGF_MOUSE_CURSOR_RESIZE_EW},  {"ns-resize", WGF_MOUSE_CURSOR_RESIZE_NS},
                 {"nwse-resize", WGF_MOUSE_CURSOR_RESIZE_NWSE}, {"nesw-resize", WGF_MOUSE_CURSOR_RESIZE_NESW},
                 {"not-allowed", WGF_MOUSE_CURSOR_NOT_ALLOWED}, {"arrow", WGF_MOUSE_CURSOR_ARROW}};
    wgf_mouse_cursor_t cursor = WGF_MOUSE_CURSOR_DEFAULT;
    size_t i;
    for (i = 0; name != NULL && i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(names[i].name, name) == 0) cursor = names[i].cursor;
    }
    if ((int)cursor == doc.cursor) return;
    doc.cursor = (int)cursor;
    wgf_mouse_set_cursor(cursor);
}

unsigned char *wgf_ui_priv_doc_file_read(const char *path, int *size)
{
    char normalized[WGF_CORE_PRIV_FS_PATH_MAX];
    unsigned char *data = NULL;
    if (path == NULL || !wgf_core_priv_fs_normalize_path(path, normalized, sizeof(normalized))) return NULL;
    if (!wgf_core_priv_fs_read(normalized, &data, size)) return NULL;
    return data;
}

void wgf_ui_priv_doc_file_free(unsigned char *data)
{
    wgf_core_priv_fs_read_free(data);
}

/* ---- textures ----------------------------------------------------------------------- */

static uintptr_t add_texture(const texture_t *t)
{
    int i;
    for (i = 0; i < doc.texture_count && doc.textures[i].used; i++) {
    }
    if (i == doc.texture_count) {
        texture_t *grown = (texture_t *)realloc(doc.textures, sizeof(texture_t) * (size_t)(doc.texture_count + 16));
        if (grown == NULL) return 0;
        memset(grown + doc.texture_count, 0, sizeof(texture_t) * 16);
        doc.textures = grown;
        doc.texture_count += 16;
    }
    doc.textures[i] = *t;
    doc.textures[i].used = true;
    return (uintptr_t)i + 2;
}

static texture_t *texture_at(uintptr_t texture)
{
    if (texture < 2 || texture - 2 >= (uintptr_t)doc.texture_count || !doc.textures[texture - 2].used) return NULL;
    return &doc.textures[texture - 2];
}

uintptr_t wgf_ui_priv_doc_texture_load(const char *path, int *width, int *height)
{
    char normalized[WGF_CORE_PRIV_FS_PATH_MAX];
    texture_t t;
    unsigned char *data = NULL;
    int size = 0;
    void *decoded;
    memset(&t, 0, sizeof(t));
    *width = *height = 0;
    if (path == NULL || !wgf_core_priv_fs_normalize_path(path, normalized, sizeof(normalized))) return 0;
    /* a texture the program has at the path, loaded */
    t.texture = wgf_core_priv_resource_find(WGF_CORE_PRIV_HANDLE_KIND_TEXTURE, normalized);
    if (t.texture != 0 && wgf_resource_get_status(t.texture) != WGF_RESOURCE_STATUS_READY) {
        wgf_resource_release(t.texture);
        t.texture = 0;
    }
    if (t.texture == 0) { /* else the file, decoded now: it is local, the document's loader made it so */
        if (!wgf_core_priv_fs_read(normalized, &data, &size)) {
            wgf_log_warn("wgf_document: %s: no such image", normalized);
            return 0;
        }
        decoded = wgf_gfx_priv_texture_decode(data, size, normalized);
        wgf_core_priv_fs_read_free(data);
        if (decoded == NULL) return 0;
        t.texture = wgf_gfx_priv_texture_create_decoded(decoded);
        if (t.texture == 0) return 0;
    }
    t.width = wgf_texture_get_width(t.texture);
    t.height = wgf_texture_get_height(t.texture);
    *width = t.width;
    *height = t.height;
    {
        const uintptr_t handle = add_texture(&t);
        if (handle == 0) wgf_resource_release(t.texture);
        return handle;
    }
}

uintptr_t wgf_ui_priv_doc_texture_generate(const unsigned char *rgba, int width, int height)
{
    texture_t t;
    sg_image_desc image_desc;
    sg_view_desc view_desc;
    unsigned char *straight;
    const size_t size = (size_t)width * (size_t)height * 4;
    size_t i;
    uintptr_t handle;
    if (width <= 0 || height <= 0 || !wgf_gfx_priv_render_is_running()) return 0;
    straight = (unsigned char *)malloc(size);
    if (straight == NULL) return 0;
    for (i = 0; i < size; i += 4) { /* drawn blended straight, as libwgf draws */
        const unsigned a = rgba[i + 3];
        straight[i + 0] = (unsigned char)(a > 0 ? (rgba[i + 0] * 255u + a / 2) / a : 0);
        straight[i + 1] = (unsigned char)(a > 0 ? (rgba[i + 1] * 255u + a / 2) / a : 0);
        straight[i + 2] = (unsigned char)(a > 0 ? (rgba[i + 2] * 255u + a / 2) / a : 0);
        straight[i + 3] = (unsigned char)a;
    }
    memset(&t, 0, sizeof(t));
    memset(&image_desc, 0, sizeof(image_desc));
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    image_desc.data.mip_levels[0].ptr = straight;
    image_desc.data.mip_levels[0].size = size;
    image_desc.label = "wgf_document";
    t.image = sg_make_image(&image_desc);
    free(straight);
    if (sg_query_image_state(t.image) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(t.image);
        return 0;
    }
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.texture.image = t.image;
    t.view = sg_make_view(&view_desc);
    t.width = width;
    t.height = height;
    handle = add_texture(&t);
    if (handle == 0) {
        sg_destroy_view(t.view);
        sg_destroy_image(t.image);
    }
    return handle;
}

void wgf_ui_priv_doc_texture_release(uintptr_t texture)
{
    texture_t *t = texture_at(texture);
    if (t == NULL) return;
    if (wgf_resource_get_status(t->texture) != WGF_RESOURCE_STATUS_NONE) wgf_resource_release(t->texture);
    if (t->view.id != SG_INVALID_ID) sg_destroy_view(t->view);
    if (t->image.id != SG_INVALID_ID) sg_destroy_image(t->image);
    memset(t, 0, sizeof(*t));
}

/* ---- drawing ------------------------------------------------------------------------ */

void wgf_ui_priv_doc_draw(const wgf_ui_priv_vertex_t *vertices, int vertex_count, const int *indices, int index_count,
                          float tx, float ty, uintptr_t texture)
{
    sg_view view = {SG_INVALID_ID};
    sg_sampler sampler = {SG_INVALID_ID}; /* set when textured */
    sgl_pipeline pipeline = wgf_gfx_priv_render_pipeline_2d(WGF_ALPHA_MODE_BLEND);
    bool textured = false;
    int i;
    if (!doc.drawing || vertices == NULL || indices == NULL || index_count < 3) return;
    if (texture == WGF_UI_PRIV_ATLAS) {
        if (!wgf_gfx_priv_font_atlas(&view, &sampler, &pipeline)) return;
        textured = true;
    } else if (texture != 0) {
        const texture_t *t = texture_at(texture);
        int width, height;
        bool placeholder;
        if (t == NULL) return;
        if (t->texture != 0) {
            if (!wgf_gfx_priv_texture_get_binding(t->texture, &view, &sampler, &width, &height, &placeholder)) return;
        } else {
            view = t->view;
            sampler = wgf_gfx_priv_texture_sampler(WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                                                   WGF_TEXTURE_FILTER_LINEAR, false);
        }
        textured = true;
    }
    sgl_push_pipeline();
    sgl_load_pipeline(pipeline);
    if (textured) {
        sgl_enable_texture();
        sgl_texture(view, sampler);
    } else {
        sgl_disable_texture();
    }
    sgl_begin_triangles();
    for (i = 0; i + 2 < index_count; i += 3) {
        int k;
        if (indices[i] < 0 || indices[i] >= vertex_count || indices[i + 1] < 0 || indices[i + 1] >= vertex_count ||
            indices[i + 2] < 0 || indices[i + 2] >= vertex_count)
            continue;
        for (k = 0; k < 3; k++) {
            const wgf_ui_priv_vertex_t *v = &vertices[indices[i + k]];
            sgl_v2f_t2f_c4b(v->x + tx, v->y + ty, v->u, v->v, v->r, v->g, v->b, v->a);
        }
    }
    sgl_end();
    if (textured) sgl_disable_texture();
    sgl_pop_pipeline();
}

void wgf_ui_priv_doc_scissor(bool enable, int x, int y, int width, int height)
{
    if (!doc.drawing) return;
    if (doc.clipped) {
        wgf_render_pop_clip();
        doc.clipped = false;
    }
    if (enable) {
        wgf_render_push_clip(doc.origin_x + (float)x, doc.origin_y + (float)y, (float)(width > 0 ? width : 0),
                             (float)(height > 0 ? height : 0));
        doc.clipped = true;
    }
}

void wgf_ui_priv_doc_transform(const float *matrix)
{
    if (!doc.drawing) return;
    sgl_matrix_mode_modelview();
    sgl_load_identity();
    sgl_translate(doc.origin_x, doc.origin_y, 0.0f);
    if (matrix != NULL) sgl_mult_matrix(matrix);
}

/* ---- the frame ---------------------------------------------------------------------- */

static bool any_pad(wgf_input_state_t (*state)(int, wgf_gamepad_button_t), wgf_gamepad_button_t button,
                    wgf_input_state_t want)
{
    int pad;
    for (pad = 0; pad < 4; pad++) {
        if (state(pad, button) == want) return true;
    }
    return false;
}

/* The keys a document reads: every one libwgf has (wgf_keyboard.h). */
static int key_at(int i)
{
    static const int ranges[][2] = {{32, 32},   {39, 39},   {44, 57},   {59, 59},   {61, 61},
                                    {65, 93},   {96, 96},   {256, 269}, {280, 284}, {290, 313},
                                    {320, 336}, {340, 348}};
    size_t r;
    for (r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
        const int n = ranges[r][1] - ranges[r][0] + 1;
        if (i < n) return ranges[r][0] + i;
        i -= n;
    }
    return 0;
}

#define KEYS_MAX 160

bool wgf_ui_draw_documents(void)
{
    static const struct {
        wgf_gamepad_button_t button;
        int key;
    } pad_keys[] = {{WGF_GAMEPAD_BUTTON_DPAD_UP, WGF_KEY_UP},
                    {WGF_GAMEPAD_BUTTON_DPAD_DOWN, WGF_KEY_DOWN},
                    {WGF_GAMEPAD_BUTTON_DPAD_LEFT, WGF_KEY_LEFT},
                    {WGF_GAMEPAD_BUTTON_DPAD_RIGHT, WGF_KEY_RIGHT},
                    {WGF_GAMEPAD_BUTTON_SOUTH, WGF_KEY_ENTER}};
    wgf_ui_priv_rml_input_t input;
    int down[KEYS_MAX + 8], up[KEYS_MAX + 8];
    float visible_x, visible_y, visible_w, visible_h;
    bool pointer, keyboard;
    const unsigned frame = wgf_gfx_priv_render_get_frame();
    size_t p;
    int i, key;
    if (!wgf_gfx_priv_is_in_frame()) {
        wgf_log_error("wgf_ui_draw_documents: outside a frame (call it in the frame callback)");
        return false;
    }
    if (!doc.started) return false;
    if (!doc.rml) return true; /* none has loaded yet: nothing to draw */
    wgf_gfx_priv_render_get_visible(&visible_x, &visible_y, &visible_w, &visible_h);
    memset(&input, 0, sizeof(input));
    input.width = visible_w;
    input.height = visible_h;
    if (frame != doc.input_frame) { /* the frame's input, once */
        const wgf_vec2_t mouse = wgf_mouse_get_position(), wheel = wgf_mouse_get_wheel();
        doc.input_frame = frame;
        input.mouse_x = mouse.x - visible_x;
        input.mouse_y = mouse.y - visible_y;
        input.mouse_inside = true;
        for (i = 0; i < 3; i++) {
            if (wgf_mouse_is_pressed((wgf_mouse_button_t)i)) input.buttons_pressed |= 1 << i;
            if (wgf_mouse_is_released((wgf_mouse_button_t)i)) input.buttons_released |= 1 << i;
        }
        input.wheel_x = -wheel.x;
        input.wheel_y = -wheel.y; /* RmlUi's down is positive, libwgf's up */
        input.shift = wgf_keyboard_is_down(WGF_KEY_LEFT_SHIFT) || wgf_keyboard_is_down(WGF_KEY_RIGHT_SHIFT);
        input.ctrl = wgf_keyboard_is_down(WGF_KEY_LEFT_CONTROL) || wgf_keyboard_is_down(WGF_KEY_RIGHT_CONTROL);
        input.alt = wgf_keyboard_is_down(WGF_KEY_LEFT_ALT) || wgf_keyboard_is_down(WGF_KEY_RIGHT_ALT);
        input.super = wgf_keyboard_is_down(WGF_KEY_LEFT_SUPER) || wgf_keyboard_is_down(WGF_KEY_RIGHT_SUPER);
        for (i = 0; (key = key_at(i)) != 0 && i < KEYS_MAX; i++) {
            if (wgf_keyboard_is_pressed((wgf_keyboard_key_t)key) || wgf_keyboard_is_repeated((wgf_keyboard_key_t)key))
                down[input.key_down_count++] = key;
            if (wgf_keyboard_is_released((wgf_keyboard_key_t)key)) up[input.key_up_count++] = key;
        }
        for (p = 0; p < sizeof(pad_keys) / sizeof(pad_keys[0]); p++) { /* a pad's, as keys */
            if (any_pad(wgf_gamepad_get_button_state, pad_keys[p].button, WGF_INPUT_STATE_PRESSED))
                down[input.key_down_count++] = pad_keys[p].key;
            if (any_pad(wgf_gamepad_get_button_state, pad_keys[p].button, WGF_INPUT_STATE_RELEASED))
                up[input.key_up_count++] = pad_keys[p].key;
        }
        input.keys_down = down;
        input.keys_up = up;
        input.text = wgf_input_get_chars();
    } else {
        /* drawn again: the pointer where it was */
        const wgf_vec2_t mouse = wgf_mouse_get_position();
        input.mouse_x = mouse.x - visible_x;
        input.mouse_y = mouse.y - visible_y;
        input.mouse_inside = true;
    }
    doc.origin_x = visible_x;
    doc.origin_y = visible_y;
    wgf_gfx_priv_render_set_2d();
    doc.drawing = true;
    doc.clipped = false;
    wgf_ui_priv_doc_transform(NULL);
    wgf_ui_priv_rml_frame(&input, &pointer, &keyboard);
    doc.drawing = false;
    sgl_matrix_mode_modelview();
    sgl_load_identity(); /* back as the rest of the frame expects it */
    wgf_input_set_pointer_captured(pointer);
    wgf_input_set_keyboard_captured(keyboard);
    doc.captured = true;
    doc.drawn = true;
    return true;
}

bool wgf_ui_priv_document_element_bounds(uint32_t element, float *x, float *y, float *width, float *height)
{
    const int slot = slot_of(element);
    if (slot == 0 || !wgf_ui_priv_rml_element_box(slot, x, y, width, height)) return false;
    *x += doc.origin_x;
    *y += doc.origin_y;
    return true;
}
