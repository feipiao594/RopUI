add_subdirectory(log_test)
add_subdirectory(work_steal_trace_demo)
add_subdirectory(tcp_client_example)
add_subdirectory(tcp_server_example)
add_subdirectory(tcp_server_async_example)

if (UNIX AND NOT APPLE)
add_subdirectory(linux_loop_core_test)
add_subdirectory(rop_hive_epoll_demo)
add_subdirectory(egl_window)
endif()

if(APPLE)
add_subdirectory(cocoa_metal_bad_apple)  # 需要手动启用，详见该目录下的README.md
endif()

if(WIN32)
add_subdirectory(iocp_tcp_server_example)
add_subdirectory(win32_window_example)


# 连接 Windows Socket 库 ws2_32.lib，提供 Winsock API 的实现
target_link_libraries(tcp_server_example PRIVATE ws2_32)
target_link_libraries(tcp_server_async_example PRIVATE ws2_32)
target_link_libraries(tcp_client_example PRIVATE ws2_32)
endif()
