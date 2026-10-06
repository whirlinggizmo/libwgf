# ladder-3-sprites, the feature cost ladder's step 3, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_3_sprites LANGUAGES C)
include(../../c/wgf_example.cmake)
wgf_example(ladder-3-sprites ASSETS main.c)
