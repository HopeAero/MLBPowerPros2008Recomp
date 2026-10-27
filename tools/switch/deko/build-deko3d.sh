#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds deko3d 0.5.0 for Horizon with SwitchWaker's two fixes (deko3d-switchwaker.patch, PATCHES.md in
# the tree) instead of devkitPro's libdeko3d.a 0.5.0-1, the same version unpatched:
#   1. dkCmdBufBindBlendStates wrote the color's destination factor into FuncAlphaDst, so a blend state
#      with its own alpha factors (WebGPU's separate alpha blend, which aurora uses) blended alpha wrong;
#   2. CmdBuf::clear() leaked every control memory chunk but the newest (1 KiB each, per frame that
#      needed more than one; hundreds of MiB over a long session).
# Both are still unfixed upstream. dekotools' outputs (generated/) come from the same SwitchWaker commit
# (devkitPro's MSYS2 has no dekotools). Output: $MLB_SWITCH_WORK/deko/deko3d/build/deko3d/libdeko3d_release.a
# and libdeko3d_debug.a (upstream's libdeko3d / libdeko3dd), headers in .../deko3d/src/include.
#   MSYSTEM=MSYS /d/msys64/usr/bin/bash.exe -l tools/switch/deko/build-deko3d.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

DEKO3D=5cc144dcf5606fbcd78430f6c62923546e5f9cf4  # devkitPro/deko3d tag v0.5.0
src=$deko/deko3d/src build=$deko/deko3d/build
patched_tree "$src" https://github.com/devkitPro/deko3d.git "$DEKO3D" "$here/deko3d-switchwaker.patch"
switchwaker_generated switch/deko3d/generated "$src/generated"
# SwitchWaker's CMakeLists.txt is meant for add_subdirectory(): a top-level project around it.
top=$deko/deko3d/top/CMakeLists.txt
text="cmake_minimum_required(VERSION 3.20)
project(deko3d_switchwaker C CXX)
add_subdirectory(\"$src\" deko3d)"
mkdir -p "${top%/*}"
[[ -f $top && $(< "$top") == "$text" ]] || printf '%s\n' "$text" > "$top"
build_lib "$deko/deko3d/top" "$build" "$deko/deko3d/build.log" deko3d_release deko3d_debug
for v in release debug; do
    echo "built: $build/deko3d/libdeko3d_$v.a ($(du -h "$build/deko3d/libdeko3d_$v.a" | cut -f1))"
done
