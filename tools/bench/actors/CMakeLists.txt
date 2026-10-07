# The actor benchmark, built against a staged libwgf (examples/c/wgf_example.cmake) by
# tools/bench/measure_actors.py.
cmake_minimum_required(VERSION 3.21)
project(bench_actors LANGUAGES C)
include(../../../examples/c/wgf_example.cmake)
wgf_example(bench_actors main.c)
