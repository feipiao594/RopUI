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

add_test(NAME task_deque_test COMMAND task_deque_test)

add_executable(hive_epoll_smoke_test
    hive_epoll_smoke_test.cc
)

target_include_directories(hive_epoll_smoke_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(hive_epoll_smoke_test
    PRIVATE
        ropui
)

add_test(NAME hive_epoll_smoke_test COMMAND hive_epoll_smoke_test)

add_executable(ip_endpoint_test
    ip_endpoint_test.cc
)

target_include_directories(ip_endpoint_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(ip_endpoint_test
    PRIVATE
        ropui
)

add_test(NAME ip_endpoint_test COMMAND ip_endpoint_test)
