# ui: game UI. Immediate mode laid out by Clay (deps/clay), and documents in RML and RCSS
# on RmlUi (deps/rmlui), both drawn by gfx.
file(GLOB_RECURSE rmlui_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/deps/rmlui/Source/Core/*.cpp")

# RmlUi and the one C++ file, a target of their own, which the archive collects, as
# physics3d's Jolt is (libwgf/physics3d/CMakeLists.txt): C++ sees only RmlUi and the
# bridge's C header, never the other layers' include paths. A program that creates no
# document links none of it: only wgf_ui_document.c calls in.
add_library(wgf_ui_rmlui OBJECT src/wgf_ui_rmlui.cpp ${rmlui_sources})
target_include_directories(wgf_ui_rmlui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(wgf_ui_rmlui SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/rmlui/Include")
wgf_target_defaults(wgf_ui_rmlui)
# RmlUi compiled into libwgf's archive (no dll exports), on the standard library's
# containers, its version string its release's
target_compile_definitions(wgf_ui_rmlui PRIVATE RMLUI_STATIC_LIB RMLUI_NO_THIRDPARTY_CONTAINERS
                                                "RMLUI_VERSION=\"6.3\""
                                                $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>)
set_target_properties(wgf_ui_rmlui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
                                               CXX_VISIBILITY_PRESET hidden)
# no exceptions or RTTI (RmlUi has its own type ids for its casts), and for size in a web
# release, as Clay and Jolt are
target_compile_options(wgf_ui_rmlui PRIVATE
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/GR-;/EHs-c-,-fno-exceptions;-fno-rtti>"
    "$<$<AND:$<PLATFORM_ID:Emscripten>,$<CONFIG:Release>>:-Oz>")
# RmlUi's own sources: its code, compiled as it comes, its warnings not libwgf's
set_source_files_properties(${rmlui_sources} PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
target_link_libraries(wgf PUBLIC wgf_ui_rmlui)

wgf_layer(ui
    SOURCES
        src/wgf_ui.c
        src/wgf_ui_clay_impl.c
        src/wgf_ui_skin.c
        src/wgf_ui_document.c
        src/wgf_ui_document_draw.c
    DEPS
        math core platform asset gfx)

target_include_directories(wgf_ui SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/clay" "${PROJECT_SOURCE_DIR}/deps/sokol")
# Clay for size in a web release (docs/HISTORY.md): it lays out a few dozen boxes a frame.
set_source_files_properties(src/wgf_ui_clay_impl.c PROPERTIES COMPILE_OPTIONS
                            "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>;$<$<AND:$<PLATFORM_ID:Emscripten>,$<CONFIG:Release>>:-Oz>")

if(WGF_TESTS AND WGF_HEADLESS)
    wgf_test(ui wgf_ui_test)
    wgf_test(ui wgf_ui_widgets_test)
    if(EMSCRIPTEN)
        # a UI of the plain widgets links none of the skins or the other widgets: its link map
        # read by tools/check_link_map.py (wasm-ld drops what nothing reaches in any build)
        wgf_test(ui wgf_ui_unlinked_test)
        target_link_options(wgf_ui_unlinked_test PRIVATE "-Wl,--Map=$<TARGET_FILE_DIR:wgf_ui_unlinked_test>/wgf_ui_unlinked_test.map")
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME wgf_ui_unlinked_check
                     COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_link_map.py"
                             "$<TARGET_FILE_DIR:wgf_ui_unlinked_test>/wgf_ui_unlinked_test.map" --absent
                             wgf_ui_skin.c wgf_ui_toggle wgf_ui_checkbox wgf_ui_slider wgf_ui_image_button
                             wgf_ui_icon_button wgf_ui_image wgf_ui_custom wgf_ui_begin_group wgf_ui_separator
                             draw_slider draw_check draw_image draw_image_button draw_group)
        endif()
    endif()
    wgf_test(ui wgf_ui_document_test)
endif()
