cmake_minimum_required(VERSION 3.15)

if(WIN32)
    project(GCPad_Lib VERSION 1.0.0 LANGUAGES CXX RC)
else()
    project(GCPad_Lib VERSION 1.0.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# ── Source files (platform-conditional) ───────────────────────────────────────

# Cross-platform sources
set(GCPAD_SOURCES
    src/sdl_device.cpp
    src/GamepadManager.cpp
    src/build_metadata.cpp
    src/gcpad_c.cpp
)

if(WIN32)
    # Windows-only backends. controller_library.cpp / usb_interface_parser.cpp
    # both depend on the raw-HID PS/Xbox/Nintendo enumerators, which are Win32
    # only. On Linux SDL2 is the only backend we ship, so those modules are
    # Windows-only too.
    list(APPEND GCPAD_SOURCES
        src/hid_device.cpp
        src/ps_device.cpp
        src/xbox_device.cpp
        src/nintendo_device.cpp
        src/xinput_device.cpp
        src/dinput_device.cpp
        src/controller_library.cpp
        src/usb_interface_parser.cpp
    )

    # Version resource
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        @ONLY
    )
    list(APPEND GCPAD_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

# ── Shared library ────────────────────────────────────────────────────────────

list(APPEND GCPAD_SOURCES
    ${CMAKE_SOURCE_DIR}/GCPad_Remap/src/gamepad_input_remapper.cpp
)

add_library(gcpad SHARED ${GCPAD_SOURCES})

target_include_directories(gcpad
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/internal
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/../GCPad_Remap/include
)

target_compile_definitions(gcpad PUBLIC
    GCPAD_API_EXPORTS
    GCPAD_PACKAGE_VERSION="${PROJECT_VERSION}"
    GCPAD_GIT_COMMIT="${GCPAD_GIT_COMMIT}"
    GCPAD_BUILD_TIMESTAMP="${GCPAD_BUILD_TIMESTAMP}"
    GCPAD_BUILD_GENERATOR="${CMAKE_GENERATOR}"
)

# ── Platform-specific linking ─────────────────────────────────────────────────

if(WIN32)
    target_compile_definitions(gcpad PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_directories(gcpad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)
    target_link_libraries(gcpad PRIVATE Xinput9_1_0 dinput8 dxguid SDL2)
else()
    # Linux: use system SDL2, set visibility for shared lib exports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(gcpad PRIVATE PkgConfig::SDL2)
    # X11 + XTest for synthetic input on Linux (used by GCPad_Remap, which is
    # compiled into libgcpad on this platform).
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xtst)
    target_link_libraries(gcpad PRIVATE PkgConfig::X11)
    target_compile_definitions(gcpad PRIVATE
        GCPAD_LINUX
    )
    set_target_properties(gcpad PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

set_target_properties(gcpad PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ── Install rules ─────────────────────────────────────────────────────────────

install(TARGETS gcpad
    EXPORT      GCPadTargets
    LIBRARY     DESTINATION lib
    ARCHIVE     DESTINATION lib
    RUNTIME    DESTINATION bin
    INCLUDES   DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

# Don't export gcpad_remap to the CMake export set (it links statically into gcpad)
# Users who use gcpad will get gcpad_remap implicitly through the static lib

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
