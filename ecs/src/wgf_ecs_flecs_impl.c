/* flecs, compiled once for the ecs: its core alone (FLECS_CUSTOM_BUILD, with only its
 * default OS API: allocation, time, and a lock), as the module's CMakeLists.txt defines
 * for every file that includes flecs.h. Vendored code, compiled as it comes. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* clock_gettime, under strict C11 */
#endif
#include "flecs.c"
