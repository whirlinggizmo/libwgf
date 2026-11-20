# gfx-bitmap-font, built against a staged libwgf (../wgf_example.cmake), with the files in
# examples/assets/.
cmake_minimum_required(VERSION 3.21)
project(gfx_bitmap_font LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-bitmap-font ASSETS main.c)
