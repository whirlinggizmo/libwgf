# gfx-sprite3d, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(gfx_sprite3d LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-sprite3d ASSETS main.c)
