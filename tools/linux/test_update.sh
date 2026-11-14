#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for tools/linux/update.sh on a fake install and a fake kit (apply, build failure, swap, rollback,
# SHA-256 mismatch). The build is a stub (MLBPP_UPDATE_BUILD_CMD); a real build.sh --stage run is not tested here.
#   tools/linux/test_update.sh
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
update=$here/update.sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fails=0
zeros=$(printf '0%.0s' {1..64})
check() { # name, then a test expression (evaluated, so [[ ]] and && work)
    local name=$1; shift
    if eval "$*"; then echo "ok   $name"; else echo "FAIL $name"; fails=$((fails + 1)); fi
}
contents() { [[ $(cat "$1" 2>/dev/null) == "$2" ]]; }
has() { grep -q -- "$1" <<<"$2"; }
hasnt() { ! grep -q -- "$1" <<<"$2"; }

# Stub for build.sh: stages "program <VERSION of the kit root>" unless the kit has a FAIL file.
cat > "$tmp/fakebuild.sh" <<'STUB'
#!/usr/bin/env bash
root=$FAKE_ROOT
echo "args: $*" >> "$root/local/stub-args.txt"
echo "self: $MLBPP_UPDATE_SELF" >> "$root/local/stub-args.txt"
echo "==> Fake step"
if [[ -f $root/FAIL ]]; then echo "error: boom from the stub" >&2; exit 1; fi
while [[ $# -gt 0 ]]; do [[ $1 == --stage ]] && stage=$2; shift; done
mkdir -p "$stage/launcher"
echo "program $(cat "$root/VERSION")" > "$stage/MLBPowerPros2008"
echo "launcher $(cat "$root/VERSION")" > "$stage/launcher/l.txt"
cp "$root/VERSION" "$stage/version.txt"
STUB
chmod +x "$tmp/fakebuild.sh"

new_world() { # $1 name -> $install, $kit (0.1.7 installed)
    w=$tmp/$1
    install="$w/Game dir Ñ"
    kit="$w/kit"
    rm -rf "$w"
    mkdir -p "$install/UserData/NAND" "$install/UserData/ModSaves" "$install/UserData/arrange" \
        "$install/UserData/Cache" "$install/mods" "$install/Data/sys" "$install/launcher" \
        "$kit/local/disc" "$kit/config" "$kit/tools/linux"
    echo "0.1.7" > "$kit/VERSION"
    echo "same" > "$kit/config/same.txt"; touch -d @946684800 "$kit/config/same.txt"
    echo "old" > "$kit/config/changed.txt"; touch -d @946684800 "$kit/config/changed.txt"
    echo "bye" > "$kit/config/gone.txt"
    echo "clone" > "$kit/local/keep.txt"; touch -d @946684800 "$kit/local/keep.txt"
    echo "cf1" > "$kit/tools/linux/Containerfile"
    printf '%s\n' VERSION config/changed.txt config/gone.txt config/same.txt kit-files.txt tools/linux/Containerfile \
        > "$kit/kit-files.txt"   # what make-kits.sh writes: the previous kit's own files
    echo "program 0.1.7" > "$install/MLBPowerPros2008"
    echo "launcher 0.1.7" > "$install/launcher/l.txt"; echo 0.1.7 > "$install/version.txt"
    mkdir -p "$install/screenshots"; echo shot > "$install/screenshots/s.png"; echo note > "$install/notes.txt"
    echo p > "$install/portable.txt"; echo d > "$install/Data/sys/main.dol"; echo m > "$install/mods/y"
    echo save1 > "$install/UserData/NAND/x"; echo ms > "$install/UserData/ModSaves/m"
    echo ar > "$install/UserData/arrange/a"; echo cfg > "$install/UserData/Config.toml"; echo c > "$install/UserData/Cache/c"
    printf 'root=%s
platform=linux\noptions=--no-studio --container\n' "$kit" > "$install/kit.txt"
    printf 'version 0.1.7
' > "$kit/local/update-manifest.txt"
    export FAKE_ROOT=$kit MLBPP_UPDATE_BUILD_CMD="$tmp/fakebuild.sh"
}
make_kit() { # $1 version, $2 Containerfile text, $3 optional extra file -> $tarball, $sha
    local k=$w/new/MLB-Power-Pros-2008-Linux-$1
    rm -rf "$w/new"; mkdir -p "$k/config" "$k/tools/linux"
    echo "$1" > "$k/VERSION"
    echo same > "$k/config/same.txt"; echo new > "$k/config/changed.txt"; echo added > "$k/config/added.txt"
    echo "$2" > "$k/tools/linux/Containerfile"
    [[ -n ${3:-} ]] && echo x > "$k/$3"
    { (cd "$k" && find . \( -type f -o -type l \) -printf '%P\n'); echo kit-files.txt; } | LC_ALL=C sort -u > "$w/kit-files"
    mv "$w/kit-files" "$k/kit-files.txt"
    tarball=$w/kit-$1.tar.gz
    tar -C "$w/new" -czf "$tarball" "MLB-Power-Pros-2008-Linux-$1"
    sha=$(sha256sum "$tarball" | cut -d' ' -f1)
}
run() { out=$(bash "$update" --lang en "$@" 2>&1); code=$?; }

# ---- apply + swap ----
new_world a; make_kit 0.1.8 cf2
run "$install" "$tarball" "$sha"
check "update exits 0" '[[ $code -eq 0 ]]'
check "progress ends at 100" 'has "^@@ 100 " "$out"'
check "container rebuild announced" 'has "Rebuilding the container" "$out"'
check "unchanged file keeps mtime" '[[ $(stat -c %Y "$kit/config/same.txt") -eq 946684800 ]]'
check "changed file written now" 'contents "$kit/config/changed.txt" new'
check "changed file has current mtime" '[[ $(stat -c %Y "$kit/config/changed.txt") -gt $(( $(date +%s) - 300 )) ]]'
check "added file appears" 'contents "$kit/config/added.txt" added'
check "removed file is gone" '[[ ! -e $kit/config/gone.txt ]]'
check "local/ untouched" '[[ $(stat -c %Y "$kit/local/keep.txt") -eq 946684800 ]]'
check "build got the kit options and --stage" 'grep -q -- "--no-studio --container --stage $install.staging" "$kit/local/stub-args.txt"'
check "new program installed" 'contents "$install/MLBPowerPros2008" "program 0.1.8"'
check "old program in previous" 'contents "$install.previous/MLBPowerPros2008" "program 0.1.7"'
check "old launcher in previous" 'contents "$install.previous/launcher/l.txt" "launcher 0.1.7"'
check "UserData untouched" 'contents "$install/UserData/NAND/x" save1'
check "mods, Data, portable, kit.txt untouched" '[[ -f $install/mods/y && -f $install/Data/sys/main.dol && -f $install/portable.txt && -f $install/kit.txt ]]'
check "backup has the saves" '[[ -f $install.backup/NAND/x && -f $install.backup/ModSaves/m && -f $install.backup/arrange/a && -f $install.backup/Config.toml ]]'
check "backup skips the cache" '[[ ! -e $install.backup/Cache ]]'
check "versions recorded" '[[ $(cat "$install.backup/version.txt") == 0.1.7 && $(cat "$install.previous/version.txt") == 0.1.7 ]]'
check "staging and extract removed" '[[ ! -e $install.staging && ! -e $install.extract ]]'
check "local manifest has the new version" 'grep -q "^version 0.1.8$" "$kit/local/update-manifest.txt"'

check "unknown items stay at the install root" '[[ -f $install/screenshots/s.png && -f $install/notes.txt && ! -e $install.previous/screenshots && ! -e $install.previous/notes.txt ]]'
check "version.txt moved with the program" '[[ $(cat "$install/version.txt") == 0.1.8 && $(cat "$install.previous/version.txt") == 0.1.7 ]]'

# ---- rollback ----
echo save2 > "$install/UserData/NAND/x"
run --rollback "$install"
check "rollback exits 0" '[[ $code -eq 0 ]]'
check "program back to old" 'contents "$install/MLBPowerPros2008" "program 0.1.7"'
check "new program now in previous" 'contents "$install.previous/MLBPowerPros2008" "program 0.1.8"'
check "saves kept without --restore-saves" 'contents "$install/UserData/NAND/x" save2'
check "rollback: unknown items stay put" '[[ -f $install/screenshots/s.png && -f $install/notes.txt && ! -e $install.previous/screenshots ]]'
check "rollback: version.txt is the old one, previous has the new" '[[ $(cat "$install/version.txt") == 0.1.7 && $(cat "$install.previous/version.txt") == 0.1.8 ]]'
check "manifest back to old" 'grep -q "^version 0.1.7$" "$kit/local/update-manifest.txt"'
run --rollback "$install"   # forward again, so the next rollback goes to 0.1.7 once more
run --rollback --restore-saves "$install"
check "rollback --restore-saves restores NAND" 'contents "$install/UserData/NAND/x" save1'
check "rollback --restore-saves program is old" 'contents "$install/MLBPowerPros2008" "program 0.1.7"'

# ---- SIGTERM (the launcher's «Cancelar») during the swap and the rollback moves ----
new_world h; make_kit 0.1.8 cf1
export MLBPP_UPDATE_TEST_TERM=swap
run "$install" "$tarball" "$sha"
unset MLBPP_UPDATE_TEST_TERM
check "TERM mid-swap: the update still completes" '[[ $code -eq 0 ]] && has "^@@ 100 " "$out"'
check "TERM mid-swap: @@COMMIT announced before the moves" 'has "^@@COMMIT$" "$out"'
check "TERM mid-swap: the new program is whole" 'contents "$install/MLBPowerPros2008" "program 0.1.8" && contents "$install/launcher/l.txt" "launcher 0.1.8"'
check "TERM mid-swap: the old program is whole in previous" 'contents "$install.previous/MLBPowerPros2008" "program 0.1.7" && contents "$install.previous/launcher/l.txt" "launcher 0.1.7"'
check "TERM mid-swap: no staging left" '[[ ! -e $install.staging ]]'
export MLBPP_UPDATE_TEST_TERM=rollback
run --rollback "$install"
unset MLBPP_UPDATE_TEST_TERM
check "TERM mid-rollback: the rollback still completes" '[[ $code -eq 0 ]] && has "^@@ 100 " "$out" && has "^@@COMMIT$" "$out"'
check "TERM mid-rollback: the old program is whole" 'contents "$install/MLBPowerPros2008" "program 0.1.7" && contents "$install/launcher/l.txt" "launcher 0.1.7"'
check "TERM mid-rollback: the new program is whole in previous" 'contents "$install.previous/MLBPowerPros2008" "program 0.1.8" && contents "$install.previous/launcher/l.txt" "launcher 0.1.8"'
check "TERM mid-rollback: no temporary folder left" '[[ ! -e $install.rollback ]]'

# ---- update.sh itself rewritten by the sync, run from the kit's own copy ----
new_world f; make_kit 0.1.8 cf1
mkdir -p "$kit/tools/linux"; cp "$update" "$kit/tools/linux/update.sh"
{ head -n 1 "$update"; for i in $(seq 1 300); do echo "# padding $i of a newer update.sh, shifts every byte offset"; done; tail -n +2 "$update"; } > "$w/new/MLB-Power-Pros-2008-Linux-0.1.8/tools/linux/update.sh"
tar -C "$w/new" -czf "$tarball" MLB-Power-Pros-2008-Linux-0.1.8; sha=$(sha256sum "$tarball" | cut -d' ' -f1)
out=$(bash "$kit/tools/linux/update.sh" --lang en "$install" "$tarball" "$sha" 2>&1); code=$?
check "update from the kit's own update.sh completes" '[[ $code -eq 0 ]] && has "^@@ 100 " "$out"'
check "it ran from a temp copy, not the kit file" 'grep "^self: " "$kit/local/stub-args.txt" | grep -q "mlbpp-update\." '
check "the kit's update.sh was replaced meanwhile" 'grep -q "padding 300" "$kit/tools/linux/update.sh"'
check "no temp copy left behind" '[[ -z $(ls "${TMPDIR:-/tmp}"/mlbpp-update.* 2>/dev/null) ]]'

# ---- tarball with a ../ member ----
new_world g; make_kit 0.1.8 cf1
python3 -I - "$w" <<'PY'
import io, sys, tarfile
w = sys.argv[1]
with tarfile.open(w + "/evil.tar.gz", "w:gz") as t:
    for name in ("MLB-Power-Pros-2008-Linux-0.1.8/VERSION", "MLB-Power-Pros-2008-Linux-0.1.8/../evil"):
        data = b"0.1.8"
        i = tarfile.TarInfo(name); i.size = len(data)
        t.addfile(i, io.BytesIO(data))
PY
run "$install" "$w/evil.tar.gz" "$(sha256sum "$w/evil.tar.gz" | cut -d' ' -f1)"
check "../ member refused" '[[ $code -eq 1 ]] && has "^@@ERROR .*suspicious" "$out"'
check "../ member: nothing extracted or changed" '[[ ! -e $w/evil && ! -e $install.extract && $(cat "$kit/config/changed.txt") == old ]]'

# ---- failed build ----
new_world b; make_kit 0.1.9 cf1 FAIL
run "$install" "$tarball" "$sha"
check "failed build exits 1" '[[ $code -eq 1 ]]'
check "failed build prints @@ERROR with the cause" 'has "^@@ERROR .*boom from the stub" "$out"'
check "install still old" 'contents "$install/MLBPowerPros2008" "program 0.1.7"'
check "no previous/backup/staging after failed build" '[[ ! -e $install.previous && ! -e $install.backup && ! -e $install.staging ]]'
check "local manifest not advanced" 'grep -q "^version 0.1.7$" "$kit/local/update-manifest.txt"'
check "no container note when Containerfile is the same" 'hasnt "Rebuilding the container" "$out"'

# ---- sha mismatch ----
new_world c; make_kit 0.1.8 cf1
run "$install" "$tarball" "$zeros"
check "sha mismatch exits 1 with @@ERROR" '[[ $code -eq 1 ]] && has "^@@ERROR .*SHA-256" "$out"'
check "sha mismatch changes nothing" '[[ $(cat "$kit/config/changed.txt") == old && -f $kit/config/gone.txt && ! -e $install.extract ]]'

# ---- no kit.txt ----
new_world d; make_kit 0.1.8 cf1; rm "$install/kit.txt"
run "$install" "$tarball" "$sha"
check "missing kit.txt refuses" '[[ $code -eq 1 ]] && has "^@@ERROR .*kit.txt" "$out"'
check "missing kit.txt changes nothing" '[[ $(cat "$kit/config/changed.txt") == old ]]'

# ---- the player's files in the kit folder survive (a disc image, a folder of their own) ----
new_world i; make_kit 0.1.8 cf1
echo disc > "$kit/game.rvz"; mkdir -p "$kit/My stuff/deep"; echo mine > "$kit/My stuff/deep/notes.txt"
echo mine > "$kit/config/player-note.txt"   # next to kit files, but never shipped by a kit
run "$install" "$tarball" "$sha"
check "stray files: the update completes" '[[ $code -eq 0 ]] && has "^@@ 100 " "$out"'
check "stray files: a disc image in the kit folder survives" 'contents "$kit/game.rvz" disc'
check "stray files: the player's own folder survives" 'contents "$kit/My stuff/deep/notes.txt" mine'
check "stray files: an unlisted file next to kit files survives" 'contents "$kit/config/player-note.txt" mine'
check "stray files: a file the old kit shipped and the new one does not is removed" '[[ ! -e $kit/config/gone.txt ]]'
check "stray files: the new kit list is in place" 'grep -qx "config/added.txt" "$kit/kit-files.txt"'

# ---- without the previous kit's list nothing is pruned ----
new_world j; make_kit 0.1.8 cf1; rm "$kit/kit-files.txt"
run "$install" "$tarball" "$sha"
check "no kit list: the update completes" '[[ $code -eq 0 ]] && contents "$install/MLBPowerPros2008" "program 0.1.8"'
check "no kit list: nothing is pruned" 'contents "$kit/config/gone.txt" bye'

# ---- an install inside the kit folder is refused, nothing touched ----
new_world k; make_kit 0.1.8 cf1
inner="$kit/Installed game"
mv "$install" "$inner"
sed -i "s|^root=.*|root=$kit|" "$inner/kit.txt"
run "$inner" "$tarball" "$sha"
check "install inside the kit: refused" '[[ $code -eq 1 ]] && has "^@@ERROR .*inside the kit folder" "$out"'
check "install inside the kit: saves and program intact" 'contents "$inner/UserData/NAND/x" save1 && contents "$inner/MLBPowerPros2008" "program 0.1.7"'
check "install inside the kit: kit folder not synced" 'contents "$kit/config/changed.txt" old && [[ -f $kit/config/gone.txt ]]'

# ---- rollback guards: an empty .previous, a leftover .rollback ----
new_world l; make_kit 0.1.8 cf1
mkdir -p "$install.previous"
run --rollback "$install"
check "empty previous: rollback refused" '[[ $code -eq 1 ]] && has "^@@ERROR .*no previous version" "$out"'
check "empty previous: program untouched" 'contents "$install/MLBPowerPros2008" "program 0.1.7"'
run "$install" "$tarball" "$sha"
check "swap after the refusal works" '[[ $code -eq 0 ]] && contents "$install/MLBPowerPros2008" "program 0.1.8"'
mkdir -p "$install.rollback/launcher"; echo "only copy" > "$install.rollback/launcher/l.txt"
run --rollback "$install"
check "leftover .rollback: rollback refused" '[[ $code -eq 1 ]] && has "^@@ERROR .*left over" "$out"'
make_kit 0.1.9 cf1
run "$install" "$tarball" "$sha"
check "leftover .rollback: update refused" '[[ $code -eq 1 ]] && has "^@@ERROR .*left over" "$out"'
check "leftover .rollback: kept, program and previous untouched" 'contents "$install.rollback/launcher/l.txt" "only copy" && contents "$install/MLBPowerPros2008" "program 0.1.8" && contents "$install.previous/MLBPowerPros2008" "program 0.1.7"'

# ---- the launcher closes after @@COMMIT (broken pipe): the switch still completes ----
new_world m; make_kit 0.1.8 cf1
bash "$update" --lang en "$install" "$tarball" "$sha" 2>/dev/null | while IFS= read -r line; do [[ $line == @@COMMIT ]] && break; done
check "closed pipe after commit: new program whole" 'contents "$install/MLBPowerPros2008" "program 0.1.8" && contents "$install/launcher/l.txt" "launcher 0.1.8"'
check "closed pipe after commit: old program whole in previous" 'contents "$install.previous/MLBPowerPros2008" "program 0.1.7" && [[ ! -e $install.staging ]]'

# ---- Spanish ----
new_world e; make_kit 0.1.8 cf1
out=$(bash "$update" --lang es "$install" "$tarball" "$zeros" 2>&1)
check "Spanish error text" 'has "dañada" "$out"'

echo
if (( fails == 0 )); then echo "all passed"; else echo "$fails failed"; fi
exit $(( fails != 0 ))
