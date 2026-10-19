# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of the season save diff (docs/superpowers/plans/2026-10-19-season-archive-research.md, Task 2).

python tools/test_season_save.py   (synthetic bytes only, no game data). Exits 1 on failure.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from season_save import diff, strided

failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


a = bytes(64)
b = bytearray(a)
b[10:12] = b"\x01\x02"
b[20] = 3
check(diff(a, bytes(b), gap=4) == [(10, 12), (20, 21)], "diff keeps far ranges apart")
check(diff(a, bytes(b), gap=16) == [(10, 21)], "diff joins close ranges")
check(diff(a, a) == [], "no difference")
check(diff(a, a + b"\x05") == [(64, 65)], "longer file: the tail differs")

c = bytearray(2000)
for year in range(3):
    c[100 + 428 * year:104 + 428 * year] = b"\xff" * 4
ranges = diff(bytes(2000), bytes(c), gap=4)
check(strided(ranges, 428) == [(100, 104, 3)], "three changes 428 bytes apart are one table")
check(strided([(0, 4), (900, 904)], 428) == [], "a lone range is not a table")

sys.exit(1 if failures else 0)
