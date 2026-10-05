# app-hello, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(app_hello LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(app-hello main.c)
