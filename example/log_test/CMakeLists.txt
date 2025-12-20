cmake_minimum_required(VERSION 3.14)

project(log_test)

add_executable(log_test main.cc)

target_link_libraries(log_test PUBLIC ropui)