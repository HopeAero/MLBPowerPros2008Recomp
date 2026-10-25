# SPDX-License-Identifier: GPL-3.0-or-later
# Smallest check of the aarch64 cross toolchain: links pthread and zlib from the sysroot and runs
# under qemu-aarch64 (CMAKE_CROSSCOMPILING_EMULATOR) through ctest.
cmake_minimum_required(VERSION 3.20)
project(aarch64_smoke CXX)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
add_executable(smoke smoke.cpp)
target_link_libraries(smoke PRIVATE Threads::Threads ZLIB::ZLIB)
enable_testing()
add_test(NAME aarch64_smoke COMMAND smoke)
set_tests_properties(aarch64_smoke PROPERTIES PASS_REGULAR_EXPRESSION "aarch64 ok")
