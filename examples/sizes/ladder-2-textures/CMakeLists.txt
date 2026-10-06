# ladder-2-textures, the feature cost ladder's step 2, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_2_textures LANGUAGES C)
include(../../c/wgf_example.cmake)
wgf_example(ladder-2-textures ASSETS main.c)
