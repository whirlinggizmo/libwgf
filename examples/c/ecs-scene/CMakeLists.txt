# ecs-scene, built against a staged libwgf (../wgf_example.cmake).
cmake_minimum_required(VERSION 3.21)
project(ecs_scene LANGUAGES C)
include(../wgf_example.cmake)
wgf_example(ecs-scene ASSETS main.c)
