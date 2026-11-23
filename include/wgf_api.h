#ifndef WGF_API_H
#define WGF_API_H

/* WGF_API marks every public function as exported from the library.
 *
 * The library hides its symbols by default, and every public header includes this one.
 * On Windows the library is static and nothing is exported, so WGF_API expands to nothing.
 * tools/headers.py parses with WGF_API_PARSE defined, so it sees the mark on every
 * platform. */

#if defined(_WIN32) && !defined(WGF_API_PARSE)
#define WGF_API
#else
#define WGF_API __attribute__((visibility("default")))
#endif

#endif
