cmake_minimum_required(VERSION 3.15)

project(GCPad_API VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "GCPad currently only targets Windows. Linux support is planned.")
endif()

# ── Build metadata ────────────────────────────────────────────────────────────

string(TIMESTAMP GCPAD_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GCPAD_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GCPAD_GIT_COMMIT)
    set(GCPAD_GIT_COMMIT "unknown")
endif()

# Generate version.rc before add_library so the source path is unambiguous
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

# ── Main shared library ───────────────────────────────────────────────────────

add_library(gcpad SHARED
    src/hid_device.cpp
    src/ps_device.cpp
    src/xbox_device.cpp
    src/nintendo_device.cpp
    src/xinput_device.cpp
    src/dinput_device.cpp
    src/sdl_device.cpp
    src/GamepadManager.cpp
    src/build_metadata.cpp
    src/gcpad_c.cpp
    gcpadLib/controller_library.cpp
    gcpadLib/usb_interface_parser.cpp
    gcpadLib/gamepad_input_remapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

target_include_directories(gcpad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gcpadLib>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(gcpad PRIVATE
    GCPAD_API_EXPORTS
    GCPAD_PACKAGE_VERSION="${PROJECT_VERSION}"
    GCPAD_GIT_COMMIT="${GCPAD_GIT_COMMIT}"
    GCPAD_BUILD_TIMESTAMP="${GCPAD_BUILD_TIMESTAMP}"
    GCPAD_BUILD_GENERATOR="${CMAKE_GENERATOR}"
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Local libs directory contains SDL2.lib, Xinput9_1_0.lib, dinput8.lib, dxguid.lib
target_link_directories(gcpad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)

target_link_libraries(gcpad PRIVATE
    Xinput9_1_0
    dinput8
    dxguid
    SDL2
)

set_target_properties(gcpad PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ── Install rules ─────────────────────────────────────────────────────────────

install(TARGETS gcpad
    EXPORT      GCPadTargets
    LIBRARY     DESTINATION lib
    ARCHIVE     DESTINATION lib
    RUNTIME     DESTINATION bin
    INCLUDES    DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT GCPadTargets
    FILE        GCPadTargets.cmake
    NAMESPACE   GCPad::
    DESTINATION lib/cmake/GCPad
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/GCPadConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfig.cmake
    INSTALL_DESTINATION lib/cmake/GCPad
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfigVersion.cmake
    DESTINATION lib/cmake/GCPad
)

# ── Example / test executable ─────────────────────────────────────────────────

option(GCPAD_BUILD_EXAMPLE "Build the gcpad_example test program" ON)
if(GCPAD_BUILD_EXAMPLE)
    add_executable(gcpad_example examples/gcpad_example.cpp)
    target_link_libraries(gcpad_example PRIVATE gcpad)

    # Copy gcpad.dll next to the executable so it runs without installation
    add_custom_command(TARGET gcpad_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:gcpad>
            $<TARGET_FILE_DIR:gcpad_example>
        COMMENT "Copying gcpad.dll"
    )

    # Copy SDL2.dll next to the executable
    add_custom_command(TARGET gcpad_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/dlls/SDL2.dll
            $<TARGET_FILE_DIR:gcpad_example>
        COMMENT "Copying SDL2.dll"
    )
endif()