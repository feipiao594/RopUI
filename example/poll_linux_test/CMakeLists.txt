cmake_minimum_required(VERSION 3.14)

project(poll_linux_test)

add_executable(poll_linux_test main.cc)

target_link_libraries(poll_linux_test PUBLIC ropui)