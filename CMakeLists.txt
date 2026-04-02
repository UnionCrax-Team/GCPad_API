cmake_minimum_required(VERSION 3.10)
project(GCPad_API VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create the library
add_library(gcpad SHARED
    src/hid_device.cpp
    src/ps_device.cpp
    src/xbox_device.cpp
    src/nintendo_device.cpp
    src/GamepadManager.cpp
    src/build_metadata.cpp
    gcpadLib/controller_library.cpp
    gcpadLib/usb_interface_parser.cpp
    gcpadLib/gamepad_input_remapper.cpp
    version.rc
)

# Include directories
target_include_directories(gcpad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gcpadLib>
    $<INSTALL_INTERFACE:include>
)

# Link required libraries
target_link_libraries(gcpad PRIVATE
    hid.lib
    setupapi.lib
)

# Set version properties for the library
set_target_properties(gcpad PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Build metadata (timestamp + commit) for runtime introspection
string(TIMESTAMP GCPAD_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GCPAD_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GCPAD_GIT_COMMIT)
    set(GCPAD_GIT_COMMIT "unknown")
endif()

target_compile_definitions(gcpad PRIVATE
    GCPAD_API_EXPORTS
    GCPAD_PACKAGE_VERSION="${PROJECT_VERSION}"
    GCPAD_GIT_COMMIT="${GCPAD_GIT_COMMIT}"
    GCPAD_BUILD_TIMESTAMP="${GCPAD_BUILD_TIMESTAMP}"
    GCPAD_BUILD_GENERATOR="${CMAKE_GENERATOR}"
)

# Install targets
install(TARGETS gcpad
    EXPORT GCPadTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

# Install CMake config files
install(EXPORT GCPadTargets
    NAMESPACE GCPad::
    DESTINATION lib/cmake/GCPad)

include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    INSTALL_DESTINATION lib/cmake/GCPad
)

configure_file(
    version.rc.in
    version.rc
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfigVersion.cmake
    DESTINATION lib/cmake/GCPad
)