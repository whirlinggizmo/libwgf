# app-touch, built against a staged libwgf (../wgf_example.cmake), with the files in
# examples/assets/.
cmake_minimum_required(VERSION 3.21)
project(app_touch LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(app-touch ASSETS main.c)
