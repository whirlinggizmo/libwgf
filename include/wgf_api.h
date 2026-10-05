#ifndef WGF_API_H
#define WGF_API_H

/* WGF_API marks every public function: exported from the library, where symbols are
 * hidden by default. Every public header includes this. */

#if defined(_WIN32)
#define WGF_API
#else
#define WGF_API __attribute__((visibility("default")))
#endif

#endif
