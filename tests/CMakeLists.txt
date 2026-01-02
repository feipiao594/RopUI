add_executable(task_deque_test
    task_deque_test.cc
)

target_include_directories(task_deque_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/utils
)

target_link_libraries(task_deque_test
    PRIVATE
        ropui_concurrency
)

add_executable(thread_pool_test
    thread_pool_test.cc
)

target_include_directories(thread_pool_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/utils
)

target_link_libraries(thread_pool_test
    PRIVATE
        ropui_concurrency
)

add_test(NAME task_deque_test COMMAND task_deque_test)
add_test(NAME thread_pool_test COMMAND thread_pool_test)

