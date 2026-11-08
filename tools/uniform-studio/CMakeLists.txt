# SPDX-License-Identifier: GPL-3.0-or-later
# Power Pros Wii 2008 Uniform Studio. Built by tools/uniform-studio/build.ps1 with the pinned llvm-mingw,
# statically against SDL3 and Dear ImGui, reusing the Texture Studio's core (gx/images/config, core.h, ui.h).
cmake_minimum_required(VERSION 3.24)
project(UniformStudio LANGUAGES C CXX)

set(SDL_SOURCE_DIR "" CACHE PATH "SDL3 source tree")
set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui source tree")
set(UI_INCLUDE_DIR "" CACHE PATH "Patched Wiicompiled runtime/include (power_pros_ui.h)")

set(TEXTURE_STUDIO "${CMAKE_CURRENT_SOURCE_DIR}/../texture-studio")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO CAMERA HAPTIC SENSOR POWER JOYSTICK)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SDL_SOURCE_DIR}" sdl EXCLUDE_FROM_ALL)

add_executable(UniformStudio
    main.cpp canvas.cpp layers.cpp preview.cpp render3d.cpp
    "${TEXTURE_STUDIO}/gx.cpp" "${TEXTURE_STUDIO}/images.cpp" "${TEXTURE_STUDIO}/config.cpp"
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(UniformStudio PRIVATE "${IMGUI_SOURCE_DIR}" "${UI_INCLUDE_DIR}" "${TEXTURE_STUDIO}")
target_compile_features(UniformStudio PRIVATE cxx_std_20)
target_compile_definitions(UniformStudio PRIVATE UNICODE _UNICODE)
target_link_libraries(UniformStudio PRIVATE SDL3::SDL3-static)
if(WIN32)
    set_target_properties(UniformStudio PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(UniformStudio PRIVATE -static)
endif()
