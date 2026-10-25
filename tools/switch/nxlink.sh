#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Sends an NRO to the Switch over Wi-Fi and launches it, then prints what it writes to stdout here
# (and to local/switch/logs/nxlink-<name>-<time>.log) until it exits. On the Switch: open the homebrew
# menu (holding R over a game gives it full memory) and press Y; the screen shows the console's IP.
#
#   tools/switch/nxlink.sh <switch-ip> <file.nro> [--path sdmc:/switch/mlbpowerpros2008/X.nro] [-- args]
#
# Uses devkitPro's nxlink from the build image. The NRO calls back to this PC on port 28771 to send
# its output (our NROs do so when nxlink started them); the container publishes that port, and Windows
# may ask once to allow it through the firewall. A VPN that tunnels local traffic blocks both ways:
# allow local network access in it, or pause it while testing.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
(( $# >= 2 )) || { sed -n '3,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
ip=$1 nro=$2; shift 2
[[ -f $nro ]] || { echo "error: $nro not found" >&2; exit 1; }
upload=() args=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --path) upload=(--path "$2"); shift 2 ;;
        --) shift; args=(--args "$*"); break ;;
        *) echo "error: unknown option $1" >&2; exit 2 ;;
    esac
done
ping -n 1 -w 2000 "$ip" >/dev/null 2>&1 || ping -c 1 -W 2 "$ip" >/dev/null 2>&1 ||
    echo "warning: $ip does not answer ping (VPN or another network?); trying anyway" >&2

rel=$(realpath --relative-to="$root" "$nro")
mkdir -p "$root/local/switch/logs"
log=$root/local/switch/logs/nxlink-$(basename "$nro" .nro)-$(date +%Y%m%d-%H%M%S).log
"$root/tools/switch/docker-build.sh" --publish 28771:28771 \
    /opt/devkitpro/tools/bin/nxlink -a "$ip" -r 10 -s "${upload[@]}" "${args[@]}" "/work/$rel" 2>&1 | tee "$log"
echo "log: ${log#$root/}"
