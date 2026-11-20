#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_app_priv.h"

/* The program's options, from a page's query as a test gives it: present or not, with a
 * value or without, numbers or not, decoded, the last of a name winning; and NULL. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    wgf_app_priv_set_options("?enemies=200&wave&name=Rob%20K&speed=1.5&count=x&enemies=300&greeting=a+b");
    expect(wgf_app_has_option("enemies") && wgf_app_get_option_int("enemies", 0) == 300, "a number, the last given winning");
    expect(wgf_app_has_option("wave") && strcmp(wgf_app_get_option("wave"), "") == 0 && wgf_app_get_option_int("wave", 7) == 7,
           "one given without a value");
    expect(strcmp(wgf_app_get_option("name"), "Rob K") == 0 && strcmp(wgf_app_get_option("greeting"), "a b") == 0,
           "decoded as a query is");
    expect(wgf_app_get_option_float("speed", 0) == 1.5f && wgf_app_get_option_int("count", 9) == 9, "a float; not a number");
    expect(!wgf_app_has_option("missing") && strcmp(wgf_app_get_option("missing"), "") == 0 &&
               wgf_app_get_option_float("missing", 2.5f) == 2.5f && !wgf_app_has_option(NULL),
           "not given, and NULL");
    wgf_app_priv_set_options(NULL);
    expect(!wgf_app_has_option("enemies"), "the program's own again: this test was given none");
    return failures == 0 ? 0 : 1;
}
