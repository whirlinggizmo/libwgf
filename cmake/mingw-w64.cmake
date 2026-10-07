# Windows programs built with MinGW-w64: the windows-x64-mingw-* presets. On Linux or
# macOS that is a cross build (x86_64-w64-mingw32-gcc from the system's packages), whose
# tests ctest runs under Wine through tools/run_wine.py; on Windows it is the pinned
# WinLibs GCC that tools/setup_mingw.py sets up in the per-user cache the first time,
# never whichever gcc is on PATH, run as it is. From wgrender's.
if(CMAKE_HOST_WIN32)
    # The MinGW bin: $LIBWGF_MINGW_BIN when set (another MinGW, on purpose, said at every
    # configure), else the pinned one tools/setup_mingw.py sets up. Once per configure;
    # CMake reads a toolchain file again for each try_compile, which gets the answer from
    # this cache entry or, in a try_compile's own project, asks again (setup_mingw.py only
    # prints once the compiler is there). Changing the variable takes effect: the cached
    # entry remembers where it came from.
    set(wgf_mingw_from "$ENV{LIBWGF_MINGW_BIN}")
    if(wgf_mingw_from)
        file(TO_CMAKE_PATH "${wgf_mingw_from}" wgf_mingw_from)
    endif()
    if(NOT WGF_MINGW_BIN OR NOT "${WGF_MINGW_FROM}" STREQUAL "${wgf_mingw_from}")
        if(wgf_mingw_from)
            set(bin "${wgf_mingw_from}")
        else()
            find_program(WGF_MINGW_PYTHON NAMES python3 python py REQUIRED)
            execute_process(COMMAND "${WGF_MINGW_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/setup_mingw.py"
                            OUTPUT_VARIABLE bin OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE failed)
            if(failed)
                message(FATAL_ERROR "libwgf: setting up MinGW-w64 failed (tools/setup_mingw.py, above)")
            endif()
            file(TO_CMAKE_PATH "${bin}" bin)
        endif()
        set(WGF_MINGW_BIN "${bin}" CACHE INTERNAL "the MinGW-w64 bin the windows-x64-mingw presets build with")
        set(WGF_MINGW_FROM "${wgf_mingw_from}" CACHE INTERNAL "LIBWGF_MINGW_BIN when WGF_MINGW_BIN was set")
    endif()
    foreach(program gcc.exe g++.exe windres.exe)
        if(NOT EXISTS "${WGF_MINGW_BIN}/${program}")
            if(wgf_mingw_from)
                message(FATAL_ERROR "libwgf: LIBWGF_MINGW_BIN is ${wgf_mingw_from}, which has no ${program}")
            endif()
            message(FATAL_ERROR "libwgf: the pinned MinGW-w64 in ${WGF_MINGW_BIN} has no ${program} "
                                "(tools/setup_mingw.py)")
        endif()
    endforeach()
    if(wgf_mingw_from AND NOT CMAKE_IN_TRY_COMPILE)
        message(STATUS "libwgf: MinGW-w64 from LIBWGF_MINGW_BIN: ${wgf_mingw_from} (not the pinned one)")
    endif()
    # a native build: naming the system would make CMake treat it as a cross build
    set(CMAKE_C_COMPILER "${WGF_MINGW_BIN}/gcc.exe")
    set(CMAKE_CXX_COMPILER "${WGF_MINGW_BIN}/g++.exe") # physics3d's one C++ file and Jolt
    set(CMAKE_RC_COMPILER "${WGF_MINGW_BIN}/windres.exe")
else()
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++) # physics3d's one C++ file and Jolt
    set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    # tools/run_wine.py runs by its #! line: the host here is Linux or macOS
    set(CMAKE_CROSSCOMPILING_EMULATOR "${CMAKE_CURRENT_LIST_DIR}/../tools/run_wine.py")
endif()

# MinGW's runtime linked in, so a program runs with no MinGW DLLs beside it, and never
# picks up another MinGW's from PATH (a mismatched libwinpthread fails at load, 0xc00004bc),
# as wgrender's programs link
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
