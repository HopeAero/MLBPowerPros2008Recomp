# SPDX-License-Identifier: GPL-3.0-or-later
# Shared by tools/switch/deko/build-*.sh (sourced): pinned fetches, patched checkouts and the CMake recipe
# for static libraries built with devkitA64, natively in devkitPro's MSYS2
# (MSYSTEM=MSYS /d/msys64/usr/bin/bash.exe -l tools/switch/deko/build-uam.sh) or in the build image.
# Trees and builds in $MLB_SWITCH_WORK/deko (default local/switch/work/deko).

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
deko=${MLB_SWITCH_WORK:-$root/local/switch/work}/deko
here=$root/tools/switch/deko
# SwitchWaker (github.com/centollOS/SwitchWaker), whose switch/uam and switch/deko3d our patches come
# from; the bison/flex/mako and dekotools outputs (generated/) are taken from this same commit.
SWITCHWAKER=56c0758a4651155ca491ef9d6d3be0ae35be726f
mkdir -p "$deko"

fetch() {  # fetch <dir> <url> <commit>: a shallow clone of one pinned commit
    # LF as upstream wrote it, also under Git for Windows (whose system autocrlf would make git apply
    # write CRLF files)
    [[ -d $1/.git ]] || { git init -q "$1" && git -C "$1" config core.autocrlf false; }
    git -C "$1" cat-file -e "$3^{commit}" 2>/dev/null && return
    # GitHub's HTTP/2 drops long shallow fetches now and then: HTTP/1.1, three tries.
    for try in 1 2 3; do
        git -C "$1" -c http.version=HTTP/1.1 fetch -q --depth 1 "$2" "$3" && return
        echo "fetch $3 failed (try $try), retrying" >&2
    done
    return 1
}

patched_tree() {  # patched_tree <src> <url> <commit> <patch>: the pinned commit plus the patch
    local src=$1 url=$2 commit=$3 patch=$4
    fetch "$src" "$url" "$commit"
    # A tree that already has the patch keeps its timestamps (its objects stay up to date).
    if [[ $(git -C "$src" rev-parse HEAD 2>/dev/null) != "$commit" ]] ||
       ! tr -d '\r' < "$patch" | git -C "$src" apply --reverse --check 2>/dev/null; then
        git -C "$src" -c core.autocrlf=false checkout -q -f "$commit"
        git -C "$src" clean -fdxq
        tr -d '\r' < "$patch" | git -C "$src" apply
    fi
}

switchwaker_generated() {  # switchwaker_generated <path in SwitchWaker> <dest dir>
    local sw=$deko/switchwaker stamp=$2/.switchwaker-commit
    [[ -f $stamp && $(cat "$stamp") == "$SWITCHWAKER" ]] && return
    fetch "$sw" https://github.com/centollOS/SwitchWaker.git "$SWITCHWAKER"
    rm -rf "$2" && mkdir -p "$2"
    local depth; depth=$(tr -cd / <<< "$1" | wc -c)
    git -C "$sw" -c core.autocrlf=false archive "$SWITCHWAKER" "$1" | tar -x -C "$2" --strip-components=$((depth + 1))
    echo "$SWITCHWAKER" > "$stamp"
}

# One job per 2 GB free, at most 2 unless MLB_SWITCH_JOBS says otherwise (the host ran out of memory).
jobs=${MLB_SWITCH_JOBS:-$(awk '/^MemFree:/ { j = int($2 / 2097152); print (j < 1 ? 1 : (j > 2 ? 2 : j)) }' /proc/meminfo)}

build_lib() {  # build_lib <source dir> <build dir> <log> <cmake target>...: Release, devkitA64
    local src=$1 build=$2 log=$3; shift 3
    # devkitA64 is a native Windows compiler under MSYS2: archives take their arguments from a response
    # file (msys-rules.cmake), past Windows' 32K command line.
    local msys=()
    [[ $(uname -o) == Msys ]] && msys=(-DCMAKE_PROJECT_INCLUDE="$root/tools/switch/windows/msys-rules.cmake")
    cmake -S "$src" -B "$build" -G Ninja "${msys[@]}" \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release \
        > "$log.configure" 2>&1 || { tail -40 "$log.configure"; exit 1; }
    local targets=(); for t in "$@"; do targets+=(--target "$t"); done
    cmake --build "$build" "${targets[@]}" -j"$jobs" > "$log" 2>&1 ||
        { grep -m20 -E "error|Error" "$log"; tail -20 "$log"; exit 1; }
}
