# ui: game UI, immediate mode, laid out by Clay (deps/clay) and drawn by gfx.
wgf_layer(ui
    SOURCES
        src/wgf_ui.c
        src/wgf_ui_clay_impl.c
    DEPS
        math core platform asset gfx)

target_include_directories(wgf_ui SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/clay" "${PROJECT_SOURCE_DIR}/deps/sokol")
# Clay for size in a web release, as flecs is (docs/HISTORY.md): it lays out a few dozen boxes a frame.
set_source_files_properties(src/wgf_ui_clay_impl.c PROPERTIES COMPILE_OPTIONS
                            "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>;$<$<AND:$<PLATFORM_ID:Emscripten>,$<CONFIG:Release>>:-Oz>")

if(WGF_TESTS AND WGF_HEADLESS)
    wgf_test(ui wgf_ui_test)
endif()
