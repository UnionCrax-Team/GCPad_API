cmake_minimum_required(VERSION 3.15)

project(GCPad_Frontend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "GCPad_Frontend currently only targets Windows.")
endif()

add_executable(gcpad_frontend src/main.cpp)

target_compile_definitions(gcpad_frontend PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Link against both the core library and the remapper
target_link_libraries(gcpad_frontend PRIVATE gcpad gcpad_remap)

# Copy DLLs next to the executable so it runs without installation
add_custom_command(TARGET gcpad_frontend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gcpad>
        $<TARGET_FILE_DIR:gcpad_frontend>
    COMMENT "Copying gcpad.dll"
)

# Copy SDL2.dll
add_custom_command(TARGET gcpad_frontend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/GCPad_Lib/libs/dlls/SDL2.dll
        $<TARGET_FILE_DIR:gcpad_frontend>
    COMMENT "Copying SDL2.dll"
)
