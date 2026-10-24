# SPDX-License-Identifier: GPL-3.0-or-later
# Power Pros Wii 2008 Texture Studio. Built by tools/texture-studio/build.ps1 (and Make-Payload) with the
# pinned llvm-mingw, statically against SDL3, Dear ImGui and xxhash from the runtime's dependency sources.
cmake_minimum_required(VERSION 3.24)
project(TextureStudio LANGUAGES C CXX)

set(SDL_SOURCE_DIR "" CACHE PATH "SDL3 source tree")
set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui source tree")
set(XXHASH_SOURCE_DIR "" CACHE PATH "xxhash source tree")
set(UI_INCLUDE_DIR "" CACHE PATH "Patched Wiicompiled runtime/include (power_pros_ui.h)")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO CAMERA HAPTIC SENSOR POWER JOYSTICK)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SDL_SOURCE_DIR}" sdl EXCLUDE_FROM_ALL)

add_executable(TextureStudio
    studio.cpp selftest.cpp gx.cpp pack.cpp projectbin.cpp names.cpp images.cpp categories.cpp catalog.cpp config.cpp modfolder.cpp transfer.cpp capture.cpp upscale.cpp
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(TextureStudio PRIVATE "${IMGUI_SOURCE_DIR}" "${UI_INCLUDE_DIR}" "${XXHASH_SOURCE_DIR}")
target_compile_features(TextureStudio PRIVATE cxx_std_20)
target_compile_definitions(TextureStudio PRIVATE UNICODE _UNICODE)
target_link_libraries(TextureStudio PRIVATE SDL3::SDL3-static)
if(WIN32)
    set_target_properties(TextureStudio PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(TextureStudio PRIVATE -static)
endif()
