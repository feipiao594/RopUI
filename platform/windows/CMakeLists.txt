set(ROPUI_PLATFORM_WINDOWS_SOURCES)

# Windows backends are intentionally disabled by default on non-Windows CI/dev machines.
# Enable when building on Windows:
#   -DROPUI_ENABLE_WINDOWS_BACKENDS=ON
if (ROPUI_ENABLE_WINDOWS_BACKENDS)
    list(APPEND ROPUI_PLATFORM_WINDOWS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/win32_backend.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/iocp_backend.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/win32_wakeup.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/iocp_wakeup.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/win32_worker_wakeup.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/iocp_worker_wakeup.cc
    )
endif()

set(ROPUI_PLATFORM_WINDOWS_SOURCES
    ${ROPUI_PLATFORM_WINDOWS_SOURCES}
    PARENT_SCOPE
)

