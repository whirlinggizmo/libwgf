# The runtime: wgf_app_run, its loop, and what it drives, as wgrender's wgr.c.
if(WGF_BROWSER)
    set(app_autopilot_source src/wgf_app_autopilot_web.c)
else()
    set(app_autopilot_source src/wgf_app_autopilot_native.c src/wgf_app_record_native.c)
endif()

wgf_layer(app
    SOURCES
        src/wgf_app.c
        src/wgf_app_autopilot.c
        src/wgf_app_debug.c
        src/wgf_app_tick_clock.c
        ${app_autopilot_source}
    DEPS
        math core platform gfx)

if(WGF_TESTS)
    wgf_test(app wgf_app_tick_clock_test)
    if(WGF_HEADLESS)
        wgf_test(app wgf_app_loop_test)
        wgf_test(app wgf_app_autopilot_test)
        # Recording writes a run's keystrokes to a file: Windows Defender quarantines a MinGW build
        # of that test as a keylogger (Trojan:Win64/XWorm.GHM!MTB, a false positive; MSVC's isn't
        # flagged), so it runs everywhere but a MinGW build on a Windows host. Under Wine it runs.
        if(NOT (MINGW AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows"))
            wgf_test(app wgf_app_record_test)
        endif()
    endif()
    wgf_web_test(app wgf_app_web_test 1) # reads pixels back from WebGL2
endif()
