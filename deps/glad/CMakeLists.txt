add_library(glad OBJECT
    src/egl.c
    src/gles2.c
)

target_include_directories(glad
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
