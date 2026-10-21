# SPDX-License-Identifier: GPL-3.0-or-later
# Power Pros Wii 2008 Season Studio. Built by tools/season-studio/build.ps1 (and Make-Payload) with the
# pinned llvm-mingw, statically against SDL3 and Dear ImGui from the runtime's dependency sources. It
# shares the Texture Studio's look (ui.h) and Config.toml editing (config.cpp), and the runtime's season
# headers (season_pack.h, season_schedule.h, season_schedule_mlb.h, season_text.h) and toml11.
cmake_minimum_required(VERSION 3.24)
project(SeasonStudio LANGUAGES C CXX)

set(SDL_SOURCE_DIR "" CACHE PATH "SDL3 source tree")
set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui source tree")
set(UI_INCLUDE_DIR "" CACHE PATH "Patched Wiicompiled runtime/include")
set(TOML_INCLUDE_DIR "" CACHE PATH "toml11 (runtime/third_party/toml11)")
set(TEXTURE_STUDIO_DIR "${CMAKE_CURRENT_LIST_DIR}/../texture-studio")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO CAMERA HAPTIC SENSOR POWER JOYSTICK)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SDL_SOURCE_DIR}" sdl EXCLUDE_FROM_ALL)

add_executable(SeasonStudio
    studio.cpp
    "${TEXTURE_STUDIO_DIR}/config.cpp"
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(SeasonStudio PRIVATE "${IMGUI_SOURCE_DIR}" "${UI_INCLUDE_DIR}" "${TOML_INCLUDE_DIR}" "${TEXTURE_STUDIO_DIR}")
target_compile_features(SeasonStudio PRIVATE cxx_std_20)
target_compile_definitions(SeasonStudio PRIVATE UNICODE _UNICODE)
target_link_libraries(SeasonStudio PRIVATE SDL3::SDL3-static)
if(WIN32)
    set_target_properties(SeasonStudio PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(SeasonStudio PRIVATE -static)
endif()
