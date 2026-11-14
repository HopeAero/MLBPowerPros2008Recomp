#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Updates a kit install (Linux / Steam Deck) in place; the launcher starts it. Counterpart of
# tools/setup/Update.ps1.
#
#   update.sh [--lang es|en] [--jobs N] <install dir> <kit tarball> <sha256>
#   update.sh [--lang es|en] --rollback [--restore-saves] <install dir>
#
# 1. verifies the tarball's SHA-256 and extracts it to a temporary folder
# 2. syncs the kit folder (kit.txt's root=) by content, except local/ (the Wiicompiled clone, the
#    build and the generated code stay): changed files get the current time, unchanged ones are left;
#    only files the previous kit listed in kit-files.txt are removed, anything else there is kept.
#    An install inside the kit folder (other than under local/) is refused.
# 3. builds the new program with build.sh <kit.txt's options> --stage <install>.staging
# 4. copies the saves to <install>.backup and swaps the program: <install> -> <install>.previous
# --rollback swaps <install> and <install>.previous back; --restore-saves also restores <install>.backup.
# A leftover <install>.rollback (an interrupted rollback) blocks both until the kit installer runs again.
# <install>/UserData, Data and mods are never moved or overwritten. Needs <install>/kit.txt, which
# only a kit installer's build.sh (--from-kit) writes: a git clone is not updated by this script.
# Progress goes to stdout as "@@ <percent> <message>", failures as "@@ERROR <message>". "@@COMMIT" says
# the folders start moving: from there on SIGTERM / SIGINT are ignored, so the launcher stops offering
# «Cancelar» and a cancel can never leave the install half moved.
# MLBPP_UPDATE_BUILD_CMD (tests only) replaces "<root>/tools/linux/build.sh" in step 3.
# MLBPP_UPDATE_TEST_TERM=swap|rollback (tests only) sends this script a SIGTERM after it moved the
# first program item, as the launcher's «Cancelar» would.
set -euo pipefail

# The content sync below may rewrite this very file (the kit's own copy); bash reads scripts
# incrementally, so run from a temporary copy.
if [[ -z ${MLBPP_UPDATE_SELF:-} ]]; then
    self=$(mktemp "${TMPDIR:-/tmp}/mlbpp-update.XXXXXX")
    cp -- "$0" "$self"
    MLBPP_UPDATE_SELF=$self exec bash "$self" "$@"
fi
trap 'rm -f -- "$MLBPP_UPDATE_SELF"' EXIT

lang=es
jobs=""
rollback=0
restore_saves=0
pos=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --lang) lang=$2; shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --rollback) rollback=1; shift ;;
        --restore-saves) restore_saves=1; shift ;;
        *) pos+=("$1"); shift ;;
    esac
done
t() { if [[ $lang == en ]]; then printf '%s' "$2"; else printf '%s' "$1"; fi; }
# "|| true": after @@COMMIT a closed launcher (broken pipe, SIGPIPE ignored) must not stop the moves.
progress() { printf '@@ %s %s\n' "$1" "$2" || true; }
die() { printf '@@ERROR %s\n' "$*" || true; exit 1; }
trap 'die "$(t "Falló la actualización (línea $LINENO)." "The update failed (line $LINENO).")"' ERR

(( rollback )) && n=1 || n=3
[[ ${#pos[@]} -eq $n ]] || die "usage: update.sh [--lang es|en] [--jobs N] <install dir> <kit tarball> <sha256> | --rollback [--restore-saves] <install dir>"
install=$(realpath -m "${pos[0]}")
staging=$install.staging
previous=$install.previous
backup=$install.backup
extract=$install.extract
[[ -d $install ]] || die "$(t "No existe la instalación: $install" "No such install: $install")"

kit_get() { sed -n "s/^$1=//p" "$install/kit.txt" | head -n 1; }
# The first-level names of an install that belong to the program (what build.sh installs; the Windows
# list is tools/setup/ProgramItems.ps1). Anything else there is the player's and is never moved:
# UserData, mods, Data, portable.txt, kit.txt and whatever else they keep at the install root.
# version.txt (written by build.sh) travels with the program, so swap and rollback carry it.
program_items() { # only the items that exist in folder $1
    local n
    for n in MLBPowerPros2008 dsp_coef.bin initial_pipeline_cache.db cacert.pem config wii_bootstrap launcher \
             TextureStudio SeasonStudio powerupplus version.txt; do
        if [[ -e $1/$n || -L $1/$n ]]; then printf '%s\n' "$n"; fi
    done
}
reset_dir() { rm -rf "$1"; mkdir -p "$1"; }
# From here to the end the program folders move: a SIGTERM (the launcher's «Cancelar», sent to the
# whole process group), a Ctrl+C or the launcher closing (SIGPIPE on the next progress line) must not
# stop it halfway. Ignored signals stay ignored in mv / cp.
commit() { trap '' TERM INT PIPE; printf '@@COMMIT\n' || true; }
test_term() { if [[ ${MLBPP_UPDATE_TEST_TERM:-} == "$1" ]]; then MLBPP_UPDATE_TEST_TERM=; kill -TERM $$; fi; }
move_items() { # from, to, names...
    local from=$1 to=$2 name; shift 2
    for name in "$@"; do mv -- "$from/$name" "$to/$name"; done
}
# What the saves backup covers (relative to UserData). The cache never.
save_items=(NAND ModSaves arrange Config.toml)
installed_version() {
    local f=$root/local/update-manifest.txt v=""
    [[ -f $f ]] && v=$(sed -n 's/^version[[:space:]]\+//p' "$f" | head -n 1 | tr -d '[:space:]')
    printf '%s' "${v:-unknown}"
}
write_manifest() { mkdir -p "$root/local"; printf 'version %s\n' "$1" > "$root/local/update-manifest.txt"; }

[[ -f $install/kit.txt ]] || die "$(t \
    "Esta instalación no viene de un kit (falta kit.txt): se actualiza descargando el kit nuevo o con git." \
    "This install does not come from a kit (kit.txt is missing): update it with the new kit or with git.")"
root=$(kit_get root)
platform=$(kit_get platform)
read -ra options <<<"$(kit_get options)"
[[ -n $root && -d $root ]] || die "$(t "kit.txt apunta a una carpeta que no existe: $root" "kit.txt points to a folder that does not exist: $root")"

# A leftover .rollback (power cut or failed move mid-rollback) may hold the only copy of a program:
# nothing moves until the kit's installer has rebuilt the install (it removes the leftover).
interrupted() {
    [[ ! -e $install.rollback ]] || die "$(t \
        "Quedó $install.rollback de un cambio interrumpido; no se toca nada. Vuelve a ejecutar el instalador del kit." \
        "$install.rollback is left over from an interrupted switch; nothing was touched. Run the kit's installer again.")"
}

if (( rollback )); then
    [[ -f $previous/MLBPowerPros2008 ]] || die "$(t 'No hay una versión anterior.' 'There is no previous version.')"
    interrupted
    old=$(tr -d '[:space:]' < "$previous/version.txt" 2>/dev/null || true)
    tmp=$install.rollback
    commit
    reset_dir "$tmp"
    progress 10 "$(t 'Volviendo a la versión anterior...' 'Going back to the previous version...')"
    # ponytail: same ceiling as the swap (a power cut mid-move leaves the program half and half;
    # re-running the kit's installer rebuilds it). Not worth a transaction log.
    mapfile -t game_items < <(program_items "$install")
    mapfile -t prev_items < <(program_items "$previous")
    move_items "$install" "$tmp" "${game_items[@]:0:1}"
    test_term rollback
    move_items "$install" "$tmp" "${game_items[@]:1}"
    move_items "$previous" "$install" "${prev_items[@]}"
    move_items "$tmp" "$previous" "${game_items[@]}"
    rmdir "$tmp"
    [[ -n $old ]] && write_manifest "$old"
    if (( restore_saves )); then
        progress 60 "$(t 'Restaurando las partidas...' 'Restoring the saves...')"
        for name in "${save_items[@]}"; do
            [[ -e $backup/$name ]] || continue
            rm -rf "${install:?}/UserData/$name"
            mkdir -p "$install/UserData"
            cp -a "$backup/$name" "$install/UserData/$name"
        done
    fi
    progress 100 "$(t 'Versión anterior restaurada.' 'Previous version restored.')"
    exit 0
fi

tarball=$(realpath -m "${pos[1]}")
want=$(printf '%s' "${pos[2]}" | tr 'A-F' 'a-f')
[[ -f $tarball ]] || die "$(t "No se encuentra la descarga: $tarball" "Download not found: $tarball")"
[[ ! -e $root/.git ]] || die "$(t "$root es un clon de git: se actualiza con git." "$root is a git clone: update it with git.")"
# The sync below replaces the kit folder's files: an install (saves included) kept inside it is refused.
root_real=$(realpath -m "$root")
case "$install/" in
    "$root_real/local/"*) ;;
    "$root_real/"*) die "$(t \
        "La instalación está dentro de la carpeta del kit ($root); no se toca nada. Instala en otra carpeta con el instalador del kit (--install)." \
        "The install is inside the kit folder ($root); nothing was touched. Install into another folder with the kit's installer (--install).")" ;;
esac
interrupted
[[ -f $install/MLBPowerPros2008 ]] || die "$(t \
    'La instalación está incompleta (falta MLBPowerPros2008); no se toca nada. Vuelve a ejecutar el instalador del kit.' \
    'The install is incomplete (MLBPowerPros2008 is missing); nothing was touched. Run the kit installer again.')"

progress 1 "$(t 'Comprobando la descarga...' 'Checking the download...')"
got=$(sha256sum "$tarball" | cut -d' ' -f1)
[[ $got == "$want" ]] || die "$(t "La descarga está dañada (SHA-256 distinto). No se cambió nada." "The download is corrupt (SHA-256 mismatch). Nothing was changed.")"
listing=$(tar -tzf "$tarball") || die "$(t 'No se puede leer el kit. No se cambió nada.' 'The kit cannot be read. Nothing was changed.')"
if grep -qE '(^/|(^|/)\.\.(/|$))' <<<"$listing"; then
    die "$(t 'El kit contiene rutas sospechosas. No se cambió nada.' 'The kit contains suspicious paths. Nothing was changed.')"
fi

progress 4 "$(t 'Extrayendo el kit...' 'Extracting the kit...')"
reset_dir "$extract"
tar -xzf "$tarball" -C "$extract" --strip-components=1
[[ -f $extract/VERSION ]] || die "$(t 'El kit no trae VERSION. No se cambió nada.' 'The kit has no VERSION. Nothing was changed.')"
new_version=$(tr -d '[:space:]' < "$extract/VERSION")
old_version=$(installed_version)

# A new Containerfile means a new image (about 1.5 GB): say so before the long step.
rebuild_note=0
if [[ " ${options[*]} " == *" --container "* ]] && [[ -f $root/tools/linux/Containerfile ]] &&
   ! cmp -s "$root/tools/linux/Containerfile" "$extract/tools/linux/Containerfile"; then
    rebuild_note=1
fi

progress 8 "$(t 'Aplicando los archivos nuevos...' 'Applying the new files...')"
python3 -I - "$extract" "$root" <<'EOF'
import hashlib, os, shutil, sys, time

src, dst = sys.argv[1:3]
KEEP = {"local"}  # top-level folders of the kit that updates never touch
LIST = "kit-files.txt"  # every file a kit ships (make-kits.sh writes it into the kit)

def listed(path):
    """The previous kit's own files (None when it has no list)."""
    try:
        with open(path, encoding="utf-8") as f:
            names = {os.path.normpath(line.rstrip("\n")) for line in f if line.strip()}
    except FileNotFoundError:
        return None
    return {n for n in names if not os.path.isabs(n) and n.split(os.sep)[0] not in KEEP | {"..", "."}}

shipped_before = listed(os.path.join(dst, LIST))  # read before the sync replaces it

def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.digest()

def same(a, b):
    if os.path.islink(a) or os.path.islink(b):
        return os.path.islink(a) and os.path.islink(b) and os.readlink(a) == os.readlink(b)
    return os.path.getsize(a) == os.path.getsize(b) and sha(a) == sha(b)

def remove(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    else:
        os.remove(path)

wanted = set()
for base, dirs, files in os.walk(src):
    rel_base = os.path.relpath(base, src)
    if rel_base == ".":
        dirs[:] = [d for d in dirs if d not in KEEP]
        files = [f for f in files if f not in KEEP]
    for name in files + [d for d in dirs if os.path.islink(os.path.join(base, d))]:
        s = os.path.join(base, name)
        rel = os.path.normpath(os.path.join(rel_base, name))
        wanted.add(rel)
        d = os.path.join(dst, rel)
        if os.path.lexists(d) and (os.path.isdir(d) and not os.path.islink(d)):
            shutil.rmtree(d)
        if os.path.lexists(d) and same(s, d):
            continue
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if os.path.lexists(d):
            os.remove(d)
        if os.path.islink(s):
            os.symlink(os.readlink(s), d)
            continue
        # A changed file is written fresh and gets the current time: an older stamp would make Ninja skip it.
        shutil.copyfile(s, d)
        shutil.copymode(s, d)
        now = time.time()
        os.utime(d, (now, now))

# Only files the previous kit shipped and the new one does not, then their folders if left empty.
# Anything else in the kit folder is the player's (a disc image, notes, ...) and is never deleted;
# without the previous kit's list nothing is pruned.
real_dst = os.path.realpath(dst)
for rel in sorted((shipped_before or set()) - wanted):
    path = os.path.join(dst, rel)
    parent = os.path.dirname(path)
    if os.path.realpath(parent) != real_dst and not os.path.realpath(parent).startswith(real_dst + os.sep):
        continue  # through a symlinked folder: not ours to touch
    if not (os.path.islink(path) or os.path.isfile(path)):
        continue
    os.remove(path)
    while os.path.realpath(parent) != real_dst and os.path.isdir(parent) and not os.listdir(parent):
        os.rmdir(parent)
        parent = os.path.dirname(parent)
EOF
rm -rf "$extract"

progress 12 "$(t 'Compilando la versión nueva...' 'Building the new version...')"
(( rebuild_note )) && progress 13 "$(t 'Reconstruyendo el contenedor (tarda más)...' 'Rebuilding the container (takes longer)...')"
rm -rf "$staging"
build_cmd=("$root/tools/linux/build.sh")
[[ -n ${MLBPP_UPDATE_BUILD_CMD:-} ]] && read -ra build_cmd <<<"$MLBPP_UPDATE_BUILD_CMD"
extra=()
[[ -n $jobs ]] && extra+=(--jobs "$jobs")
log=$root/local/analysis/update-build.log
mkdir -p "$(dirname "$log")"
pct=14
trap - ERR
set +e
# build.sh's "==> step" lines become progress lines; the rest goes to the log.
"${build_cmd[@]}" "${options[@]}" "${extra[@]}" --stage "$staging" 2>&1 | while IFS= read -r line; do
    printf '%s\n' "$line" >> "$log"
    if [[ $line == *'==> '* ]]; then
        step=${line#*==> }; step=${step%$'\033'*}
        (( pct < 85 )) && pct=$((pct + 10))
        progress "$pct" "$step"
    fi
done
status=${PIPESTATUS[0]}
set -e
trap 'die "$(t "Falló la actualización (línea $LINENO)." "The update failed (line $LINENO).")"' ERR
if (( status != 0 )) || [[ ! -d $staging ]]; then
    msg=$(grep -a -m 3 'error' "$log" | tail -n 3 | tr '\n' ' ' | tr -d '\033' || true)
    rm -rf "$staging"
    die "$(t "No se pudo compilar la versión nueva; la instalación sigue como estaba. " "The new version could not be built; the install is unchanged. ")${msg}(log: $log)"
fi
[[ -n $(program_items "$staging") ]] || die "$(t 'La compilación no dejó programa en Staging.' 'The build left no program in staging.')"

commit  # the saves backup first: a cancelled one would leave .backup half written
progress 88 "$(t 'Copiando las partidas...' 'Backing up the saves...')"
reset_dir "$backup"
for name in "${save_items[@]}"; do
    [[ -e $install/UserData/$name ]] && cp -a "$install/UserData/$name" "$backup/$name"
done
printf '%s\n' "$old_version" > "$backup/version.txt"

progress 94 "$(t 'Cambiando de versión...' 'Switching versions...')"
reset_dir "$previous"
# ponytail: renames inside one filesystem, no journal; same ceiling as Update.ps1's swap. A power cut
# between the two moves leaves the program half old / half new; the kit's installer rebuilds it.
mapfile -t old_items < <(program_items "$install")
mapfile -t new_items < <(program_items "$staging")
moved_old=(); moved_new=()
swap_back() {
    local n
    for n in "${moved_new[@]}"; do mv -- "$install/$n" "$staging/$n" 2>/dev/null || true; done
    for n in "${moved_old[@]}"; do mv -- "$previous/$n" "$install/$n" 2>/dev/null || true; done
}
trap 'swap_back; die "$(t "Falló el cambio de versión." "Switching versions failed.")"' ERR
for n in "${old_items[@]}"; do mv -- "$install/$n" "$previous/$n"; moved_old+=("$n"); test_term swap; done
for n in "${new_items[@]}"; do mv -- "$staging/$n" "$install/$n"; moved_new+=("$n"); done
trap - ERR
rm -rf "$staging"
write_manifest "$new_version"
progress 100 "$(t 'Versión cambiada.' 'Version switched.')"
