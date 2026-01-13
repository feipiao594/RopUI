add_subdirectory(log_test)
add_subdirectory(work_steal_trace_demo)

if (UNIX AND NOT APPLE)
add_subdirectory(linux_loop_core_test)
add_subdirectory(tcp_client_example)
add_subdirectory(rop_hive_epoll_demo)
add_subdirectory(egl_window)
endif()

if(APPLE)
add_subdirectory(kqueue_tcp_server_example)
# add_subdirectory(cocoa_metal_bad_apple)  # 需要手动启用，详见该目录下的README.md
endif()

if(WIN32)
add_subdirectory(iocp_tcp_server_example)
add_subdirectory(win32_window_example)
endif()
