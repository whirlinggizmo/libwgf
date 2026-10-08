# ladder-7-physics3d, the feature cost ladder's step 7, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_7_physics3d LANGUAGES C CXX)
include(../../c/wgf_example.cmake)
wgf_example(ladder-7-physics3d ASSETS main.c)
set_target_properties(ladder-7-physics3d PROPERTIES LINKER_LANGUAGE CXX) # Jolt: C++'s runtime
