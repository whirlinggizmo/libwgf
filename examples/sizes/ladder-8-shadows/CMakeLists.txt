# ladder-8-shadows, the feature cost ladder's step 8, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_8_shadows LANGUAGES C CXX)
include(../../c/wgf_example.cmake)
wgf_example(ladder-8-shadows ASSETS main.c)
set_target_properties(ladder-8-shadows PROPERTIES LINKER_LANGUAGE CXX) # Jolt: C++'s runtime
