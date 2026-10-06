# Sound and music: sounds and voices (docs/ROADMAP.md, phase 2). On the web the browser
# does all audio; natively, libwgf's mixer and these decoders, as wgrender's
# wgr_audio.c and wgr_sound.c.
set(xiph "${PROJECT_SOURCE_DIR}/deps/xiph")
set(audio_sources src/wgf_audio.c src/wgf_audio_sound.c src/wgf_audio_voice.c)
set(audio_decoders "")
if(EMSCRIPTEN)
    list(APPEND audio_sources src/wgf_audio_sound_web.c src/wgf_audio_voice_web.c)
else()
    list(APPEND audio_sources src/wgf_audio_sound_native.c src/wgf_audio_mix.c)
    if(NOT WGF_HEADLESS)
        list(APPEND audio_decoders src/wgf_audio_sokol_impl.c) # vendored: compiled as it comes
    endif()
    list(APPEND audio_decoders src/wgf_audio_decoders_impl.c
         ${xiph}/ogg/src/bitwise.c ${xiph}/ogg/src/framing.c)
    foreach(file mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0 mapping0
                 registry codebook sharedbook lookup bitrate vorbisfile)
        list(APPEND audio_decoders ${xiph}/vorbis/lib/${file}.c)
    endforeach()
endif()

wgf_layer(audio
    SOURCES
        ${audio_sources} ${audio_decoders}
    DEPS
        math core platform asset)

if(NOT EMSCRIPTEN)
    set(decoder_includes "${PROJECT_SOURCE_DIR}/deps/dr" "${xiph}/ogg/include" "${xiph}/vorbis/include"
                         "${xiph}/vorbis/lib")
    target_include_directories(wgf_audio SYSTEM PRIVATE ${decoder_includes} "${PROJECT_SOURCE_DIR}/deps/sokol")
    # sokol_audio's device: ALSA on Linux, WASAPI on Windows (MSVC links it by pragma;
    # MinGW needs ole32), AudioToolbox on macOS
    if(NOT WGF_HEADLESS)
        if(APPLE)
            target_link_libraries(wgf_audio PUBLIC "-framework AudioToolbox")
        elseif(WIN32)
            if(MINGW)
                target_link_libraries(wgf_audio PUBLIC ole32)
            endif()
        else()
            target_link_libraries(wgf_audio PUBLIC asound)
        endif()
    endif()
    # the layer's own tests read the decoders' headers
    target_include_directories(wgf_audio_priv SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/deps/dr" "${xiph}/ogg/include"
                               "${xiph}/vorbis/include")
    # vendored code is compiled as it comes: its warnings are not ours to fix
    set_source_files_properties(${audio_decoders} PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
    # what Xiph's configure would find: alloca.h where there is one (os.h takes Windows' own)
    set_source_files_properties(${audio_decoders} PROPERTIES COMPILE_DEFINITIONS
                                "$<$<NOT:$<PLATFORM_ID:Windows>>:HAVE_ALLOCA_H>;$<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>")
endif()

if(WGF_TESTS AND NOT EMSCRIPTEN)
    # broken, cut-short, and byte-flipped WAV, MP3, and Ogg files, the examples' music and
    # sound as the whole ones: under the sanitizer presets, nothing reads past a file
    wgf_test(audio wgf_audio_decoders_test)
    target_compile_definitions(wgf_audio_decoders_test PRIVATE WGF_TEST_ASSETS="${PROJECT_SOURCE_DIR}/examples/assets")
    # sounds natively: each format decoded and streamed, from the examples' music and sound
    wgf_test(audio wgf_audio_sound_test)
    # voices, the mixer pulled in place of a device: wgf_audio_voice_checks.c's steps
    if(WGF_HEADLESS)
        wgf_test(audio wgf_audio_voice_test)
        # MP3's encoder delay: markers at known times, with the delay written and without
        wgf_test(audio wgf_audio_markers_test)
        target_compile_definitions(wgf_audio_markers_test PRIVATE WGF_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        # the mixer on a thread beside the game's: the tsan preset's race check
        wgf_test(audio wgf_audio_voice_thread_test)
        target_compile_definitions(wgf_audio_voice_thread_test PRIVATE WGF_TEST_ASSETS="${PROJECT_SOURCE_DIR}/examples/assets")
        set_tests_properties(wgf_audio_voice_thread_test PROPERTIES SKIP_RETURN_CODE 77)
        # a streamed sound played while its file arrives: the program's downloads, answered a
        # piece at a time by the test
        wgf_test(audio wgf_audio_arrive_test)
        target_compile_definitions(wgf_audio_arrive_test PRIVATE WGF_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
                                   WGF_TEST_ASSETS="${PROJECT_SOURCE_DIR}/examples/assets")
    endif()
    target_compile_definitions(wgf_audio_sound_test PRIVATE WGF_TEST_ASSETS="${PROJECT_SOURCE_DIR}/examples/assets")
endif()
if(WGF_TESTS AND NOT EMSCRIPTEN)
    # a slow frame doesn't stop the sound: the mixer on the device's thread, in a window
    wgf_window_test(audio wgf_audio_stall_test)
endif()
if(WGF_TESTS AND EMSCRIPTEN)
    # a streamed sound played as its file arrives, fetched by its <audio> element from a
    # server sending it slowly: tools/check_stream.py (headless on WebGL2; the WebGPU
    # preset's on the GPU, as the asset cache check)
    add_executable(wgf_audio_stream_page tests/wgf_audio_stream_page.c)
    set_target_properties(wgf_audio_stream_page PROPERTIES WGF_TEST ON)
    target_link_libraries(wgf_audio_stream_page PRIVATE wgf)
    wgf_target_defaults(wgf_audio_stream_page)
    find_package(Python3 COMPONENTS Interpreter)
    target_link_options(wgf_audio_stream_page PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
                                                      -sALLOW_MEMORY_GROWTH=1)
    if(Python3_FOUND)
        add_test(NAME wgf_audio_stream_check
                 COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_stream.py"
                         $<TARGET_FILE:wgf_audio_stream_page>)
        set_tests_properties(wgf_audio_stream_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    endif()
endif()
if(WGF_TESTS AND EMSCRIPTEN)
    # sounds in a real browser, which decodes them: the same checks as natively, through
    # app's runtime
    wgf_web_test(audio wgf_audio_sound_web_test 1)
    # voices played by Web Audio: wgf_audio_voice_checks.c's steps, the native test's
    wgf_web_test(audio wgf_audio_voice_web_test 1)
    # a slow frame doesn't stop the sound: the browser mixes on its own thread
    wgf_web_test(audio wgf_audio_stall_web_test 1)
    if(TARGET wgf_audio_stall_web_test)
        target_link_options(wgf_audio_stall_web_test PRIVATE
            "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/examples/assets/music/a_hero_is_born.mp3@/wgf/music/a_hero_is_born.mp3")
    endif()
    # MP3's encoder delay as the browser decodes it: the native marker test's files
    wgf_web_test(audio wgf_audio_markers_web_test 1)
    if(TARGET wgf_audio_markers_web_test)
        target_link_options(wgf_audio_markers_web_test PRIVATE
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/markers.mp3@/wgf/markers.mp3"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/markers_gapless.mp3@/wgf/markers_gapless.mp3")
        set_property(TARGET wgf_audio_markers_web_test APPEND PROPERTY LINK_DEPENDS
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/markers.mp3" "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/markers_gapless.mp3")
    endif()
    if(TARGET wgf_audio_sound_web_test)
        target_link_options(wgf_audio_sound_web_test PRIVATE
            "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/examples/assets/music/a_hero_is_born.mp3@/wgf/music/a_hero_is_born.mp3"
            "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/examples/assets/sounds/click_004.ogg@/wgf/sounds/click_004.ogg")
    endif()
endif()
