# scene-stream, built against a staged libwgf (../../c/wgf_example.cmake). C++ too: the
# library's physics is Jolt, which a program making a body links with C++'s runtime.
cmake_minimum_required(VERSION 3.21)
project(scene_stream LANGUAGES C CXX)
include(../../c/wgf_example.cmake)
wgf_example(scene-stream ASSETS main.c)
set_target_properties(scene-stream PROPERTIES LINKER_LANGUAGE CXX)
