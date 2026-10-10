# ecs: actors' components, behaviors, systems, and scenes, their plain data in sparse sets
# (wgf_ecs_store.c, behind wgf_ecs_store_priv.h), which replaced flecs by measurement
# (docs/HISTORY.md, "flecs or sparse sets, measured").
wgf_layer(ecs
    SOURCES
        src/wgf_ecs.c
        src/wgf_ecs_behavior.c
        src/wgf_ecs_components.c
        src/wgf_ecs_dump.c
        src/wgf_ecs_actor.c
        src/wgf_ecs_store.c
        src/wgf_ecs_scene.c
    DEPS
        math core platform asset gfx audio)

target_include_directories(wgf_ecs SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/deps/sokol")
if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(wgf_ecs PRIVATE m Threads::Threads)
endif()

if(WGF_TESTS AND WGF_HEADLESS)
    wgf_test(ecs wgf_ecs_actor_test)
    wgf_test(ecs wgf_ecs_systems_test)
    wgf_test(ecs wgf_ecs_scene_test)
endif()

# The store measured on its own, its worst cases among the rows: built by the native Linux
# presets, run by tools/bench/measure_actors.py on linux-x64-release-headless (Linux alone,
# as the actor benchmark is).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    add_executable(wgf_ecs_store_bench bench/wgf_ecs_store_bench.c src/wgf_ecs_store.c)
    target_include_directories(wgf_ecs_store_bench PRIVATE src)
endif()
