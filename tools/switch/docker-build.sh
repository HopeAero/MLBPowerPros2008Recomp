#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs a command inside the Switch build image with the repository mounted at /work, building the
# image (tools/switch/Dockerfile) first when it is missing or older than the Dockerfile.
#
#   tools/switch/docker-build.sh [--publish HOST:CONTAINER] <command...>
#   e.g. tools/switch/docker-build.sh cmake --version
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
image=mlb-switch-dev:1
publish=()
if [[ ${1:-} == --publish ]]; then publish=(-p "$2"); shift 2; fi   # nxlink's stdio server
command -v docker >/dev/null || { echo "error: docker not found: install Docker Desktop" >&2; exit 1; }
docker version --format '{{.Server.Version}}' >/dev/null 2>&1 ||
    { echo "error: the Docker engine is not running: open Docker Desktop" >&2; exit 1; }

dockerfile=$root/tools/switch/Dockerfile
stamp=$(sha256sum "$dockerfile" | cut -c1-16)
if [[ $(docker image inspect "$image" --format '{{index .Config.Labels "mlb.dockerfile"}}' 2>/dev/null) != "$stamp" ]]; then
    echo "building $image (first time takes several minutes)..." >&2
    docker build --label "mlb.dockerfile=$stamp" -t "$image" -f "$dockerfile" "$root/tools/switch" >&2
fi
# Git Bash on Windows rewrites /work-style paths; MSYS_NO_PATHCONV keeps them for the container.
# Big trees (Dawn's checkout, build directories) live in the mlb-switch-cache volume at /cache: a
# Windows folder mounted into the container is slow for hundreds of thousands of files. Results
# (NROs, patches, logs) are copied to /work/local/switch.
MSYS_NO_PATHCONV=1 docker run --rm "${publish[@]}" -v "$root:/work" -v mlb-switch-cache:/cache -w /work "$image" "$@"
