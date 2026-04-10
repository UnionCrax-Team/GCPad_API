cmake_minimum_required(VERSION 3.15)

project(GCPad_Frontend_CMD VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "GCPad_Frontend currently only targets Windows.")
endif()

add_executable(GCPad_Frontend_CMD src/main.cpp)

target_compile_definitions(GCPad_Frontend_CMD PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Link against both the core library and the remapper
target_link_libraries(GCPad_Frontend_CMD PRIVATE gcpad gcpad_remap)

target_link_directories(GCPad_Frontend_CMD PRIVATE ${CMAKE_SOURCE_DIR}/GCPad_Lib/libs)
target_link_libraries(GCPad_Frontend_CMD PRIVATE SDL2)

# Copy DLLs next to the executable so it runs without installation
add_custom_command(TARGET GCPad_Frontend_CMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gcpad>
        $<TARGET_FILE_DIR:GCPad_Frontend_CMD>
    COMMENT "Copying gcpad.dll"
)

# Copy SDL2.dll
add_custom_command(TARGET GCPad_Frontend_CMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/GCPad_Lib/libs/dlls/SDL2.dll
        $<TARGET_FILE_DIR:GCPad_Frontend_CMD>
    COMMENT "Copying SDL2.dll"
)
