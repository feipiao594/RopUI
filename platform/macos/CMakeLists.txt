set(ROPUI_PLATFORM_MACOS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/poll_backend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/poll_wakeup.cc
)

# macOS backends are intentionally disabled by default on non-macOS machines.
# Enable when building on macOS:
#   -DROPUI_ENABLE_MACOS_BACKENDS=ON
if (ROPUI_ENABLE_MACOS_BACKENDS)
    list(APPEND ROPUI_PLATFORM_MACOS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/kqueue_backend.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/cocoa_backend.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/kqueue_wakeup.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/cocoa_wakeup.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/kqueue_worker_wakeup.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/schedule/watcher/cocoa_worker_wakeup.mm
    )
endif()

set(ROPUI_PLATFORM_MACOS_SOURCES
    ${ROPUI_PLATFORM_MACOS_SOURCES}
    PARENT_SCOPE
)
