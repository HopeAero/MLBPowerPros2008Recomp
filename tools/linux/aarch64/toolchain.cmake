# SPDX-License-Identifier: GPL-3.0-or-later
# Cross toolchain for Linux aarch64: the host clang/lld against the sysroot from make-sysroot.sh.
# Tests (ctest) run through qemu-aarch64 with the same sysroot as the dynamic loader's root.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

get_filename_component(_mlb_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MLB_AARCH64_SYSROOT "${_mlb_root}/local/sysroot/noble-arm64" CACHE PATH "Ubuntu 24.04 arm64 sysroot")
if(NOT EXISTS "${MLB_AARCH64_SYSROOT}/usr/include/aarch64-linux-gnu")
    message(FATAL_ERROR "no aarch64 sysroot at ${MLB_AARCH64_SYSROOT}: run tools/linux/aarch64/make-sysroot.sh")
endif()
set(CMAKE_SYSROOT "${MLB_AARCH64_SYSROOT}")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_ASM_COMPILER_TARGET aarch64-linux-gnu)
foreach(_kind EXE SHARED MODULE)
    set(CMAKE_${_kind}_LINKER_FLAGS_INIT "-fuse-ld=lld")
endforeach()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(_mlb_qemu NAMES qemu-aarch64-static qemu-aarch64)
if(_mlb_qemu)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_mlb_qemu};-L;${MLB_AARCH64_SYSROOT}")
endif()

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MLB_AARCH64_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${MLB_AARCH64_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${MLB_AARCH64_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
