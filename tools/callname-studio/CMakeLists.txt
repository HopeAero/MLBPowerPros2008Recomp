# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(CallnameStudio LANGUAGES C CXX)
set(SDL_SOURCE_DIR "" CACHE PATH "SDL3 sources")
set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui sources")
set(UI_INCLUDE_DIR "" CACHE PATH "Patched runtime/include")
set(TOML_INCLUDE_DIR "" CACHE PATH "Runtime toml11")
set(TEXTURE_STUDIO_DIR "${CMAKE_CURRENT_LIST_DIR}/../texture-studio")
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
foreach(subsystem CAMERA HAPTIC SENSOR POWER JOYSTICK)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SDL_SOURCE_DIR}" sdl EXCLUDE_FROM_ALL)
get_filename_component(RUNTIME_DIR "${UI_INCLUDE_DIR}/.." ABSOLUTE)
add_executable(CallnameStudio studio.cpp core.cpp tests.cpp
    "${RUNTIME_DIR}/src/callnames/collect.cpp"
    "${RUNTIME_DIR}/src/callnames/audio.cpp"
    "${TEXTURE_STUDIO_DIR}/config.cpp"
    "${IMGUI_SOURCE_DIR}/imgui.cpp" "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp" "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(CallnameStudio PRIVATE "${IMGUI_SOURCE_DIR}" "${UI_INCLUDE_DIR}" "${TOML_INCLUDE_DIR}" "${TEXTURE_STUDIO_DIR}" "${RUNTIME_DIR}/../aurora-main/include")
target_compile_features(CallnameStudio PRIVATE cxx_std_20)
target_compile_definitions(CallnameStudio PRIVATE UNICODE _UNICODE)
target_link_libraries(CallnameStudio PRIVATE SDL3::SDL3-static)
if(WIN32)
    set_target_properties(CallnameStudio PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(CallnameStudio PRIVATE -static)
endif()
enable_testing()
add_test(NAME callname_studio_selftest COMMAND CallnameStudio --selftest)
