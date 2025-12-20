cmake_minimum_required(VERSION 3.14)

project(vg LANGUAGE C)

add_library(nanovg_lib STATIC
    nanovg.c
)

target_include_directories(nanovg_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nanovg)