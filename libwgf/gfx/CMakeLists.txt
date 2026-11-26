# 2D drawing, on sokol_gfx and sokol_gl, whose implementations platform compiles.
# An image file's decode: the browser's in a page, stb_image's everywhere else (a headless web
# build runs under node, which has no image decoder).
if(WGF_BROWSER)
    set(gfx_decode_source src/texture/wgf_gfx_decode_web.c)
    set(gfx_glyph_source src/text/wgf_gfx_glyph_web.c)
else()
    set(gfx_decode_source src/texture/wgf_gfx_decode_stb.c)
    set(gfx_glyph_source src/text/wgf_gfx_glyph_stb.c)
endif()

wgf_layer(gfx
    SOURCES
        ${gfx_decode_source}
        ${gfx_glyph_source}
        src/color/wgf_gfx_color.c
        src/draw/wgf_gfx_draw.c
        src/draw/wgf_gfx_draw_more.c
        src/draw/wgf_gfx_draw3d.c
        src/draw/wgf_gfx_draw_model.c
        src/draw/wgf_gfx_draw_rounded.c
        src/emitter/wgf_gfx_emitter.c
        src/emitter/wgf_gfx_emitter2d.c
        src/emitter/wgf_gfx_emitter3d.c
        src/emitter/wgf_gfx_emitter_scene.c
        src/material/wgf_gfx_material.c
        src/material/wgf_gfx_material_custom.c
        src/material/wgf_gfx_shader.c
        src/mesh/wgf_gfx_cgltf_impl.c
        src/mesh/wgf_gfx_mesh.c
        src/mesh/wgf_gfx_mesh_gltf.c
        src/mesh/wgf_gfx_mesh_merge.c
        src/mesh/wgf_gfx_mesh_shapes.c
        src/actor/wgf_gfx_camera2d.c
        src/actor/wgf_gfx_camera3d.c
        src/actor/wgf_gfx_stage2d.c
        src/actor/wgf_gfx_pick2d.c
        src/actor/wgf_gfx_pointer.c
        src/actor/wgf_gfx_actor.c
        src/actor/wgf_gfx_follow.c
        src/actor/wgf_gfx_fly.c
        src/render/wgf_gfx_presentation.c
        src/render/wgf_gfx_render.c
        src/render/wgf_gfx_render_commands.c
        src/render/wgf_gfx_render_send.c
        src/render/wgf_gfx_render_effect.c
        src/render/wgf_gfx_render_target.c
        src/shape/wgf_gfx_shape2d.c
        src/shape/wgf_gfx_shape3d.c
        src/sprite/wgf_gfx_sprite.c
        src/sprite/wgf_gfx_sprite3d.c
        src/sprite/wgf_gfx_sprite_material.c
        src/sprite/wgf_gfx_sprite_batch.c
        src/sprite/wgf_gfx_sprite_batch2d.c
        src/sprite/wgf_gfx_sprite_run.c
        src/sprite/wgf_gfx_sheet.c
        src/stage/wgf_gfx_animation.c
        src/stage/wgf_gfx_light.c
        src/stage/wgf_gfx_model.c
        src/stage/wgf_gfx_model_file.c
        src/stage/wgf_gfx_shadow.c
        src/stage/wgf_gfx_environment.c
        src/stage/wgf_gfx_stage3d.c
        src/stage/wgf_gfx_stage3d_custom.c
        src/stage/wgf_gfx_pick3d.c
        src/text/wgf_gfx_font.c
        src/text/wgf_gfx_font_bitmap.c
        src/text/wgf_gfx_font_bitmap_read.c
        src/text/wgf_gfx_fontstash_impl.c
        src/text/wgf_gfx_text.c
        src/tilemap/wgf_gfx_tilemap.c
        src/tilemap/wgf_gfx_tilemap_layer.c
        src/texture/wgf_gfx_image.c
        src/texture/wgf_gfx_image_gen.c
        src/texture/wgf_gfx_image_texture.c
        src/texture/wgf_gfx_cubemap.c
        src/texture/wgf_gfx_ktx.c
        src/texture/wgf_gfx_stb_impl.c
        src/texture/wgf_gfx_png.c
        src/texture/wgf_gfx_texture.c
        src/texture/wgf_gfx_texture_read.c
    DEPS
        math core platform asset)

target_include_directories(wgf_gfx SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/sokol" "${PROJECT_SOURCE_DIR}/deps/stb"
                           "${PROJECT_SOURCE_DIR}/deps/cgltf"
                           "${PROJECT_SOURCE_DIR}/deps/fontstash" "${PROJECT_SOURCE_DIR}/deps/jetbrains-mono")
# a font's glyphs: the browser's in a page, where fontstash's stb_truetype.h is text/web's, which
# includes deps/stb's and turns fontstash's drawing calls (text/wgf_gfx_glyph_priv.h)
if(WGF_BROWSER)
    target_include_directories(wgf_gfx BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/text/web")
endif()
# vendored code is compiled as it comes: its warnings are not ours to fix
set_source_files_properties(src/texture/wgf_gfx_stb_impl.c src/text/wgf_gfx_fontstash_impl.c src/mesh/wgf_gfx_cgltf_impl.c
                            PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
# an environment's preparation is numeric loops, which unoptimized take seconds on the web for a
# 1024 by 512 image (CI's browser check found it still loading at 20 s): optimized in every
# build (MSVC's debug runtime checks forbid it there, and its builds aren't the web's)
set_source_files_properties(src/stage/wgf_gfx_environment.c
                            PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-O2>")
# what includes gfx's private headers (app, gfx's tests) sees sokol's
target_include_directories(wgf_gfx_priv SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/deps/sokol"
                           "${PROJECT_SOURCE_DIR}/deps/jetbrains-mono" # the tests compare the built-in font
                           "${PROJECT_SOURCE_DIR}/deps/stb") # and decode what the PNG writer wrote

if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_gfx PRIVATE m)
endif()
if(WGF_TESTS)
    wgf_test(gfx wgf_gfx_color_test)
    wgf_test(gfx wgf_gfx_ktx_test)
    wgf_test(gfx wgf_gfx_png_test)
    if(WGF_HEADLESS)
        wgf_test(gfx wgf_gfx_frame_test)
        wgf_test(gfx wgf_gfx_context_test)
        wgf_test(gfx wgf_gfx_presentation_test)
        wgf_test(gfx wgf_gfx_actor_test)
        wgf_test(gfx wgf_gfx_camera3d_test)
        wgf_test(gfx wgf_gfx_mesh_test)
        wgf_test(gfx wgf_gfx_gltf_test)
        wgf_test(gfx wgf_gfx_skin_test)
        wgf_test(gfx wgf_gfx_merge_test)
        wgf_test(gfx wgf_gfx_stage3d_test)
        wgf_test(gfx wgf_gfx_shadow_test)
        wgf_test(gfx wgf_gfx_environment_test)
        wgf_test(gfx wgf_gfx_shape3d_test)
        wgf_test(gfx wgf_gfx_text_test)
        # the examples' other typeface, to load a font again as; under node, which has no disk
        # of the host's, packed into the program's own files at the same path
        set(other_font "${PROJECT_SOURCE_DIR}/examples/assets/fonts/Komika/KOMIKAH_.ttf")
        target_compile_definitions(wgf_gfx_text_test PRIVATE WGF_TEST_ASSETS="${PROJECT_SOURCE_DIR}/examples/assets")
        if(EMSCRIPTEN)
            target_link_options(wgf_gfx_text_test PRIVATE "SHELL:--preload-file ${other_font}@${other_font}")
            set_property(TARGET wgf_gfx_text_test APPEND PROPERTY LINK_DEPENDS "${other_font}")
        endif()
        wgf_test(gfx wgf_gfx_font_bitmap_test)
        wgf_test(gfx wgf_gfx_font_no_bitmaps_test)
        wgf_test(gfx wgf_gfx_texture_test)
        wgf_test(gfx wgf_gfx_image_test)
        wgf_test(gfx wgf_gfx_draw_test)
        wgf_test(gfx wgf_gfx_draw_model_test)
        wgf_test(gfx wgf_gfx_pick_test)
        wgf_test(gfx wgf_gfx_shape_test)
        wgf_test(gfx wgf_gfx_sprite_test)
        wgf_test(gfx wgf_gfx_sprite_batch_test)
        wgf_test(gfx wgf_gfx_sprite_material_test)
        wgf_test(gfx wgf_gfx_follow_test)
        wgf_test(gfx wgf_gfx_fly_test)
        wgf_test(gfx wgf_gfx_sheet_test)
        wgf_test(gfx wgf_gfx_tilemap_test)
        wgf_test(gfx wgf_gfx_tilemap_terrain_test)
        wgf_test(gfx wgf_gfx_tilemap_grids_test)
        wgf_test(gfx wgf_gfx_emitter_test)
        wgf_test(gfx wgf_gfx_target_test)
        wgf_test(gfx wgf_gfx_shader_test)
        wgf_test(gfx wgf_gfx_cubemap_test)
    endif()
    # pixels, read back from WebGL2 in a browser
    wgf_web_test(gfx wgf_gfx_frame_web_test 1)
    wgf_web_test(gfx wgf_gfx_presentation_web_test 1)
    wgf_window_test(gfx wgf_gfx_presentation_window_test)
    wgf_window_test(gfx wgf_gfx_draw3d_window_test)
    wgf_window_test(gfx wgf_gfx_stage3d_window_test)
    wgf_window_test(gfx wgf_gfx_texture_window_test)
    wgf_window_test(gfx wgf_gfx_target_window_test)
    wgf_window_test(gfx wgf_gfx_shader_window_test)
    wgf_window_test(gfx wgf_gfx_effect_window_test)
    wgf_window_test(gfx wgf_gfx_texture_read_window_test)
    wgf_window_test(gfx wgf_gfx_sprite_run_window_test)
    wgf_window_test(gfx wgf_gfx_font_bitmap_window_test)
    wgf_window_test(gfx wgf_gfx_mesh_window_test)
    wgf_window_test(gfx wgf_gfx_cubemap_window_test)
    wgf_window_test(gfx wgf_gfx_rounded_window_test)
    foreach(test wgf_gfx_shader_window_test wgf_gfx_effect_window_test wgf_gfx_cubemap_window_test)
        if(TARGET ${test}) # the tests' packed shaders, from where they are
            target_compile_definitions(${test} PRIVATE WGF_TEST_FILES="${CMAKE_CURRENT_SOURCE_DIR}/tests")
        endif()
    endforeach()
    wgf_web_test(gfx wgf_gfx_texture_web_test 1)
    wgf_web_test(gfx wgf_gfx_decode_web_test 1)
    wgf_web_test(gfx wgf_gfx_text_web_test 1)
    wgf_web_test(gfx wgf_gfx_stage2d_web_test 1)
    wgf_web_test(gfx wgf_gfx_draw3d_web_test 1)
    wgf_web_test(gfx wgf_gfx_stage3d_web_test 1)
    wgf_web_test(gfx wgf_gfx_shadow_web_test 1)
    wgf_web_test(gfx wgf_gfx_target_web_test 1)
    wgf_web_test(gfx wgf_gfx_texture_read_web_test 1)
    wgf_web_test(gfx wgf_gfx_sprite_run_web_test 1)
    wgf_web_test(gfx wgf_gfx_shader_web_test 1)
    if(TARGET wgf_gfx_shader_web_test)
        target_link_options(wgf_gfx_shader_web_test PRIVATE
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/flat.wgfshader@/wgf/shaders/flat.wgfshader")
    endif()
    wgf_web_test(gfx wgf_gfx_effect_web_test 1)
    if(TARGET wgf_gfx_effect_web_test)
        target_link_options(wgf_gfx_effect_web_test PRIVATE
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/invert.wgfshader@/wgf/shaders/invert.wgfshader")
    endif()
    wgf_web_test(gfx wgf_gfx_context_web_test 1)
    if(TARGET wgf_gfx_context_web_test)
        target_link_options(wgf_gfx_context_web_test PRIVATE
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/flat.wgfshader@/wgf/shaders/flat.wgfshader")
    endif()
    wgf_web_test(gfx wgf_gfx_environment_web_test 1)
    wgf_web_test(gfx wgf_gfx_gltf_web_test 1)
endif()

# Custom shaders' tool and interface (wgf_shader.h), staged with the variant as libwgt's are: a
# program outside libwgf packs its shaders with share/wgf/tools/pack_shader.py, which finds
# share/wgf/shaders/wgf.glsl beside it.
install(FILES "${PROJECT_SOURCE_DIR}/tools/pack_shader.py" "${PROJECT_SOURCE_DIR}/tools/spirv.py"
              "${PROJECT_SOURCE_DIR}/tools/setup_shdc.py" "${PROJECT_SOURCE_DIR}/tools/usercache.py"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/wgf/tools)
install(FILES shaders/wgf.glsl DESTINATION ${CMAKE_INSTALL_DATADIR}/wgf/shaders)
