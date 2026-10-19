# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of the stadium pack writer (docs/superpowers/plans/2026-10-18-stadium-pack-writer.md).

python tools/test_stadium_writer.py [project.bin]
Without project.bin only the synthetic tests run (no game data). With it, also the ones that read the
game's stadiums; their output stays in memory or under local/tmp/stadium-writer. Exits 1 on failure.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "blender"))

failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


def stadium_codes(archive):
    """Codes of the stadiums that have a table, in archive order."""
    return [m[1] for name, *_ in archive.entries() if (m := re.search(r"/stadium_([a-z0-9]+)tbl\.pack$", name))]


def packs(archive, code):
    return archive.read(f"/stadium_{code}.pack"), archive.read(f"/stadium_{code}tbl.pack")


# ---- Task 1: description and reader --------------------------------------------------------------

def test_reader_known_values(archive):
    from powerpros_stadium.stadium_read import read_stadium
    col = read_stadium(*packs(archive, "col"))
    check(len(col.placements) == 82, f"col: 82 placements ({len(col.placements)})")
    check([len(g.points) for g in col.unk_20] == [32, 50, 50, 25], f"col: unk_20 groups {[len(g.points) for g in col.unk_20]}")
    check([len(x) for x in col.unk_24] == [7, 7], f"col: unk_24 lists {[len(x) for x in col.unk_24]}")
    check(col.preload_order == list(range(8)) and len(col.preload) == 8, f"col: 8 preloaded textures in order 0-7")
    nyy = read_stadium(*packs(archive, "nyy"))
    check([len(g.points) for g in nyy.unk_20] == [150, 144], f"nyy: unk_20 groups {[len(g.points) for g in nyy.unk_20]}")
    bull = read_stadium(*packs(archive, "bullpen"))
    check(bull.unk_20 == [] and bull.unk_24 == [[]], f"bullpen: no unk_20 groups, one empty unk_24 list ({bull.unk_24})")
    words = {m.scene_word for code in stadium_codes(archive) for m in read_stadium(*packs(archive, code)).models}
    check(words == {0x01000000}, f"every scene word is 0x01000000 ({sorted(hex(w) for w in words)})")


def uncovered(data, spans):
    """Offsets of non-zero bytes outside every span, and spans that leave the file."""
    covered = bytearray(len(data))
    outside = [s for s in spans if s.start < 0 or s.end > len(data) or s.end < s.start]
    for s in spans:
        covered[max(0, s.start):min(len(data), s.end)] = b"\1" * (min(len(data), s.end) - max(0, s.start))
    return [i for i, b in enumerate(data) if b and not covered[i]], outside


def test_reader_covers_every_byte(archive):
    from powerpros_stadium.stadium_read import read_stadium
    bad = []
    for code in stadium_codes(archive):
        gfx, tbl = packs(archive, code)
        spans = {"gfx": [], "tbl": []}
        read_stadium(gfx, tbl, spans)
        for name, data in (("gfx", gfx), ("tbl", tbl)):
            loose, outside = uncovered(data, spans[name])
            if loose or outside:
                bad.append(f"{code} {name}: {len(loose)} loose bytes (first 0x{loose[0]:X})" if loose else
                           f"{code} {name}: {len(outside)} spans outside the file")
    check(not bad, "every non-zero byte of the 40 stadiums is read" + ("" if not bad else ": " + "; ".join(bad[:6])))


SYNTHETIC = []
GAME = [test_reader_known_values, test_reader_covers_every_byte]


def main(argv):
    for test in SYNTHETIC:
        test()
    if len(argv) > 1:
        from projectbin import Archive
        archive = Archive(argv[1])
        for test in GAME:
            try:
                test(archive)
            except Exception as e:          # an error in one test must not hide the others
                check(False, f"{test.__name__} raised {type(e).__name__}: {e}")
    print("RESULT: " + ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
