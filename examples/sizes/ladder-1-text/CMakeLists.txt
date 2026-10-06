# ladder-1-text, the feature cost ladder's step 1, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_1_text LANGUAGES C)
include(../../c/wgf_example.cmake)
wgf_example(ladder-1-text main.c)
