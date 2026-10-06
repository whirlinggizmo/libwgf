# audio-music, built against a staged libwgf (../wgf_example.cmake), with the files in
# examples/assets/.
cmake_minimum_required(VERSION 3.21)
project(audio_music LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(audio-music ASSETS main.c)
