#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_resource.h"
#include "wgf_text_buffer.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The UI's text views, headless, frame by frame through the runtime: a paragraph wrapped into
 * more lines than the view shows, newlines kept; a chat that follows its end as lines are
 * added, and stays where it is scrolled up to; a buffer set again, and one released; and the
 * refusals. Its content's height is read through its scroll: scrolled as far as it goes, it
 * is the content's less the view's. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static wgf_text_buffer_t chat, credits;

static void views(void)
{
    expect(wgf_ui_begin(), "begun");
    wgf_ui_begin_panel("panel");
    expect(wgf_ui_text_view("chat", chat, 300, 200, true), "the chat");
    expect(wgf_ui_text_view("credits", credits, 200, 100, false), "the credits");
    wgf_ui_end_panel();
    expect(wgf_ui_end(), "ended");
}

/* How far the view `id` scrolls: its content's height less its own. */
static float most(const char *id)
{
    const wgf_vec2_t was = wgf_ui_get_scroll(id);
    float y;
    wgf_ui_set_scroll(id, 0, 1e6f);
    y = wgf_ui_get_scroll(id).y;
    wgf_ui_set_scroll(id, was.x, was.y);
    return y;
}

static int frame_number;
static float tall; /* the credits' content, as first laid out */

static void on_frame(void *user)
{
    int i;
    (void)user;
    switch (frame_number++) {
    case 0:
        chat = wgf_text_buffer_create_text("player_1: hello\nplayer_2: hi\nplayer_1: ready?");
        credits = wgf_text_buffer_create_text(
            "Made with libwgf, by everyone who wrote a line of it, tested it, or played the games made "
            "with it, over many evenings and some mornings.\n\nThanks.");
        views();
        break;
    case 1:
    case 2: views(); break;
    case 3:
        views();
        expect(wgf_ui_get_scroll("chat").y == 0.0f && most("chat") == 0.0f, "three lines: they fit, no scroll");
        tall = most("credits");
        expect(tall > 0.0f, "a paragraph wrapped past the view");
        for (i = 0; i < 30; i++) wgf_text_buffer_append(chat, "\nplayer_3: one more line of the chat");
        break;
    case 4:
    case 5: views(); break;
    case 6:
        views();
        expect(most("chat") > 0.0f && wgf_ui_get_scroll("chat").y == most("chat"), "lines added: kept at its end");
        wgf_ui_set_scroll("chat", 0, 0); /* scrolled up */
        break;
    case 7:
        views();
        wgf_text_buffer_append(chat, "\nplayer_4: and another");
        break;
    case 8:
    case 9: views(); break;
    case 10:
        views();
        expect(wgf_ui_get_scroll("chat").y == 0.0f, "scrolled up: it stays as lines are added");
        wgf_text_buffer_set_text(credits, "Short.");
        break;
    case 11:
    case 12: views(); break;
    case 13:
        views();
        expect(most("credits") == 0.0f, "set again: its lines broken again");
        wgf_resource_release(credits);
        break;
    case 14:
    case 15: views(); break;
    case 16:
        views();
        expect(most("credits") == 0.0f, "a buffer released: nothing shown");
        expect(wgf_ui_begin(), "begun");
        expect(!wgf_ui_text_view(NULL, chat, 100, 100, false) && !wgf_ui_text_view("none", 0, 100, 100, false) &&
                   !wgf_ui_text_view("bad", chat, -1, 100, false),
               "no id, no buffer, a size below 0: refused");
        expect(wgf_ui_end(), "ended");
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 800);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 17, "every frame ran");
    printf("wgf_ui_text_view_test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
