#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds MLB Power Pros 2008 as a Switch NRO from your own copy:
#
#   tools/switch/build-nro.sh [--graphics null|vulkan] [--fresh]
#
# Needs the disc already extracted to local/disc/DATA (tools/linux/build.sh --disc, or the Windows
# Setup / tools/phase0.ps1, does that). Prepares the pinned Wiicompiled tree with the same patch list
# as tools/linux/build.sh, translates the game, and compiles it with devkitA64 for Horizon. Runs with
# devkitPro installed natively in an MSYS2 (tools/switch/windows/setup-devkitpro.sh), else in the build
# image. Trees live in $MLB_SWITCH_WORK/mlb (default local/switch/work/mlb; the image sets its /cache
# volume); the result is local/switch/build/MLBPowerPros2008.nro.
# --graphics null builds the runtime without a GPU (Dawn's Null backend), for the headless console test.
#
# Profile-guided optimization (support files in tools/switch/pgo/: pgo-project-include.cmake, mlb_pgo_gen.cpp,
# pgo_core.cpp/.h, pgo_recv.py, pgo_check.py):
#   MLB_SWITCH_PGO=gen            instrumented NRO (build-<g>-pgogen, local/switch/build/MLBPowerPros2008-pgogen.nro):
#                                 play it, its profile lands in sdmc:/switch/mlbpowerpros2008/pgo
#   MLB_SWITCH_PGO=use:<dir>      optimized NRO (build-<g>-pgo, ...-pgo.nro) built with the .gcda files in <dir>
#   MLB_SWITCH_PGO_UPDATE=single  gen only: plain (not atomic) counters outside the translated shards
#                                 (faster instrumented game; worker threads may lose a few counts)
# Both need the same translated tree (the profile belongs to the exact code it was recorded on).
set -euo pipefail

if [[ ! -d /opt/devkitpro ]]; then   # on the host: run this same script where devkitPro is
    here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    msys=${MSYS2_ROOT:-/d/msys64}
    [[ -d $msys/opt/devkitpro ]] && MSYSTEM=MSYS exec "$msys/usr/bin/bash.exe" -l "$here/build-nro.sh" "$@"
    exec "$here/docker-build.sh" bash tools/switch/build-nro.sh "$@"
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

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
step() { printf '\n==> %s\n' "$*"; }
fail() { echo "error: $*" >&2; exit 1; }
work=${MLB_SWITCH_WORK:-$root/local/switch/work}
dawn=$work/dawn work=$work/mlb
src=$work/src build=$work/build-$graphics vendor=$work/vendor
gen=$src/generated
out=$root/local/switch/build
logs=$root/local/switch/logs
mkdir -p "$work" "$out" "$logs"
export DOTNET_ROLL_FORWARD=Major DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
# MSYS2's login shell leaves out the Windows PATH, where .NET is; files .NET reads need Windows paths.
command -v dotnet >/dev/null || PATH=$PATH:"/c/Program Files/dotnet"
winpath() { cygpath -m "$1" 2>/dev/null || echo "$1"; }

# ---- PGO mode (MLB_SWITCH_PGO): its own build dir, NRO name and compile/link options ---------------
pgo_dir=$root/tools/switch/pgo
pgo_mode= pgo_args=() pgo_nro_suffix= pgo_link=
case ${MLB_SWITCH_PGO:-} in
    "") ;;
    gen|use:*)
        pgo_mode=${MLB_SWITCH_PGO%%:*}
        [[ -f $pgo_dir/pgo-project-include.cmake ]] || fail "MLB_SWITCH_PGO: $pgo_dir is missing (copy local/tmp/cpu/PGO/{pgo-project-include.cmake,mlb_pgo_gen.cpp,pgo_core.cpp,pgo_core.h,pgo_recv.py,pgo_check.py} there)"
        [[ $pgo_mode == gen ]] && pgo_nro_suffix=-pgogen || pgo_nro_suffix=-pgo
        build=$work/build-$graphics$pgo_nro_suffix
        # GCC (a native Windows program under MSYS2) names a profile after its working directory in
        # Windows form (D:\...\build-vulkan-pgogen); -fprofile-prefix-path spelled exactly so strips it,
        # which makes the .gcda names relative to the build dir and equal in the gen and the use build.
        native_build=$build msys_rules=
        if [[ $(uname -o) == Msys ]]; then
            native_build=$(cygpath -w "$build")
            msys_rules=$root/tools/switch/windows/msys-rules.cmake
        fi
        # The options go through add_compile_options (pgo-project-include.cmake: CMake quotes them for the
        # shell); -DCMAKE_CXX_FLAGS would REPLACE the toolchain's -march/-mtp=soft/-D__SWITCH__ flags.
        if [[ $pgo_mode == gen ]]; then
            # -fno-profile-values: libgcov's indirect-call/topn profilers use TLS (tpidr_el0), which libnx
            # never sets up (they would crash); the arc counters (block layout, inlining, branch
            # probabilities) need none. "pgo" is a dummy directory: the runtime points every profile file
            # at sdmc:/switch/mlbpowerpros2008/pgo (pgo_core.cpp), the baked '\' paths cannot be opened.
            pgo_opts="-fprofile-generate=pgo;-fprofile-prefix-path=$native_build;-fprofile-update=${MLB_SWITCH_PGO_UPDATE:-atomic};-fno-profile-values"
            pgo_link=-fprofile-generate   # links libgcov
            # The translated shards only run on the main thread: plain counters there (an atomic update
            # is a ldaxr/stlxr loop on the Cortex-A57, 3-5x a plain add). Default as in
            # runtime/cmake/PublicProducts.cmake: keep in step with it.
            shard=${MLB_SWITCH_SHARD_FLAGS-"-O3;-fwrapv;-fomit-frame-pointer"}
            pgo_args+=(-DMLB_SWITCH_SHARD_FLAGS="${shard:+$shard;}-fprofile-update=single")
        else
            pgo_use=${MLB_SWITCH_PGO#use:}
            [[ -d $pgo_use ]] || fail "MLB_SWITCH_PGO=use:<dir>: $pgo_use is not a directory"
            compgen -G "$pgo_use/*.gcda" > /dev/null || fail "no .gcda files in $pgo_use"
            # partial training: code the session never ran stays normally optimized (instead of for size);
            # correction: counters of a multi-threaded run can be slightly inconsistent; the warnings stay
            # off (the shards compile with -w anyway): tools/switch/pgo/pgo_check.py reports what is used.
            pgo_opts="-fprofile-use=$(winpath "$pgo_use");-fprofile-prefix-path=$native_build;-fprofile-partial-training;-fprofile-correction;-Wno-missing-profile;-Wno-error=coverage-mismatch"
            [[ -z ${MLB_SWITCH_HOT_ORDER:-} ]] || echo "warning: MLB_SWITCH_HOT_ORDER names .text.<fn> sections; PGO puts hot code in .text.hot.<fn> (it reorders by itself): unset it for the PGO build" >&2
        fi
        pgo_args+=(-DMLB_PGO_COMPILE_OPTIONS="$pgo_opts" -DMLB_PGO_LINK_OPTIONS="$pgo_link"
                   -DCMAKE_PROJECT_INCLUDE="$pgo_dir/pgo-project-include.cmake" -DMLB_PGO_MSYS_RULES="$msys_rules"
                   -DMLB_PGO_DIR="$pgo_dir" -DMLB_PGO_MODE="$pgo_mode" -DMLB_PGO_TARGETS=WiiCompiled)
        ;;
    *) echo "error: MLB_SWITCH_PGO must be gen or use:<dir>" >&2; exit 2 ;;
esac
tag=$graphics$pgo_nro_suffix   # log and result names
# MSYS2 also hands native programs a trimmed environment: NuGet needs the Windows profile folders and
# ProgramFiles(x86) (a name bash cannot export), and an MSBuild node left running would keep that
# environment for later builds, even ones started from Windows.
dotnet() {
    if [[ $(uname -o) != Msys ]]; then command dotnet "$@"; return; fi
    env USERPROFILE="$(cygpath -w -F 40)" APPDATA="$(cygpath -w -F 26)" LOCALAPPDATA="$(cygpath -w -F 28)" \
        ProgramData="$(cygpath -w -F 35)" ProgramFiles="$(cygpath -w -F 38)" "ProgramFiles(x86)=$(cygpath -w -F 42)" \
        MSBUILDDISABLENODEREUSE=1 dotnet "$@"
}
# Fetched archives (zstd's) hold symlinks to files extracted later: MSYS2's default copy needs the target.
export MSYS=winsymlinks:sys

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
    # the Switch platform comes last (two files: the repository's 512 KiB per-file limit)
    patches+=(wiicompiled-linux wiicompiled-switch wiicompiled-switch-2 wiicompiled-switch-3)
    for patch in "${patches[@]}"; do
        # Some Windows working copies of the patches are CRLF; git apply needs the LF they are committed as.
        tr -d '\r' < "$root/tools/patches/$patch.patch" | git -C "$src" apply || fail "patch $patch does not apply"
    done
    echo "applied ${#patches[@]} patches"
    mkdir -p "$gen"
    python3 tools/wiicompiled_rebind.py "$src/runtime" "$vendor/projects/mkwii/MAP.txt" \
        config/wiicompiled/rl8e54-functions.map config/wiicompiled/rl8e54-data.csv \
        os audio storage vi gx input ios
fi

# ---- 3. Translate ---------------------------------------------------------------------------------
translator=$src/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
step "Building the translator"   # incremental: picks up patches to the translator
dotnet build "$src/translator/src/Translator.Cli/Translator.Cli.csproj" -c Release -v quiet -nologo | tail -n 3
# The project file pins its tree and output under local/wiicompiled-mlb/ (the desktop builds'); this
# build's copy points them at its own tree, off the desktop one (and, in the image, off the slow mount).
yml=$work/rl8e54.yml
sed -e "s|^workspace_root: .*|workspace_root: $(winpath "$root")|" \
    -e "s|local/wiicompiled-mlb/generated|$(winpath "$gen")|g" -e "s|local/wiicompiled-mlb/|$(winpath "$work")/|g" \
    config/wiicompiled/rl8e54.yml > "$yml"
project=(--project "$yml")
step "Translating the game code (PowerPC to C++)"
dotnet "$translator" translate-recursive 0x8000403C "${project[@]}" --outdir "$gen/functions" \
    --output-metadata "$gen/translation-metadata.json" --threads "$(nproc)" --prune-stale | tail -n 2
dotnet "$translator" generate-data-init "${project[@]}" | tail -n 1
dotnet "$translator" emit-build-shards "${project[@]}" --base-metadata "$gen/translation-metadata.json" \
    --base-functions-dir "$gen/functions" --native-source-dir "$src/runtime/src" --out "$gen/build_shards" | tail -n 1
# .NET wrote the shard list with Windows paths, which MSYS2's CMake takes for relative ones.
[[ $(winpath "$root") == "$root" ]] || sed -i "s|$(winpath "$root")|$root|g" "$gen/build_shards/shards.cmake"

# ---- 4. Compile for Horizon -----------------------------------------------------------------------
# GCC on a translated shard peaks near 1.6 GB (more than clang): one job per 2.5 GB free now, or the
# host (Docker Desktop's VM, in the image) runs out of memory and drops the build.
# MLB_SWITCH_JOBS overrides it (when the host is short of memory or swap).
jobs=${MLB_SWITCH_JOBS:-$(awk -v n="$(nproc)" '/^MemFree:/ { j = int($2 / 2621440); print (j < 1 ? 1 : (j > n ? n : j)) }' /proc/meminfo)}
# The deko3d renderer's toolchain (patched deko3d, uamlib): no-ops once built.
step "Building deko3d and uam"
for part in deko3d uam; do
    bash "$root/tools/switch/deko/build-$part.sh" > "$logs/build-$part.log" 2>&1 ||
        { tail -n 20 "$logs/build-$part.log"; fail "$part build failed (local/switch/logs/build-$part.log)"; }
done
step "Configuring for Horizon (graphics: $graphics)"
# Precompiled headers stay off: under MSYS2, devkitA64 (a native Windows compiler) cannot open the
# absolute MSYS paths the generated PCH header #includes (only command lines get converted), so unity
# sources #include theirs relatively; archives and links take their arguments from a response file
# (msys-rules.cmake), past Windows' 32K command line.
msys=()
[[ $(uname -o) == Msys ]] && msys=(-DCMAKE_PROJECT_INCLUDE="$root/tools/switch/windows/msys-rules.cmake")
# Optional A/B knobs (phase 31): MLB_SWITCH_SHARD_FLAGS (the translated shards' extra flags; "" = the
# plain -O2 build) and MLB_SWITCH_HOT_ORDER (a linker section-ordering file; unset = link order).
[[ -v MLB_SWITCH_SHARD_FLAGS ]] && msys+=(-DMLB_SWITCH_SHARD_FLAGS="$MLB_SWITCH_SHARD_FLAGS")
msys+=(-DMLB_SWITCH_HOT_ORDER="${MLB_SWITCH_HOT_ORDER:-}")
# ccache when installed (MSYS2: pacman -S ccache): a header change and its revert, or a rebuild of an
# earlier state, comes back from the cache instead of recompiling the translated shards.
# MLB_SWITCH_CCACHE=0 leaves it out of a new build tree (one configured with it keeps it until
# --fresh: passing an empty launcher would change every compile rule and rebuild everything).
# Never for the PGO builds: the result depends on the .gcda files, which ccache's key may not cover.
if [[ ${MLB_SWITCH_CCACHE:-1} != 0 && -z $pgo_mode ]] && command -v ccache > /dev/null; then
    export CCACHE_DIR=${CCACHE_DIR:-$root/local/switch/ccache}
    ccache --max-size=25G > /dev/null
    msys+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake -S "$src/runtime" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${msys[@]}" "${pgo_args[@]}" \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DCMAKE_UNITY_BUILD_RELOCATABLE=ON \
    -DWIICOMPILED_GAME=RL8E54 -DMKW_TRANSLATED_COMPILE_JOBS="$jobs" \
    -DMKW_SWITCH_GRAPHICS="$graphics" -DMKW_SWITCH_DAWN_DIR="$dawn" \
    -DMLB_SWITCH_DEKO_DIR="$root/tools/switch/deko" \
    -DAURORA_DAWN_PROVIDER=system -DDawn_DIR="$root/tools/switch/dawn" \
    -DAURORA_SDL3_PROVIDER=system -DAURORA_SDL3_VERSION=3.4.0 \
    > "$logs/configure-$tag.log" 2>&1 || { tail -n 30 "$logs/configure-$tag.log"; fail "configure failed (local/switch/logs/configure-$tag.log)"; }
# aurora fetches its own Abseil (20240722.0): the same Horizon fixes as Dawn's.
if [[ -d $build/_deps/abseil-cpp-src ]]; then
    python3 tools/switch/dawn/resolve-abseil-horizon.py "$build/_deps/abseil-cpp-src"
fi
step "Compiling (this takes the longest)"
cmake --build "$build" --target WiiCompiled_nro --parallel "$jobs" -- -k 0 > "$logs/build-$tag.log" 2>&1 || {
    grep -m 25 "error" "$logs/build-$tag.log" || tail -n 30 "$logs/build-$tag.log"
    fail "build failed (local/switch/logs/build-$tag.log)"; }
cp -f "$build/MLBPowerPros2008.nro" "$out/MLBPowerPros2008$pgo_nro_suffix.nro"
# The runtime's own files go beside the NRO on the SD card, as beside the desktop executable.
rm -rf "$out/wii_bootstrap"
cp -rf "$build"/{dsp_coef.bin,cacert.pem,initial_pipeline_cache.db,wii_bootstrap} "$out/"
echo "built: local/switch/build/MLBPowerPros2008$pgo_nro_suffix.nro ($(du -h "$out/MLBPowerPros2008$pgo_nro_suffix.nro" | cut -f1))"
[[ $pgo_mode == gen ]] && echo "PGO: run it on the Switch, play ~10 min, press L+R+ZL+ZR+Minus (1 s) with tools/switch/pgo/pgo_recv.py <dir> running on the PC"
[[ $pgo_mode == use ]] && echo "PGO: check what was consumed: python tools/switch/pgo/pgo_check.py local/switch/work/mlb/build-$graphics-pgo ${MLB_SWITCH_PGO#use:} --sample 3"
