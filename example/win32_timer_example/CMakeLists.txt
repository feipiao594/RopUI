cmake_minimum_required(VERSION 3.14)

project(win32_timer_example)

add_executable(win32_timer_example main.cc)


target_link_libraries(win32_timer_example PUBLIC ropui)
