# ui-menu, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(ui_menu LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(ui-menu main.c)
