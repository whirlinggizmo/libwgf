#ifndef WGF_IDENTITY_H
#define WGF_IDENTITY_H

#include <stdbool.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sets or gets the program's identity: its company and product names.
 *
 * On the desktop, the identity names the program's own directories:
 * - Its saved files ("user:" paths, wgf_fs.h) go under <the user's data>/<company>/<product>.
 * - Its downloaded assets (wgf_asset.h) go under <the user's cache>/<company>/<product>.
 * - The company is "DefaultCompany" until set, so that no unset name looks like anyone's.
 * - The product is the executable's name without its extension. Where that can't be
 *   told, it is "DefaultApp".
 *
 * Set both for anything you ship. Two programs left with the defaults and the same name
 * share a cache, and renaming the executable would leave its cache behind. Set them before
 * libwgf keeps any files.
 *
 * Each name is made safe as one path component:
 * - Separators, and the characters Windows refuses, become "_".
 * - Leading and trailing dots and spaces are removed.
 * - A Windows device name, such as "CON", gets a "_" in front.
 *
 * NULL or "" puts the default back. A setter returns false, and keeps the last name, if
 * nothing is left of the name after that, or if it is 128 bytes or more. A long name is
 * refused, never cut short.
 *
 * On the web nothing is kept by these names, since the browser keeps each site's files
 * apart itself. */
WGF_API bool wgf_identity_set_company(const char *company);
WGF_API const char *wgf_identity_get_company(void);
WGF_API bool wgf_identity_set_product(const char *name);
WGF_API const char *wgf_identity_get_product(void);

#ifdef __cplusplus
}
#endif

#endif
