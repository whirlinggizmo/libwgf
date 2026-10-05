# Headers alone: every operation is static inline, C's own; a binding does its math in
# its own language (docs/HISTORY.md, "Math values are returned, never taken").
wgf_layer(math)
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_math INTERFACE m) # sqrtf and friends, inlined into callers
endif()

if(WGF_TESTS)
    wgf_test(math wgf_math_test)
endif()
