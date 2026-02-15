set(ROPUI_PLATFORM_WINDOWS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/win32_wrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/win32_wrapper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/win32_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/iocp_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/win32_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/iocp_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/win32_worker_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/iocp_worker_wakeup.cc

# Network backends
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_accept_watcher.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_accept_watcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_socket_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_connect_watcher.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_connect_watcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_connection_watcher.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/network/watcher/tcp_connection_watcher.h
)

set(ROPUI_PLATFORM_WINDOWS_SOURCES
    ${ROPUI_PLATFORM_WINDOWS_SOURCES}
    PARENT_SCOPE
)

