# gfx-instancing, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(gfx_instancing LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-instancing ASSETS main.c)
