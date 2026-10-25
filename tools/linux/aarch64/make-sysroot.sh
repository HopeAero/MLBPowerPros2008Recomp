#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Creates the Ubuntu 24.04 arm64 sysroot that tools/linux/aarch64/toolchain.cmake cross-compiles
# against (local/sysroot/noble-arm64 by default): the -dev packages of docs/en/linux.md plus Mesa's
# arm64 drivers, which qemu-aarch64 -L <sysroot> also uses to run the result on this PC.
#
#   tools/linux/aarch64/make-sysroot.sh [DIR]
#
# Needs an Ubuntu/Debian host (WSL works); installs mmdebstrap, qemu-user-static, binfmt-support and
# symlinks with apt when missing. Re-running with the same package list does nothing.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
sysroot=$(realpath -m "${1:-$root/local/sysroot/noble-arm64}")
packages=(
    libc6-dev libstdc++-13-dev libgcc-13-dev
    libpipewire-0.3-dev libpulse-dev libasound2-dev libxss-dev libdecor-0-dev libwayland-dev
    wayland-protocols libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
    libxfixes-dev libxtst-dev libgbm-dev libdrm-dev libudev-dev libdbus-1-dev libusb-1.0-0-dev
    libpng-dev zlib1g-dev libfreetype-dev
    libegl-dev libgles-dev libgl1-mesa-dri mesa-vulkan-drivers libvulkan1
)
stamp=$sysroot/.mlb-sysroot
want=$(printf '%s\n' "${packages[@]}" | sha256sum | cut -c1-16)
if [[ -f $stamp && $(cat "$stamp") == "$want" ]]; then
    echo "sysroot up to date: $sysroot"
    exit 0
fi

sudo=()
(( EUID == 0 )) || sudo=(sudo)
host_tools=()
command -v mmdebstrap >/dev/null || host_tools+=(mmdebstrap)
command -v qemu-aarch64-static >/dev/null || host_tools+=(qemu-user-static)
[[ -d /usr/lib/binfmt.d || -x /usr/sbin/update-binfmts ]] || host_tools+=(binfmt-support)
command -v symlinks >/dev/null || host_tools+=(symlinks)
if (( ${#host_tools[@]} )); then
    echo "installing ${host_tools[*]}"
    "${sudo[@]}" apt-get install -y "${host_tools[@]}"
fi

rm -rf "$sysroot"
mkdir -p "$(dirname "$sysroot")"
# Root mode keeps file ownership simple; the maintainer scripts run through qemu-user-static.
"${sudo[@]}" mmdebstrap --arch=arm64 --variant=minbase --components=main,universe \
    --include="$(IFS=,; echo "${packages[*]}")" \
    noble "$sysroot" http://ports.ubuntu.com/ubuntu-ports
"${sudo[@]}" chown -R "$(id -u):$(id -g)" "$sysroot"
# Absolute links (/usr/lib/...) would point into the host; make them relative to the sysroot.
symlinks -cr "$sysroot" >/dev/null
echo "$want" > "$stamp"
echo "sysroot ready: $sysroot"
