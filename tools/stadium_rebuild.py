# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebuild a game stadium with the from-scratch writer, as a new-stadium mod (for comparing in the game).

python tools/stadium_rebuild.py <project.bin> <game code> <mods folder> [--code <new code>] [--original]

Writes <mods folder>/Reconstruido <game code>/ with mod.toml ([[stadium]] code rb_<game code>, base the
game stadium) and project/stadium_<code>.pack and stadium_<code>tbl.pack, read with stadium_read and
written with stadium_write. With --original, copies the game's packs unchanged instead, as
"Original <game code>" with code og_<game code>, so both go through the same new-stadium route.
The packs are game content: keep the folder local, never share it.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "blender"))
from projectbin import Archive                                              # noqa: E402
from powerpros_stadium.names import valid_code                              # noqa: E402
from powerpros_stadium.stadium_read import read_stadium                     # noqa: E402
from powerpros_stadium.stadium_write import write_stadium                   # noqa: E402


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("project_bin")
    ap.add_argument("game_code")
    ap.add_argument("mods")
    ap.add_argument("--code")
    ap.add_argument("--original", action="store_true")
    a = ap.parse_args(argv)
    archive = Archive(a.project_bin)
    gfx, tbl = archive.read(f"/stadium_{a.game_code}.pack"), archive.read(f"/stadium_{a.game_code}tbl.pack")
    if gfx is None or tbl is None:
        sys.exit(f"project.bin has no stadium_{a.game_code}.pack with its table")
    code = a.code or ("og_" if a.original else "rb_") + a.game_code
    if not valid_code(code):
        sys.exit(f"'{code}': a stadium code has 2 to 16 lowercase letters, digits or _")
    name = ("Original " if a.original else "Reconstruido ") + a.game_code
    if not a.original:
        gfx, tbl = write_stadium(read_stadium(gfx, tbl))
    mod = Path(a.mods) / name
    (mod / "project").mkdir(parents=True, exist_ok=True)
    (mod / "project" / f"stadium_{code}.pack").write_bytes(gfx)
    (mod / "project" / f"stadium_{code}tbl.pack").write_bytes(tbl)
    (mod / "mod.toml").write_text(f'name = "{name}"\n\n[[stadium]]\ncode = "{code}"\nname = "{name}"\nbase = "{a.game_code}"\n',
                                  encoding="utf-8")
    print(f"{mod}: stadium {code}, {len(gfx)} + {len(tbl)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
