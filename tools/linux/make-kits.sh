#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Linux and Steam Deck release kits from a commit: the repository's tracked files (git
# archive: nothing from local/, no game data) plus a one-command installer at the top. Each player's
# machine extracts, translates and compiles their own copy with tools/linux/build.sh.
#   tools/linux/make-kits.sh <version> [commit]     -> local/release/*.tar.gz + SHA256SUMS.txt
set -euo pipefail
version=${1:?usage: make-kits.sh <version> [commit]}
commit=${2:-HEAD}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=$root/local/release
mkdir -p "$out"
python3 "$root/tools/check_no_game_data.py" --all >/dev/null || { echo "check_no_game_data failed" >&2; exit 1; }

make_kit() {  # $1 kit name, $2 installer file name, $3 installer body
    local name=MLB-Power-Pros-2008-$1-$version stage
    stage=$(mktemp -d)
    git -C "$root" archive --format=tar --prefix="$name/" "$commit" | tar -x -C "$stage"
    printf '%s\n' "$3" > "$stage/$name/$2"
    chmod +x "$stage/$name/$2"
    tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
    rm -rf "$stage"
    echo "$out/$name.tar.gz"
}

common=$(cat <<'EOF'
set -euo pipefail
disc=${1:-}
if [[ -z $disc ]]; then
    read -r -p "Path to your copy of MLB Power Pros 2008 (Wii, USA): " disc
    disc=$(printf '%s' "$disc" | tr -d "\"'")   # a path pasted from a file manager may be quoted
fi
[[ -f $disc ]] || { echo "Not found: $disc" >&2; exit 1; }
disc=$(realpath "$disc")
cd "$(dirname "${BASH_SOURCE[0]}")"
shift || true
EOF
)

make_kit Linux install.sh "#!/usr/bin/env bash
# Builds and installs MLB Power Pros 2008 from your own copy of the game (docs/en/linux.md).
#   ./install.sh '/path/to/your copy.rvz' [--install DIR] [other tools/linux/build.sh options]
# Needs the packages listed in docs/en/linux.md; add --container to use podman/docker instead.
$common
exec tools/linux/build.sh --disc \"\$disc\" --desktop-entry \"\$@\""

make_kit SteamDeck install-steam-deck.sh "#!/usr/bin/env bash
# Builds and installs MLB Power Pros 2008 on a Steam Deck (Desktop Mode) from your own copy of the
# game, inside a podman container: SteamOS needs no compilers (docs/en/linux.md, Steam Deck).
#   ./install-steam-deck.sh '/path/to/your copy.rvz'
# Also works on any Linux PC with podman or docker; then copy ~/Games/MLBPowerPros2008 to the Deck.
$common
command -v podman >/dev/null || command -v docker >/dev/null ||
    { echo 'podman was not found (SteamOS 3.5 or later includes it).' >&2; exit 1; }
tools/linux/build.sh --container --disc \"\$disc\" --desktop-entry \"\$@\"
cat <<'EOF'

Next: Steam (Desktop Mode) -> Games -> Add a Non-Steam Game to My Library -> Browse ->
~/Games/MLBPowerPros2008/MLBPowerPros2008. Leave Proton off. Then play it from Game Mode.
EOF"

(cd "$out" && sha256sum MLB-Power-Pros-2008-*-"$version".tar.gz > SHA256SUMS.txt && cat SHA256SUMS.txt)
