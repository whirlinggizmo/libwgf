# asset-force-fetch, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(asset_force_fetch LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(asset-force-fetch ASSETS main.c)
