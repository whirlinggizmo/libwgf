# ladder-9-environment, the feature cost ladder's step 9, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_9_environment LANGUAGES C CXX)
include(../../c/wgf_example.cmake)
wgf_example(ladder-9-environment ASSETS main.c)
set_target_properties(ladder-9-environment PROPERTIES LINKER_LANGUAGE CXX) # Jolt: C++'s runtime
