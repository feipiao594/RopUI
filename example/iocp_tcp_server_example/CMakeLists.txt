cmake_minimum_required(VERSION 3.14)

project(iocp_tcp_server_example)

add_executable(iocp_tcp_server_example main.cc)

target_link_libraries(iocp_tcp_server_example PUBLIC ropui ws2_32 mswsock)