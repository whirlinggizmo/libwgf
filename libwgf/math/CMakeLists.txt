# Headers, every operation static inline, C's own (a binding does its math in its own
# language: docs/HISTORY.md, "Math values are returned, never taken"); and the trigonometry
# the same on every target, compiled once (wgf_trig.h); and 2D collision as plain math
# (wgf_collide2d.h), exported, so a binding has it too.
wgf_layer(math SOURCES src/wgf_math_trig.c src/wgf_math_collide2d.c)
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_math INTERFACE m) # sqrtf and friends, inlined into callers
endif()

if(WGF_TESTS)
    wgf_test(math wgf_math_test)
    wgf_test(math wgf_math_collide2d_test)
endif()
