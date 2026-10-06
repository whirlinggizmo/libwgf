# ladder-4-ecs, the feature cost ladder's step 4, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_4_ecs LANGUAGES C)
include(../../c/wgf_example.cmake)
wgf_example(ladder-4-ecs ASSETS main.c)
