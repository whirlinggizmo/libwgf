# The runtime: wgf_app_run, its loop, and what it drives, as wgrender's wgr.c.
if(EMSCRIPTEN)
    set(app_script_source src/wgf_app_script_web.c)
else()
    set(app_script_source src/wgf_app_script_native.c)
endif()

wgf_layer(app
    SOURCES
        src/wgf_app.c
        src/wgf_app_script.c
        src/wgf_app_tick_clock.c
        ${app_script_source}
    DEPS
        math core platform gfx)

if(WGF_TESTS)
    wgf_test(app wgf_app_tick_clock_test)
    if(WGF_HEADLESS)
        wgf_test(app wgf_app_loop_test)
        wgf_test(app wgf_app_script_test)
    endif()
    wgf_web_test(app wgf_app_web_test 1) # reads pixels back from WebGL2
endif()
