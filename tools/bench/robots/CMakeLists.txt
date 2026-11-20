# The animated robots, raylib's models_animation as helix measures it, built against a staged
# libwgf (examples/c/wgf_example.cmake) for the web by tools/bench/measure_frames.py.
cmake_minimum_required(VERSION 3.21)
project(robots LANGUAGES C)
include(../../../examples/c/wgf_example.cmake)
wgf_example(robots ASSETS main.c)
