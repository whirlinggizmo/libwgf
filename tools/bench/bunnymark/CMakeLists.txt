# The bunnymark, raylib's textures_bunnymark as helix measures it, built against a staged
# libwgf (examples/c/wgf_example.cmake) for the web by tools/bench/measure_frames.py.
cmake_minimum_required(VERSION 3.21)
project(bunnymark LANGUAGES C)
include(../../../examples/c/wgf_example.cmake)
wgf_example(bunnymark ASSETS main.c)
