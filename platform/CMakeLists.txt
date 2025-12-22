# platform/CMakeLists.txt

set(ROPUI_PLATFORM_SOURCES
    schedule/eventloop_core.cc
    schedule/eventloop.cc
)

if (UNIX AND NOT APPLE)
    add_subdirectory(linux)
    list(APPEND ROPUI_PLATFORM_SOURCES
        ${ROPUI_PLATFORM_LINUX_SOURCES}
    )
elseif (APPLE)
    add_subdirectory(macos)
    list(APPEND ROPUI_PLATFORM_SOURCES
        ${ROPUI_PLATFORM_MACOS_SOURCES}
    )
elseif (WIN32)
    add_subdirectory(windows)
    list(APPEND ROPUI_PLATFORM_SOURCES
        ${ROPUI_PLATFORM_WINDOWS_SOURCES}
    )
endif()

add_library(ropui_platform OBJECT
    ${ROPUI_PLATFORM_SOURCES}
)

target_include_directories(ropui_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ropui_platform
    PRIVATE
        ropui_log
)
