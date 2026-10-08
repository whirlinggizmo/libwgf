# physics3d-vehicle, built against a staged libwgf (../wgf_example.cmake). C++ too: the
# library's physics is Jolt, which a program making a body links with C++'s runtime.
cmake_minimum_required(VERSION 3.21)
project(physics3d_vehicle LANGUAGES C CXX)
include(../wgf_example.cmake)
wgf_example(physics3d-vehicle main.c)
set_target_properties(physics3d-vehicle PROPERTIES LINKER_LANGUAGE CXX)
