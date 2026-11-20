# audio-synth, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(audio_synth LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(audio-synth main.c)
