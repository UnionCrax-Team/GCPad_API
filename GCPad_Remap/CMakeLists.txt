cmake_minimum_required(VERSION 3.15)

project(GCPad_Remap VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(gcpad_remap SHARED
    src/gamepad_input_remapper.cpp
    src/gcpad_remap_c.cpp
)

target_include_directories(gcpad_remap
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/../GCPad_Lib/include
)

if(WIN32)
    target_compile_definitions(gcpad_remap PUBLIC
        GCPAD_REMAP_EXPORTS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
elseif(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(gcpad_remap PUBLIC X11::X11 X11::Xtst X11::Xi)
endif()

target_link_libraries(gcpad_remap PUBLIC gcpad)
