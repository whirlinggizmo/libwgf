# asset: where a resource's file comes from (docs/ARCHITECTURE.md; ROADMAP.md, phase 3).
# Under gfx and audio, whose path-made resources install it: a path is an asset.
if(EMSCRIPTEN)
    set(asset_platform_sources src/wgf_asset_web.c)
else()
    set(asset_platform_sources src/wgf_asset_native.c)
endif()

wgf_layer(asset
    SOURCES
        src/wgf_asset.c
        src/wgf_asset_fresh.c
        src/wgf_asset_manifest.c
        src/wgf_asset_manifest_reader.c
        src/wgf_asset_sha256.c
        src/wgf_asset_task.c
        src/wgf_asset_url.c
        ${asset_platform_sources}
    DEPS
        math core)

if(WGF_TESTS AND NOT EMSCRIPTEN)
    # natively, on disk; the web's: tools/check_asset_cache.py, in a browser
    wgf_test(asset wgf_asset_test)
    wgf_test(asset wgf_asset_fetch_test)
    # the host rule: the program's directory, with the working directory elsewhere
    wgf_test(asset wgf_asset_host_test)
    set_tests_properties(wgf_asset_host_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(WGF_TESTS AND EMSCRIPTEN)
    # the web asset cache, end to end: a page fetching its one file from beside it,
    # visited again and again by tools/check_asset_cache.py (headless on WebGL2; WebGPU's
    # needs the GPU, so verify_builds runs that one)
    add_executable(wgf_asset_cache_page tests/wgf_asset_cache_page.c)
    set_target_properties(wgf_asset_cache_page PROPERTIES WGF_TEST ON)
    target_link_libraries(wgf_asset_cache_page PRIVATE wgf)
    wgf_target_defaults(wgf_asset_cache_page)
    target_link_options(wgf_asset_cache_page PRIVATE
        "-sEXPORTED_FUNCTIONS=_main,_wgf_core_priv_load_get_pending_count,_wgf_asset_clear_cache")
    target_link_options(wgf_asset_cache_page PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
                                                     -sALLOW_MEMORY_GROWTH=1)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME wgf_asset_cache_check
                 COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_asset_cache.py"
                         $<TARGET_FILE:wgf_asset_cache_page>)
        add_test(NAME wgf_asset_cache_manifest_check
                 COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_asset_cache.py"
                         $<TARGET_FILE:wgf_asset_cache_page> --manifest)
        add_test(NAME wgf_asset_cache_manifest_c_hash_check
                 COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_asset_cache.py"
                         $<TARGET_FILE:wgf_asset_cache_page> --manifest --no-subtle)
        set_tests_properties(wgf_asset_cache_check wgf_asset_cache_manifest_check
                             wgf_asset_cache_manifest_c_hash_check PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
