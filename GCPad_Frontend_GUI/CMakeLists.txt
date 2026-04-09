cmake_minimum_required(VERSION 3.15)

project(GCPad_Frontend_GUI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dear ImGui (fetched at configure time) ────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static library with SDL2 + SDL2Renderer backends
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(imgui_lib PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN NOMINMAX>
)

# ── Find SDL2 ─────────────────────────────────────────────────────────────────

if(WIN32)
    # Use the vendored SDL2 from GCPad_Lib
    set(SDL2_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/GCPad_Lib/vendor/SDL2)
    set(SDL2_LIBRARY     ${CMAKE_SOURCE_DIR}/GCPad_Lib/libs/SDL2.lib)
    target_include_directories(imgui_lib PUBLIC ${SDL2_INCLUDE_DIR})
else()
    # On Linux, use system SDL2
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_include_directories(imgui_lib PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(imgui_lib PUBLIC PkgConfig::SDL2)
endif()

# ── Frontend executable ───────────────────────────────────────────────────────

add_executable(GCPad_Frontend_GUI src/main.cpp)

target_compile_definitions(GCPad_Frontend_GUI PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN NOMINMAX>
)

target_link_libraries(GCPad_Frontend_GUI PRIVATE gcpad gcpad_remap imgui_lib)

if(WIN32)
    target_link_directories(GCPad_Frontend_GUI PRIVATE ${CMAKE_SOURCE_DIR}/GCPad_Lib/libs)
    target_link_libraries(GCPad_Frontend_GUI PRIVATE SDL2)
    set_target_properties(GCPad_Frontend_GUI PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_include_directories(GCPad_Frontend_GUI PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(GCPad_Frontend_GUI PRIVATE PkgConfig::SDL2)
endif()

# ── Copy DLLs (Windows) ──────────────────────────────────────────────────────

if(WIN32)
    add_custom_command(TARGET GCPad_Frontend_GUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:gcpad> $<TARGET_FILE_DIR:GCPad_Frontend_GUI>
        COMMENT "Copying gcpad.dll"
    )
    add_custom_command(TARGET GCPad_Frontend_GUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/GCPad_Lib/libs/dlls/SDL2.dll
            $<TARGET_FILE_DIR:GCPad_Frontend_GUI>
        COMMENT "Copying SDL2.dll"
    )
endif()
