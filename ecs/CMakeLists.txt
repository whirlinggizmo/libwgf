# ecs: entities, components, systems, and scenes, on flecs (deps/flecs), its core alone.
wgf_layer(ecs
    SOURCES
        src/wgf_ecs.c
        src/wgf_ecs_behavior.c
        src/wgf_ecs_components.c
        src/wgf_ecs_dump.c
        src/wgf_ecs_actor.c
        src/wgf_ecs_flecs_impl.c
        src/wgf_ecs_scene.c
    DEPS
        math core platform asset gfx audio)

target_include_directories(wgf_ecs SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/flecs" "${PROJECT_SOURCE_DIR}/deps/sokol")
target_include_directories(wgf_ecs_priv SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/deps/flecs")
# flecs's core alone: no addons, its default OS API; static, so nothing of it is exported.
# Every file that includes flecs.h sees the same build of it. A query of at most 8 terms
# (libwgf's have 3): flecs sizes every query description for its maximum, and creating
# the world nests them, which overflowed a web page's 64 KB stack at 32.
set(wgf_flecs_defines FLECS_CUSTOM_BUILD FLECS_OS_API_IMPL FLECS_TERM_COUNT_MAX=8)
target_compile_definitions(wgf_ecs PRIVATE ${wgf_flecs_defines})
target_compile_definitions(wgf_ecs_priv INTERFACE ${wgf_flecs_defines})
set_source_files_properties(src/wgf_ecs_flecs_impl.c PROPERTIES COMPILE_OPTIONS
                            "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>;$<$<AND:$<PLATFORM_ID:Emscripten>,$<CONFIG:Release>>:-Oz>")
if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(wgf_ecs PRIVATE m Threads::Threads)
endif()

if(WGF_TESTS AND WGF_HEADLESS)
    wgf_test(ecs wgf_ecs_actor_test)
    wgf_test(ecs wgf_ecs_systems_test)
    wgf_test(ecs wgf_ecs_scene_test)
endif()
