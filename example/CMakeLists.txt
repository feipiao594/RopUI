add_subdirectory(log_test)

if (UNIX AND NOT APPLE)
add_subdirectory(linux_loop_core_test)
add_subdirectory(tcp_client_example)
add_subdirectory(rop_hive_epoll_demo)
add_subdirectory(egl_window)
endif()
