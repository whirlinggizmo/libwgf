# 2D drawing, on sokol_gfx and sokol_gl, whose implementations platform compiles.
wgf_layer(gfx
    SOURCES
        src/color/wgf_gfx_color.c
        src/draw/wgf_gfx_draw.c
        src/emitter/wgf_gfx_emitter2d.c
        src/node/wgf_gfx_camera2d.c
        src/node/wgf_gfx_canvas.c
        src/node/wgf_gfx_node.c
        src/render/wgf_gfx_presentation.c
        src/render/wgf_gfx_render.c
        src/shape/wgf_gfx_shape2d.c
        src/sprite/wgf_gfx_sprite.c
        src/text/wgf_gfx_font.c
        src/text/wgf_gfx_fontstash_impl.c
        src/text/wgf_gfx_text.c
        src/texture/wgf_gfx_stb_impl.c
        src/texture/wgf_gfx_texture.c
    DEPS
        math core platform asset)

target_include_directories(wgf_gfx SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/sokol" "${PROJECT_SOURCE_DIR}/deps/stb"
                           "${PROJECT_SOURCE_DIR}/deps/fontstash" "${PROJECT_SOURCE_DIR}/deps/jetbrains-mono")
# vendored code is compiled as it comes: its warnings are not ours to fix
set_source_files_properties(src/texture/wgf_gfx_stb_impl.c src/text/wgf_gfx_fontstash_impl.c
                            PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
# what includes gfx's private headers (app, gfx's tests) sees sokol's
target_include_directories(wgf_gfx_priv SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/deps/sokol"
                           "${PROJECT_SOURCE_DIR}/deps/jetbrains-mono") # the tests compare the built-in font

if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_gfx PRIVATE m)
endif()
if(WGF_TESTS)
    wgf_test(gfx wgf_gfx_color_test)
    if(WGF_HEADLESS)
        wgf_test(gfx wgf_gfx_frame_test)
        wgf_test(gfx wgf_gfx_presentation_test)
        wgf_test(gfx wgf_gfx_node_test)
        wgf_test(gfx wgf_gfx_text_test)
        wgf_test(gfx wgf_gfx_texture_test)
        wgf_test(gfx wgf_gfx_draw_test)
        wgf_test(gfx wgf_gfx_shape_test)
        wgf_test(gfx wgf_gfx_sprite_test)
        wgf_test(gfx wgf_gfx_emitter_test)
    endif()
    # pixels, read back from WebGL2 in a browser
    wgf_web_test(gfx wgf_gfx_frame_web_test 1)
    wgf_web_test(gfx wgf_gfx_presentation_web_test 1)
    wgf_window_test(gfx wgf_gfx_presentation_window_test)
    wgf_web_test(gfx wgf_gfx_texture_web_test 1)
    wgf_web_test(gfx wgf_gfx_text_web_test 1)
    wgf_web_test(gfx wgf_gfx_canvas_web_test 1)
endif()
