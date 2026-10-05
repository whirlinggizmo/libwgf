if(EMSCRIPTEN)
    set(core_platform_sources src/wgf_core_fs_web.c src/wgf_core_os_posix.c src/wgf_core_thread_web.c)
elseif(WIN32)
    set(core_platform_sources src/wgf_core_fs_native.c src/wgf_core_os_windows.c src/wgf_core_thread_windows.c)
else()
    set(core_platform_sources src/wgf_core_fs_native.c src/wgf_core_os_posix.c src/wgf_core_thread_posix.c)
    find_package(Threads REQUIRED)
endif()

wgf_layer(core
    SOURCES
        src/wgf_core.c
        src/wgf_core_fs.c
        src/wgf_core_handle.c
        src/wgf_core_identity.c
        src/wgf_core_load.c
        src/wgf_core_part.c
        src/wgf_core_resource.c
        src/wgf_core_log.c
        src/wgf_core_time.c
        src/wgf_core_sokol_impl.c
        ${core_platform_sources})
target_include_directories(wgf_core SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/sokol")
# sokol's warnings aren't libwgf's to fix
set_source_files_properties(src/wgf_core_sokol_impl.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(wgf_core PRIVATE m Threads::Threads)
endif()
target_compile_definitions(wgf_core PRIVATE WGF_VERSION_STRING="${PROJECT_VERSION}")

if(WGF_TESTS)
    wgf_test(core wgf_core_version_test)
    target_compile_definitions(wgf_core_version_test PRIVATE WGF_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")
    wgf_test(core wgf_core_log_test)
    wgf_test(core wgf_core_handle_test)
    wgf_test(core wgf_core_identity_test)
    wgf_test(core wgf_core_part_test)
    wgf_test(core wgf_core_lifecycle_test)
    wgf_test(core wgf_core_fs_test)
    wgf_test(core wgf_core_thread_test)
    wgf_test(core wgf_core_load_test)
    wgf_test(core wgf_core_resource_test)
    wgf_test(core wgf_core_time_test)
    wgf_test(core wgf_core_fs_priv_test)
    if(NOT EMSCRIPTEN)
        wgf_test(core wgf_core_fs_native_test)
        wgf_test(core wgf_core_fs_partial_test)
    endif()
    if(NOT WIN32 AND NOT EMSCRIPTEN)
        wgf_test(core wgf_core_fs_links_test)
    endif()
    wgf_web_test(core wgf_core_fs_web_test 3)
    wgf_web_test(core wgf_core_load_web_test 2)
endif()
