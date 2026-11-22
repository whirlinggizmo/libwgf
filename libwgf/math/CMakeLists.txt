# Headers, every operation static inline, C's own (a binding does its math in its own
# language: docs/HISTORY.md, "Math values are returned, never taken"); and the trigonometry
# the same on every target, compiled once (wgf_trig.h); and 2D collision as plain math
# (wgf_collide2d.h, wgf_collide3d.h) and easing (wgf_ease.h), exported, so a binding has them too.
wgf_layer(math SOURCES src/wgf_math_trig.c src/wgf_math_collide2d.c src/wgf_math_collide3d.c src/wgf_math_ease.c src/wgf_math_spline.c)
# the trigonometry and the easing curves, the same bits on every target: no multiply and add
# fused into an FMA, which a target without one would round differently
set_source_files_properties(src/wgf_math_trig.c src/wgf_math_ease.c PROPERTIES COMPILE_OPTIONS
                            "$<IF:$<C_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>")
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_math INTERFACE m) # sqrtf and friends, inlined into callers
endif()

if(WGF_TESTS)
    wgf_test(math wgf_math_test)
    wgf_test(math wgf_math_collide2d_test)
    wgf_test(math wgf_math_collide3d_test)
    wgf_test(math wgf_math_spline_test)
endif()
