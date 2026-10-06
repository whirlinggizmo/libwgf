cmake_minimum_required(VERSION 3.21)

# The wasm32 presets set WGF_WEB: Emscripten's toolchain, from $EMSDK or else the emcc
# on PATH (as emsdk_env or an installed Emscripten puts it), so no preset names a path.
if(WGF_WEB AND NOT CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{EMSDK})
        set(emscripten "$ENV{EMSDK}/upstream/emscripten")
    else()
        find_program(WGF_EMCC NAMES emcc emcc.bat)
        if(NOT WGF_EMCC)
            message(FATAL_ERROR "libwgf: a wasm32 preset needs Emscripten: no EMSDK, and no emcc on PATH")
        endif()
        file(REAL_PATH "${WGF_EMCC}" emcc)
        get_filename_component(emscripten "${emcc}" DIRECTORY)
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${emscripten}/cmake/Modules/Platform/Emscripten.cmake")
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WGF_VERSION LIMIT_COUNT 1)
project(libwgf VERSION ${WGF_VERSION} LANGUAGES C)

# The web builds use exactly the Emscripten cmake/emscripten-version.txt pins: its
# patch releases change behavior and output within a series, so a size means something
# only from the same compiler. LIBWGF_EMSCRIPTEN_VERSION=<version> allows that one
# instead, on purpose, said at every configure.
if(EMSCRIPTEN)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/emscripten-version.txt" WGF_EMSCRIPTEN_PINNED LIMIT_COUNT 1)
    if(DEFINED ENV{LIBWGF_EMSCRIPTEN_VERSION} AND NOT "$ENV{LIBWGF_EMSCRIPTEN_VERSION}" STREQUAL "")
        set(wanted "$ENV{LIBWGF_EMSCRIPTEN_VERSION}")
        message(STATUS "libwgf: Emscripten ${wanted} from LIBWGF_EMSCRIPTEN_VERSION (not the pinned ${WGF_EMSCRIPTEN_PINNED})")
    else()
        set(wanted "${WGF_EMSCRIPTEN_PINNED}")
    endif()
    if(NOT EMSCRIPTEN_VERSION STREQUAL wanted)
        message(FATAL_ERROR "libwgf: Emscripten is ${EMSCRIPTEN_VERSION}, not ${wanted} (cmake/emscripten-version.txt): "
                            "`emsdk install ${wanted} && emsdk activate ${wanted}`, or set LIBWGF_EMSCRIPTEN_VERSION "
                            "to use ${EMSCRIPTEN_VERSION} on purpose")
    endif()
endif()

option(WGF_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(WGF_WERROR "Warnings are errors" ${PROJECT_IS_TOP_LEVEL})
set(WGF_SANITIZE "" CACHE STRING "thread, address, or undefined: the library and tests built with it")
option(WGF_HEADLESS "No window or GPU: gfx on sokol's dummy backend, for tests and servers anywhere" OFF)

include(GNUInstallDirs)
include(cmake/wgf_library.cmake)

if(WGF_TESTS)
    enable_testing()
endif()

# sokol's graphics backend, one for the whole library: the dummy one (no GPU) in a
# headless build, WebGL2 on the web, OpenGL core natively. Every layer is compiled with
# it, as are the tests.
if(WGF_HEADLESS)
    add_compile_definitions(SOKOL_DUMMY_BACKEND)
elseif(EMSCRIPTEN)
    add_compile_definitions(SOKOL_GLES3)
else()
    add_compile_definitions(SOKOL_GLCORE)
endif()

# One library, built in layers (cmake/wgf_library.cmake): each adds its objects here.
# A page in a browser: the web without WGF_HEADLESS. A headless web build has no canvas,
# no Web Audio, and no fetch; it runs under node (the binding's tests), and takes the
# native mixer (no device) and the native asset layer (files in the wasm's own storage).
if(EMSCRIPTEN AND NOT WGF_HEADLESS)
    set(WGF_BROWSER ON)
    add_compile_definitions(WGF_PRIV_BROWSER) # a platform file's own test, never a shared file's
else()
    set(WGF_BROWSER OFF)
endif()

add_library(wgf STATIC)
add_library(wgf::wgf ALIAS wgf)
target_include_directories(wgf PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
add_subdirectory(math)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(asset)
add_subdirectory(gfx)
add_subdirectory(audio)
add_subdirectory(ecs)
add_subdirectory(ui)
add_subdirectory(app)
install(TARGETS wgf ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/wgf_api.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# libwgf's license and the vendored code's, staged with the variant: a program built
# with it ships THIRD_PARTY_NOTICES.md (it says why).
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/wgf)

# The public API's shape (tools/check_api.py), and the checker against a header that
# breaks every rule; every tool answers --help and does nothing else
# (tools/check_tools.py). check_api is skipped (77) without a clang, which Emscripten
# brings.
if(WGF_TESTS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME check_api COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_api.py")
        add_test(NAME check_api_self_test
                 COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_api.py" --self-test)
        set_tests_properties(check_api check_api_self_test PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME check_tools COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_tools.py")
        set_tests_properties(check_tools PROPERTIES TIMEOUT 600)
        add_test(NAME check_docs COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_docs.py")
    endif()
endif()
