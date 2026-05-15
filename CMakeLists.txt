cmake_minimum_required(VERSION 3.15)

if(WIN32)
    project(GCPad VERSION 1.0.0 LANGUAGES CXX RC)
else()
    project(GCPad VERSION 1.0.0 LANGUAGES CXX)
endif()

# Build order:
#   1. GCPad_Lib    — Core library (gcpad.dll / libgcpad.so)
#   2. GCPad_Remap  — Input remapper (static lib, links gcpad)
#   3. GCPad_Frontend_CMD — Console reference app (links gcpad + gcpad_remap)
#   4. GCPad_Frontend_GUI — GUI reference app using SDL2 + Dear ImGui
#
# To build everything:
#   Windows: cmake -B build -G "Visual Studio 17 2022" && cmake --build build --config Release
#   Linux:   cmake -B build && cmake --build build
#
# To build just the library:
#   cmake -B build -DGCPAD_BUILD_FRONTEND_CMD=OFF -DGCPAD_BUILD_FRONTEND_GUI=OFF -DGCPAD_BUILD_REMAP=OFF

option(GCPAD_BUILD_REMAP        "Build the GCPad_Remap input translation module"   ON)
option(GCPAD_BUILD_FRONTEND_CMD "Build the console frontend (Windows only)"  ${WIN32})
option(GCPAD_BUILD_FRONTEND_GUI "Build the GUI frontend (SDL2 + Dear ImGui)"       ON)

# Core library (always built)
add_subdirectory(GCPad_Lib)

# Input remapper
if(GCPAD_BUILD_REMAP OR GCPAD_BUILD_FRONTEND_CMD OR GCPAD_BUILD_FRONTEND_GUI)
    add_subdirectory(GCPad_Remap)
endif()

# Console frontend (Windows only — uses conio.h, Win32 console API)
if(GCPAD_BUILD_FRONTEND_CMD AND WIN32)
    add_subdirectory(GCPad_Frontend_CMD)
endif()

# GUI frontend (cross-platform)
if(GCPAD_BUILD_FRONTEND_GUI)
    add_subdirectory(GCPad_Frontend_GUI)
endif()
