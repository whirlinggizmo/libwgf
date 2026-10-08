# wgf_example(<name> [ASSETS] <source>...): a C example as a program outside libwgf
# builds one, against a staged variant in out/ (WGF_OUT; WGF_HEADLESS when it is a
# headless one). Each example's CMakeLists.txt is a project of its own that includes
# this.
#
# ASSETS: it loads files from examples/assets/, which sits one level above every
# program: natively out/<platform>/<variant>/assets, a link to it beside bin/ (made by
# tools/examples.py); on the web /assets/ at the site's root (tools/server.py mounts it;
# tools/build_site.py copies it). Each program names it, wgf_asset_set_host("../assets"),
# and its files plainly ("fonts/..."), so a copied example works after editing that line.
#
# On the web an example is a folder of the site, <site>/<name>/: its page, index.html,
# and its program, <name>.js and <name>.wasm. The page is the example's own,
# examples/c/<name>/index.html: written once from examples/c/wgf_page.html when the
# example has none, never overwritten, and copied beside the program at each build.
# tools/finish_site.py then stamps the versions in and writes the site's launcher.
# tools/run_smoke.py, check_desktop.py, and check_web.py stage a variant and build
# every example against it.
if(EMSCRIPTEN)
    set(wgf_platform wasm32)
else()
    set(wgf_platform linux-x64)
endif()
set(WGF_OUT "${CMAKE_CURRENT_LIST_DIR}/../../out/${wgf_platform}/debug" CACHE PATH "A staged libwgf variant")
option(WGF_HEADLESS "WGF_OUT is a headless variant: no window or GPU" OFF)
# For tools/check_web.py alone: exports two of libwgf's private functions from the web
# build, by name at the link, so the check can ask how many loads are pending and which
# are stuck. The library exports nothing it doesn't declare; a program of one's own
# leaves this off.
option(WGF_CHECK_EXPORTS "Export the loader's pending count and log, for tools/check_web.py" OFF)
set(wgf_example_dir "${CMAKE_CURRENT_LIST_DIR}") # this file's: inside the function, the list dir is the caller's

# MSVC: the static release runtime, as libwgf's own builds use in every configuration
# (its CMakeLists.txt says why)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

function(wgf_example name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "ASSETS" "" "")
    if(NOT EXISTS "${WGF_OUT}/lib")
        message(FATAL_ERROR "${name}: no staged libwgf in ${WGF_OUT}; run tools/stage_variant.py first")
    endif()
    add_executable(${name} ${arg_UNPARSED_ARGUMENTS})
    set_target_properties(${name} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_include_directories(${name} PRIVATE "${WGF_OUT}/include")
    target_link_libraries(${name} PRIVATE "${WGF_OUT}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}wgf${CMAKE_STATIC_LIBRARY_SUFFIX}")
    # what the library needs from the system
    if(EMSCRIPTEN)
        # the program as <name>.js and <name>.wasm in the example's folder of the site, and
        # the example's page beside them
        set_target_properties(${name} PROPERTIES SUFFIX ".js"
                              RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}")
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/index.html")
            set(WGF_EXAMPLE "${name}")
            configure_file("${wgf_example_dir}/wgf_page.html" "${CMAKE_CURRENT_SOURCE_DIR}/index.html" @ONLY)
            message(STATUS "${name}: its page written, ${CMAKE_CURRENT_SOURCE_DIR}/index.html; it is yours from here")
        endif()
        # the page copied beside the program at every build, as it is in the source (a
        # step after the link would run only when the program relinks, and a copy kept
        # by its date would keep one tools/finish_site.py has stamped, which is newer,
        # over an edited source); the stamping comes after the build, by the tools
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            set(wgf_page_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}")
        else()
            set(wgf_page_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}")
        endif()
        add_custom_target(${name}_page ALL
                          COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/index.html"
                                  "${wgf_page_dir}/index.html"
                          VERBATIM)
        # one list: a second EXPORTED_FUNCTIONS would replace the first
        set(exports _main)
        if(WGF_CHECK_EXPORTS)
            list(APPEND exports _wgf_core_priv_load_get_pending_count _wgf_core_priv_load_log_pending)
        endif()
        if(WGF_CHECK_EXPORTS)
            list(JOIN exports "\n" exports)
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}_exports.txt" "${exports}\n")
            target_link_options(${name} PRIVATE "-sEXPORTED_FUNCTIONS=@${CMAKE_CURRENT_BINARY_DIR}/${name}_exports.txt")
        endif()
        target_link_options(${name} PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
                                            -sSTACK_SIZE=524288) # physics3d's Jolt (physics3d/CMakeLists.txt)
        # a release page, as wgrender's: Closure minifies the JS, and only a browser's
        # environments (a page, a worker) are kept
        target_link_options(${name} PRIVATE $<$<CONFIG:Release>:--closure=1 -sENVIRONMENT=web,worker>)
    elseif(WGF_HEADLESS)
        find_package(Threads REQUIRED)
        target_link_libraries(${name} PRIVATE Threads::Threads ${CMAKE_DL_LIBS} $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
    elseif(WIN32)
        # audio's device: WASAPI, which MSVC links by pragma and MinGW through ole32
        target_link_libraries(${name} PRIVATE kernel32 user32 shell32 gdi32 opengl32 $<$<BOOL:${MINGW}>:ole32>)
    elseif(APPLE)
        target_link_libraries(${name} PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL"
                                              "-framework AudioToolbox")
    else()
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        find_package(Threads REQUIRED)
        target_link_libraries(${name} PRIVATE X11::X11 X11::Xi X11::Xcursor X11::Xrandr OpenGL::GL Threads::Threads m
                                              asound ${CMAKE_DL_LIBS}) # asound: audio's device
    endif()
endfunction()
