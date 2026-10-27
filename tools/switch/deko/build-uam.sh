#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds uamlib for Horizon: devkitPro's deko3d shader compiler (uam 1.1.0: Mesa's GLSL frontend +
# nouveau's nv50_ir, GLSL -> DKSH) as a static library the NRO links to compile shaders at run time
# (uam_api.h: uam::init / uam::compile, one thread only). Upstream uam at its pinned commit plus
# uam-switchwaker.patch (SwitchWaker's 9 patches: real mutexes, DKSH in memory, log callback, no
# exit/abort, resident frontend, deterministic and Mesa-like scheduling, freed compile memory, and its
# CMakeLists.txt; see PATCHES.md in the tree), with the bison/flex/mako outputs from the same SwitchWaker
# commit. Output: $MLB_SWITCH_WORK/deko/uam/build/libuamlib.a, headers in .../uam/src (DekoConfig.cmake).
#   MSYSTEM=MSYS /d/msys64/usr/bin/bash.exe -l tools/switch/deko/build-uam.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

UAM=5a5afc2bae8b55409ab36ba45be63fcb73f68993  # devkitPro/uam master, 1.1.0
src=$deko/uam/src build=$deko/uam/build
patched_tree "$src" https://github.com/devkitPro/uam.git "$UAM" "$here/uam-switchwaker.patch"
switchwaker_generated switch/uam/generated "$src/generated"
build_lib "$src" "$build" "$deko/uam/build.log" uamlib
# uam carries Mesa (its GLSL compiler and util code), and so do NVK's and EGL's archives, in other
# versions: ~400 C and ~2200 C++ symbols share names (the GLSL ir_* classes, ralloc, hash tables). The
# NRO links with --allow-multiple-definition, so one copy would serve both and uam crashed on the other
# version's linear allocator. In one relocatable object every symbol uam defines, local or global
# (so its COMDAT group signatures too), is renamed uamx_*, except the uam:: API: uam keeps a private Mesa.
private=$build/libuamlib_private.a
if [[ ! -f $private || $build/libuamlib.a -nt $private ]]; then
    bin=/opt/devkitpro/devkitA64/bin/aarch64-none-elf
    "$bin-ld" -r --whole-archive "$build/libuamlib.a" -o "$build/uamlib_private.o"
    # Locals too: GNU ld also merges COMDAT groups whose signature is local (anonymous-namespace
    # classes such as nv50_ir's Converter passes, which EGL's nouveau carries as well). Not the
    # assembler's mapping symbols ($x, $d).
    "$bin-nm" --defined-only "$build/uamlib_private.o" | awk 'NF == 3 && $3 !~ /^[$.]/ { print $3 }' |
        grep -v '^_ZN3uam\(4initEb\|7compileENS_5StageEPKc\|8shutdownEv\)$' | sort -u |
        awk '{ print $1 " uamx_" $1 }' > "$build/uamlib_private.syms"
    "$bin-objcopy" --redefine-syms="$build/uamlib_private.syms" "$build/uamlib_private.o"
    rm -f "$private" && "$bin-ar" rcs "$private" "$build/uamlib_private.o"
fi
echo "built: $private ($(du -h "$private" | cut -f1))"
