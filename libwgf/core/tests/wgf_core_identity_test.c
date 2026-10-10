#include <stdio.h>
#include <string.h>

#include "wgf_identity.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"

/* The program's identity (wgf.h): the defaults, each name made one safe path component
 * as wgrender's wgri_app_clean_name makes it, what is refused and left as it was, and
 * the cache directory they name. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool cleans_to(const char *name, const char *want)
{
    char out[128];
    return wgf_core_priv_app_clean_name(name, out, sizeof(out)) && strcmp(out, want) == 0;
}

int main(void)
{
    char dir[600], longest[130], exe[128];
    /* the executable's name where there is one to tell (natively), else DefaultApp (the web) */
    const char *default_app = wgf_core_priv_os_executable_name(exe, sizeof(exe)) ? exe : "DefaultApp";
    memset(longest, 'a', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = '\0';

    expect(strcmp(wgf_identity_get_company(), "DefaultCompany") == 0, "the company defaults to DefaultCompany");
    expect(strcmp(wgf_identity_get_product(), default_app) == 0, "the app defaults to the executable's name, or DefaultApp");

    expect(cleans_to("My Game", "My Game"), "a plain name is kept");
    expect(cleans_to("a/b:c*d?e\"f<g>h|i\\j", "a_b_c_d_e_f_g_h_i_j"), "separators and Windows' refused characters become _");
    expect(cleans_to(" ..name.. ", "name"), "leading and trailing dots and spaces go");
    expect(cleans_to("CON", "_CON") && cleans_to("con.txt", "_con.txt") && cleans_to("Nul", "_Nul"),
           "a Windows device name gets a _ in front, whatever follows a dot");
    expect(cleans_to("COM1", "_COM1") && cleans_to("lpt9", "_lpt9") && cleans_to("COM0", "COM0") &&
               cleans_to("COMA", "COMA"),
           "COM1-9 and LPT1-9 too; COM0 and COMA are names");
    expect(cleans_to("con\x01sole", "con_sole"), "a control character becomes _");
    {
        char out[128];
        expect(!wgf_core_priv_app_clean_name(" ... ", out, sizeof(out)), "nothing left: refused");
        expect(!wgf_core_priv_app_clean_name(longest, out, sizeof(out)), "128 bytes or more: refused, never cut");
        expect(!wgf_core_priv_app_clean_name(NULL, out, sizeof(out)), "NULL: refused");
    }

    expect(wgf_identity_set_company("Whirling Gizmo") && strcmp(wgf_identity_get_company(), "Whirling Gizmo") == 0, "a company set");
    expect(wgf_identity_set_product("Hero: Reborn") && strcmp(wgf_identity_get_product(), "Hero_ Reborn") == 0, "an app set, made safe");
    expect(!wgf_identity_set_company("...") && strcmp(wgf_identity_get_company(), "Whirling Gizmo") == 0,
           "a company with nothing left is refused and the last stays");
    expect(!wgf_identity_set_product(longest) && strcmp(wgf_identity_get_product(), "Hero_ Reborn") == 0,
           "a name too long is refused and the last stays");
    if (wgf_core_priv_app_cache_dir(dir, sizeof(dir))) {
        expect(strstr(dir, "/Whirling Gizmo/Hero_ Reborn") != NULL && strchr(dir, '\\') == NULL,
               "the cache directory is <user cache>/<company>/<app>, with /");
    } else {
        printf("no user cache directory here: its path not checked\n");
    }
    expect(wgf_identity_set_company(NULL) && strcmp(wgf_identity_get_company(), "DefaultCompany") == 0, "NULL: the default again");
    expect(wgf_identity_set_product("") && strcmp(wgf_identity_get_product(), default_app) == 0, "\"\": the default again");
    return failures == 0 ? 0 : 1;
}
