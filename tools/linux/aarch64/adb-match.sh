#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the aarch64 build's headless match benchmark on a 64-bit ARM Android phone over adb (real ARM
# hardware, no root needed) and stops at the first pitch (`gameplay phase=4`) or after the limit.
#
#   tools/linux/aarch64/adb-match.sh [seconds=300] [cpus=f0]
#
# Packages local/install-aarch64 with the glibc libraries it needs from the sysroot (interpreter and
# rpath patched to /data/local/tmp/mlb), pushes it once, then runs with graphics_api = "null" and
# SDL's offscreen video: the translated game and runtime run in full, nothing is drawn. `cpus` is a
# taskset mask (f0: the big cores on most phones). ADB may point at adb.exe from WSL. Your own game
# files go to your own phone; nothing leaves it. Exit 0 only when the first pitch was reached.
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
fail() { echo "error: $*" >&2; exit 1; }
seconds=${1:-300} cpus=${2:-f0}
adb=${ADB:-adb}
install=${MLB_AARCH64_INSTALL:-$root/local/install-aarch64}
sysroot=${MLB_AARCH64_SYSROOT:-$root/local/sysroot/noble-arm64}
pkg=$root/local/android/mlb remote=/data/local/tmp/mlb
[[ -x $install/MLBPowerPros2008 ]] || fail "MLBPowerPros2008 (aarch64) not found in $install: run tools/linux/build.sh --arch aarch64"
command -v patchelf >/dev/null || fail "patchelf not found (apt install patchelf)"
"$adb" get-state >/dev/null 2>&1 || fail "no phone over adb: enable USB debugging and accept the prompt"
host_path() { [[ $adb == *.exe ]] && wslpath -w "$1" || echo "$1"; }   # Windows adb needs Windows paths

# Package: the executable plus its library closure from the sysroot.
rm -rf "$pkg" && mkdir -p "$pkg/lib"
cp "$install/MLBPowerPros2008" "$pkg/"
todo=($(readelf -d "$install/MLBPowerPros2008" | grep -oP 'NEEDED.*\[\K[^]]+')) seen=" "
while ((${#todo[@]})); do
    lib=${todo[0]} todo=("${todo[@]:1}")
    [[ $seen == *" $lib "* ]] && continue
    seen+="$lib "
    src=$(ls "$sysroot/lib/aarch64-linux-gnu/$lib" "$sysroot/usr/lib/aarch64-linux-gnu/$lib" 2>/dev/null | head -n 1)
    [[ -n $src ]] || fail "$lib not found in $sysroot"
    cp -L "$src" "$pkg/lib/"
    todo+=($(readelf -d "$src" | grep -oP 'NEEDED.*\[\K[^]]+' || true))
done
cp -L "$sysroot/lib/ld-linux-aarch64.so.1" "$pkg/lib/"
patchelf --set-interpreter $remote/lib/ld-linux-aarch64.so.1 --set-rpath '$ORIGIN/lib' "$pkg/MLBPowerPros2008"

"$adb" shell "mkdir -p $remote" || fail "adb shell failed"
"$adb" push "$(host_path "$pkg")/." $remote >/dev/null || fail "adb push failed"
if ! "$adb" shell "test -d $remote/Data"; then   # the game files, once (several GB)
    echo "pushing the game files to the phone..."
    for item in Data portable.txt dsp_coef.bin; do
        [[ -e $install/$item ]] && { "$adb" push "$(host_path "$install/$item")" $remote/ >/dev/null || fail "adb push $item failed"; }
    done
fi

log=$root/local/analysis/adb-match.log
mkdir -p "$(dirname "$log")"
"$adb" shell "cd $remote && chmod 755 MLBPowerPros2008 lib/ld-linux-aarch64.so.1 && \
    LD_LIBRARY_PATH=$remote/lib HOME=$remote TMPDIR=/data/local/tmp SDL_VIDEO_DRIVER=offscreen \
    SDL_AUDIO_DRIVER=dummy SDL_JOYSTICK_DISABLE_UDEV=1 WIICOMPILED_MATCH_BENCH=1 \
    WIICOMPILED_MATCH_SECONDS=0 WIICOMPILED_MATCH_API=null \
    taskset $cpus ./MLBPowerPros2008 > /data/local/tmp/mlb.log 2>&1 & p=\$!; s=0; \
    while [ \$s -lt $seconds ] && kill -0 \$p 2>/dev/null; do \
        grep -q 'gameplay phase=4' /data/local/tmp/mlb.log 2>/dev/null && break; sleep 1; s=\$((s+1)); done; \
    kill -9 \$p 2>/dev/null; echo \"elapsed \${s}s\"; cat /data/local/tmp/mlb.log" | tr -d '\r' > "$log"
grep -m 1 '^elapsed' "$log"
grep '\[match-bench\]' "$log"
if grep -q 'gameplay phase=4' "$log"; then
    echo "first pitch reached; log: $log"
    exit 0
fi
echo "first pitch not reached in ${seconds}s; log: $log" >&2
exit 1
