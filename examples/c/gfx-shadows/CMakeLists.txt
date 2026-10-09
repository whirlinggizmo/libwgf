# gfx-shadows, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(gfx_shadows LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-shadows ASSETS main.c)
