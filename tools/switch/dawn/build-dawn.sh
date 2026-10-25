#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds Dawn for Horizon (our pinned Dawn + tools/switch/dawn/dawn-horizon.patch) and dawn-nx's demo NRO,
# in the build image:  tools/switch/docker-build.sh tools/switch/dawn/build-dawn.sh
# Trees in /cache/dawn (the image's volume); the demo is copied to local/switch/dawn/dawn-nx-demo.nro.
# Options are dawn-nx's own CI recipe plus Vulkan and Tint's SPIR-V writer, which our older Dawn leaves
# off on an unknown platform (Horizon); dependencies are fetched by Dawn's CMake (our checkout has no
# submodules), and the Null backend stays on for the game's headless console test.
set -euo pipefail

OUR_DAWN=13abc3bc8ea2d3c2050f9e77a12d012108ceee24
src=/cache/dawn/src build=/cache/dawn/build
patch=/work/tools/switch/dawn/dawn-horizon.patch
[[ -f $patch ]] || { echo "error: $patch missing: run tools/switch/dawn/make-dawn-horizon-patch.sh" >&2; exit 1; }

if [[ ! -d $src/.git ]]; then
    git init -q "$src"
    git -C "$src" -c http.version=HTTP/1.1 fetch -q --depth 1 https://github.com/google/dawn.git "$OUR_DAWN"
fi
# Always build exactly the pinned Dawn plus the saved patch.
git -C "$src" checkout -q -f "$OUR_DAWN"
git -C "$src" clean -fdq -e third_party
git -C "$src" apply "$patch"

cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DDAWN_FETCH_DEPENDENCIES=ON \
    -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF \
    -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF \
    -DDAWN_ENABLE_NULL=ON     -DDAWN_ENABLE_VULKAN=ON -DTINT_BUILD_SPV_WRITER=ON \
    > /cache/dawn/configure.log 2>&1 || { tail -40 /cache/dawn/configure.log; exit 1; }
# Dependencies are fetched at configure time; Abseil then gets its Horizon fixes.
python3 /work/tools/switch/dawn/resolve-abseil-horizon.py "$src/third_party/abseil-cpp"
cmake --build "$build" --target dawn_nx_demo_nro -j"$(nproc)" > /cache/dawn/build.log 2>&1 ||
    { grep -m20 -E "error|Error" /cache/dawn/build.log; tail -20 /cache/dawn/build.log; exit 1; }

mkdir -p /work/local/switch/dawn
cp "$build/switch_smoke_test/dawn-nx-demo.nro" /work/local/switch/dawn/
cp /cache/dawn/build.log /work/local/switch/dawn/build.log
echo "built: local/switch/dawn/dawn-nx-demo.nro ($(du -h /work/local/switch/dawn/dawn-nx-demo.nro | cut -f1))"
