# physics3d: bodies, sensors, and wheeled vehicles on Jolt (deps/jolt), an optional
# module. Its one C++ file (src/wgf_physics3d_jolt.cpp) is its way into Jolt; no header
# outside it names Jolt, and a program that never starts physics links none of it.
file(GLOB_RECURSE jolt_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/deps/jolt/src/Jolt/*.cpp")

# Jolt and the one C++ file, a target of their own, which the archive collects: C++ sees only
# Jolt and physics3d's src, never the other layers' include paths, whose deps/ directories
# each hold a VERSION file C++'s <version> would find where case is ignored (Windows). For the
# same reason deps/jolt/src, not deps/jolt, is on its path.
add_library(wgf_physics3d_jolt OBJECT src/wgf_physics3d_jolt.cpp ${jolt_sources})
target_include_directories(wgf_physics3d_jolt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(wgf_physics3d_jolt SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/jolt/src")
wgf_target_defaults(wgf_physics3d_jolt)
# Jolt's cross-platform determinism (the same lap to the bit on the desktop and the web), a
# layer and a mask of 16 bits each in its object layer, and no exceptions or RTTI, for size
target_compile_definitions(wgf_physics3d_jolt PRIVATE JPH_CROSS_PLATFORM_DETERMINISTIC JPH_OBJECT_LAYER_BITS=32
                                                      $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>)
set_target_properties(wgf_physics3d_jolt PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
                                                     CXX_VISIBILITY_PRESET hidden)
target_compile_options(wgf_physics3d_jolt PRIVATE
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/GR-;/EHs-c-;/fp:precise,-fno-exceptions;-fno-rtti;-ffp-contract=off>"
    # for size in a web release, as Clay is (docs/HISTORY.md, "Milestone 2, step 5")
    "$<$<AND:$<PLATFORM_ID:Emscripten>,$<CONFIG:Release>>:-Oz>")
# Jolt's own sources: its code, compiled as it comes, its warnings not libwgf's
set_source_files_properties(${jolt_sources} PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
# Jolt's narrow phase keeps its buffers on the stack, past the web's default of 64 KB (a box
# landing on a floor overran it: memory out of bounds on wasm, natively nothing): every web
# program linking libwgf gets 512 KB, a quarter of it measured enough (tools/webhost.py's host too)
if(EMSCRIPTEN)
    target_link_options(wgf_physics3d_jolt INTERFACE -sSTACK_SIZE=524288)
endif()
target_link_libraries(wgf PUBLIC wgf_physics3d_jolt)

# physics3d's C: its public calls, the body and vehicle components, the tick
wgf_layer(physics3d
    SOURCES
        src/wgf_physics3d.c
    DEPS
        math core gfx ecs)

if(WGF_TESTS)
    wgf_test(physics3d wgf_physics3d_jolt_test)
    if(WGF_HEADLESS)
        wgf_test(physics3d wgf_physics3d_test)
    endif()
endif()
