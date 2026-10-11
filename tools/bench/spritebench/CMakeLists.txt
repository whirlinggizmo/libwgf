# The sprite benchmark, libwgt's spritebench (wgrender-c's), built against a staged libwgf
# (examples/c/wgf_example.cmake) for the web by tools/bench/measure_frames.py.
cmake_minimum_required(VERSION 3.21)
project(spritebench LANGUAGES C)
include(../../../examples/c/wgf_example.cmake)
wgf_example(spritebench ASSETS main.c)
