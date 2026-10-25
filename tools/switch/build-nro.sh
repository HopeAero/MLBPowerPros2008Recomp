#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds MLB Power Pros 2008 as a Switch NRO from your own copy, in the build image:
#
#   tools/switch/build-nro.sh [--graphics null|vulkan] [--fresh]
#
# Needs the disc already extracted to local/disc/DATA (tools/linux/build.sh --disc, or the Windows
# Setup / tools/phase0.ps1, does that). Prepares the pinned Wiicompiled tree with the same patch list
# as tools/linux/build.sh, translates the game, and compiles it with devkitA64 for Horizon. Trees live
# in the image's /cache volume; the result is local/switch/build/MLBPowerPros2008.nro.
# --graphics null builds the runtime without a GPU (Dawn's Null backend), for the headless console test.
set -euo pipefail

if [[ ! -d /opt/devkitpro ]]; then   # on the host: run this same script in the image
    exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/docker-build.sh" bash tools/switch/build-nro.sh "$@"
fi

graphics=vulkan fresh=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --graphics) graphics=$2; shift 2 ;;
        --fresh) fresh=1; shift ;;
        *) echo "usage: build-nro.sh [--graphics null|vulkan] [--fresh]" >&2; exit 2 ;;
    esac
done
[[ $graphics == null || $graphics == vulkan ]] || { echo "error: --graphics must be null or vulkan" >&2; exit 2; }

root=/work
cd "$root"
step() { printf '\n==> %s\n' "$*"; }
fail() { echo "error: $*" >&2; exit 1; }
work=/cache/mlb
src=$work/src gen=$work/generated build=$work/build-$graphics vendor=$work/vendor
out=$root/local/switch/build
logs=$root/local/switch/logs
mkdir -p "$work" "$out" "$logs"
export DOTNET_ROLL_FORWARD=Major DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

lock_get() { python3 -I -c "import json,sys; d=json.load(open(sys.argv[1])); print(d$1)" config/tools.lock.json; }
pin=$(lock_get "['wiicompiled']['commit']")
upstream=$(lock_get "['wiicompiled']['repository']")

# ---- 1. Disc: the pinned main.dol and ovl_main.rel ------------------------------------------------
yml_sha() { awk -v key="  $1:" '$0 == key { f = 1 } f && /sha256:/ { print $2; exit }' config/wiicompiled/rl8e54.yml; }
data=local/disc/DATA
for file in sys/main.dol:dol files/ovl_main.rel:rel; do
    path=$data/${file%%:*}
    [[ -f $path ]] || fail "$path missing: extract your copy first (tools/linux/build.sh --disc <image>)"
    [[ $(sha256sum "$path" | cut -d' ' -f1) == $(yml_sha "${file##*:}") ]] ||
        fail "$path does not match the pinned RL8E54 version"
done

# ---- 2. Patched, rebound Wiicompiled (the patch list of tools/linux/build.sh) ---------------------
(( fresh )) && rm -rf "$src" "$build"
if [[ ! -d $vendor/.git ]]; then
    step "Cloning Wiicompiled"
    git -c http.version=HTTP/1.1 clone -q "$upstream" "$vendor"
fi
git -C "$vendor" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$vendor" fetch -q origin
git -C "$vendor" -c advice.detachedHead=false checkout -q "$pin"
if [[ ! -f $src/runtime/include/game_natives_rebound.h ]]; then
    step "Preparing the Wiicompiled source tree (pinned commit + patches)"
    rm -rf "$src"
    git clone -q "$vendor" "$src"
    git -C "$src" -c advice.detachedHead=false checkout -q "$pin"
    mapfile -t patches < <(python3 -I - tools/wiicompiled-translate.ps1 <<'EOF'
import re, sys
text = open(sys.argv[1], encoding='utf-8-sig').read()
block = re.search(r"foreach \(\$patch in (.*?)\)\s*\{", text, re.S).group(1)
print('\n'.join(re.findall(r"'([\w-]+)'", block)))
EOF
)
    (( ${#patches[@]} > 0 )) || fail "could not read the patch list from tools/wiicompiled-translate.ps1"
    patches+=(wiicompiled-linux)
    for patch in "${patches[@]}"; do
        git -C "$src" apply "$root/tools/patches/$patch.patch" || fail "patch $patch does not apply"
    done
    echo "applied ${#patches[@]} patches"
    mkdir -p "$gen"
    ln -sfn "$gen" "$src/generated"
    python3 tools/wiicompiled_rebind.py "$src/runtime" "$vendor/projects/mkwii/MAP.txt" \
        config/wiicompiled/rl8e54-functions.map config/wiicompiled/rl8e54-data.csv \
        os audio storage vi gx input ios
fi

# ---- 3. Translate ---------------------------------------------------------------------------------
translator=$src/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
[[ -f $translator ]] || { step "Building the translator"; dotnet build "$src/translator/src/Translator.Cli/Translator.Cli.csproj" -c Release -v quiet -nologo; }
# The project file pins its tree and output under local/wiicompiled-mlb/ (the desktop builds'); this
# build's copy points them at /cache/mlb so translation stays off the slow mount and off that tree.
yml=$work/rl8e54.yml
sed -e 's|^workspace_root: .*|workspace_root: /work|' -e "s|local/wiicompiled-mlb/|$work/|g"     config/wiicompiled/rl8e54.yml > "$yml"
project=(--project "$yml")
step "Translating the game code (PowerPC to C++)"
dotnet "$translator" translate-recursive 0x8000403C "${project[@]}" --outdir "$gen/functions" \
    --output-metadata "$gen/translation-metadata.json" --threads "$(nproc)" --prune-stale | tail -n 2
dotnet "$translator" generate-data-init "${project[@]}" | tail -n 1
dotnet "$translator" emit-build-shards "${project[@]}" --base-metadata "$gen/translation-metadata.json" \
    --base-functions-dir "$gen/functions" --native-source-dir "$src/runtime/src" --out "$gen/build_shards" | tail -n 1

# ---- 4. Compile for Horizon -----------------------------------------------------------------------
mem_gib=$(( $(awk '/^MemTotal:/{print $2}' /proc/meminfo) / 1024 / 1024 ))
jobs=$(( mem_gib / 2 < $(nproc) ? mem_gib / 2 : $(nproc) ))
(( jobs < 1 )) && jobs=1
step "Configuring for Horizon (graphics: $graphics)"
cmake -S "$src/runtime" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
    -DWIICOMPILED_GAME=RL8E54 -DMKW_TRANSLATED_COMPILE_JOBS="$jobs" \
    -DMKW_SWITCH_GRAPHICS="$graphics" -DMKW_SWITCH_DAWN_DIR=/cache/dawn \
    > "$logs/configure-$graphics.log" 2>&1 || { tail -n 30 "$logs/configure-$graphics.log"; fail "configure failed (local/switch/logs/configure-$graphics.log)"; }
step "Compiling (this takes the longest)"
cmake --build "$build" --target WiiCompiled --parallel "$jobs" > "$logs/build-$graphics.log" 2>&1 || {
    grep -m 25 'error' "$logs/build-$graphics.log" || tail -n 30 "$logs/build-$graphics.log"
    fail "build failed (local/switch/logs/build-$graphics.log)"; }
cp -f "$build/MLBPowerPros2008.nro" "$out/MLBPowerPros2008.nro"
echo "built: local/switch/build/MLBPowerPros2008.nro ($(du -h "$out/MLBPowerPros2008.nro" | cut -f1))"
