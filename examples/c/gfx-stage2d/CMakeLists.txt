# gfx-stage2d, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(gfx_stage2d LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-stage2d ASSETS main.c)
