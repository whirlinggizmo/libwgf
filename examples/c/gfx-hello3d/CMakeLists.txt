# gfx-hello3d, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(gfx_hello3d LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-hello3d main.c)
