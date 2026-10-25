#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds tools/switch/dawn/dawn-horizon.patch: nx-mod/dawn-nx's Horizon port (Vulkan through NVK,
# VK_NN_vi_surface, static Vulkan entry points, platform gaps, the Switch smoke test) taken as a diff
# against its upstream base and carried onto the Dawn our aurora pins, so desktop and Switch share one
# Dawn. Runs in the build image (tools/switch/docker-build.sh tools/switch/dawn/make-dawn-horizon-patch.sh).
# The checkouts live in the image's /cache volume (/cache/dawn).
set -euo pipefail

work=/cache/dawn
# Dawn of aurora's AURORA_DAWN_VERSION v20260603.191052 (encounter/dawn-build: "Built from google/dawn
# ref 13abc3bc..."), dawn-nx's upstream base and its switch branch head.
OUR_DAWN=13abc3bc8ea2d3c2050f9e77a12d012108ceee24
NX_BASE=aa02747dfae033881f4334ad7e2b04a5e06bc492
NX_HEAD=173c6191adb50ce081d370ab8be252f4af67758d
patch=/work/tools/switch/dawn/dawn-horizon.patch
mkdir -p "$work"

fetch() {  # fetch <dir> <url> <commit>
    [[ -d $1/.git ]] || git init -q "$1"
    git -C "$1" cat-file -e "$3^{commit}" 2>/dev/null && return
    # GitHub's HTTP/2 drops long shallow fetches now and then: HTTP/1.1, three tries.
    for try in 1 2 3; do
        git -C "$1" -c http.version=HTTP/1.1 fetch -q --depth 1 "$2" "$3" && return
        echo "fetch $3 failed (try $try), retrying" >&2
    done
    return 1
}

case ${1:---refresh} in
--refresh)
    fetch "$work/nx" https://github.com/nx-mod/dawn-nx.git "$NX_HEAD"
    fetch "$work/nx" https://github.com/google/dawn.git "$NX_BASE"
    # The port itself; the notes, docs and CI of the fork stay out.
    git -C "$work/nx" diff "$NX_BASE" "$NX_HEAD" -- . ':!docs' ':!NX_NOTES.md' ':!.github' > "$work/nx-port.diff"
    fetch "$work/src" https://github.com/google/dawn.git "$OUR_DAWN"
    git -C "$work/src" checkout -q -f "$OUR_DAWN" && git -C "$work/src" clean -fdq
    # 25 of the port's files apply as they are; resolve-dawn-port.py finishes the three places where
    # its September base differs from our June Dawn.
    git -C "$work/src" apply --reject "$work/nx-port.diff" || true
    python3 /work/tools/switch/dawn/resolve-dawn-port.py "$work/src"
    if find "$work/src" -name '*.rej' | grep -q .; then
        echo "unresolved rejects left in $work/src" >&2
        exit 1
    fi
    "$0" --save
    ;;
--save)
    git -C "$work/src" add -A
    git -C "$work/src" diff --cached "$OUR_DAWN" > "$patch"
    echo "wrote $patch ($(grep -c '^diff --git' "$patch") files)"
    ;;
*) echo "usage: make-dawn-horizon-patch.sh [--refresh|--save]" >&2; exit 2 ;;
esac
