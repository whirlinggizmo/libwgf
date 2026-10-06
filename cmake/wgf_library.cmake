# wgf_layer(<layer> SOURCES <src>... [DEPS <layer>...])
#
# Defines one layer of the library: the object target wgf_<layer> from
# <layer>/include and <layer>/src, whose objects the one archive, wgf (libwgf.a),
# collects. A layer sees only the headers of the layers in DEPS, so an include of
# any other layer's header fails to build: that is what keeps the layers in order.
# It also defines wgf_<layer>_priv, never installed, which puts <layer>/src on the
# include path: the layers above it and its own tests link that to use its private
# headers. Each public header is compiled on its own, with only the public include
# dirs of this layer and its DEPS on the path.
function(wgf_layer layer)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPS")
    set(target wgf_${layer})
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}")

    if(arg_SOURCES)
        add_library(${target} OBJECT ${arg_SOURCES})
        target_include_directories(${target}
            PUBLIC
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                $<BUILD_INTERFACE:${dir}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            PRIVATE
                ${dir}/src)
        foreach(dep IN LISTS arg_DEPS)
            target_link_libraries(${target} PUBLIC wgf_${dep} PRIVATE wgf_${dep}_priv)
        endforeach()
        wgf_target_defaults(${target})
    else()
        # a layer of headers alone (math): its public include path, and nothing compiled
        add_library(${target} INTERFACE)
        target_include_directories(${target}
            INTERFACE
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                $<BUILD_INTERFACE:${dir}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        foreach(dep IN LISTS arg_DEPS)
            target_link_libraries(${target} INTERFACE wgf_${dep})
        endforeach()
    endif()
    target_link_libraries(wgf PUBLIC ${target})

    # (the archive itself isn't linked here: the layer links its deps' _priv, and the
    # archive links every layer, which would be a cycle; a test links wgf beside this)
    add_library(${target}_priv INTERFACE)
    if(arg_SOURCES)
        target_include_directories(${target}_priv INTERFACE ${dir}/src)
    endif()
    target_link_libraries(${target}_priv INTERFACE ${target})
    # a layer's private headers include the private headers of the layers under it
    foreach(dep IN LISTS arg_DEPS)
        target_link_libraries(${target}_priv INTERFACE wgf_${dep}_priv)
    endforeach()

    # Every public header compiles on its own.
    file(GLOB headers CONFIGURE_DEPENDS "${dir}/include/*.h")
    set(checks "")
    foreach(header IN LISTS headers)
        get_filename_component(name "${header}" NAME_WE)
        set(check "${CMAKE_CURRENT_BINARY_DIR}/header_check/${name}.c")
        # the typedef keeps a header of macros alone from being an empty file
        file(CONFIGURE OUTPUT "${check}" CONTENT "#include \"${name}.h\"\ntypedef int wgf_header_check_t;\n")
        list(APPEND checks "${check}")
    endforeach()
    if(NOT checks)
        return() # a layer with no public header yet (one being built): nothing to check or install
    endif()
    add_library(${target}_header_check OBJECT ${checks})
    target_include_directories(${target}_header_check PRIVATE "${PROJECT_SOURCE_DIR}/include" ${dir}/include)
    foreach(dep IN LISTS arg_DEPS)
        target_link_libraries(${target}_header_check PRIVATE wgf_${dep})
    endforeach()
    wgf_target_defaults(${target}_header_check)

    install(FILES ${headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endfunction()

# Language level, hidden symbols, and warnings shared by every libwgf target.
function(wgf_target_defaults target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden)
    if(MSVC)
        # /wd4324: a struct padded for an alignment it asked for, as a shader's uniform
        # blocks (sokol-shdc's, aligned to 16) are; nothing is wrong
        target_compile_options(${target} PRIVATE /W4 /wd4324)
        # fopen, strncpy, and the like are standard C; MSVC's _s replacements aren't portable
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(WGF_WERROR)
        target_compile_options(${target} PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/WX,-Werror>)
    endif()
    if(WGF_SANITIZE)
        target_compile_options(${target} PRIVATE -fsanitize=${WGF_SANITIZE} -fno-omit-frame-pointer -O1 -g)
        target_link_options(${target} PRIVATE -fsanitize=${WGF_SANITIZE})
    endif()
endfunction()

# wgf_web_test(<layer> <name> <visits>) builds <layer>/tests/<name>.c for the web
# and runs it in a real browser through tools/run_in_browser.py, loading it <visits>
# times in one browser context. Skipped (exit 77) when no browser is found.
function(wgf_web_test layer name visits)
    find_package(Python3 COMPONENTS Interpreter)
    if(NOT WGF_BROWSER OR NOT Python3_FOUND)
        return()
    endif()
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.c")
    set_target_properties(${name} PROPERTIES WGF_TEST ON) # not a page: no release page flags (platform)
    target_link_libraries(${name} PRIVATE wgf wgf_${layer}_priv)
    wgf_target_defaults(${name})
    target_link_options(${name} PRIVATE -sEXIT_RUNTIME=1)
    add_test(NAME ${name}
             COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/run_in_browser.py" $<TARGET_FILE:${name}>
                     --visits ${visits})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endfunction()

# wgf_window_test(<layer> <name>) builds <layer>/tests/<name>.c natively, with a
# window, and runs it in one on a private virtual display through
# tools/run_in_xvfb.py, where it can read its window's pixels back with OpenGL.
# Linux only, for Xvfb; skipped (exit 77) where there is none.
function(wgf_window_test layer name)
    find_package(Python3 COMPONENTS Interpreter)
    if(EMSCRIPTEN OR WGF_HEADLESS OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT Python3_FOUND)
        return()
    endif()
    find_package(OpenGL REQUIRED)
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.c")
    set_target_properties(${name} PROPERTIES WGF_TEST ON) # not a page: no release page flags (platform)
    target_link_libraries(${name} PRIVATE wgf wgf_${layer}_priv OpenGL::GL)
    # the examples' files, for a test that draws them
    target_compile_definitions(${name} PRIVATE WGF_TEST_ASSETS="${PROJECT_SOURCE_DIR}/examples/assets")
    wgf_target_defaults(${name})
    add_test(NAME ${name}
             COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/run_in_xvfb.py" $<TARGET_FILE:${name}>)
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endfunction()

# wgf_test(<layer> <name>) builds <layer>/tests/<name>.c against the library, with
# the layer's private headers reachable, and registers it. On the web it runs under
# node (the toolchain's emulator), and exits with main's return code.
function(wgf_test layer name)
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.c")
    set_target_properties(${name} PROPERTIES WGF_TEST ON) # not a page: no release page flags (platform)
    target_link_libraries(${name} PRIVATE wgf wgf_${layer}_priv)
    wgf_target_defaults(${name})
    if(EMSCRIPTEN)
        target_link_options(${name} PRIVATE -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
    endif()
    # ThreadSanitizer aborts at start ("unexpected memory mapping") on Linux kernels
    # that randomize addresses with more entropy than its runtime expects. setarch -R
    # turns randomization off for the test's process alone, not the system.
    find_program(WGF_SETARCH setarch)
    if(WGF_SANITIZE STREQUAL "thread" AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND WGF_SETARCH)
        add_test(NAME ${name} COMMAND ${WGF_SETARCH} ${CMAKE_HOST_SYSTEM_PROCESSOR} -R $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
endfunction()
