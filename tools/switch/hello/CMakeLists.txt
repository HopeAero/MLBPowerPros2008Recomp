# SPDX-License-Identifier: GPL-3.0-or-later
# Test NRO 1: configure with devkitPro's toolchain (tools/switch/docker-build.sh runs it in the image):
#   cmake -S tools/switch/hello -B local/switch/hello -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake
cmake_minimum_required(VERSION 3.13)
project(mlb_switch_hello CXX)

add_executable(hello main.cpp)
nx_generate_nacp(hello.nacp NAME "MLB Switch test 1" AUTHOR "HopeAero" VERSION "0.1.0")
# libnx's default icon: an NRO without an icon and NACP does not launch from the homebrew menu.
nx_create_nro(hello NACP hello.nacp ICON "$ENV{DEVKITPRO}/libnx/default_icon.jpg")
