# ui-document, built against a staged libwgf (../wgf_example.cmake), with the files in
# examples/assets/. C++ too: the library's documents are RmlUi, which a program making one
# links with C++'s runtime.
cmake_minimum_required(VERSION 3.21)
project(ui_document LANGUAGES C CXX)
include(../wgf_example.cmake)
wgf_example(ui-document ASSETS main.c)
set_target_properties(ui-document PROPERTIES LINKER_LANGUAGE CXX)
