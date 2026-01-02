add_library(ropui_concurrency STATIC
    task_deque.cc
)

target_include_directories(ropui_concurrency
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
