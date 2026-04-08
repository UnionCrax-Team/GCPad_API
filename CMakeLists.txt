cmake_minimum_required(VERSION 3.15)

project(GCPad VERSION 1.0.0 LANGUAGES CXX RC)

# Build order:
#   1. GCPad_Lib    — Core library (gcpad.dll)
#   2. GCPad_Remap  — Input remapper (static lib, links gcpad)
#   3. GCPad_Frontend — Reference application (links gcpad + gcpad_remap)
#
# To build everything:
#   cmake -B build -G "Visual Studio 17 2022"
#   cmake --build build --config Release
#
# To build just the library:
#   cmake -B build -G "Visual Studio 17 2022" -DGCPAD_BUILD_FRONTEND=OFF -DGCPAD_BUILD_REMAP=OFF
#   cmake --build build --config Release

option(GCPAD_BUILD_REMAP    "Build the GCPad_Remap input translation module" ON)
option(GCPAD_BUILD_FRONTEND "Build the GCPad_Frontend reference application" ON)

# Core library (always built)
add_subdirectory(GCPad_Lib)

# Input remapper
if(GCPAD_BUILD_REMAP OR GCPAD_BUILD_FRONTEND)
    add_subdirectory(GCPad_Remap)
endif()

# Frontend application
if(GCPAD_BUILD_FRONTEND)
    add_subdirectory(GCPad_Frontend)
endif()
