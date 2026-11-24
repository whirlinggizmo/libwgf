#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_core_resource_priv.h"
#include "wgf_document.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_time.h"
#include "wgf_ui.h"
#include "wgf_ui_priv.h"
#include "wgf_window.h"

/* Documents loaded again in place (wgf_asset_reload), headless, frame by frame through the
 * runtime: the program changes a document's elements (text, a class, an attribute, a
 * checkbox, an element's RML, the focus), and its file is saved changed, an element gone, one
 * new, the file's own text changed. The same handles, the elements of their ids, the
 * program's changes made again (the last of each), the file's text where the program set
 * none, the focus and visibility kept, the gone element's handle refused. Then its style
 * sheet saved, read again rather than cached; then the file saved half-written, keeping it as
 * it was. And first loads of files that aren't documents FAILED: one cut short, which RmlUi
 * would make a document of, and one with no <rml>. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        fflush(stdout);
        failures++;
    }
}

static const char first[] = "<rml><head><link type=\"text/rcss\" href=\"style.rcss\"/></head><body>\n"
                            "<h1 id=\"title\">Options</h1>\n"
                            "<p id=\"score\">0</p>\n"
                            "<button id=\"go\">Go</button>\n"
                            "<input id=\"on\" type=\"checkbox\" value=\"on\"/>\n"
                            "<div id=\"list\"></div>\n"
                            "<p id=\"gone\">soon gone</p>\n"
                            "<div id=\"box\"></div>\n"
                            "</body></rml>\n";

/* saved changed: #gone gone, #extra new, #go first, the title's text another */
static const char second[] = "<rml><head><link type=\"text/rcss\" href=\"style.rcss\"/></head><body>\n"
                             "<button id=\"go\">Go</button>\n"
                             "<h1 id=\"title\">Options, again</h1>\n"
                             "<p id=\"score\">0</p>\n"
                             "<input id=\"on\" type=\"checkbox\" value=\"on\"/>\n"
                             "<div id=\"list\"></div>\n"
                             "<p id=\"extra\">new</p>\n"
                             "<div id=\"box\"></div>\n"
                             "</body></rml>\n";

static const char style[] = "body { display: block; width: 100%; height: 100%; font-family: Mono; }\n"
                            "h1, p, div, button { display: block; }\n"
                            "button { width: 100px; height: 20px; tab-index: auto; }\n"
                            "input.checkbox { display: block; width: 20px; height: 20px; }\n"
                            "#box { width: 100px; height: 10px; }\n";

static const char wider[] = "body { display: block; width: 100%; height: 100%; font-family: Mono; }\n"
                            "h1, p, div, button { display: block; }\n"
                            "button { width: 100px; height: 20px; tab-index: auto; }\n"
                            "input.checkbox { display: block; width: 20px; height: 20px; }\n"
                            "#box { width: 150px; height: 10px; }\n";

static int step;
static double started;
static wgf_fs_task_t tasks[2];
static wgf_document_t document, cut, plain;
static wgf_element_t title, score, go, on, list, gone, box;

/* `text` written over `path` as the step's task. */
static void save(int task, const char *path, const char *text)
{
    tasks[task] = wgf_fs_write(path, (const unsigned char *)text, (int)strlen(text));
}

static bool saved(int count)
{
    int i;
    for (i = 0; i < count; i++) {
        if (wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_PENDING) return false;
    }
    for (i = 0; i < count; i++) {
        expect(wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_DONE, "a file written");
        wgf_fs_task_destroy(tasks[i]);
    }
    return true;
}

static float width_of(wgf_element_t element)
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    wgf_ui_priv_document_element_bounds(element, &x, &y, &w, &h);
    return w;
}

static void changed_by_the_program(void)
{
    title = wgf_document_find(document, "title");
    score = wgf_document_find(document, "score");
    go = wgf_document_find(document, "go");
    on = wgf_document_find(document, "on");
    list = wgf_document_find(document, "list");
    gone = wgf_document_find(document, "gone");
    box = wgf_document_find(document, "box");
    expect(title && score && go && on && list && gone && box, "its elements found");
    expect(wgf_element_set_text(score, "42") && wgf_element_set_class(title, "hot", true) &&
               wgf_element_set_attribute(title, "data-x", "7") && wgf_element_set_checked(on, true) &&
               wgf_element_set_rml(list, "<p id=\"item\">a</p>") && wgf_element_set_text(score, "43") &&
               wgf_document_set_focus(document, go),
           "changed by the program");
    expect(width_of(box) == 100.0f, "its box as its style sheet says");
}

static void after_the_save(void)
{
    expect(wgf_resource_get_status(document) == WGF_RESOURCE_STATUS_READY, "READY, the same handle");
    expect(wgf_handle_is_alive(title) && strcmp(wgf_element_get_text(title), "Options, again") == 0,
           "the title's handle on the new title, its text the file's: the program set none");
    expect(strcmp(wgf_element_get_text(score), "43") == 0, "the program's text made again, the last of it");
    expect(wgf_element_has_class(title, "hot") && strcmp(wgf_element_get_attribute(title, "data-x"), "7") == 0,
           "its class and attribute made again");
    expect(wgf_element_is_checked(on), "the checkbox checked again");
    expect(wgf_document_find(document, "item") != 0, "the list's RML made again");
    expect(wgf_document_find(document, "extra") != 0, "the new file's new element");
    expect(!wgf_handle_is_alive(gone), "an element the new file hasn't: its handle refused");
    expect(wgf_document_is_visible(document) && wgf_document_get_focus(document) == go,
           "still shown, the focus on the element of its id");
}

static void on_init(void *user)
{
    (void)user;
    wgf_asset_enable_reload(); /* what wgf serve does: changes kept from here */
    save(0, "reload_test/menu.rml", first);
    save(1, "reload_test/style.rcss", style);
    started = wgf_time_get_seconds();
}

static void on_frame(void *user)
{
    (void)user;
    if (wgf_time_get_seconds() - started > 30.0) {
        expect(false, "every step in 30 s");
        printf("(at step %d)\n", step);
        wgf_app_quit();
        return;
    }
    if (document != 0 && wgf_resource_get_status(document) == WGF_RESOURCE_STATUS_READY) wgf_ui_draw_documents();
    switch (step) {
    case 0:
        if (!saved(2)) return;
        document = wgf_document_create("reload_test/menu.rml");
        wgf_document_set_visible(document, true);
        save(0, "reload_test/cut.rml", "<rml><head></head><body><p id=\"a\">one</p><div id=\"b\"><p>tw");
        save(1, "reload_test/plain.rml", "just some text\n");
        step++;
        break;
    case 1:
        if (wgf_resource_get_status(document) != WGF_RESOURCE_STATUS_READY || !saved(2)) return;
        wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* their errors and warnings, on purpose */
        cut = wgf_document_create("reload_test/cut.rml");
        plain = wgf_document_create("reload_test/plain.rml");
        step++; /* drawn once, laid out */
        break;
    case 2:
        if (wgf_resource_get_status(cut) == WGF_RESOURCE_STATUS_PENDING ||
            wgf_resource_get_status(plain) == WGF_RESOURCE_STATUS_PENDING) {
            return;
        }
        wgf_log_set_level(WGF_LOG_LEVEL_INFO);
        expect(wgf_resource_get_status(cut) == WGF_RESOURCE_STATUS_FAILED,
               "a file cut short: FAILED, though RmlUi would make a document of it");
        expect(wgf_resource_get_status(plain) == WGF_RESOURCE_STATUS_FAILED, "a file with no <rml>: FAILED");
        wgf_resource_release(cut);
        wgf_resource_release(plain);
        changed_by_the_program();
        save(0, "reload_test/menu.rml", second);
        step++;
        break;
    case 3:
        if (!saved(1)) return;
        expect(wgf_asset_can_reload("reload_test/menu.rml") && wgf_asset_reload("reload_test/menu.rml") == 1,
               "a document loads again");
        step++;
        break;
    case 4:
        if (wgf_core_priv_resource_is_reloading(document)) return;
        after_the_save();
        save(0, "reload_test/style.rcss", wider);
        step++;
        break;
    case 5:
        if (!saved(1)) return;
        expect(wgf_asset_can_reload("reload_test/style.rcss") && wgf_asset_reload("reload_test/style.rcss") == 1,
               "its style sheet saved: the document that names it loads again");
        step++;
        break;
    case 6:
        if (wgf_core_priv_resource_is_reloading(document)) return;
        step++; /* drawn once, laid out */
        break;
    case 7:
        expect(width_of(wgf_document_find(document, "box")) == 150.0f,
               "the style sheet read again, not RmlUi's cached one");
        save(0, "reload_test/menu.rml", "<rml><head></head><body><h1 id=\"title\">Opt");
        step++;
        break;
    case 8:
        if (!saved(1)) return;
        wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* its error and warning, on purpose */
        expect(wgf_asset_reload("reload_test/menu.rml") == 1, "the file saved half-written, loading again");
        step++;
        break;
    case 9:
        if (wgf_core_priv_resource_is_reloading(document)) return;
        wgf_log_set_level(WGF_LOG_LEVEL_INFO);
        expect(wgf_resource_get_status(document) == WGF_RESOURCE_STATUS_READY &&
                   strcmp(wgf_element_get_text(title), "Options, again") == 0 &&
                   strcmp(wgf_element_get_text(score), "43") == 0,
               "what it had is kept");
        wgf_resource_release(document);
        wgf_app_quit();
        step++;
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 600);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(step == 10, "every step ran");
    printf("wgf_ui_document_reload_test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
