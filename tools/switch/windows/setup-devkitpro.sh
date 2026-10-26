#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs devkitPro's Switch toolchain natively on Windows, inside an MSYS2 (D:\msys64 is the default;
# tools/switch/windows/setup-devkitpro.ps1 extracts it), with devkitPro's own pacman repositories and
# signing key, plus the host tools the game build needs. Run in that MSYS2's bash (-l). Idempotent.
set -euo pipefail

# devkitPro's packaging key (https://devkitpro.org/wiki/devkitPro_pacman).
key=BC26F752D25B92CE272E0F44F7FD5492264BB9D0
if ! pacman-key --list-keys "$key" >/dev/null 2>&1; then
    pacman-key --recv "$key" --keyserver keyserver.ubuntu.com
    pacman-key --lsign "$key"
fi
if ! pacman -Q devkitpro-keyring >/dev/null 2>&1; then
    # pkg.devkitpro.org answers 403 to curl's and wget's user agents; it serves pacman's.
    curl -fsSL -A "pacman/7.0.0 (MSYS_NT-10.0 x86_64) libalpm/15.0.0" -o /tmp/devkitpro-keyring.pkg.tar.zst         https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst
    pacman -U --noconfirm /tmp/devkitpro-keyring.pkg.tar.zst
fi
if ! grep -q '^\[dkp-libs\]' /etc/pacman.conf; then
    cat >> /etc/pacman.conf <<'EOF'

[dkp-libs]
Server = https://pkg.devkitpro.org/packages

[dkp-windows]
Server = https://pkg.devkitpro.org/packages/windows/$arch/
EOF
fi

# MSYS2 updates its own core first (pacman may ask for a second run).
pacman -Syuu --noconfirm
pacman -Syuu --noconfirm
# The Switch toolchain (devkitA64, libnx, switch-tools, Switch CMake), the portlibs the game links,
# deko3d for later, and the host tools: Git, CMake (MSYS's: devkitPro's toolchain files refuse a
# Windows-native CMake), Ninja and Python.
pacman -S --needed --noconfirm switch-dev switch-zlib switch-libpng switch-freetype switch-bzip2 \
    switch-libzstd switch-harfbuzz switch-mesa deko3d dkp-toolchain-vars \
    git make cmake ninja python

# devkitA64 is a native Windows compiler: MSYS turns /paths into D:/paths on its command lines but not
# inside files, so precompiled headers (whose generated header #includes an absolute /path) stay off.
export DEVKITPRO=/opt/devkitpro
portlibs=/opt/devkitpro/portlibs/switch
# nxvk 26.2.2 (nx-mod release v26.2.2-nx-mod-v1): the NVK Vulkan driver Dawn links statically.
if [[ ! -f $portlibs/lib/libnvk.a ]]; then
    curl -fsSL -o /tmp/nxvk.tar.xz         https://github.com/nx-mod/nxvk/releases/download/v26.2.2-nx-mod-v1/nxvk-switch-portlib-26.2.2.tar.xz
    echo "9bbf12732d81891a2f03633b752d8f4f106b268861a8982fef956c1945a8e4e2  /tmp/nxvk.tar.xz" | sha256sum -c -
    tar -xJf /tmp/nxvk.tar.xz -C "$portlibs"
fi
# SDL3 for Switch: devkitPro/SDL branch switch-sdl-3.4, pinned (there is no package). Its Switch sources
# test SDL_VIDEO_DRIVER_SWITCH before SDL_internal.h, which the precompiled header supplies:
# with PCH off they include it themselves (an SDL without it has no Switch video driver).
# sdl-switch-vulkan.patch: its window makes an EGL surface for OpenGL windows only (Dawn's Vulkan
# presents to the same NWindow). The stamp rebuilds SDL when the pin or the patch changes.
sdl=c329016c0e338ab4397ad930f83330c9fe058348
sdl_patch=$(dirname "${BASH_SOURCE[0]}")/sdl-switch-vulkan.patch
stamp="$sdl $(sha256sum "$sdl_patch" | cut -d' ' -f1)"
if [[ ! -f $portlibs/lib/libSDL3.a || $(cat "$portlibs/share/mlb-sdl3.stamp" 2>/dev/null) != "$stamp" ]]; then
    rm -rf /tmp/sdl && mkdir /tmp/sdl
    curl -fsSL "https://github.com/devkitPro/SDL/archive/$sdl.tar.gz" | tar -xz -C /tmp/sdl --strip-components=1
    tr -d '\r' < "$sdl_patch" | git -C /tmp/sdl apply
    sed -i '1i #include "../../SDL_internal.h"' /tmp/sdl/src/video/switch/*.c
    cmake -S /tmp/sdl -B /tmp/sdl/build -G Ninja -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DCMAKE_INSTALL_PREFIX=$portlibs \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build /tmp/sdl/build
    cmake --install /tmp/sdl/build
    mkdir -p "$portlibs/share" && echo "$stamp" > "$portlibs/share/mlb-sdl3.stamp"
    rm -rf /tmp/sdl
fi
# Horizon has no dynamic loader; Dawn's CMake still links -ldl, so an empty libdl.a satisfies it.
[[ -f $portlibs/lib/libdl.a ]] || /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ar rcs $portlibs/lib/libdl.a
echo "devkitPro ready: $(/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc --version | head -n 1)"
echo "libnx $(pacman -Q libnx | cut -d' ' -f2), nxvk $(ls $portlibs/lib/libnvk.a >/dev/null && echo ok), SDL3 $(ls $portlibs/lib/libSDL3.a >/dev/null && echo ok)"
