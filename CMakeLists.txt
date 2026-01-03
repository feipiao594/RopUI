cmake_minimum_required(VERSION 3.14)

project(ropui)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXPORT_COMPILE_COMMANDS ON)

option(ROPUI_BUILD_WAYLAND "Build wayland" OFF)
option(ROPUI_BUILD_EXAMPLES "Build examples" ON)
option(ROPUI_BUILD_TESTS "Build tests" OFF)

add_subdirectory(deps)
# add_subdirectory(graphic)
add_subdirectory(platform)
add_subdirectory(utils)
add_subdirectory(vg)

add_library(ropui STATIC)

target_sources(ropui PRIVATE
    $<TARGET_OBJECTS:ropui_platform>
    $<TARGET_OBJECTS:vg>
    $<TARGET_OBJECTS:glad>
)

target_include_directories(ropui
    PUBLIC
        ${PROJECT_BINARY_DIR}/platform
        ${PROJECT_SOURCE_DIR}/deps/glad/include
        ${PROJECT_SOURCE_DIR}/vg/nanovg
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(ropui
    PUBLIC
        ropui_log
        ropui_concurrency
)

if(ROPUI_BUILD_EXAMPLES)
add_subdirectory(example)
endif()

if(ROPUI_BUILD_TESTS)
enable_testing()
add_subdirectory(tests)
endif()
