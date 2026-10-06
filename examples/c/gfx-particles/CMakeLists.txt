# gfx-particles, built against a staged libwgf (../wgf_example.cmake). It loads no
# files: libwgf's particles have no texture.
cmake_minimum_required(VERSION 3.21)
project(gfx_particles LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(gfx-particles main.c)
