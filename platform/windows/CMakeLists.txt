set(ROPUI_PLATFORM_WINDOWS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/win32_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/iocp_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/win32_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/iocp_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/win32_worker_wakeup.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/iocp_worker_wakeup.cc
)

set(ROPUI_PLATFORM_WINDOWS_SOURCES
    ${ROPUI_PLATFORM_WINDOWS_SOURCES}
    PARENT_SCOPE
)

