#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds Dawn for Horizon (our pinned Dawn + tools/switch/dawn/dawn-horizon.patch) and dawn-nx's demo NRO,
# with devkitPro installed natively (tools/switch/windows/setup-devkitpro.sh, run in its MSYS2) or in
# the build image (tools/switch/docker-build.sh tools/switch/dawn/build-dawn.sh).
# Trees in $MLB_SWITCH_WORK/dawn (default local/switch/work/dawn; the image sets its /cache volume); the
# demo is copied to local/switch/dawn/dawn-nx-demo.nro.
# Options are dawn-nx's own CI recipe plus Vulkan and Tint's SPIR-V writer, which our older Dawn leaves
# off on an unknown platform (Horizon); dependencies are fetched by Dawn's CMake (our checkout has no
# submodules), and the Null backend stays on for the game's headless console test.
set -euo pipefail

OUR_DAWN=13abc3bc8ea2d3c2050f9e77a12d012108ceee24
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
dawn=${MLB_SWITCH_WORK:-$root/local/switch/work}/dawn
src=$dawn/src build=$dawn/build
patch=$root/tools/switch/dawn/dawn-horizon.patch
[[ -f $patch ]] || { echo "error: $patch missing: run tools/switch/dawn/make-dawn-horizon-patch.sh" >&2; exit 1; }

if [[ ! -d $src/.git ]]; then
    git init -q "$src"
    git -C "$src" -c http.version=HTTP/1.1 fetch -q --depth 1 https://github.com/google/dawn.git "$OUR_DAWN"
fi
# Build the pinned Dawn plus the saved patch; a tree that already has it keeps its timestamps (and its
# objects stay up to date).
if [[ $(git -C "$src" rev-parse HEAD) != "$OUR_DAWN" ]] ||
   ! tr -d '\r' < "$patch" | git -C "$src" apply --reverse --check 2>/dev/null; then
    git -C "$src" -c core.autocrlf=false checkout -q -f "$OUR_DAWN"
    git -C "$src" clean -fdq -e third_party
    tr -d '\r' < "$patch" | git -C "$src" apply
fi

# devkitA64 is a native Windows compiler under MSYS2: precompiled headers stay off (their generated
# header #includes an absolute MSYS path, which only command lines get converted), and archives and links
# take their arguments from a response file (msys-rules.cmake), past Windows' 32K command line.
msys=()
[[ $(uname -o) == Msys ]] && msys=(-DCMAKE_PROJECT_INCLUDE="$root/tools/switch/windows/msys-rules.cmake")
cmake -S "$src" -B "$build" -G Ninja "${msys[@]}" \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DDAWN_FETCH_DEPENDENCIES=ON \
    -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF \
    -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF \
    -DDAWN_ENABLE_NULL=ON -DDAWN_ENABLE_VULKAN=ON -DTINT_BUILD_SPV_WRITER=ON \
    > "$dawn/configure.log" 2>&1 || { tail -40 "$dawn/configure.log"; exit 1; }
# Dependencies are fetched at configure time; Abseil then gets its Horizon fixes.
python3 "$root/tools/switch/dawn/resolve-abseil-horizon.py" "$src/third_party/abseil-cpp"
# One job per 2 GB free: the host keeps working while this builds.
jobs=$(awk -v n="$(nproc)" '/^MemFree:/ { j = int($2 / 2097152); print (j < 1 ? 1 : (j > n ? n : j)) }' /proc/meminfo)
cmake --build "$build" --target dawn_nx_demo_nro -j"$jobs" > "$dawn/build.log" 2>&1 ||
    { grep -m20 -E "error|Error" "$dawn/build.log"; tail -20 "$dawn/build.log"; exit 1; }

mkdir -p "$root/local/switch/dawn"
cp "$build/switch_smoke_test/dawn-nx-demo.nro" "$dawn/build.log" "$root/local/switch/dawn/"
echo "built: local/switch/dawn/dawn-nx-demo.nro ($(du -h "$root/local/switch/dawn/dawn-nx-demo.nro" | cut -f1))"
