# asset-loading, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(asset_loading LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(asset-loading ASSETS main.c)
