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
# Include HID, SetupAPI, XInput, DirectInput, and SDL2 imports
if(WIN32)
    # Add local libs directory as fallback so the project can build from copied .lib files
    link_directories("${CMAKE_SOURCE_DIR}/libs")

    find_library(XINPUT_LIB NAMES xinput1_4 xinput9_1_0 PATHS "${CMAKE_SOURCE_DIR}/libs" NO_DEFAULT_PATH)
    if(NOT XINPUT_LIB)
        find_library(XINPUT_LIB NAMES xinput1_4 xinput9_1_0)
    endif()

    find_library(DINPUT_LIB NAMES dinput8 PATHS "${CMAKE_SOURCE_DIR}/libs" NO_DEFAULT_PATH)
    if(NOT DINPUT_LIB)
        find_library(DINPUT_LIB NAMES dinput8)
    endif()

    find_library(DXGUID_LIB NAMES dxguid PATHS "${CMAKE_SOURCE_DIR}/libs" NO_DEFAULT_PATH)
    if(NOT DXGUID_LIB)
        find_library(DXGUID_LIB NAMES dxguid)
    endif()

    set(GCPAD_BASE_LIBS hid setupapi)

    if(XINPUT_LIB)
        list(APPEND GCPAD_BASE_LIBS ${XINPUT_LIB})
    else()
        message(WARNING "XInput library not found (xinput1_4 / xinput9_1_0). Please place the .lib into libs/ or install Windows SDK.")
    endif()

    if(DINPUT_LIB)
        list(APPEND GCPAD_BASE_LIBS ${DINPUT_LIB})
    else()
        message(WARNING "DirectInput library not found (dinput8). Please place the .lib into libs/ or install Windows SDK.")
    endif()

    if(DXGUID_LIB)
        list(APPEND GCPAD_BASE_LIBS ${DXGUID_LIB})
    else()
        message(WARNING "dxguid library not found. Please place the .lib into libs/ or install Windows SDK.")
    endif()

    target_link_libraries(gcpad PRIVATE ${GCPAD_BASE_LIBS})

    find_library(SDL2_LIBRARY NAMES SDL2 SDL2d PATHS "${CMAKE_SOURCE_DIR}/libs" "${CMAKE_SOURCE_DIR}/lib" "${CMAKE_SOURCE_DIR}/third_party/lib")
    if(SDL2_LIBRARY)
        target_link_libraries(gcpad PRIVATE ${SDL2_LIBRARY})
    else()
        message(WARNING "SDL2 library not found; SDL support will be unavailable until SDL2 is installed and in a library path")
    endif()
else()
    target_link_libraries(gcpad PRIVATE
        hid
        setupapi
    )
endif()

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
    FILE GCPadTargets.cmake
    NAMESPACE GCPad::
    DESTINATION lib/cmake/GCPad)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GCPadConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_file(
    version.rc.in
    version.rc
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/GCPadConfigVersion.cmake
    DESTINATION lib/cmake/GCPad
)