#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Linux counterpart of the Windows Setup (tools/setup/LocalBuild.ps1) and of
# tools/wiicompiled-translate.ps1 + tools/wiicompiled-build.ps1: from your own copy of the game to an
# installed native build, everything under local/ except the install folder.
#
#   tools/linux/build.sh --disc '<your copy>.iso|.rvz|.wbfs|.gcz|...' [--install DIR] [options]
#
# 1. checks the disc is a clean RL8E54 dump and extracts it (pinned nodtool; old 16 KiB-block GCZ
#    files are expanded with tools/gcz_to_iso.py first, since nodtool rejects them)
# 2. clones Wiicompiled at the pinned commit, applies tools/patches (the same list and order as
#    wiicompiled-translate.ps1, then wiicompiled-linux.patch) and rebinds it to MLB
# 3. translates main.dol + ovl_main.rel to C++ and compiles it with the system clang
# 4. builds Texture Studio and installs everything into --install (default ~/Games/MLBPowerPros2008)
# With --container, steps 1-3 run in tools/linux/Containerfile (podman or docker): the result runs on
# SteamOS and other distributions with an older glibc than this one, and the host needs no compiler.
# Nothing from the game leaves this PC. Re-running is incremental.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
disc=""
install_dir=""
jobs=$(nproc)
fresh=0
skip_studio=0
skip_roster=0
desktop_entry=0
container=0
in_container=0
arch=x86_64
pages16k=0

usage() {
    sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<'EOF'
Options:
  --disc FILE        your copy of MLB Power Pros 2008 (Wii, USA, RL8E54); not needed once extracted
  --install DIR      where the playable game goes (default: ~/Games/MLBPowerPros2008)
  --jobs N           compile jobs (default: all cores; translated shards are also capped by RAM)
  --fresh            recreate the patched Wiicompiled tree and the native build from scratch
  --no-studio        skip Texture Studio
  --no-roster-editor skip PowerUp+ (the roster editor; needs the .NET 10 SDK, Node.js is downloaded)
  --desktop-entry    add "MLB Power Pros 2008" to the desktop's application menu
  --container        build inside a glibc 2.35 container (podman or docker): a portable build for
                     the Steam Deck / SteamOS, and no compilers or -devel packages needed on the host
  --arch aarch64     experimental: cross-compile for Linux ARM64 against the sysroot from
                     tools/linux/aarch64/make-sysroot.sh (installs into local/install-aarch64; no
                     Texture/Season Studio or PowerUp+)
  --16k-pages        with --arch aarch64: also run on 16 KiB-page systems (Raspberry Pi 5's default
                     kernel); slower, every guest memory access checks the page size
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --disc) disc=$(realpath "$2"); shift 2 ;;
        --install) install_dir=$(realpath -m "$2"); shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --fresh) fresh=1; shift ;;
        --no-studio) skip_studio=1; shift ;;
        --no-roster-editor) skip_roster=1; shift ;;
        --desktop-entry) desktop_entry=1; shift ;;
        --container) container=1; shift ;;
        --arch) arch=$2; shift 2 ;;
        --16k-pages) pages16k=1; shift ;;
        --in-container) in_container=1; shift ;;   # internal: the half of --container that runs inside
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
cd "$root"
[[ $arch == x86_64 || $arch == aarch64 ]] || fail "--arch must be x86_64 or aarch64"
[[ $arch == aarch64 ]] && (( container )) && fail "--arch aarch64 is not supported with --container"
(( pages16k )) && [[ $arch != aarch64 ]] && fail "--16k-pages needs --arch aarch64"
[[ -n $install_dir ]] || install_dir=$HOME/Games/MLBPowerPros2008
[[ $arch == aarch64 && $install_dir == "$HOME/Games/MLBPowerPros2008" ]] && install_dir=$root/local/install-aarch64

work=$root/local/wiicompiled-mlb
data=$root/local/disc/DATA
dol=$data/sys/main.dol
rel=$data/files/ovl_main.rel
# Portable (container) builds keep their own CMake trees: a different compiler and sysroot.
if (( container || in_container )); then
    build=$work/build-portable
    log_tag=-portable
    studio_build=$root/local/build/texture-studio-portable
    roster_build=$root/local/powerupplus/publish-linux-portable
elif [[ $arch == aarch64 ]]; then
    # Cross build: its own tree; the Studios and PowerUp+ stay x86_64-only for now.
    build=$work/build-aarch64
    log_tag=-aarch64
    studio_build=$root/local/build/texture-studio-aarch64
    roster_build=$root/local/powerupplus/publish-linux-aarch64
    skip_studio=1
    skip_roster=1
else
    build=$work/build
    log_tag=
    studio_build=$root/local/build/texture-studio
    roster_build=$root/local/powerupplus/publish-linux
fi

if (( container && ! in_container )); then
    # ---- Steps 1-3 in the container -------------------------------------------------------------
    engine=$(command -v podman || command -v docker || true)
    [[ -n $engine ]] || fail "--container needs podman or docker (SteamOS 3.5+ includes podman)"
    # Tagged by the Containerfile's hash: a changed Containerfile builds a new image.
    image=mlbpp08-linux-build:$(sha256sum tools/linux/Containerfile | cut -c1-12)
    if ! "$engine" image inspect "$image" >/dev/null 2>&1; then
        step "Building the container image (once; about 1.5 GB)"
        "$engine" build -t "$image" -f tools/linux/Containerfile tools/linux
    fi
    run=(run --rm -v "$root:$root" -w "$root" -e HOME="$root/local/cache/container-home"
         -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*')
    if [[ $engine == *podman ]]; then
        run+=(--userns=keep-id --security-opt label=disable)
    else
        run+=(--user "$(id -u):$(id -g)")
    fi
    inner=(--in-container --jobs "$jobs")
    if [[ -n $disc ]]; then
        run+=(-v "$(dirname "$disc"):$(dirname "$disc"):ro")
        inner+=(--disc "$disc")
    fi
    (( fresh )) && inner+=(--fresh)
    (( skip_studio )) && inner+=(--no-studio)
    (( skip_roster )) && inner+=(--no-roster-editor)
    mkdir -p "$root/local/cache/container-home"
    step "Building in the container ($engine)"
    "$engine" "${run[@]}" "$image" bash tools/linux/build.sh "${inner[@]}"
    [[ -f $studio_build/TextureStudio ]] || skip_studio=1
    [[ -x $roster_build/PowerUp.ElectronUI ]] || skip_roster=1
else
# ---- Prerequisites --------------------------------------------------------------------------------
missing=()
for tool in git python3 dotnet cmake ninja clang clang++ pkg-config sha256sum curl; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if (( ${#missing[@]} )); then
    fail "missing tools: ${missing[*]}
  Fedora:        sudo dnf install git python3 dotnet-sdk-8.0 cmake ninja-build clang lld pkgconf-pkg-config
  Debian/Ubuntu: sudo apt install git python3 dotnet-sdk-8.0 cmake ninja-build clang lld pkg-config curl
  Arch/SteamOS:  sudo pacman -S git python dotnet-sdk cmake ninja clang lld pkgconf
  (see docs/en/linux.md; on a Steam Deck build inside a distrobox or on another PC)"
fi
dotnet --list-sdks | grep -qE '^([89]|[1-9][0-9])\.' || fail "the .NET 8 SDK (or newer) is required for the translator"
# The translator targets net8.0; let a newer installed runtime run it.
export DOTNET_ROLL_FORWARD=Major DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

# Locally extracted -devel packages (docs/en/linux.md, "without root"), if any; never in the
# container, which has its own.
sysdeps=$root/local/sysdeps/usr
(( in_container )) && sysdeps=/nonexistent
if [[ $arch == aarch64 ]]; then
    # pkg-config (the audio check below and SDL's configure) looks only in the arm64 sysroot.
    sysdeps=/nonexistent
    sysroot=$root/local/sysroot/noble-arm64
    [[ -d $sysroot/usr/include/aarch64-linux-gnu ]] || fail "no arm64 sysroot: run tools/linux/aarch64/make-sysroot.sh"
    export PKG_CONFIG_SYSROOT_DIR=$sysroot PKG_CONFIG_PATH=
    export PKG_CONFIG_LIBDIR=$sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig
fi
if [[ -d $sysdeps/lib64/pkgconfig ]]; then
    export PKG_CONFIG_PATH="$sysdeps/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi
# SDL builds without sound when no audio backend headers are found: refuse rather than ship a mute game.
if ! pkg-config --exists libpipewire-0.3 && ! pkg-config --exists libpulse && ! pkg-config --exists alsa; then
    fail "no audio development files (PipeWire, PulseAudio or ALSA); the game would have no sound.
  Fedora:        sudo dnf install pipewire-devel pulseaudio-libs-devel alsa-lib-devel libXScrnSaver-devel libdecor-devel
  Debian/Ubuntu: sudo apt install libpipewire-0.3-dev libpulse-dev libasound2-dev libxss-dev libdecor-0-dev libwayland-dev libxkbcommon-dev
  Arch/SteamOS:  sudo pacman -S pipewire libpulse alsa-lib libxss libdecor"
fi

lock=config/tools.lock.json
lock_get() { python3 -I -c "import json,sys; d=json.load(open(sys.argv[1])); print(d$1)" "$lock"; }
pin=$(lock_get "['wiicompiled']['commit']")
upstream=$(lock_get "['wiicompiled']['repository']")
yml_sha() { awk -v key="  $1:" '$0 == key { f = 1 } f && /sha256:/ { print $2; exit }' config/wiicompiled/rl8e54.yml; }
dol_pin=$(yml_sha dol)
rel_pin=$(yml_sha rel)

# ---- 1. Disc ----------------------------------------------------------------------------------------
inputs_ok() {
    [[ -f $dol && -f $rel ]] &&
    [[ $(sha256sum "$dol" | cut -d' ' -f1) == "$dol_pin" ]] &&
    [[ $(sha256sum "$rel" | cut -d' ' -f1) == "$rel_pin" ]]
}
if ! inputs_ok; then
    [[ -n $disc ]] || fail "no extracted game in local/disc/DATA: pass --disc with your copy of the game"
    [[ -f $disc ]] || fail "disc image not found: $disc"
    nodtool=$root/local/tools/nodtool/nodtool-linux-x86_64
    nodtool_sha=f853b83268b9542faf0d28815bc3a23c090ae28ebf397e46f99cbb08122c8307  # encounter/nod v2.0.0-alpha.10
    if [[ ! -x $nodtool || $(sha256sum "$nodtool" | cut -d' ' -f1) != "$nodtool_sha" ]]; then
        step "Downloading nodtool (encounter/nod v2.0.0-alpha.10)"
        mkdir -p "$(dirname "$nodtool")"
        curl -fL --retry 3 -o "$nodtool.download" \
            https://github.com/encounter/nod/releases/download/v2.0.0-alpha.10/nodtool-linux-x86_64
        [[ $(sha256sum "$nodtool.download" | cut -d' ' -f1) == "$nodtool_sha" ]] || fail "nodtool hash mismatch"
        chmod +x "$nodtool.download" && mv "$nodtool.download" "$nodtool"
    fi
    step "Checking the disc image"
    image=$disc
    if ! info=$("$nodtool" --no-color info "$image" 2>&1); then
        if [[ ${image,,} == *.gcz ]] && grep -q 'not a multiple of sector size' <<<"$info"; then
            image=$root/local/images/$(basename "${disc%.*}").iso
            if [[ ! -f $image ]]; then
                echo "nodtool cannot read this GCZ's block size; expanding it to $image"
                mkdir -p "$root/local/images"
                python3 -I tools/gcz_to_iso.py "$disc" "$image"
            fi
            info=$("$nodtool" --no-color info "$image" 2>&1) || fail "cannot read the disc image: $info"
        else
            fail "cannot read the disc image: $info"
        fi
    fi
    grep -q 'Game ID: RL8E54' <<<"$info" ||
        fail "this disc is not MLB Power Pros 2008 for Wii (USA, RL8E54): $(grep -o 'Game ID: \w*' <<<"$info" || echo 'unknown ID')"
    step "Extracting the disc files"
    rm -rf "$data"
    mkdir -p "$(dirname "$data")"
    "$nodtool" --no-color extract "$image" "$data" > "$root/local/disc/extract.log" 2>&1 ||
        fail "extraction failed (see local/disc/extract.log)"
    inputs_ok || fail "the disc does not match the expected version (main.dol or ovl_main.rel differ); a clean, unmodified RL8E54 dump is needed"
fi
echo "main.dol and ovl_main.rel match the pinned hashes"

# ---- 2. Patched, rebound Wiicompiled ---------------------------------------------------------------
vendor=$root/local/vendor/Wiicompiled
src=$work/src
if [[ ! -d $vendor/.git ]]; then
    step "Cloning Wiicompiled"
    git clone -q "$upstream" "$vendor"
fi
git -C "$vendor" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$vendor" fetch -q origin
git -C "$vendor" checkout -q "$pin"   # its MAP.txt feeds the rebind

if (( fresh )); then rm -rf "$src" "$build"; fi
if [[ ! -f $src/runtime/include/game_natives_rebound.h ]]; then
    step "Preparing the Wiicompiled source tree (pinned commit + patches)"
    rm -rf "$src"
    git clone -q "$vendor" "$src"
    git -C "$src" checkout -q "$pin"
    # One list for both platforms: the Windows script's, in its order, then the Linux patch.
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
    mkdir -p "$work/generated"
    ln -sfn ../generated "$src/generated"   # the runtime CMake reads translator output from <src>/generated
    python3 tools/wiicompiled_rebind.py "$src/runtime" "$vendor/projects/mkwii/MAP.txt" \
        config/wiicompiled/rl8e54-functions.map config/wiicompiled/rl8e54-data.csv \
        os audio storage vi gx input ios
fi

# ---- 3. Translate and compile ---------------------------------------------------------------------
translator=$src/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
if [[ ! -f $translator ]]; then
    step "Building the translator"
    dotnet build "$src/translator/src/Translator.Cli/Translator.Cli.csproj" -c Release -v quiet -nologo
fi
gen=$work/generated
project=(--project config/wiicompiled/rl8e54.yml)
step "Translating the game code (PowerPC to C++)"
threads=$(( jobs < 16 ? jobs : 16 ))
dotnet "$translator" translate-recursive 0x8000403C "${project[@]}" --outdir "$gen/functions" \
    --output-metadata "$gen/translation-metadata.json" --threads "$threads" --prune-stale | tail -n 4
dotnet "$translator" generate-data-init "${project[@]}" | tail -n 1
dotnet "$translator" emit-build-shards "${project[@]}" --base-metadata "$gen/translation-metadata.json" \
    --base-functions-dir "$gen/functions" --native-source-dir "$src/runtime/src" --out "$gen/build_shards" | tail -n 1

mkdir -p local/analysis
mem_gib=$(( $(awk '/^MemTotal:/{print $2}' /proc/meminfo) / 1024 / 1024 ))
translated_jobs=$(( mem_gib / 2 < jobs ? mem_gib / 2 : jobs ))
(( translated_jobs < 1 )) && translated_jobs=1
configure=(-S "$src/runtime" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
    -DWIICOMPILED_GAME=RL8E54 -DMKW_TRANSLATED_COMPILE_JOBS="$translated_jobs")
if [[ $arch == aarch64 ]]; then
    # aurora vendors (builds from source) every dependency when cross-compiling; Dawn has a pinned
    # prebuilt linux-aarch64 package, like the x86_64 build uses.
    configure+=(-DCMAKE_TOOLCHAIN_FILE="$root/tools/linux/aarch64/toolchain.cmake" -DAURORA_DAWN_PROVIDER=package)
    configure+=(-DMKW_AARCH64_4K_PAGES=$( (( pages16k )) && echo OFF || echo ON ))
else
    configure+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
fi
# SDL options shared by the game and Texture Studio. SDL looks for some headers and sonames outside
# pkg-config (ALSA, XScrnSaver, libusb); screensaver inhibition through XScrnSaver is optional (SDL
# also uses D-Bus), so do not fail without it.
sdl_flags=()
[[ -d $sysdeps/lib64/pkgconfig ]] && sdl_flags+=(-DCMAKE_PREFIX_PATH="$sysdeps")
[[ -f ${sysroot:-}/usr/include/X11/extensions/scrnsaver.h || -f $sysdeps/include/X11/extensions/scrnsaver.h ]] ||
    sdl_flags+=(-DSDL_X11_XSCRNSAVER=OFF)
# Portable builds carry their C++ runtime: the container's libstdc++ is newer than some hosts'.
(( in_container )) && sdl_flags+=(-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc")
configure+=("${sdl_flags[@]}")
step "Configuring the native build"
cmake "${configure[@]}" > local/analysis/linux${log_tag}-configure.log 2>&1 || {
    tail -n 30 local/analysis/linux${log_tag}-configure.log; fail "configure failed (see local/analysis/linux${log_tag}-configure.log)"; }
step "Compiling the game (this takes the longest)"
if ! cmake --build "$build" --target WiiCompiled --parallel "$jobs" > local/analysis/linux${log_tag}-build.log 2>&1; then
    grep -m 20 'error' local/analysis/linux${log_tag}-build.log || tail -n 30 local/analysis/linux${log_tag}-build.log
    fail "build failed (see local/analysis/linux${log_tag}-build.log)"
fi

if (( ! skip_studio )); then
    step "Building Texture Studio"
    if cmake -S tools/texture-studio -B "$studio_build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
            -DSDL_SOURCE_DIR="$build/_deps/sdl-src" -DIMGUI_SOURCE_DIR="$build/_deps/imgui-src" \
            -DXXHASH_SOURCE_DIR="$build/_deps/xxhash-src" -DUI_INCLUDE_DIR="$src/runtime/include" "${sdl_flags[@]}" \
            > local/analysis/linux${log_tag}-texture-studio.log 2>&1 &&
       cmake --build "$studio_build" --parallel "$jobs" >> local/analysis/linux${log_tag}-texture-studio.log 2>&1; then
        :
    else
        echo "warning: Texture Studio failed to build (local/analysis/linux${log_tag}-texture-studio.log); the game is unaffected" >&2
        skip_studio=1
    fi
fi

if (( ! skip_studio )); then   # same SDL and ImGui; its own season headers and toml11 come from the runtime
    step "Building Season Studio"
    if cmake -S tools/season-studio -B "${studio_build/texture-studio/season-studio}" -G Ninja -DCMAKE_BUILD_TYPE=Release             -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++             -DSDL_SOURCE_DIR="$build/_deps/sdl-src" -DIMGUI_SOURCE_DIR="$build/_deps/imgui-src"             -DUI_INCLUDE_DIR="$src/runtime/include" -DTOML_INCLUDE_DIR="$src/runtime/third_party/toml11" "${sdl_flags[@]}"             > local/analysis/linux${log_tag}-season-studio.log 2>&1 &&
       cmake --build "${studio_build/texture-studio/season-studio}" --parallel "$jobs" >> local/analysis/linux${log_tag}-season-studio.log 2>&1; then
        :
    else
        echo "warning: Season Studio failed to build (local/analysis/linux${log_tag}-season-studio.log); the game is unaffected" >&2
    fi
fi

if (( ! skip_roster )); then
    # PowerUp+ (the roster editor), as tools/powerupplus.ps1 -Publish builds it for Windows: the pinned
    # commit with tools/patches/powerupplus-*.patch, its web UI and a self-contained server, and its
    # Data folder without the 2007 saves and rosters (game content).
    if ! dotnet --list-sdks | grep -qE '^(1[0-9]|[2-9][0-9])\.'; then
        echo "warning: PowerUp+ needs the .NET 10 SDK; skipping the roster editor (or use --container)" >&2
        skip_roster=1
    else
        step "Building the roster editor (PowerUp+)"
        roster_log=$root/local/analysis/linux${log_tag}-powerupplus.log
        node_version=v22.23.3
        node_sha=df450af89261115ef9f9e3830c3eeb2cc9213b63c720b1af623cb5dcbe2e02de
        node_dir=$root/local/tools/node-$node_version-linux-x64
        if [[ ! -x $node_dir/bin/node ]]; then
            archive=$root/local/tools/node-$node_version-linux-x64.tar.xz
            curl -fL --retry 3 -o "$archive" "https://nodejs.org/dist/$node_version/node-$node_version-linux-x64.tar.xz"
            [[ $(sha256sum "$archive" | cut -d' ' -f1) == "$node_sha" ]] || fail "Node.js hash mismatch"
            tar -xJf "$archive" -C "$root/local/tools" && rm -f "$archive"
        fi
        pup_vendor=$root/local/vendor/PowerUpPlus
        pup_src=$root/local/powerupplus/src
        pup_pin=eb1a2c238fdc6b27c62b46fbe23a33a460b59066
        [[ -d $pup_vendor/.git ]] || git clone -q https://github.com/ogura-connection/PowerUpPlus.git "$pup_vendor"
        if [[ ! -d $pup_src/.git ]]; then
            git clone -q "$pup_vendor" "$pup_src"
            git -C "$pup_src" -c advice.detachedHead=false checkout -q "$pup_pin"
            for patch in "$root"/tools/patches/powerupplus-*.patch; do
                git -C "$pup_src" apply --whitespace=nowarn "$patch" || fail "patch $(basename "$patch") does not apply"
            done
        fi
        ui=$pup_src/PowerUp/PowerUp.ElectronUI
        if (
            export PATH="$node_dir/bin:$PATH" npm_config_cache="$root/local/cache/npm"
            cd "$ui/electron"
            [[ -d node_modules ]] || npm ci --ignore-scripts --no-audit --no-fund
            npm run build
            # The project lists macOS and Windows runtimes only; add Linux for this publish.
            dotnet publish "$ui/PowerUp.ElectronUI.csproj" -c Release -r linux-x64 --self-contained true \
                -p:RuntimeIdentifiers=linux-x64 -o "$roster_build"
        ) > "$roster_log" 2>&1; then
            rm -rf "$roster_build/Data"
            cp -r "$ui/Data" "$roster_build/Data"
            find "$roster_build/Data" \( -name '*.dat' -o -name '*.db' -o -name '*.pack' \) -delete
        else
            echo "warning: PowerUp+ failed to build ($roster_log); the game is unaffected" >&2
            skip_roster=1
        fi
    fi
fi
fi  # steps 1-3
(( in_container )) && exit 0

# ---- 4. Install -----------------------------------------------------------------------------------
step "Installing into $install_dir"
mkdir -p "$install_dir"
cp -f "$build/WiiCompiled" "$install_dir/MLBPowerPros2008"
for name in dsp_coef.bin initial_pipeline_cache.db cacert.pem; do
    [[ -f $build/$name ]] && cp -f "$build/$name" "$install_dir/"
done
for dir in config wii_bootstrap; do
    [[ -d $build/$dir ]] && cp -rf "$build/$dir" "$install_dir/"
done
mkdir -p "$install_dir/launcher" "$install_dir/mods" "$install_dir/UserData"
cp -f assets/launcher/*.png "$install_dir/launcher/"
(( skip_studio )) || cp -f "$studio_build/TextureStudio" "$install_dir/"
[[ -f ${studio_build/texture-studio/season-studio}/SeasonStudio ]] && cp -f "${studio_build/texture-studio/season-studio}/SeasonStudio" "$install_dir/"
if (( ! skip_roster )); then   # the launcher's TOOLS > EDIT ROSTER starts it; its database lives in UserData
    rm -rf "$install_dir/powerupplus"
    cp -r "$roster_build" "$install_dir/powerupplus"
fi
touch "$install_dir/portable.txt"   # Config.toml, NAND and caches stay in UserData/ next to the game
# The game reads the disc files from Data/ next to it, like the Windows install. A hard-link copy
# costs no space when the install is on the same filesystem as local/.
if [[ ! -f $install_dir/Data/sys/main.dol ]] || ! cmp -s "$dol" "$install_dir/Data/sys/main.dol"; then
    rm -rf "$install_dir/Data"
    cp -al "$data" "$install_dir/Data" 2>/dev/null || cp -a "$data" "$install_dir/Data"
fi
# Relative to UserData/ (where Config.toml is), so the folder can be moved or copied to another
# machine, a Steam Deck for example.
python3 -I - "$install_dir/UserData/Config.toml" ../Data <<'EOF'
import pathlib, re, sys
config, dvd = pathlib.Path(sys.argv[1]), sys.argv[2]
line = f"dvd_root = '{dvd}'"
text = config.read_text(encoding='utf-8') if config.exists() else ''
if re.search(r'(?m)^dvd_root\s*=', text):
    text = re.sub(r'(?m)^dvd_root\s*=.*$', lambda m: line, text)
elif re.search(r'(?m)^\[paths\]\s*$', text):
    text = re.sub(r'(?m)^\[paths\]\s*$', lambda m: '[paths]\n' + line, text, count=1)
else:
    text = f'[paths]\n{line}\n\n' + text
config.write_text(text, encoding='utf-8')
EOF

if (( desktop_entry )); then
    applications=${XDG_DATA_HOME:-$HOME/.local/share}/applications
    mkdir -p "$applications"
    cat > "$applications/mlb-power-pros-2008.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=MLB Power Pros 2008
Comment=Native recompilation of MLB Power Pros 2008 (Wii)
Exec="$install_dir/MLBPowerPros2008"
Path=$install_dir
Icon=applications-games
Terminal=false
Categories=Game;SportsGame;
EOF
    echo "added $applications/mlb-power-pros-2008.desktop"
fi

printf '\n\033[1mDone.\033[0m Play: %s/MLBPowerPros2008\n' "$install_dir"
echo "Steam / Steam Deck: add it as a non-Steam game (docs/en/linux.md)."
