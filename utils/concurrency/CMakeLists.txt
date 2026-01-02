add_library(ropui_concurrency STATIC
    thread_pool.cc
    task_deque.cc
)

target_include_directories(ropui_concurrency
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
