# 2D drawing, on sokol_gfx and sokol_gl, whose implementations platform compiles.
wgf_layer(gfx
    SOURCES
        src/color/wgf_gfx_color.c
        src/draw/wgf_gfx_draw.c
        src/render/wgf_gfx_render.c
    DEPS
        math core platform)

target_include_directories(wgf_gfx SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/sokol")
# what includes gfx's private headers (app, gfx's tests) sees sokol's
target_include_directories(wgf_gfx_priv SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/deps/sokol")

if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_gfx PRIVATE m)
endif()
if(WGF_TESTS)
    wgf_test(gfx wgf_gfx_color_test)
    if(WGF_HEADLESS)
        wgf_test(gfx wgf_gfx_frame_test)
    endif()
endif()
