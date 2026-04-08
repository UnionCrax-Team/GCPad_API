cmake_minimum_required(VERSION 3.15)

project(GCPad_Remap VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "GCPad_Remap currently only targets Windows.")
endif()

add_library(gcpad_remap STATIC
    src/gamepad_input_remapper.cpp
)

target_include_directories(gcpad_remap
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(gcpad_remap PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Depends on GCPad_Lib for gcpad.h types
target_link_libraries(gcpad_remap PUBLIC gcpad)
