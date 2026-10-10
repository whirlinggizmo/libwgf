#ifndef WGF_IDENTITY_H
#define WGF_IDENTITY_H

#include <stdbool.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The program's identity: the files libwgf keeps for it on the desktop (fs's "cache:"
 * paths) go under <the user's cache>/<company>/<product>. The company is "DefaultCompany" until set, so that
 * nothing unset looks like anyone's; the product is the executable's name, less its
 * extension ("DefaultApp" where there is none to tell). Set both for anything shipped:
 * two programs left with the defaults and the same name share a cache, and renaming
 * the executable would leave its cache behind. Each is made safe as one path component
 * (separators and the characters Windows refuses become "_", leading and trailing dots
 * and spaces go, a Windows device name such as "CON" gets a "_" in front); NULL or ""
 * puts the default back. False, and the last name kept, for one with nothing left after
 * that, or of 128 bytes or more: refused, never cut short. Set them before
 * anything is kept. On the web nothing is kept by these names: the browser keeps a
 * site's files apart itself. */
WGF_API bool wgf_identity_set_company(const char *company);
WGF_API const char *wgf_identity_get_company(void);
WGF_API bool wgf_identity_set_product(const char *name);
WGF_API const char *wgf_identity_get_product(void);

#ifdef __cplusplus
}
#endif

#endif
