# ladder-5-ui, the feature cost ladder's step 5, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_5_ui LANGUAGES C)
include(../../c/wgf_example.cmake)
wgf_example(ladder-5-ui ASSETS main.c)
