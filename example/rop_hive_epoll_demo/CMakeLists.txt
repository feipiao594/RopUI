add_executable(rop_hive_epoll_demo
    main.cc
)

target_include_directories(rop_hive_epoll_demo
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(rop_hive_epoll_demo
    PRIVATE
        ropui
)

