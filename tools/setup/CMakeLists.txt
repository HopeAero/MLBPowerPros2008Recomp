# SPDX-License-Identifier: GPL-3.0-or-later
# MLB Power Pros 2008 Setup (setup.cpp). Built by tools/setup/Make-Payload.ps1 with the pinned
# llvm-mingw, statically against SDL3 and Dear ImGui from the runtime's own dependency sources.
cmake_minimum_required(VERSION 3.24)
# Bump with each release: it is the Setup's file version (setup.rc).
project(MLBPowerProsSetup VERSION 0.1.6 LANGUAGES C CXX RC)

set(SDL_SOURCE_DIR "" CACHE PATH "SDL3 source tree")
set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui source tree")
set(UI_INCLUDE_DIR "" CACHE PATH "Patched Wiicompiled runtime/include (power_pros_ui.h)")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO CAMERA HAPTIC SENSOR POWER)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SDL_SOURCE_DIR}" sdl EXCLUDE_FROM_ALL)

add_executable(MLBPowerPros2008-Setup WIN32
    setup.cpp
    setup.rc
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(MLBPowerPros2008-Setup PRIVATE "${IMGUI_SOURCE_DIR}" "${UI_INCLUDE_DIR}")
target_compile_features(MLBPowerPros2008-Setup PRIVATE cxx_std_20)
target_compile_definitions(MLBPowerPros2008-Setup PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00
    SETUP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} SETUP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SETUP_VERSION_PATCH=${PROJECT_VERSION_PATCH})
target_link_libraries(MLBPowerPros2008-Setup PRIVATE SDL3::SDL3-static)
target_link_options(MLBPowerPros2008-Setup PRIVATE -static)
