# ladder-6-model, the feature cost ladder's step 6, built against a staged libwgf
# (../../c/wgf_example.cmake) by tools/measure_sizes.py.
cmake_minimum_required(VERSION 3.21)
project(ladder_6_model LANGUAGES C)
include(../../c/wgf_example.cmake)
wgf_example(ladder-6-model ASSETS main.c)
