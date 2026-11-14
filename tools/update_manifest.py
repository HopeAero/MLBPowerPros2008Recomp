# SPDX-License-Identifier: GPL-3.0-or-later
"""Update manifest and payload pieces.

  update_manifest.py hash <payload dir>
  update_manifest.py release <payload dir> <out dir> <version> <discussion_es> [<discussion_en>]
                             [--kits <dir>] [--containerfile <file>]
  update_manifest.py payload <payload dir> <version>     (manifest for the Setup: no zips)

Format: see docs/superpowers/specs/2026-11-11-launcher-updates-design.md.
"""
import hashlib
import sys
import zipfile
from pathlib import Path

MANIFEST = "update-manifest.txt"

PIECES = {
    "core": [
        "workspace/LocalBuild.ps1", "workspace/Update.ps1", "workspace/NativeBuildFlags.ps1",
        "workspace/VERSION", "workspace/ProgramItems.ps1",
        "workspace/config", "workspace/launcher", "workspace/src/runtime", "workspace/src/aurora-main",
        "toolkit/nodtool.exe",
    ],
    "studios": [
        "workspace/TextureStudio.exe", "workspace/SeasonStudio.exe", "workspace/CallnameStudio.exe",
        "workspace/UniformStudio.exe", "MLBPowerPros2008-Setup.exe",
    ],
    "translator": ["toolkit/Translator"],
    "deps": ["workspace/Dependencies"],
    "buildtools": ["toolkit/CMake", "toolkit/Ninja", "toolkit/README-LICENSES.txt"],
    "llvm": ["toolkit/llvm-mingw"],
    "powerupplus": ["workspace/powerupplus"],
    "uniform": ["workspace/uniform-studio"],
    "licenses": ["licenses"],
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def piece_files(root: Path, roots) -> list:
    """Relative '/' paths of every file under the roots (a root may be a file or a folder)."""
    found = set()
    for r in roots:
        p = root / r
        if p.is_file():
            found.add(r)
        elif p.is_dir():
            for f in p.rglob("*"):
                if f.is_file():
                    found.add(f.relative_to(root).as_posix())
    return sorted(found)


def content_hash(root: Path, roots) -> str:
    h = hashlib.sha256()
    for rel in piece_files(root, roots):
        h.update(f"{rel}\t{sha256_file(root / rel)}\n".encode("utf-8"))
    return h.hexdigest()


def check_coverage(root: Path) -> None:
    """Every payload file (except the manifest) must be in exactly one piece."""
    owners = {}
    for name, roots in PIECES.items():
        for rel in piece_files(root, roots):
            owners.setdefault(rel, []).append(name)
    errors = []
    stray = sorted(f.relative_to(root).as_posix() for f in root.rglob("*")
                   if f.is_file() and f.relative_to(root).as_posix() not in owners
                   and f.relative_to(root).as_posix() != MANIFEST)
    if stray:
        errors.append("payload files outside every piece: " + ", ".join(stray[:20]))
    dup = sorted(f"{rel} ({'+'.join(p)})" for rel, p in owners.items() if len(p) > 1)
    if dup:
        errors.append("payload files in more than one piece: " + ", ".join(dup[:20]))
    if errors:
        raise ValueError("; ".join(errors))


def make_zip(root: Path, roots, zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for rel in piece_files(root, roots):
            z.write(root / rel, rel)


def _header(version, es=None, en=None):
    lines = [f"version {version}"]
    if es:
        lines.append(f"discussion_es {es}")
        lines.append(f"discussion_en {en or es}")
    return lines


def _write(path: Path, lines) -> None:
    path.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))


def release(root: Path, out: Path, version: str, es: str, en: str = None,
            kits: Path = None, containerfile: Path = None) -> None:
    root, out = Path(root), Path(out)
    check_coverage(root)
    if kits:
        # Both kits of this version or none: a release without a platform's kit line breaks every
        # update on that platform (the launcher would offer it and then find nothing to download).
        found = {label: Path(kits) / f"MLB-Power-Pros-2008-{label}-{version}.tar.gz" for label in ("Linux", "SteamDeck")}
        missing = [k.name for k in found.values() if not k.is_file()]
        if missing:
            raise ValueError(f"{kits} has no {', '.join(missing)}: run tools/linux/make-kits.sh {version} "
                             "first (or move the folder away for a release without Linux kits)")
    out.mkdir(parents=True, exist_ok=True)
    lines = _header(version, es, en)
    for name, roots in PIECES.items():
        zp = out / f"MLBPP-{version}-{name}.zip"
        make_zip(root, roots, zp)
        lines.append(f"piece {name} {zp.name} {sha256_file(zp)} {zp.stat().st_size} "
                     f"{content_hash(root, roots)} {';'.join(roots)}")
    if kits:
        cf = sha256_file(Path(containerfile)) if containerfile else None
        for kind, label in (("linux", "Linux"), ("steamdeck", "SteamDeck")):
            k = found[label]
            line = f"kit {kind} {k.name} {sha256_file(k)} {k.stat().st_size}"
            lines.append(line + (f" {cf}" if cf else ""))
    _write(out / MANIFEST, lines)


def payload_manifest(root: Path, version: str) -> None:
    """Manifest inside the Setup payload: version + content fingerprints, no zips ('-' / 0)."""
    root = Path(root)
    check_coverage(root)
    lines = _header(version)
    for name, roots in PIECES.items():
        lines.append(f"piece {name} - - 0 {content_hash(root, roots)} {';'.join(roots)}")
    _write(root / MANIFEST, lines)


def main(argv) -> int:
    if len(argv) >= 3 and argv[1] == "hash":
        root = Path(argv[2])
        for name, roots in PIECES.items():
            print(name, content_hash(root, roots))
        return 0
    if len(argv) >= 4 and argv[1] == "payload":
        payload_manifest(Path(argv[2]), argv[3])
        return 0
    if len(argv) >= 6 and argv[1] == "release":
        args, opts, i = [], {}, 2
        while i < len(argv):
            if argv[i] in ("--kits", "--containerfile"):
                opts[argv[i][2:]] = Path(argv[i + 1])
                i += 2
            else:
                args.append(argv[i])
                i += 1
        if len(args) not in (4, 5):
            print(__doc__)
            return 2
        release(Path(args[0]), Path(args[1]), args[2], args[3], args[4] if len(args) == 5 else None,
                opts.get("kits"), opts.get("containerfile"))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
