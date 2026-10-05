# The window system, its events, and input: none in a headless build; sokol_app
# otherwise, with the code that differs between the desktop and the web in a file of
# its own. And sokol's implementations, compiled once for the library.
if(WGF_HEADLESS)
    set(platform_sources src/wgf_platform_system_headless.c)
elseif(EMSCRIPTEN)
    set(platform_sources src/wgf_platform_system_sokol.c src/wgf_platform_system_web.c)
else()
    set(platform_sources src/wgf_platform_system_sokol.c src/wgf_platform_system_native.c)
endif()

# gamepads: each platform's own way to read them, or none (headless: tests feed their own)
if(WGF_HEADLESS)
    set(platform_gamepad_source src/wgf_platform_gamepad_none.c)
elseif(EMSCRIPTEN)
    set(platform_gamepad_source src/wgf_platform_gamepad_web.c)
elseif(WIN32)
    set(platform_gamepad_source src/wgf_platform_gamepad_windows.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(platform_gamepad_source src/wgf_platform_gamepad_linux.c)
else()
    set(platform_gamepad_source src/wgf_platform_gamepad_none.c)
endif()

wgf_layer(platform
    SOURCES
        src/wgf_platform_gamepad.c
        src/wgf_platform_input.c
        src/wgf_platform_sokol_impl.c
        src/wgf_platform_window.c
        ${platform_gamepad_source}
        ${platform_sources}
    DEPS
        math core)
target_include_directories(wgf_platform SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/sokol"
                           "${PROJECT_SOURCE_DIR}/deps/sokol_utils")
# platform's private headers take sokol's types
target_include_directories(wgf_platform_priv SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/deps/sokol")
# vendored code is compiled as it comes: its warnings are not ours to fix
set_source_files_properties(src/wgf_platform_sokol_impl.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")

# what sokol needs from the system, for a program linking the library
if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(wgf_platform PRIVATE m Threads::Threads)
endif()
if(EMSCRIPTEN)
    target_link_options(wgf_platform INTERFACE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
elseif(NOT WGF_HEADLESS)
    if(APPLE)
        set_property(SOURCE src/wgf_platform_sokol_impl.c APPEND PROPERTY COMPILE_OPTIONS -x objective-c)
        target_link_libraries(wgf_platform PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL")
    elseif(WIN32)
        target_link_libraries(wgf_platform PRIVATE kernel32 user32 shell32 gdi32 opengl32)
    else()
        # the system's GL and X11 dev packages: stop here, with the command that
        # installs them, rather than at find_package or the link
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_FOUND)
            execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/setup_system_packages.py" check
                            RESULT_VARIABLE wgf_packages_missing)
            if(wgf_packages_missing)
                message(FATAL_ERROR "libwgf: system packages missing for the Linux desktop build (above)")
            endif()
        endif()
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        target_link_libraries(wgf_platform PRIVATE X11::X11 X11::Xi X11::Xcursor X11::Xrandr OpenGL::GL
                                                   ${CMAKE_DL_LIBS})
    endif()
endif()
if(EMSCRIPTEN)
    # a release page, as wgrender's: Closure minifies the JS, and only a browser's
    # environments (a page, a worker) are kept. libwgf's own tests (WGF_TEST, set by
    # wgf_test and the rest) aren't pages: they keep every environment, node's included.
    target_link_options(wgf_platform INTERFACE
                        "$<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:$<TARGET_PROPERTY:WGF_TEST>>>>:--closure=1;-sENVIRONMENT=web,worker>")
endif()

if(WGF_TESTS AND WGF_HEADLESS)
    wgf_test(platform wgf_platform_input_test)
    wgf_test(platform wgf_platform_touch_test)
    wgf_test(platform wgf_platform_gamepad_test)
endif()
