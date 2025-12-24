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

if (ROPUI_BUILD_WAYLAND)
    include(CheckIncludeFiles)
    include(CheckFunctionExists)
    check_function_exists(memfd_create HAVE_MEMFD_CREATE)

    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
    if (NOT WAYLAND_SCANNER_EXECUTABLE)
        message(FATAL_ERROR "Failed to find wayland-scanner")
    endif()

    macro(generate_wayland_protocol protocol_name)
        set(protocol_path "${CMAKE_SOURCE_DIR}/platform/linux/wayland/proto_xml/${protocol_name}.xml")

        set(header_file "${protocol_name}-client-protocol.h")
        set(code_file "${protocol_name}-client-protocol-code.c")

        add_custom_command(OUTPUT ${header_file} ${code_file}
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${protocol_path}" ${header_file}
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${protocol_path}" ${code_file}
            DEPENDS "${protocol_path}"
            VERBATIM
        )

        add_custom_target(
            generate-${protocol_name}-protocol
            DEPENDS ${header_file} ${code_file}
        )

        add_dependencies(ropui_platform generate-${protocol_name}-protocol)

        target_sources(ropui_platform PRIVATE ${header_file} ${code_file})
    endmacro()

    generate_wayland_protocol("wayland")
    generate_wayland_protocol("viewporter")
    generate_wayland_protocol("xdg-shell")
    generate_wayland_protocol("idle-inhibit-unstable-v1")
    generate_wayland_protocol("pointer-constraints-unstable-v1")
    generate_wayland_protocol("relative-pointer-unstable-v1")
    generate_wayland_protocol("fractional-scale-v1")
    generate_wayland_protocol("xdg-activation-v1")
    generate_wayland_protocol("xdg-decoration-unstable-v1")
endif()

target_include_directories(ropui_platform
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ropui_platform
    PRIVATE
        ropui_log
)