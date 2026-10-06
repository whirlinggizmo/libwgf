# gfx-sprite2d, built against a staged libwgf (../wgf_example.cmake), with the files in
# examples/assets/.
cmake_minimum_required(VERSION 3.21)
project(gfx_sprite2d LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-sprite2d ASSETS main.c)
