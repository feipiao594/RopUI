set(ROPUI_PLATFORM_LINUX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/poll_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/epoll_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/epoll_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/epoll_worker_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/poll_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/poll_worker_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland_backend.cc
)

set(ROPUI_PLATFORM_LINUX_SOURCES
    ${ROPUI_PLATFORM_LINUX_SOURCES}
    PARENT_SCOPE
)
